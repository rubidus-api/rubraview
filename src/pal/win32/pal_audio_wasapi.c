#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <math.h>
#include "rubraview/pal/pal_audio.h"
#include "rubraview/pal/pal_time.h"
#include "rubraview/audio_dsp.h"

/*
 * WASAPI shared-mode output (RFC-0001 §5.4). One thread per output owns
 * COM, the device and the render client; it wakes on the device's event,
 * tops the device buffer up from the PCM ring, and publishes where the
 * listener is. Windows converts the sample rate and channel layout
 * (AUTOCONVERTPCM), so the decoder hands over whatever the file has.
 */

/* Older MinGW headers lack these two flags; the values are Windows'. */
#ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000
#endif
#ifndef AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
#define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000
#endif

/* Defined here rather than taken from an import library, so the link does
   not depend on which MinGW happens to carry them. */
static const GUID RV_CLSID_MMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const GUID RV_IID_IMMDeviceEnumerator  = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static const GUID RV_IID_IAudioClient         = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
static const GUID RV_IID_ISimpleAudioVolume   = {0x87CE5498, 0x68D6, 0x44E5, {0x92, 0x15, 0x6D, 0xA4, 0x7E, 0xF8, 0x83, 0xD8}};
static const GUID RV_IID_IAudioRenderClient   = {0xF294ACFC, 0x3146, 0x4483, {0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}};
static const GUID RV_SUBTYPE_IEEE_FLOAT       = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

#define DEVICE_BUFFER_100NS 1000000   /* 100 ms: generous for a viewer, never a glitch */
#define OPEN_TIMEOUT_MS 3000
#define FLUSH_TIMEOUT_MS 2000

/* D-15: the session volume, for every output of this process. A change
   bumps the generation; each output thread applies it when it sees a
   generation newer than the one it applied. */
static _Atomic uint32_t volume_permille = 1000;
static _Atomic bool volume_muted = false;
static _Atomic uint32_t volume_generation = 1;

static _Atomic uint32_t speed_permille = 1000;

void rubraview_pal_audio_set_speed(double speed) {
    if (!(speed >= 0.25)) speed = 0.25;
    if (speed > 4.0) speed = 4.0;
    atomic_store_explicit(&speed_permille, (uint32_t)lround(speed * 1000.0), memory_order_release);
}

void rubraview_pal_audio_set_volume(double volume, bool muted) {
    if (!(volume >= 0.0)) volume = 0.0;
    if (volume > 1.0) volume = 1.0;
    atomic_store_explicit(&volume_permille, (uint32_t)lround(volume * 1000.0), memory_order_relaxed);
    atomic_store_explicit(&volume_muted, muted, memory_order_relaxed);
    atomic_fetch_add_explicit(&volume_generation, 1, memory_order_release);
}

struct rubraview_audio_out {
    HANDLE thread;
    HANDLE ready;                 /* set once the thread has opened, or failed to open, the device */
    HANDLE wake;                  /* the device's event; also set by us to wake the thread */
    uint32_t rate, channels;
    rubraview_pcm_ring_t *ring;
    bool opened;                  /* written by the thread before `ready` */

    _Atomic bool quit;
    _Atomic bool playing;
    _Atomic uint64_t flush_request;
    _Atomic uint64_t flush_ack;
    _Atomic int64_t flush_base_100ns;

    /* The heard position, published under a sequence counter so a reader
       never pairs one record's position with another's time. */
    _Atomic uint32_t seq;
    _Atomic int64_t heard_100ns;
    _Atomic int64_t heard_wall_us;
    _Atomic bool drained;
};

static DWORD channel_mask(uint32_t channels) {
    switch (channels) {
        case 1: return 0x4;     /* front centre */
        case 2: return 0x3;     /* front left, front right */
        case 6: return 0x3F;    /* 5.1 */
        case 8: return 0x63F;   /* 7.1 */
        default: return 0;      /* let Windows decide */
    }
}

static void publish(rubraview_audio_out_t *out, double heard_seconds) {
    uint32_t s = atomic_load_explicit(&out->seq, memory_order_relaxed);
    atomic_store_explicit(&out->seq, s + 1, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&out->heard_100ns, (int64_t)llround(heard_seconds * 1e7), memory_order_relaxed);
    atomic_store_explicit(&out->heard_wall_us, (int64_t)llround(rubraview_pal_time_now_seconds() * 1e6),
                          memory_order_relaxed);
    atomic_store_explicit(&out->seq, s + 2, memory_order_release);
}

