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
static const GUID RV_IID_IAudioRenderClient   = {0xF294ACFC, 0x3146, 0x4483, {0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}};
static const GUID RV_SUBTYPE_IEEE_FLOAT       = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

#define DEVICE_BUFFER_100NS 1000000   /* 100 ms: generous for a viewer, never a glitch */
#define OPEN_TIMEOUT_MS 3000
#define FLUSH_TIMEOUT_MS 2000

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

static void run(rubraview_audio_out_t *out, IAudioClient *client, IAudioRenderClient *render,
                UINT32 buffer_frames) {
    uint64_t submitted = 0;
    double base = 0.0;
    bool started = false;
    publish(out, 0.0);

    while (!atomic_load_explicit(&out->quit, memory_order_acquire)) {
        WaitForSingleObject(out->wake, 50);

        uint64_t request = atomic_load_explicit(&out->flush_request, memory_order_acquire);
        if (request != atomic_load_explicit(&out->flush_ack, memory_order_relaxed)) {
            /* A seek. The producer is waiting for our answer and not
               writing, so emptying the ring cannot lose new samples. */
            if (started) { IAudioClient_Stop(client); started = false; }
            IAudioClient_Reset(client);
            rubraview_pcm_ring_discard(out->ring);
            submitted = 0;
            base = (double)atomic_load_explicit(&out->flush_base_100ns, memory_order_relaxed) / 1e7;
            publish(out, base);
            atomic_store_explicit(&out->flush_ack, request, memory_order_release);
        }

        UINT32 padding = 0;
        if (FAILED(IAudioClient_GetCurrentPadding(client, &padding))) padding = 0;

        bool want = atomic_load_explicit(&out->playing, memory_order_acquire);
        if (want) {
            UINT32 space = buffer_frames > padding ? buffer_frames - padding : 0;
            size_t ready_frames = rubraview_pcm_ring_count(out->ring) / out->channels;
            UINT32 n = (UINT32)(ready_frames < space ? ready_frames : space);
            if (n > 0) {
                BYTE *data = NULL;
                if (SUCCEEDED(IAudioRenderClient_GetBuffer(render, n, &data)) && data) {
                    rubraview_pcm_ring_read(out->ring, (float*)(void*)data, (size_t)n * out->channels);
                    IAudioRenderClient_ReleaseBuffer(render, n, 0);
                    submitted += n;
                    padding += n;
                }
            }
            /* Started only after the first fill, so playback begins with
               sound rather than a buffer of silence. */
            if (!started) { IAudioClient_Start(client); started = true; }
        } else if (started) {
            IAudioClient_Stop(client);   /* what is buffered stays, and plays on resume */
            started = false;
        }

        publish(out, rubraview_audio_heard_seconds(base, submitted, padding, out->rate));
        atomic_store_explicit(&out->drained,
                              rubraview_pcm_ring_count(out->ring) == 0 && padding == 0,
                              memory_order_release);
    }
    if (started) IAudioClient_Stop(client);
}

static DWORD WINAPI audio_thread(LPVOID arg) {
    rubraview_audio_out_t *out = (rubraview_audio_out_t*)arg;
    bool com = SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED));

    IMMDeviceEnumerator *enumerator = NULL;
    IMMDevice *device = NULL;
    IAudioClient *client = NULL;
    IAudioRenderClient *render = NULL;
    UINT32 buffer_frames = 0;

    bool ok = SUCCEEDED(CoCreateInstance(&RV_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                                         &RV_IID_IMMDeviceEnumerator, (void**)&enumerator)) && enumerator &&
              SUCCEEDED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, eRender, eConsole, &device)) && device &&
              SUCCEEDED(IMMDevice_Activate(device, &RV_IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&client)) && client;
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
        ok = SUCCEEDED(IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_SHARED, flags, DEVICE_BUFFER_100NS, 0,
                                               (WAVEFORMATEX*)&format, NULL)) &&
             SUCCEEDED(IAudioClient_SetEventHandle(client, out->wake)) &&
             SUCCEEDED(IAudioClient_GetBufferSize(client, &buffer_frames)) && buffer_frames > 0 &&
             SUCCEEDED(IAudioClient_GetService(client, &RV_IID_IAudioRenderClient, (void**)&render)) && render;
    }

    out->opened = ok;
    SetEvent(out->ready);
    if (ok) run(out, client, render, buffer_frames);

    if (render) IAudioRenderClient_Release(render);
    if (client) IAudioClient_Release(client);
    if (device) IMMDevice_Release(device);
    if (enumerator) IMMDeviceEnumerator_Release(enumerator);
    if (com) CoUninitialize();
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
