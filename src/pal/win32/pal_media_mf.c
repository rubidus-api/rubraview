#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <d3d11.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rubraview/pal/pal_media.h"
#include "rubraview/pal/pal_audio.h"
#include "rubraview/pal/pal_media_backend.h"

/*
 * Media Foundation backend for the media PAL (D-8). Video comes out of the
 * reader's own video processor as 32-bit RGB; sound comes out as 32-bit
 * float and goes to the WASAPI output (pal_audio) through a PCM ring
 * (slice 2). On a machine with no audio device the sound stream is not
 * decoded at all and the caller plays on the wall clock.
 *
 * The decode thread owns everything Media Foundation hands out. It
 * starts COM and Media Foundation for itself, opens the reader itself,
 * and talks to the caller's thread only through the ring, the slot
 * array and a few atomics — never through the imaging, drawing or arena
 * code, which is the mistake the 0.0.2 pre-cache made.
 *
 * FFmpeg is the second backend (D-8, slice 3). Until it lands, it
 * reports itself unavailable, and rubraview_media_backend_order never
 * offers it.
 */

#define SLOT_COUNT 4
#define MAX_DIMENSION 16384
#define OPEN_TIMEOUT_MS 15000
#define NO_GENERATION UINT64_MAX
#define NO_STREAM ((DWORD)-1)
#define PCM_RING_SECONDS 2          /* more than the audio/video interleave of any sane file */
#define AUDIO_ROOM_SAMPLES 16384    /* room wanted before reading the next sample */

typedef struct media_slot {
    uint8_t *pixels;       /* width * height * 4, top row first */
    ID3D11Texture2D *texture;   /* RV-062: the picture kept on the card; made on first use,
                                   shared with the renderer's device under a keyed mutex */
    IDXGIKeyedMutex *mutex;
    bool on_gpu;                /* this frame is in `texture`, not in `pixels` */
    double pts, duration;
    uint64_t generation;   /* which seek this frame belongs to */
} media_slot_t;

typedef struct mf_media {
    HANDLE thread;
    HANDLE opened;                          /* set by the thread once it has said yes or no */
    WCHAR path[MAX_PATH * 2];
    /* Written by the thread before `opened`. */
    bool ok;
    rubraview_media_failure_t failure;
    rubraview_media_info_t info;

    DWORD video_stream;                     /* NO_STREAM for a file with no picture */
    DWORD audio_stream;                     /* NO_STREAM when the sound is not played */
    uint32_t audio_rate, audio_channels;
    rubraview_pcm_ring_t pcm;
    float *pcm_storage;
    rubraview_audio_out_t *audio;           /* NULL: no sound (no device, or no audio stream) */
    int32_t width, height;
    LONG default_stride;                    /* the negotiated type's stride; negative = bottom-up */

    rubraview_spsc_t ring;
    media_slot_t slots[SLOT_COUNT];

    _Atomic bool quit;
    _Atomic int32_t pending_audio_stream;   /* §3.16.2: -1 when nothing was asked */
    _Atomic int64_t seek_target_100ns;      /* written before seek_generation is bumped */
    _Atomic uint64_t seek_generation;
    _Atomic uint64_t end_generation;        /* the generation whose decode reached the end */

    uint64_t consumer_generation;           /* caller's thread only */

    /* RV-062: decoding on the graphics card. */
    rubraview_media_gpu_t gpu;              /* what the caller offered */
    IMFDXGIDeviceManager *manager;          /* NULL unless the device was handed over */
    bool gpu_active;                        /* the reader decodes on the card */

    /* §3.16.2: what the container holds, worked out once while opening.
       The strings live here so the set can be copied out by value. */
    rubraview_track_set_t tracks;
    char track_text[RUBRAVIEW_MAX_TRACKS][3][64];   /* language, title, codec */
} mf_media_t;

/*
 * Media Foundation is loaded at run time, not linked. A Windows "N"
 * edition ships without it until the Media Feature Pack is installed;
 * importing MFPlat.dll directly would stop rubraview from starting there
 * at all — the image viewer included. Only System32 is searched, so a
 * look-alike DLL placed beside the executable is never picked up.
 */
typedef HRESULT (WINAPI *mf_startup_fn)(ULONG, DWORD);
typedef HRESULT (WINAPI *mf_shutdown_fn)(void);
typedef HRESULT (WINAPI *mf_create_attributes_fn)(IMFAttributes**, UINT32);
typedef HRESULT (WINAPI *mf_create_media_type_fn)(IMFMediaType**);
typedef HRESULT (WINAPI *mf_create_reader_fn)(LPCWSTR, IMFAttributes*, IMFSourceReader**);
typedef HRESULT (WINAPI *mf_create_dxgi_manager_fn)(UINT*, IMFDXGIDeviceManager**);

/* RV-062. Spelled out here: the import libraries do not carry all of them. */
static const GUID RV_IID_IMFDXGIBuffer = { 0xe7174cfa, 0x1c9e, 0x48b1, { 0x88, 0x66, 0x62, 0x62, 0x26, 0xbf, 0xc2, 0x58 } };
static const GUID RV_IID_ID3D11Texture2D = { 0x6f15aaf2, 0xd208, 0x4e89, { 0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c } };
static const GUID RV_IID_IDXGIKeyedMutex = { 0x9d8e1289, 0xd7b3, 0x465f, { 0x81, 0x26, 0x25, 0x0e, 0x34, 0x9a, 0xf8, 0x5d } };

static struct {
    bool tried, ok;
    mf_startup_fn startup;
    mf_shutdown_fn shutdown;
    mf_create_attributes_fn create_attributes;
    mf_create_media_type_fn create_media_type;
    mf_create_reader_fn create_reader;
    mf_create_dxgi_manager_fn create_dxgi_manager;   /* optional: without it, no GPU decode */
} g_mf;

/* Called on the caller's thread before any decode thread exists, so the
   thread start is what publishes these pointers to it. */