static void apply_volume(ISimpleAudioVolume *volume, uint32_t *applied) {
    uint32_t generation = atomic_load_explicit(&volume_generation, memory_order_acquire);
    if (!volume || generation == *applied) return;
    float level = (float)atomic_load_explicit(&volume_permille, memory_order_relaxed) / 1000.0f;
    bool muted = atomic_load_explicit(&volume_muted, memory_order_relaxed);
    ISimpleAudioVolume_SetMasterVolume(volume, level, NULL);
    ISimpleAudioVolume_SetMute(volume, muted ? TRUE : FALSE, NULL);
    *applied = generation;
}

/* The device went away: unplugged, disabled, or a remote session's sound
   turned off. Not every audio call says so the same way. */
#define RV_AUDCLNT_E_DEVICE_INVALIDATED ((HRESULT)0x88890004L)
#define RV_AUDCLNT_E_SERVICE_NOT_RUNNING ((HRESULT)0x88890010L)
static bool device_gone(HRESULT hr) {
    return hr == RV_AUDCLNT_E_DEVICE_INVALIDATED || hr == RV_AUDCLNT_E_SERVICE_NOT_RUNNING;
}

typedef struct device {
    IMMDeviceEnumerator *enumerator;
    IMMDevice *device;
    IAudioClient *client;
    IAudioRenderClient *render;
    ISimpleAudioVolume *volume;
    UINT32 buffer_frames;
} device_t;

static void close_device(device_t *d) {
    if (d->volume) ISimpleAudioVolume_Release(d->volume);
    if (d->render) IAudioRenderClient_Release(d->render);
    if (d->client) IAudioClient_Release(d->client);
    if (d->device) IMMDevice_Release(d->device);
    if (d->enumerator) IMMDeviceEnumerator_Release(d->enumerator);
    *d = (device_t){0};
}

/* The default output device, opened in our format. */
static bool open_device(rubraview_audio_out_t *out, device_t *d) {
    *d = (device_t){0};
    bool ok = SUCCEEDED(CoCreateInstance(&RV_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                                         &RV_IID_IMMDeviceEnumerator, (void**)&d->enumerator)) && d->enumerator &&
              SUCCEEDED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(d->enumerator, eRender, eConsole, &d->device)) && d->device &&
              SUCCEEDED(IMMDevice_Activate(d->device, &RV_IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&d->client)) && d->client;
    if (ok) {
        WAVEFORMATEXTENSIBLE format = {0};
        format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        format.Format.nChannels = (WORD)out->channels;
        format.Format.nSamplesPerSec = out->rate;
        format.Format.wBitsPerSample = 32;
        format.Format.nBlockAlign = (WORD)(out->channels * 4u);
        format.Format.nAvgBytesPerSec = out->rate * format.Format.nBlockAlign;
        format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        format.Samples.wValidBitsPerSample = 32;
        format.dwChannelMask = channel_mask(out->channels);
        format.SubFormat = RV_SUBTYPE_IEEE_FLOAT;

        DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                      AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        ok = SUCCEEDED(IAudioClient_Initialize(d->client, AUDCLNT_SHAREMODE_SHARED, flags, DEVICE_BUFFER_100NS, 0,
                                               (WAVEFORMATEX*)&format, NULL)) &&
             SUCCEEDED(IAudioClient_SetEventHandle(d->client, out->wake)) &&
             SUCCEEDED(IAudioClient_GetBufferSize(d->client, &d->buffer_frames)) && d->buffer_frames > 0 &&
             SUCCEEDED(IAudioClient_GetService(d->client, &RV_IID_IAudioRenderClient, (void**)&d->render)) && d->render;
        /* Volume is a nicety: an output that cannot give it still plays. */
        if (ok && FAILED(IAudioClient_GetService(d->client, &RV_IID_ISimpleAudioVolume, (void**)&d->volume))) d->volume = NULL;
    }
    if (!ok) close_device(d);
    return ok;
}

/* Feeds the device until quit (false) or until the device goes away
   (true). `io_base` comes in as the position to start from and goes out
   as the position of the first sound not yet read from the ring. */