static bool mf_load(void) {
    if (g_mf.tried) return g_mf.ok;
    g_mf.tried = true;
    HMODULE plat = LoadLibraryExW(L"mfplat.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE rw = LoadLibraryExW(L"mfreadwrite.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!plat || !rw) return false;
    g_mf.startup = (mf_startup_fn)(void*)GetProcAddress(plat, "MFStartup");
    g_mf.shutdown = (mf_shutdown_fn)(void*)GetProcAddress(plat, "MFShutdown");
    g_mf.create_attributes = (mf_create_attributes_fn)(void*)GetProcAddress(plat, "MFCreateAttributes");
    g_mf.create_media_type = (mf_create_media_type_fn)(void*)GetProcAddress(plat, "MFCreateMediaType");
    g_mf.create_reader = (mf_create_reader_fn)(void*)GetProcAddress(rw, "MFCreateSourceReaderFromURL");
    g_mf.create_dxgi_manager = (mf_create_dxgi_manager_fn)(void*)GetProcAddress(plat, "MFCreateDXGIDeviceManager");
    g_mf.ok = g_mf.startup && g_mf.shutdown && g_mf.create_attributes &&
              g_mf.create_media_type && g_mf.create_reader;
    return g_mf.ok;
}

static bool mf_available(void) {
    return mf_load();
}

/* ---- decode thread ---- */

static void fail(mf_media_t *m, rubraview_media_failure_t failure) {
    m->ok = false;
    m->failure = failure;
}

/* ---- what the container holds (§3.16.2) ---- */

/* Media Foundation speaks UTF-16 everywhere; the rest of the program
   speaks UTF-8. An empty or over-long name comes back empty rather than
   cut in the middle of a character. */
static u8str_t mf_utf8(char *buffer, size_t capacity, const WCHAR *wide) {
    u8str_t empty = { .ptr = buffer, .len = 0 };
    buffer[0] = '\0';
    if (!wide || !wide[0]) return empty;
    int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, buffer, (int)capacity, NULL, NULL);
    if (n <= 1) return empty;
    return (u8str_t){ .ptr = buffer, .len = (size_t)(n - 1) };
}

/* A stream attribute that is a string, or nothing. */
static u8str_t mf_stream_string(IMFSourceReader *reader, DWORD stream, const GUID *key,
                                char *buffer, size_t capacity) {
    u8str_t empty = { .ptr = buffer, .len = 0 };
    buffer[0] = '\0';
    PROPVARIANT pv;
    PropVariantInit(&pv);
    HRESULT hr = IMFSourceReader_GetPresentationAttribute(reader, stream, key, &pv);
    u8str_t out = empty;
    if (SUCCEEDED(hr) && pv.vt == VT_LPWSTR) out = mf_utf8(buffer, capacity, pv.pwszVal);
    PropVariantClear(&pv);
    return out;
}

/* The codec, for a label. Video subtypes carry a printable FOURCC in
   their first field; audio subtypes carry a small number instead, so
   those are named from a short table. */
static u8str_t mf_codec_name(const GUID *subtype, char *buffer, size_t capacity) {
    static const struct { uint32_t id; const char *name; } AUDIO[] = {
        { 0x0001, "pcm" },   { 0x0003, "float" }, { 0x0055, "mp3" },
        { 0x0160, "wma" },   { 0x0161, "wma" },   { 0x0162, "wma" }, { 0x0163, "wma" },
        { 0x1610, "aac" },   { 0x2000, "ac3" },   { 0x2001, "dts" },
        { 0xF1AC, "flac" },  { 0x704F, "opus" },  { 0x6C61, "alac" },
    };
    uint32_t id = (uint32_t)subtype->Data1;
    for (size_t i = 0; i < sizeof(AUDIO) / sizeof(AUDIO[0]); ++i) {
        if (AUDIO[i].id == id) {
            size_t len = strlen(AUDIO[i].name);
            if (len >= capacity) len = capacity - 1;
            memcpy(buffer, AUDIO[i].name, len);
            buffer[len] = '\0';
            return (u8str_t){ .ptr = buffer, .len = len };
        }
    }
    char chars[4] = { (char)(id & 0xFF), (char)((id >> 8) & 0xFF),
                      (char)((id >> 16) & 0xFF), (char)((id >> 24) & 0xFF) };
    for (size_t i = 0; i < 4; ++i) {
        if (chars[i] < 0x20 || chars[i] > 0x7E) {
            buffer[0] = '\0';
            return (u8str_t){ .ptr = buffer, .len = 0 };
        }
    }
    if (capacity < 5) { buffer[0] = '\0'; return (u8str_t){ .ptr = buffer, .len = 0 }; }
    memcpy(buffer, chars, 4);
    buffer[4] = '\0';
    return (u8str_t){ .ptr = buffer, .len = 4 };
}

static void mf_track_add(mf_media_t *m, IMFSourceReader *reader, DWORD stream,
                         rubraview_track_kind_t kind, IMFMediaType *type, const GUID *subtype) {
    size_t slot = m->tracks.count;
    if (slot >= RUBRAVIEW_MAX_TRACKS) return;

    UINT32 channels = 0;
    if (kind == RUBRAVIEW_TRACK_AUDIO) {
        IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_NUM_CHANNELS, &channels);
    }
    rubraview_track_t track = {
        .kind = kind,
        .stream_index = (int32_t)stream,
        .language = mf_stream_string(reader, stream, &MF_SD_LANGUAGE,
                                     m->track_text[slot][0], sizeof(m->track_text[slot][0])),
        .title = mf_stream_string(reader, stream, &MF_SD_STREAM_NAME,
                                  m->track_text[slot][1], sizeof(m->track_text[slot][1])),
        .codec = mf_codec_name(subtype, m->track_text[slot][2], sizeof(m->track_text[slot][2])),
        .channels = (int32_t)channels,
    };
    rubraview_tracks_add(&m->tracks, track);
}

/* Puts one audio stream into float samples and says what came out.
   Used when the file is opened and again when the reader switches to
   another sound track (§3.16.2). */
static bool mf_negotiate_audio(IMFSourceReader *reader, DWORD stream,
                               UINT32 *out_rate, UINT32 *out_channels) {
    *out_rate = 0; *out_channels = 0;
    IMFMediaType *want = NULL;
    bool decodable = false;
    if (SUCCEEDED(g_mf.create_media_type(&want)) && want) {
        IMFMediaType_SetGUID(want, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
        IMFMediaType_SetGUID(want, &MF_MT_SUBTYPE, &MFAudioFormat_Float);
        decodable = SUCCEEDED(IMFSourceReader_SetCurrentMediaType(reader, stream, NULL, want));
        IMFMediaType_Release(want);
    }
    if (!decodable) return false;
    IMFMediaType *current = NULL;
    if (SUCCEEDED(IMFSourceReader_GetCurrentMediaType(reader, stream, &current)) && current) {
        IMFMediaType_GetUINT32(current, &MF_MT_AUDIO_SAMPLES_PER_SECOND, out_rate);
        IMFMediaType_GetUINT32(current, &MF_MT_AUDIO_NUM_CHANNELS, out_channels);
        IMFMediaType_Release(current);
    }
    return *out_rate > 0 && *out_channels > 0 && *out_channels <= 8;
}

static bool open_reader(mf_media_t *m, IMFSourceReader **out_reader, bool on_gpu) {
    *out_reader = NULL;

    IMFAttributes *attrs = NULL;
    if (FAILED(g_mf.create_attributes(&attrs, 3)) || !attrs) { fail(m, RUBRAVIEW_MEDIA_FAIL_FILE); return false; }
    if (on_gpu) {
        /* RV-062: decode on the card, and let the reader's video processor
           turn the picture into BGRA there too. */
        IMFAttributes_SetUnknown(attrs, &MF_SOURCE_READER_D3D_MANAGER, (IUnknown*)m->manager);
        IMFAttributes_SetUINT32(attrs, &MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
        IMFAttributes_SetUINT32(attrs, &MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    } else {
        /* Lets the reader convert YUV to RGB32 itself, so the caller gets
           pixels Direct2D can take as they are. */
        IMFAttributes_SetUINT32(attrs, &MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    }

    IMFSourceReader *reader = NULL;
    HRESULT hr = g_mf.create_reader(m->path, attrs, &reader);
    IMFAttributes_Release(attrs);
    if (FAILED(hr) || !reader) {
        bool readable = GetFileAttributesW(m->path) != INVALID_FILE_ATTRIBUTES;
        fail(m, rubraview_media_classify(readable, false, false, 0));
        return false;
    }

    /* Find the first stream of each kind, and note every one of them
       for the track menu on the way past (§3.16.2).
       The sound is the one the file puts first (owner, 2026-09-22: "그냥
       트랙 순서로"). Media Foundation does not number streams in the
       file's order — for two_audio.mp4 (tracks: video, eng, kor) it gave
       kor, eng, video, on a Korean Windows — but the stream it selects by
       itself is the file's first sound track (eng there). So that one
       plays and is listed first; the rest follow in the reader's order. */
    m->tracks = rubraview_tracks_create();
    DWORD video_stream = NO_STREAM, audio_stream = NO_STREAM, first_audio = NO_STREAM;
    uint32_t fourcc = 0;
    for (DWORD s = 0; ; ++s) {
        IMFMediaType *type = NULL;
        hr = IMFSourceReader_GetNativeMediaType(reader, s, 0, &type);
        if (hr == (HRESULT)MF_E_INVALIDSTREAMNUMBER) break;
        if (FAILED(hr) || !type) continue;
        GUID major = {0};
        IMFMediaType_GetGUID(type, &MF_MT_MAJOR_TYPE, &major);
        IMFMediaType_Release(type);
        if (!IsEqualGUID(&major, &MFMediaType_Audio)) continue;
        if (first_audio == NO_STREAM) first_audio = s;
        BOOL selected = FALSE;
        if (audio_stream == NO_STREAM && SUCCEEDED(IMFSourceReader_GetStreamSelection(reader, s, &selected)) && selected) {
            audio_stream = s;
        }
    }
    if (audio_stream == NO_STREAM) audio_stream = first_audio;
    for (int pass = 0; pass < 2; ++pass) {   /* the chosen sound first, then everything else */
        for (DWORD s = 0; ; ++s) {
            IMFMediaType *type = NULL;
            hr = IMFSourceReader_GetNativeMediaType(reader, s, 0, &type);
            if (hr == (HRESULT)MF_E_INVALIDSTREAMNUMBER) break;
            if (FAILED(hr) || !type) continue;
            if ((pass == 0) != (s == audio_stream)) { IMFMediaType_Release(type); continue; }
            GUID major = {0}, subtype = {0};
            IMFMediaType_GetGUID(type, &MF_MT_MAJOR_TYPE, &major);
            IMFMediaType_GetGUID(type, &MF_MT_SUBTYPE, &subtype);
            if (IsEqualGUID(&major, &MFMediaType_Video)) {
                mf_track_add(m, reader, s, RUBRAVIEW_TRACK_VIDEO, type, &subtype);
                if (video_stream == NO_STREAM) {
                    video_stream = s;
                    fourcc = (uint32_t)subtype.Data1;   /* Media Foundation subtypes carry the FOURCC here */
                }
            } else if (IsEqualGUID(&major, &MFMediaType_Audio)) {
                mf_track_add(m, reader, s, RUBRAVIEW_TRACK_AUDIO, type, &subtype);
            }
            IMFMediaType_Release(type);
        }
    }
    for (size_t i = 0; i < m->tracks.count; ++i) {
        if (m->tracks.tracks[i].kind == RUBRAVIEW_TRACK_VIDEO &&
            m->tracks.tracks[i].stream_index == (int32_t)video_stream) {
            m->tracks.current_video = (int32_t)i;
        }
        if (m->tracks.tracks[i].kind == RUBRAVIEW_TRACK_AUDIO &&
            m->tracks.tracks[i].stream_index == (int32_t)audio_stream) {
            m->tracks.current_audio = (int32_t)i;
        }
    }

    bool has_audio = audio_stream != NO_STREAM;
    if (video_stream == NO_STREAM && !has_audio) {
        IMFSourceReader_Release(reader);
        fail(m, RUBRAVIEW_MEDIA_FAIL_CODEC);
        return false;
    }

    IMFSourceReader_SetStreamSelection(reader, (DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);

    int32_t width = 0, height = 0;
    UINT64 frame_rate = 0;
    if (video_stream != NO_STREAM) {
        IMFSourceReader_SetStreamSelection(reader, video_stream, TRUE);
        IMFMediaType *want = NULL;
        bool decodable = false;
        if (SUCCEEDED(g_mf.create_media_type(&want)) && want) {
            IMFMediaType_SetGUID(want, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
            /* On the card, ARGB32 is B8G8R8A8 — the format Direct2D's
               bitmaps take, so a frame can be copied into one as it is. */
            IMFMediaType_SetGUID(want, &MF_MT_SUBTYPE, on_gpu ? &MFVideoFormat_ARGB32 : &MFVideoFormat_RGB32);
            decodable = SUCCEEDED(IMFSourceReader_SetCurrentMediaType(reader, video_stream, NULL, want));
            IMFMediaType_Release(want);
        }
        if (!decodable) {
            IMFSourceReader_Release(reader);
            fail(m, rubraview_media_classify(true, true, false, fourcc));
            return false;
        }

        IMFMediaType *current = NULL;
        UINT64 frame_size = 0;
        UINT32 stride = 0;
        if (SUCCEEDED(IMFSourceReader_GetCurrentMediaType(reader, video_stream, &current)) && current) {
            IMFMediaType_GetUINT64(current, &MF_MT_FRAME_SIZE, &frame_size);
            IMFMediaType_GetUINT64(current, &MF_MT_FRAME_RATE, &frame_rate);
            if (FAILED(IMFMediaType_GetUINT32(current, &MF_MT_DEFAULT_STRIDE, &stride))) stride = 0;
            IMFMediaType_Release(current);
        }
        width = (int32_t)(frame_size >> 32);
        height = (int32_t)(frame_size & 0xFFFFFFFFu);
        if (width <= 0 || height <= 0 || width > MAX_DIMENSION || height > MAX_DIMENSION) {
            IMFSourceReader_Release(reader);
            fail(m, RUBRAVIEW_MEDIA_FAIL_CODEC);
            return false;
        }
        for (int i = 0; i < SLOT_COUNT; ++i) {
            m->slots[i].pixels = (uint8_t*)malloc((size_t)width * (size_t)height * 4u);
            if (!m->slots[i].pixels) {
                IMFSourceReader_Release(reader);
                fail(m, RUBRAVIEW_MEDIA_FAIL_FILE);
                return false;
            }
        }
        m->default_stride = (LONG)(INT32)stride;
    }

    /* The sound: float samples, at whatever rate and layout the file has. */
    if (has_audio) {
        IMFSourceReader_SetStreamSelection(reader, audio_stream, TRUE);
        UINT32 rate = 0, channels = 0;
        bool decodable = mf_negotiate_audio(reader, audio_stream, &rate, &channels);
        size_t capacity = (size_t)rate * channels * PCM_RING_SECONDS;
        if (decodable) {
            m->pcm_storage = (float*)malloc(capacity * sizeof(float));
            if (m->pcm_storage && rubraview_pcm_ring_init(&m->pcm, m->pcm_storage, capacity)) {
                m->audio = rubraview_pal_audio_open(rate, channels, &m->pcm);
            }
        }
        if (m->audio) {
            m->audio_stream = audio_stream;
            m->audio_rate = rate;
            m->audio_channels = channels;
        } else {
            /* No device, or a sound we cannot decode: the picture plays
               alone. A file that is only sound still "plays" — on the
               wall clock, silently — so its time and its end are right. */
            IMFSourceReader_SetStreamSelection(reader, audio_stream, FALSE);
            free(m->pcm_storage);
            m->pcm_storage = NULL;
        }
    }

    double duration = 0.0;
    PROPVARIANT var;
    PropVariantInit(&var);
    if (SUCCEEDED(IMFSourceReader_GetPresentationAttribute(reader, (DWORD)MF_SOURCE_READER_MEDIASOURCE,
                                                           &MF_PD_DURATION, &var)) && var.vt == VT_UI8) {
        duration = (double)var.uhVal.QuadPart / 1e7;
    }
    PropVariantClear(&var);

    UINT32 rate_num = (UINT32)(frame_rate >> 32), rate_den = (UINT32)(frame_rate & 0xFFFFFFFFu);

    m->video_stream = video_stream;
    m->width = width;
    m->height = height;
    m->ok = true;
    m->failure = RUBRAVIEW_MEDIA_OPENED;
    m->info = (rubraview_media_info_t){
        .backend = RUBRAVIEW_BACKEND_MEDIA_FOUNDATION,
        .duration_seconds = duration,
        .frame_rate = rate_den ? (double)rate_num / (double)rate_den : 0.0,
        .width = width,
        .height = height,
        .has_video = video_stream != NO_STREAM,
        .has_audio = has_audio,
        .audio_output = m->audio != NULL,
        .video_fourcc = fourcc,
    };
    *out_reader = reader;
    return true;
}

/* Copies one decoded sample into a slot, top row first. */
static bool copy_sample(mf_media_t *m, IMFSample *sample, uint8_t *dst) {
    IMFMediaBuffer *buffer = NULL;
    if (FAILED(IMFSample_ConvertToContiguousBuffer(sample, &buffer)) || !buffer) return false;

    const size_t row = (size_t)m->width * 4u;
    bool ok = false;

    IMF2DBuffer *two_d = NULL;
    if (SUCCEEDED(IMFMediaBuffer_QueryInterface(buffer, &IID_IMF2DBuffer, (void**)&two_d)) && two_d) {
        /* Lock2D hands over the first row and the pitch between rows,
           negative for a bottom-up image — the case that is otherwise
           easy to get upside down. */
        BYTE *scan0 = NULL;
        LONG pitch = 0;
        if (SUCCEEDED(IMF2DBuffer_Lock2D(two_d, &scan0, &pitch)) && scan0) {
            for (int32_t y = 0; y < m->height; ++y) {
                memcpy(dst + (size_t)y * row, scan0 + (ptrdiff_t)y * pitch, row);
            }
            IMF2DBuffer_Unlock2D(two_d);
            ok = true;
        }
        IMF2DBuffer_Release(two_d);
    } else {
        BYTE *data = NULL;
        DWORD max_len = 0, cur_len = 0;
        if (SUCCEEDED(IMFMediaBuffer_Lock(buffer, &data, &max_len, &cur_len)) && data) {
            LONG stride = m->default_stride != 0 ? m->default_stride : (LONG)row;
            size_t abs_stride = (size_t)(stride < 0 ? -stride : stride);
            if (abs_stride >= row && (size_t)cur_len >= abs_stride * (size_t)m->height) {
                for (int32_t y = 0; y < m->height; ++y) {
                    int32_t src_y = stride < 0 ? (m->height - 1 - y) : y;
                    memcpy(dst + (size_t)y * row, data + (size_t)src_y * abs_stride, row);
                }
                ok = true;
            }
            IMFMediaBuffer_Unlock(buffer);
        }
    }
    IMFMediaBuffer_Release(buffer);
    return ok;
}

/* RV-062: a picture decoded on the card goes into the slot's own texture
   there, and the sample goes straight back to the decoder (holding it
   would starve the decoder's small pool of surfaces). A texture in some
   other format is read back into the slot's pixels instead. */
static bool store_gpu_picture(mf_media_t *m, media_slot_t *slot, ID3D11Texture2D *source, UINT subresource) {
    ID3D11Device *device = (ID3D11Device*)m->gpu.device;
    D3D11_TEXTURE2D_DESC desc;
    ID3D11Texture2D_GetDesc(source, &desc);
    if ((int32_t)desc.Width < m->width || (int32_t)desc.Height < m->height) return false;
    ID3D11DeviceContext *context = NULL;
    ID3D11Device_GetImmediateContext(device, &context);
    if (!context) return false;
    D3D11_BOX box = { .left = 0, .top = 0, .front = 0, .right = (UINT)m->width, .bottom = (UINT)m->height, .back = 1 };
    bool ok = false;
    if (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM) {
        if (!slot->texture) {
            /* Shared: the renderer draws with a device of its own and opens
               this one by its handle; the keyed mutex orders the two. */
            D3D11_TEXTURE2D_DESC own = {
                .Width = (UINT)m->width, .Height = (UINT)m->height, .MipLevels = 1, .ArraySize = 1,
                .Format = DXGI_FORMAT_B8G8R8A8_UNORM, .SampleDesc = { .Count = 1, .Quality = 0 },
                .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
                .MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX,
            };
            if (FAILED(ID3D11Device_CreateTexture2D(device, &own, NULL, &slot->texture))) slot->texture = NULL;
            if (slot->texture &&
                FAILED(ID3D11Texture2D_QueryInterface(slot->texture, &RV_IID_IDXGIKeyedMutex, (void**)&slot->mutex))) {
                slot->mutex = NULL;
            }
        }
        if (slot->texture && slot->mutex && IDXGIKeyedMutex_AcquireSync(slot->mutex, 0, 500) == S_OK) {
            ID3D11DeviceContext_CopySubresourceRegion(context, (ID3D11Resource*)slot->texture, 0, 0, 0, 0,
                                                      (ID3D11Resource*)source, subresource, &box);
            IDXGIKeyedMutex_ReleaseSync(slot->mutex, 0);
            slot->on_gpu = true;
            ok = true;
        }
    } else if (desc.Format == DXGI_FORMAT_B8G8R8X8_UNORM) {
        D3D11_TEXTURE2D_DESC staging = {
            .Width = (UINT)m->width, .Height = (UINT)m->height, .MipLevels = 1, .ArraySize = 1,
            .Format = desc.Format, .SampleDesc = { .Count = 1, .Quality = 0 },
            .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ,
        };
        ID3D11Texture2D *copy = NULL;
        if (SUCCEEDED(ID3D11Device_CreateTexture2D(device, &staging, NULL, &copy)) && copy) {
            ID3D11DeviceContext_CopySubresourceRegion(context, (ID3D11Resource*)copy, 0, 0, 0, 0,
                                                      (ID3D11Resource*)source, subresource, &box);
            D3D11_MAPPED_SUBRESOURCE mapped;
            if (SUCCEEDED(ID3D11DeviceContext_Map(context, (ID3D11Resource*)copy, 0, D3D11_MAP_READ, 0, &mapped))) {
                const size_t row = (size_t)m->width * 4u;
                for (int32_t y = 0; y < m->height; ++y) {
                    memcpy(slot->pixels + (size_t)y * row, (const uint8_t*)mapped.pData + (size_t)y * mapped.RowPitch, row);
                }
                ID3D11DeviceContext_Unmap(context, (ID3D11Resource*)copy, 0);
                slot->on_gpu = false;
                ok = true;
            }
            ID3D11Texture2D_Release(copy);
        }
    }
    ID3D11DeviceContext_Release(context);
    return ok;
}

static bool store_picture(mf_media_t *m, IMFSample *sample, media_slot_t *slot) {
    if (m->gpu_active) {
        IMFMediaBuffer *buffer = NULL;
        if (SUCCEEDED(IMFSample_GetBufferByIndex(sample, 0, &buffer)) && buffer) {
            IMFDXGIBuffer *dxgi = NULL;
            bool on_card = SUCCEEDED(IMFMediaBuffer_QueryInterface(buffer, &RV_IID_IMFDXGIBuffer, (void**)&dxgi)) && dxgi;
            bool ok = false;
            if (on_card) {
                ID3D11Texture2D *source = NULL;
                UINT subresource = 0;
                ok = SUCCEEDED(IMFDXGIBuffer_GetResource(dxgi, &RV_IID_ID3D11Texture2D, (void**)&source)) && source &&
                     SUCCEEDED(IMFDXGIBuffer_GetSubresourceIndex(dxgi, &subresource)) &&
                     store_gpu_picture(m, slot, source, subresource);
                if (source) ID3D11Texture2D_Release(source);
                IMFDXGIBuffer_Release(dxgi);
            }
            IMFMediaBuffer_Release(buffer);
            if (on_card) return ok;
        }
    }
    slot->on_gpu = false;
    return copy_sample(m, sample, slot->pixels);
}

/* Writes decoded samples to the ring, waiting for room. A seek or quit
   makes the rest unwanted, so it stops there. */
static void push_audio(mf_media_t *m, const float *data, size_t count, uint64_t generation) {
    while (count > 0) {
        size_t n = rubraview_pcm_ring_write(&m->pcm, data, count);
        data += n;
        count -= n;
        if (count == 0) break;
        if (atomic_load_explicit(&m->quit, memory_order_acquire) ||
            atomic_load_explicit(&m->seek_generation, memory_order_acquire) != generation) return;
        Sleep(2);
    }
}

/* One decoded audio sample into the ring. After a seek, packets wholly
   before the target are dropped and the one that straddles it is cut, so
   the sound starts where the picture does. */
static void deliver_audio(mf_media_t *m, IMFSample *sample, double pts, double *skip_until,
                          uint64_t generation) {
    IMFMediaBuffer *buffer = NULL;
    if (FAILED(IMFSample_ConvertToContiguousBuffer(sample, &buffer)) || !buffer) return;
    BYTE *data = NULL;
    DWORD max_len = 0, len = 0;
    if (SUCCEEDED(IMFMediaBuffer_Lock(buffer, &data, &max_len, &len)) && data) {
        size_t frames = len / ((size_t)m->audio_channels * sizeof(float));
        size_t skip = 0;
        if (*skip_until >= 0.0) {
            double end = pts + (double)frames / (double)m->audio_rate;
            if (end <= *skip_until + 1e-4) {
                skip = frames;
            } else {
                if (pts < *skip_until) skip = (size_t)((*skip_until - pts) * (double)m->audio_rate);
                *skip_until = -1.0;
            }
        }
        if (skip < frames) {
            push_audio(m, (const float*)(const void*)data + skip * m->audio_channels,
                       (frames - skip) * m->audio_channels, generation);
        }
        IMFMediaBuffer_Unlock(buffer);
    }
    IMFMediaBuffer_Release(buffer);
}

/* §3.16.2, on the decode thread: stop reading one sound track and start
   reading another. The caller seeks straight afterwards, which is what
   empties what the old track had already handed to the device. */
static bool mf_apply_audio_switch(mf_media_t *m, IMFSourceReader *reader, DWORD wanted) {
    DWORD previous = m->audio_stream;
    if (wanted == previous) return false;

    IMFSourceReader_SetStreamSelection(reader, previous, FALSE);
    IMFSourceReader_SetStreamSelection(reader, wanted, TRUE);
    UINT32 rate = 0, channels = 0;
    if (!mf_negotiate_audio(reader, wanted, &rate, &channels)) {
        /* Put the old one back — a track that cannot be decoded must not
           cost the sound that was playing. */
        IMFSourceReader_SetStreamSelection(reader, wanted, FALSE);
        IMFSourceReader_SetStreamSelection(reader, previous, TRUE);
        mf_negotiate_audio(reader, previous, &rate, &channels);
        return false;
    }

    if (rate != m->audio_rate || channels != m->audio_channels) {
        /* A different rate or a different number of channels is a
           different device format and a different ring size. */
        rubraview_pal_audio_close(m->audio);
        m->audio = NULL;
        free(m->pcm_storage);
        size_t capacity = (size_t)rate * channels * PCM_RING_SECONDS;
        m->pcm_storage = (float*)malloc(capacity * sizeof(float));
        if (!m->pcm_storage || !rubraview_pcm_ring_init(&m->pcm, m->pcm_storage, capacity)) {
            free(m->pcm_storage);
            m->pcm_storage = NULL;
            m->audio_stream = NO_STREAM;
            return false;
        }
        m->audio = rubraview_pal_audio_open(rate, channels, &m->pcm);
        if (!m->audio) {
            free(m->pcm_storage);
            m->pcm_storage = NULL;
            m->audio_stream = NO_STREAM;
            return false;
        }
    }
    m->audio_stream = wanted;
    m->audio_rate = rate;
    m->audio_channels = channels;
    for (size_t i = 0; i < m->tracks.count; ++i) {
        if (m->tracks.tracks[i].kind == RUBRAVIEW_TRACK_AUDIO &&
            m->tracks.tracks[i].stream_index == (int32_t)wanted) {
            m->tracks.current_audio = (int32_t)i;
        }
    }
    return true;
}

static void decode_loop(mf_media_t *m, IMFSourceReader *reader) {
    uint64_t generation = atomic_load_explicit(&m->seek_generation, memory_order_acquire);
    double skip_video = -1.0, skip_audio = -1.0;
    const bool want_video = m->video_stream != NO_STREAM;
    bool video_done = false, audio_done = false, ended = false;

    while (!atomic_load_explicit(&m->quit, memory_order_acquire)) {
        /* §3.16.2: another sound track was asked for. Reading it is this
           thread's business, so the request waits here for it. */
        int32_t asked = atomic_exchange_explicit(&m->pending_audio_stream, -1, memory_order_acq_rel);
        if (asked >= 0) {
            if (mf_apply_audio_switch(m, reader, (DWORD)asked)) audio_done = false;
        }
        const bool want_audio = m->audio_stream != NO_STREAM;

        uint64_t wanted = atomic_load_explicit(&m->seek_generation, memory_order_acquire);
        if (wanted != generation) {
            generation = wanted;
            int64_t target = atomic_load_explicit(&m->seek_target_100ns, memory_order_relaxed);
            /* Empty the sound first: this thread is not writing while the
               output thread discards, so nothing new is lost. */
            if (m->audio) rubraview_pal_audio_flush(m->audio, (double)target / 1e7);
            PROPVARIANT position;
            PropVariantInit(&position);
            position.vt = VT_I8;
            position.hVal.QuadPart = target;
            /* The reader lands on the keyframe before the target; what lies
               between is decoded and skipped below. */
            IMFSourceReader_SetCurrentPosition(reader, &GUID_NULL, &position);
            skip_video = skip_audio = (double)target / 1e7;
            video_done = audio_done = ended = false;
        }
        if (ended || (!want_video && !want_audio)) { Sleep(5); continue; }

        /* Read only what there is room for. With the picture's ring full
           the sound is still read, and the other way round — otherwise a
           full picture ring waiting on a sound clock that has run dry
           would wait for ever. */
        /* A stream is read only when it is wanted, not finished, and has
           room. "Not wanted" must never count as room: a film whose sound
           is not played (no device, as on the VM) asked for its missing
           audio stream as soon as the picture ring filled, Media
           Foundation refused, and playback stopped at the fourth frame. */
        size_t slot = 0;
        bool video_can = want_video && !video_done && rubraview_spsc_acquire_write(&m->ring, &slot);
        bool audio_can = want_audio && !audio_done &&
                         rubraview_pcm_ring_space(&m->pcm) >= AUDIO_ROOM_SAMPLES;
        if (!video_can && !audio_can) { Sleep(2); continue; }
        DWORD which = (video_can && audio_can) ? (DWORD)MF_SOURCE_READER_ANY_STREAM
                    : video_can ? m->video_stream : m->audio_stream;

        DWORD actual = 0, flags = 0;
        LONGLONG timestamp = 0;
        IMFSample *sample = NULL;
        HRESULT hr = IMFSourceReader_ReadSample(reader, which, 0, &actual, &flags, &timestamp, &sample);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) {
            if (sample) IMFSample_Release(sample);
            video_done = audio_done = true;
        } else {
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                if (actual == m->video_stream) video_done = true;
                if (actual == m->audio_stream) audio_done = true;
            }
            if (sample) {
                double pts = (double)timestamp / 1e7;
                if (video_can && actual == m->video_stream) {
                    LONGLONG duration = 0;
                    IMFSample_GetSampleDuration(sample, &duration);
                    double frame_duration = (double)duration / 1e7;
                    /* Skip frames that end at or before the target; the
                       0.1 ms of slack is for a target on a frame boundary,
                       where pts + duration lands a hair above it (seen on
                       the VM: 0.083 s + 5 s stopped at 5.042, not 5.083). */
                    bool before = skip_video >= 0.0 &&
                        pts + (frame_duration > 0.0 ? frame_duration : 1e-3) <= skip_video + 1e-4;
                    if (!before) {
                        skip_video = -1.0;
                        if (store_picture(m, sample, &m->slots[slot])) {
                            m->slots[slot].pts = pts;
                            m->slots[slot].duration = frame_duration;
                            m->slots[slot].generation = generation;
                            rubraview_spsc_commit_write(&m->ring);   /* publishes the pixels above */
                        }
                    }
                } else if (want_audio && actual == m->audio_stream) {
                    deliver_audio(m, sample, pts, &skip_audio, generation);
                }
                IMFSample_Release(sample);
            }
        }

        if ((!want_video || video_done) && (!want_audio || audio_done)) {
            atomic_store_explicit(&m->end_generation, generation, memory_order_release);
            ended = true;
        }
    }
}

static bool gpu_wanted(const mf_media_t *m) {
    return g_mf.create_dxgi_manager &&
           rubraview_media_gpu_attach(m->gpu.mode, m->gpu.device != NULL, m->gpu.decoder_profiles);
}

static bool make_manager(mf_media_t *m) {
    UINT token = 0;
    if (FAILED(g_mf.create_dxgi_manager(&token, &m->manager)) || !m->manager) { m->manager = NULL; return false; }
    if (FAILED(IMFDXGIDeviceManager_ResetDevice(m->manager, (IUnknown*)m->gpu.device, token))) {
        IMFDXGIDeviceManager_Release(m->manager);
        m->manager = NULL;
        return false;
    }
    return true;
}

/* Whatever a failed open_reader left behind, so it can be tried again. */
static void undo_open(mf_media_t *m) {
    rubraview_pal_audio_close(m->audio);
    m->audio = NULL;
    free(m->pcm_storage);
    m->pcm_storage = NULL;
    for (int i = 0; i < SLOT_COUNT; ++i) {
        free(m->slots[i].pixels);
        m->slots[i].pixels = NULL;
    }
    m->video_stream = NO_STREAM;
    m->audio_stream = NO_STREAM;
    m->ok = false;
    m->failure = RUBRAVIEW_MEDIA_FAIL_FILE;
}

/* Reads until the first picture comes out, then goes back to the start. */
static bool first_picture_arrives(mf_media_t *m, IMFSourceReader *reader) {
    for (int i = 0; i < 200; ++i) {
        DWORD actual = 0, flags = 0;
        LONGLONG timestamp = 0;
        IMFSample *sample = NULL;
        HRESULT hr = IMFSourceReader_ReadSample(reader, (DWORD)MF_SOURCE_READER_ANY_STREAM, 0, &actual, &flags,
                                                &timestamp, &sample);
        bool picture = sample && actual == m->video_stream;
        if (sample) IMFSample_Release(sample);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) return false;
        if (picture) {
            PROPVARIANT start;
            PropVariantInit(&start);
            start.vt = VT_I8;
            start.hVal.QuadPart = 0;
            IMFSourceReader_SetCurrentPosition(reader, &GUID_NULL, &start);
            return true;
        }
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) && actual == m->video_stream) return false;
    }
    return false;
}

static DWORD WINAPI decode_thread(LPVOID arg) {
    mf_media_t *m = (mf_media_t*)arg;

    bool com = SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED));
    bool mf = SUCCEEDED(g_mf.startup(MF_VERSION, MFSTARTUP_LITE));

    IMFSourceReader *reader = NULL;
    bool opened = false;
    /* RV-062: the card, where it was offered and it has decoders (or the
       diagnostic mode says to try anyway). If the reader will not open
       with it, or no picture comes out of it (T065 on the VM: a card with
       no decoders stops the decode), the film opens again without it. */
    if (mf && gpu_wanted(m) && make_manager(m)) {
        opened = open_reader(m, &reader, true);
        if (opened && m->video_stream != NO_STREAM && !first_picture_arrives(m, reader)) {
            IMFSourceReader_Release(reader);
            reader = NULL;
            undo_open(m);
            opened = false;
        }
        if (opened) {
            m->gpu_active = m->video_stream != NO_STREAM;
            m->info.hardware_decode = m->gpu_active;
        } else {
            undo_open(m);
            IMFDXGIDeviceManager_Release(m->manager);
            m->manager = NULL;
        }
    }
    if (mf && !opened) opened = open_reader(m, &reader, false);
    if (!mf) fail(m, RUBRAVIEW_MEDIA_FAIL_FILE);
    SetEvent(m->opened);

    if (opened) decode_loop(m, reader);

    if (reader) IMFSourceReader_Release(reader);
    if (mf) g_mf.shutdown();
    if (com) CoUninitialize();
    return 0;
}