static bool run(rubraview_audio_out_t *out, device_t *d, double *io_base) {
    IAudioClient *client = d->client;
    IAudioRenderClient *render = d->render;
    ISimpleAudioVolume *volume = d->volume;
    UINT32 buffer_frames = d->buffer_frames;
    uint64_t submitted = 0;
    double source_frames = 0.0;   /* file frames read for what went to the device (D-15) */
    double base = *io_base;
    bool started = false, lost = false;
    uint32_t volume_applied = 0;
    double step = 1.0;
    /* After the device was emptied (a seek) the sound fades in over 5 ms
       instead of starting mid-wave (T058 on the VM: a click). */
    const uint32_t fade_length = out->rate / 200u;
    uint32_t fade_left = 0;
    /* Speed reads up to 4 file frames per device frame; one buffer's worth, read ahead. */
    rubraview_speed_resampler_t resampler = rubraview_speed_resampler_create(out->channels);
    size_t scratch_frames = (size_t)buffer_frames * 4u + 2u;
    float *scratch = (float*)malloc(scratch_frames * out->channels * sizeof(float));
    if (!scratch) return false;
    publish(out, base);

    while (!atomic_load_explicit(&out->quit, memory_order_acquire)) {
        apply_volume(volume, &volume_applied);   /* before the wait: a new output starts at the right level */
        WaitForSingleObject(out->wake, 50);

        uint64_t request = atomic_load_explicit(&out->flush_request, memory_order_acquire);
        if (request != atomic_load_explicit(&out->flush_ack, memory_order_relaxed)) {
            /* A seek. The producer is waiting for our answer and not
               writing, so emptying the ring cannot lose new samples. */
            /* Stopped and left alone, the stream ends quietly (every pause
               measured on the VM ended at 0); reset at once, it was cut
               mid-wave. So the engine gets a few periods to finish before
               what was queued is thrown away (T058). */
            if (started) { IAudioClient_Stop(client); started = false; Sleep(30); }
            IAudioClient_Reset(client);
            rubraview_pcm_ring_discard(out->ring);
            submitted = 0;
            source_frames = 0.0;
            resampler = rubraview_speed_resampler_create(out->channels);
            base = (double)atomic_load_explicit(&out->flush_base_100ns, memory_order_relaxed) / 1e7;
            fade_left = fade_length;
            publish(out, base);
            atomic_store_explicit(&out->flush_ack, request, memory_order_release);
        }

        UINT32 padding = 0;
        HRESULT hr = IAudioClient_GetCurrentPadding(client, &padding);
        if (FAILED(hr)) {
            if (device_gone(hr)) { lost = true; break; }
            padding = 0;
        }

        bool want = atomic_load_explicit(&out->playing, memory_order_acquire);
        if (want) {
            step = (double)atomic_load_explicit(&speed_permille, memory_order_acquire) / 1000.0;
            UINT32 space = buffer_frames > padding ? buffer_frames - padding : 0;
            size_t ready_frames = rubraview_pcm_ring_count(out->ring) / out->channels;
            /* Every write goes through the resampler; at speed 1 it hands the samples through. */
            size_t k = rubraview_speed_source_needed(&resampler, space, step, ready_frames);
            if (k > scratch_frames) k = scratch_frames;
            if (space > 0 && k > 0) {
                BYTE *data = NULL;
                hr = IAudioRenderClient_GetBuffer(render, space, &data);
                if (SUCCEEDED(hr) && data) {
                    rubraview_pcm_ring_read(out->ring, scratch, k * out->channels);
                    size_t m = rubraview_speed_resample(&resampler, scratch, k, step, (float*)(void*)data, space);
                    rubraview_fade_in((float*)(void*)data, m, out->channels, &fade_left, fade_length);
                    IAudioRenderClient_ReleaseBuffer(render, (UINT32)m, 0);
                    submitted += m;
                    padding += (UINT32)m;
                    source_frames += (double)k;
                } else if (device_gone(hr)) {
                    lost = true;
                    break;
                }
            }
            /* Started only after the first fill, so playback begins with
               sound rather than a buffer of silence. */
            if (!started) { IAudioClient_Start(client); started = true; }
        } else if (started) {
            IAudioClient_Stop(client);   /* what is buffered stays, and plays on resume */
            started = false;
        }

        (void)submitted;
        publish(out, rubraview_audio_heard_media_seconds(base, source_frames, padding, step, out->rate));
        atomic_store_explicit(&out->drained,
                              rubraview_pcm_ring_count(out->ring) == 0 && padding == 0,
                              memory_order_release);
    }
    if (started && !lost) IAudioClient_Stop(client);
    free(scratch);
    /* What the device had queued is gone with it: go on from the ring. */
    *io_base = base + source_frames / (double)out->rate;
    return lost;
}