/* ---- caller's thread ---- */

static void mf_close(void *handle) {
    mf_media_t *media = (mf_media_t*)handle;
    if (!media) return;
    atomic_store_explicit(&media->quit, true, memory_order_release);
    if (media->thread) {
        WaitForSingleObject(media->thread, 10000);
        CloseHandle(media->thread);
    }
    if (media->opened) CloseHandle(media->opened);
    /* The output thread reads the PCM storage, so it goes before the storage does. */
    rubraview_pal_audio_close(media->audio);
    free(media->pcm_storage);
    for (int i = 0; i < SLOT_COUNT; ++i) {
        free(media->slots[i].pixels);
        if (media->slots[i].mutex) IDXGIKeyedMutex_Release(media->slots[i].mutex);
        if (media->slots[i].texture) ID3D11Texture2D_Release(media->slots[i].texture);
    }
    if (media->manager) IMFDXGIDeviceManager_Release(media->manager);
    /* The decoder's device outlives a renderer rebuilt meanwhile: held here. */
    if (media->gpu.device) IUnknown_Release((IUnknown*)media->gpu.device);
    free(media);
}

static void *mf_open(u8str_t path, rubraview_media_failure_t *out_failure, rubraview_media_info_t *out_info,
                     const rubraview_media_gpu_t *gpu) {
    *out_failure = RUBRAVIEW_MEDIA_FAIL_FILE;
    if (!mf_load() || path.len == 0 || path.len >= MAX_PATH * 4) return NULL;

    mf_media_t *m = (mf_media_t*)calloc(1, sizeof(*m));
    if (!m) return NULL;
    if (gpu) m->gpu = *gpu;
    if (m->gpu.device) IUnknown_AddRef((IUnknown*)m->gpu.device);

    char narrow[MAX_PATH * 4];
    memcpy(narrow, path.ptr, path.len);
    narrow[path.len] = '\0';
    if (MultiByteToWideChar(CP_UTF8, 0, narrow, -1, m->path, (int)(sizeof(m->path) / sizeof(m->path[0]))) <= 0) {
        free(m);
        return NULL;
    }

    atomic_init(&m->quit, false);
    atomic_init(&m->seek_target_100ns, 0);
    atomic_init(&m->seek_generation, 0);
    atomic_init(&m->end_generation, NO_GENERATION);
    atomic_init(&m->pending_audio_stream, -1);
    m->consumer_generation = 0;
    m->video_stream = NO_STREAM;
    m->audio_stream = NO_STREAM;
    rubraview_spsc_init(&m->ring, SLOT_COUNT);
    m->failure = RUBRAVIEW_MEDIA_FAIL_FILE;

    m->opened = CreateEventW(NULL, TRUE, FALSE, NULL);
    m->thread = m->opened ? CreateThread(NULL, 0, decode_thread, m, 0, NULL) : NULL;
    if (!m->thread || WaitForSingleObject(m->opened, OPEN_TIMEOUT_MS) != WAIT_OBJECT_0) {
        mf_close(m);
        return NULL;
    }

    *out_failure = m->failure;
    if (!m->ok) {
        mf_close(m);
        return NULL;
    }
    *out_info = m->info;
    rubraview_pal_audio_set_playing(m->audio, true);   /* NULL-safe: no sound, nothing to start */
    return m;
}

static bool mf_peek_frame(void *handle, rubraview_video_frame_t *out_frame) {
    mf_media_t *media = (mf_media_t*)handle;
    if (!media || !out_frame) return false;
    for (;;) {
        size_t slot;
        if (!rubraview_spsc_peek_read(&media->ring, &slot)) return false;
        const media_slot_t *s = &media->slots[slot];
        if (s->generation != media->consumer_generation) {
            /* Decoded before the latest seek: nobody wants it now. */
            rubraview_spsc_release_read(&media->ring);
            continue;
        }
        *out_frame = (rubraview_video_frame_t){
            .pixels = s->on_gpu ? NULL : s->pixels,
            .gpu_texture = s->on_gpu ? (const void*)s->texture : NULL,
            .gpu_subresource = 0,
            .width = media->width,
            .height = media->height,
            .stride = media->width * 4,
            .pts = s->pts,
            .duration = s->duration,
        };
        return true;
    }
}

static void mf_pop_frame(void *handle) {
    mf_media_t *media = (mf_media_t*)handle;
    if (!media) return;
    size_t slot;
    if (rubraview_spsc_peek_read(&media->ring, &slot)) rubraview_spsc_release_read(&media->ring);
}

static size_t mf_frames_ready(void *handle) {
    mf_media_t *media = (mf_media_t*)handle;
    return media ? rubraview_spsc_count(&media->ring) : 0;
}