/* No device (T058 on the VM: with the remote session's sound turned off
   mid-film, the position froze, since the clock follows the sound). The
   sound is used up on the wall clock instead, so the clock, the picture
   and the end of the file go on, silently; seeks are still answered.
   Once a second the default device is tried again, and playing carries
   on there. Returns true with `d` open, false on quit. */
static bool run_silent(rubraview_audio_out_t *out, device_t *d, double *io_base) {
    enum { CHUNK = 4096 };
    float scratch[CHUNK];
    double base = *io_base, used = 0.0, carry = 0.0;
    double last = rubraview_pal_time_now_seconds(), next_try = last + 1.0;
    publish(out, base);
    while (!atomic_load_explicit(&out->quit, memory_order_acquire)) {
        WaitForSingleObject(out->wake, 10);
        uint64_t request = atomic_load_explicit(&out->flush_request, memory_order_acquire);
        if (request != atomic_load_explicit(&out->flush_ack, memory_order_relaxed)) {
            rubraview_pcm_ring_discard(out->ring);
            base = (double)atomic_load_explicit(&out->flush_base_100ns, memory_order_relaxed) / 1e7;
            used = 0.0;
            carry = 0.0;
            publish(out, base);
            atomic_store_explicit(&out->flush_ack, request, memory_order_release);
        }
        double now = rubraview_pal_time_now_seconds();
        double elapsed = now - last;
        last = now;
        if (atomic_load_explicit(&out->playing, memory_order_acquire)) {
            double step = (double)atomic_load_explicit(&speed_permille, memory_order_acquire) / 1000.0;
            size_t due = rubraview_silent_frames_due(elapsed, (double)out->rate, step, &carry);
            size_t ready = rubraview_pcm_ring_count(out->ring) / out->channels;
            if (due > ready) due = ready;
            while (due > 0) {
                size_t n = due < CHUNK / out->channels ? due : CHUNK / out->channels;
                rubraview_pcm_ring_read(out->ring, scratch, n * out->channels);
                due -= n;
                used += (double)n;
            }
        } else {
            carry = 0.0;
        }
        publish(out, base + used / (double)out->rate);
        atomic_store_explicit(&out->drained, rubraview_pcm_ring_count(out->ring) == 0, memory_order_release);
        if (now >= next_try) {
            next_try = now + 1.0;
            if (open_device(out, d)) {
                *io_base = base + used / (double)out->rate;
                return true;
            }
        }
    }
    return false;
}

static DWORD WINAPI audio_thread(LPVOID arg) {
    rubraview_audio_out_t *out = (rubraview_audio_out_t*)arg;
    bool com = SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED));

    /* Windows' multimedia scheduler (MMCSS) gives the thread that feeds
       the device the CPU ahead of ordinary work. Without it, a busy
       machine let the 100 ms buffer run dry (T058 on the 2-core VM, while
       a helper script compiled: 10 ms of silence, a click). avrt.dll is
       looked up by name, so a Windows without it still plays, at a raised
       priority instead. */
    typedef HANDLE (WINAPI *mmcss_set_fn)(LPCWSTR, LPDWORD);
    typedef BOOL (WINAPI *mmcss_revert_fn)(HANDLE);
    HMODULE avrt = LoadLibraryW(L"avrt.dll");
    mmcss_set_fn mmcss_set = avrt ? (mmcss_set_fn)(void*)GetProcAddress(avrt, "AvSetMmThreadCharacteristicsW") : NULL;
    mmcss_revert_fn mmcss_revert = avrt ? (mmcss_revert_fn)(void*)GetProcAddress(avrt, "AvRevertMmThreadCharacteristics") : NULL;
    DWORD mmcss_task = 0;
    HANDLE mmcss = mmcss_set ? mmcss_set(L"Playback", &mmcss_task) : NULL;
    if (!mmcss) SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    device_t d = {0};
    bool ok = open_device(out, &d);
    out->opened = ok;
    SetEvent(out->ready);
    double base = 0.0;
    while (ok) {
        if (!run(out, &d, &base)) break;   /* quit */
        close_device(&d);                  /* the device went away */
        ok = run_silent(out, &d, &base);
    }
    close_device(&d);

    if (com) CoUninitialize();
    if (mmcss && mmcss_revert) mmcss_revert(mmcss);
    if (avrt) FreeLibrary(avrt);
    return 0;
}