static void mf_seek(void *handle, double seconds) {
    mf_media_t *media = (mf_media_t*)handle;
    if (!media) return;
    if (!(seconds > 0.0)) seconds = 0.0;
    atomic_store_explicit(&media->seek_target_100ns, (int64_t)llround(seconds * 1e7), memory_order_relaxed);
    /* The release on the generation is what carries the target with it. */
    media->consumer_generation =
        atomic_fetch_add_explicit(&media->seek_generation, 1, memory_order_release) + 1;
}

static bool mf_finished(void *handle) {
    mf_media_t *media = (mf_media_t*)handle;
    if (!media) return true;
    rubraview_video_frame_t frame;
    if (mf_peek_frame(media, &frame)) return false;
    return atomic_load_explicit(&media->end_generation, memory_order_acquire) == media->consumer_generation &&
           rubraview_pal_audio_drained(media->audio);
}

static void mf_set_paused(void *handle, bool paused) {
    mf_media_t *media = (mf_media_t*)handle;
    if (media) rubraview_pal_audio_set_playing(media->audio, !paused);
}

static bool mf_audio_position(void *handle, double *out_position, double *out_wall) {
    mf_media_t *media = (mf_media_t*)handle;
    return media && media->audio && rubraview_pal_audio_position(media->audio, out_position, out_wall);
}

static bool mf_select_audio_track(void *handle, int32_t stream_index) {
    mf_media_t *m = (mf_media_t*)handle;
    if (!m || stream_index < 0) return false;
    /* Nothing is being played: there is no sound to switch (the VM has
       no audio device, and the stream was never even selected). */
    if (!m->audio || m->audio_stream == NO_STREAM) return false;
    if ((DWORD)stream_index == m->audio_stream) return true;
    bool known = false;
    for (size_t i = 0; i < m->tracks.count; ++i) {
        if (m->tracks.tracks[i].kind == RUBRAVIEW_TRACK_AUDIO &&
            m->tracks.tracks[i].stream_index == stream_index) known = true;
    }
    if (!known) return false;
    atomic_store_explicit(&m->pending_audio_stream, stream_index, memory_order_release);
    return true;
}

static bool mf_tracks(void *handle, rubraview_track_set_t *out_set) {
    mf_media_t *media = (mf_media_t*)handle;
    if (!media || !out_set) return false;
    /* Filled on the decode thread before `opened` was signalled, and
       never touched again — so this copy needs no lock. */
    *out_set = media->tracks;
    return media->tracks.count > 0;
}


/* RV-075: this player's own level, so two tracks can cross over. */
static void mf_set_gain(void *handle, double gain) {
    mf_media_t *media = (mf_media_t*)handle;
    if (media) rubraview_pal_audio_set_gain(media->audio, gain);
}

static const rubraview_media_backend_api_t MF_API = {
    .available = mf_available,
    .open = mf_open,
    .close = mf_close,
    .peek_frame = mf_peek_frame,
    .pop_frame = mf_pop_frame,
    .frames_ready = mf_frames_ready,
    .seek = mf_seek,
    .finished = mf_finished,
    .set_paused = mf_set_paused,
    .audio_position = mf_audio_position,
    .tracks = mf_tracks,
    .select_audio_track = mf_select_audio_track,
    .set_gain = mf_set_gain,
};

const rubraview_media_backend_api_t *rubraview_media_backend_mf(void) {
    return &MF_API;
}

#endif /* _WIN32 */