void rubraview_pal_audio_close(rubraview_audio_out_t *out) {
    if (!out) return;
    atomic_store_explicit(&out->quit, true, memory_order_release);
    if (out->wake) SetEvent(out->wake);
    if (out->thread) {
        WaitForSingleObject(out->thread, 5000);
        CloseHandle(out->thread);
    }
    if (out->ready) CloseHandle(out->ready);
    if (out->wake) CloseHandle(out->wake);
    free(out);
}

rubraview_audio_out_t *rubraview_pal_audio_open(uint32_t sample_rate, uint32_t channels,
                                                rubraview_pcm_ring_t *ring) {
    if (!ring || sample_rate == 0 || channels == 0 || channels > 8) return NULL;
    rubraview_audio_out_t *out = (rubraview_audio_out_t*)calloc(1, sizeof(*out));
    if (!out) return NULL;
    out->rate = sample_rate;
    out->channels = channels;
    out->ring = ring;
    atomic_init(&out->quit, false);
    atomic_init(&out->playing, false);
    atomic_init(&out->flush_request, 0);
    atomic_init(&out->flush_ack, 0);
    atomic_init(&out->flush_base_100ns, 0);
    atomic_init(&out->seq, 0);
    atomic_init(&out->heard_100ns, 0);
    atomic_init(&out->heard_wall_us, 0);
    atomic_init(&out->drained, true);

    out->ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    out->wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    out->thread = (out->ready && out->wake) ? CreateThread(NULL, 0, audio_thread, out, 0, NULL) : NULL;
    if (!out->thread || WaitForSingleObject(out->ready, OPEN_TIMEOUT_MS) != WAIT_OBJECT_0 || !out->opened) {
        rubraview_pal_audio_close(out);
        return NULL;
    }
    return out;
}

void rubraview_pal_audio_set_playing(rubraview_audio_out_t *out, bool playing) {
    if (!out) return;
    atomic_store_explicit(&out->playing, playing, memory_order_release);
    SetEvent(out->wake);
}

void rubraview_pal_audio_flush(rubraview_audio_out_t *out, double base_seconds) {
    if (!out) return;
    atomic_store_explicit(&out->flush_base_100ns, (int64_t)llround(base_seconds * 1e7), memory_order_relaxed);
    uint64_t request = atomic_fetch_add_explicit(&out->flush_request, 1, memory_order_release) + 1;
    SetEvent(out->wake);
    for (int waited = 0; waited < FLUSH_TIMEOUT_MS; ++waited) {
        if (atomic_load_explicit(&out->flush_ack, memory_order_acquire) == request) return;
        Sleep(1);
    }
}

bool rubraview_pal_audio_position(rubraview_audio_out_t *out, double *out_position, double *out_wall) {
    if (!out || !out_position || !out_wall) return false;
    for (int tries = 0; tries < 1000; ++tries) {
        uint32_t before = atomic_load_explicit(&out->seq, memory_order_acquire);
        if (before & 1u) continue;   /* a record is being written */
        int64_t position = atomic_load_explicit(&out->heard_100ns, memory_order_relaxed);
        int64_t wall = atomic_load_explicit(&out->heard_wall_us, memory_order_relaxed);
        atomic_thread_fence(memory_order_acquire);
        if (atomic_load_explicit(&out->seq, memory_order_relaxed) == before) {
            *out_position = (double)position / 1e7;
            *out_wall = (double)wall / 1e6;
            return true;
        }
    }
    return false;
}

bool rubraview_pal_audio_drained(rubraview_audio_out_t *out) {
    return !out || atomic_load_explicit(&out->drained, memory_order_acquire);
}

#endif /* _WIN32 */
