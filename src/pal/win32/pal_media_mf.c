#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rubraview/pal/pal_media.h"

/*
 * Media Foundation backend for the media PAL (D-8), slice 1: video only,
 * the reader's own video processor converting whatever the decoder
 * produces into 32-bit RGB.
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

typedef struct media_slot {
    uint8_t *pixels;       /* width * height * 4, top row first */
    double pts, duration;
    uint64_t generation;   /* which seek this frame belongs to */
} media_slot_t;

struct rubraview_media {
    HANDLE thread;
    HANDLE opened;                          /* set by the thread once it has said yes or no */
    WCHAR path[MAX_PATH * 2];
    rubraview_media_open_result_t result;   /* written by the thread before `opened` */

    DWORD video_stream;
    int32_t width, height;
    LONG default_stride;                    /* the negotiated type's stride; negative = bottom-up */

    rubraview_spsc_t ring;
    media_slot_t slots[SLOT_COUNT];

    _Atomic bool quit;
    _Atomic int64_t seek_target_100ns;      /* written before seek_generation is bumped */
    _Atomic uint64_t seek_generation;
    _Atomic uint64_t end_generation;        /* the generation whose decode reached the end */

    uint64_t consumer_generation;           /* caller's thread only */
};

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

static struct {
    bool tried, ok;
    mf_startup_fn startup;
    mf_shutdown_fn shutdown;
    mf_create_attributes_fn create_attributes;
    mf_create_media_type_fn create_media_type;
    mf_create_reader_fn create_reader;
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
    g_mf.ok = g_mf.startup && g_mf.shutdown && g_mf.create_attributes &&
              g_mf.create_media_type && g_mf.create_reader;
    return g_mf.ok;
}

bool rubraview_pal_media_backend_available(rubraview_media_backend_t backend) {
    return backend == RUBRAVIEW_BACKEND_MEDIA_FOUNDATION && mf_load();
}

/* ---- decode thread ---- */

static void fail(rubraview_media_t *m, rubraview_media_failure_t failure) {
    m->result.media = NULL;
    m->result.failure = failure;
}

static bool open_reader(rubraview_media_t *m, IMFSourceReader **out_reader) {
    *out_reader = NULL;

    IMFAttributes *attrs = NULL;
    if (FAILED(g_mf.create_attributes(&attrs, 1)) || !attrs) { fail(m, RUBRAVIEW_MEDIA_FAIL_FILE); return false; }
    /* Lets the reader convert YUV to RGB32 itself, so the caller gets
       pixels Direct2D can take as they are. */
    IMFAttributes_SetUINT32(attrs, &MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

    IMFSourceReader *reader = NULL;
    HRESULT hr = g_mf.create_reader(m->path, attrs, &reader);
    IMFAttributes_Release(attrs);
    if (FAILED(hr) || !reader) {
        bool readable = GetFileAttributesW(m->path) != INVALID_FILE_ATTRIBUTES;
        fail(m, rubraview_media_classify(readable, false, false, 0));
        return false;
    }

    /* Find the first video stream, and note whether there is audio. */
    DWORD video_stream = (DWORD)-1;
    uint32_t fourcc = 0;
    bool has_audio = false;
    for (DWORD s = 0; ; ++s) {
        IMFMediaType *type = NULL;
        hr = IMFSourceReader_GetNativeMediaType(reader, s, 0, &type);
        if (hr == (HRESULT)MF_E_INVALIDSTREAMNUMBER) break;
        if (FAILED(hr) || !type) continue;
        GUID major = {0}, subtype = {0};
        IMFMediaType_GetGUID(type, &MF_MT_MAJOR_TYPE, &major);
        IMFMediaType_GetGUID(type, &MF_MT_SUBTYPE, &subtype);
        if (IsEqualGUID(&major, &MFMediaType_Video) && video_stream == (DWORD)-1) {
            video_stream = s;
            fourcc = (uint32_t)subtype.Data1;   /* Media Foundation subtypes carry the FOURCC here */
        } else if (IsEqualGUID(&major, &MFMediaType_Audio)) {
            has_audio = true;
        }
        IMFMediaType_Release(type);
    }
    if (video_stream == (DWORD)-1) {
        /* Audio-only files are slice 2. */
        IMFSourceReader_Release(reader);
        fail(m, RUBRAVIEW_MEDIA_FAIL_CODEC);
        return false;
    }

    IMFSourceReader_SetStreamSelection(reader, (DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    IMFSourceReader_SetStreamSelection(reader, video_stream, TRUE);

    IMFMediaType *want = NULL;
    bool decodable = false;
    if (SUCCEEDED(g_mf.create_media_type(&want)) && want) {
        IMFMediaType_SetGUID(want, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
        IMFMediaType_SetGUID(want, &MF_MT_SUBTYPE, &MFVideoFormat_RGB32);
        decodable = SUCCEEDED(IMFSourceReader_SetCurrentMediaType(reader, video_stream, NULL, want));
        IMFMediaType_Release(want);
    }
    if (!decodable) {
        IMFSourceReader_Release(reader);
        fail(m, rubraview_media_classify(true, true, false, fourcc));
        return false;
    }

    IMFMediaType *current = NULL;
    UINT64 frame_size = 0, frame_rate = 0;
    UINT32 stride = 0;
    if (SUCCEEDED(IMFSourceReader_GetCurrentMediaType(reader, video_stream, &current)) && current) {
        IMFMediaType_GetUINT64(current, &MF_MT_FRAME_SIZE, &frame_size);
        IMFMediaType_GetUINT64(current, &MF_MT_FRAME_RATE, &frame_rate);
        if (FAILED(IMFMediaType_GetUINT32(current, &MF_MT_DEFAULT_STRIDE, &stride))) stride = 0;
        IMFMediaType_Release(current);
    }
    int32_t width = (int32_t)(frame_size >> 32);
    int32_t height = (int32_t)(frame_size & 0xFFFFFFFFu);
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
    m->default_stride = (LONG)(INT32)stride;
    m->result.media = m;
    m->result.failure = RUBRAVIEW_MEDIA_OPENED;
    m->result.info = (rubraview_media_info_t){
        .backend = RUBRAVIEW_BACKEND_MEDIA_FOUNDATION,
        .duration_seconds = duration,
        .frame_rate = rate_den ? (double)rate_num / (double)rate_den : 0.0,
        .width = width,
        .height = height,
        .has_video = true,
        .has_audio = has_audio,
        .video_fourcc = fourcc,
    };
    *out_reader = reader;
    return true;
}

/* Copies one decoded sample into a slot, top row first. */
static bool copy_sample(rubraview_media_t *m, IMFSample *sample, uint8_t *dst) {
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

static void decode_loop(rubraview_media_t *m, IMFSourceReader *reader) {
    uint64_t generation = atomic_load_explicit(&m->seek_generation, memory_order_acquire);
    double skip_until = -1.0;
    bool ended = false;

    while (!atomic_load_explicit(&m->quit, memory_order_acquire)) {
        uint64_t wanted = atomic_load_explicit(&m->seek_generation, memory_order_acquire);
        if (wanted != generation) {
            generation = wanted;
            int64_t target = atomic_load_explicit(&m->seek_target_100ns, memory_order_relaxed);
            PROPVARIANT position;
            PropVariantInit(&position);
            position.vt = VT_I8;
            position.hVal.QuadPart = target;
            /* The reader lands on the keyframe before the target; the
               frames between are decoded and skipped below, so the first
               frame the caller sees is the one the target falls in. */
            IMFSourceReader_SetCurrentPosition(reader, &GUID_NULL, &position);
            skip_until = (double)target / 1e7;
            ended = false;
        }
        if (ended) { Sleep(5); continue; }

        size_t slot;
        if (!rubraview_spsc_acquire_write(&m->ring, &slot)) { Sleep(2); continue; }

        DWORD actual = 0, flags = 0;
        LONGLONG timestamp = 0;
        IMFSample *sample = NULL;
        HRESULT hr = IMFSourceReader_ReadSample(reader, m->video_stream, 0, &actual, &flags, &timestamp, &sample);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
            if (sample) IMFSample_Release(sample);
            atomic_store_explicit(&m->end_generation, generation, memory_order_release);
            ended = true;
            continue;
        }
        if (!sample) continue;   /* a gap or a format note, no picture */

        LONGLONG duration = 0;
        IMFSample_GetSampleDuration(sample, &duration);
        double pts = (double)timestamp / 1e7;
        double frame_duration = (double)duration / 1e7;

        if (skip_until >= 0.0 && pts + (frame_duration > 0.0 ? frame_duration : 1e-3) <= skip_until) {
            IMFSample_Release(sample);
            continue;
        }
        skip_until = -1.0;

        if (copy_sample(m, sample, m->slots[slot].pixels)) {
            m->slots[slot].pts = pts;
            m->slots[slot].duration = frame_duration;
            m->slots[slot].generation = generation;
            rubraview_spsc_commit_write(&m->ring);   /* publishes the pixels above */
        }
        IMFSample_Release(sample);
    }
}

static DWORD WINAPI decode_thread(LPVOID arg) {
    rubraview_media_t *m = (rubraview_media_t*)arg;

    bool com = SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED));
    bool mf = SUCCEEDED(g_mf.startup(MF_VERSION, MFSTARTUP_LITE));

    IMFSourceReader *reader = NULL;
    bool opened = mf && open_reader(m, &reader);
    if (!mf) fail(m, RUBRAVIEW_MEDIA_FAIL_FILE);
    SetEvent(m->opened);

    if (opened) decode_loop(m, reader);

    if (reader) IMFSourceReader_Release(reader);
    if (mf) g_mf.shutdown();
    if (com) CoUninitialize();
    return 0;
}

/* ---- caller's thread ---- */

void rubraview_pal_media_close(rubraview_media_t *media) {
    if (!media) return;
    atomic_store_explicit(&media->quit, true, memory_order_release);
    if (media->thread) {
        WaitForSingleObject(media->thread, 10000);
        CloseHandle(media->thread);
    }
    if (media->opened) CloseHandle(media->opened);
    for (int i = 0; i < SLOT_COUNT; ++i) free(media->slots[i].pixels);
    free(media);
}

rubraview_media_open_result_t rubraview_pal_media_open(u8str_t path, rubraview_media_backend_t backend) {
    rubraview_media_open_result_t result = { .media = NULL, .failure = RUBRAVIEW_MEDIA_FAIL_FILE };
    if (backend != RUBRAVIEW_BACKEND_MEDIA_FOUNDATION || !mf_load()) {
        /* D-8's second backend is slice 3; a Windows without Media
           Foundation has no first one either. */
        result.failure = RUBRAVIEW_MEDIA_FAIL_CONTAINER;
        return result;
    }
    if (path.len == 0 || path.len >= MAX_PATH * 4) return result;

    rubraview_media_t *m = (rubraview_media_t*)calloc(1, sizeof(*m));
    if (!m) return result;

    char narrow[MAX_PATH * 4];
    memcpy(narrow, path.ptr, path.len);
    narrow[path.len] = '\0';
    if (MultiByteToWideChar(CP_UTF8, 0, narrow, -1, m->path, (int)(sizeof(m->path) / sizeof(m->path[0]))) <= 0) {
        free(m);
        return result;
    }

    atomic_init(&m->quit, false);
    atomic_init(&m->seek_target_100ns, 0);
    atomic_init(&m->seek_generation, 0);
    atomic_init(&m->end_generation, NO_GENERATION);
    m->consumer_generation = 0;
    rubraview_spsc_init(&m->ring, SLOT_COUNT);
    m->result.failure = RUBRAVIEW_MEDIA_FAIL_FILE;

    m->opened = CreateEventW(NULL, TRUE, FALSE, NULL);
    m->thread = m->opened ? CreateThread(NULL, 0, decode_thread, m, 0, NULL) : NULL;
    if (!m->thread || WaitForSingleObject(m->opened, OPEN_TIMEOUT_MS) != WAIT_OBJECT_0) {
        rubraview_pal_media_close(m);
        return result;
    }

    result = m->result;
    if (result.failure != RUBRAVIEW_MEDIA_OPENED) {
        rubraview_pal_media_close(m);
        result.media = NULL;
    }
    return result;
}

bool rubraview_pal_media_peek_frame(rubraview_media_t *media, rubraview_video_frame_t *out_frame) {
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
            .pixels = s->pixels,
            .width = media->width,
            .height = media->height,
            .stride = media->width * 4,
            .pts = s->pts,
            .duration = s->duration,
        };
        return true;
    }
}

void rubraview_pal_media_pop_frame(rubraview_media_t *media) {
    if (!media) return;
    size_t slot;
    if (rubraview_spsc_peek_read(&media->ring, &slot)) rubraview_spsc_release_read(&media->ring);
}

size_t rubraview_pal_media_frames_ready(rubraview_media_t *media) {
    return media ? rubraview_spsc_count(&media->ring) : 0;
}

void rubraview_pal_media_seek(rubraview_media_t *media, double seconds) {
    if (!media) return;
    if (!(seconds > 0.0)) seconds = 0.0;
    atomic_store_explicit(&media->seek_target_100ns, (int64_t)llround(seconds * 1e7), memory_order_relaxed);
    /* The release on the generation is what carries the target with it. */
    media->consumer_generation =
        atomic_fetch_add_explicit(&media->seek_generation, 1, memory_order_release) + 1;
}

bool rubraview_pal_media_finished(rubraview_media_t *media) {
    if (!media) return true;
    rubraview_video_frame_t frame;
    if (rubraview_pal_media_peek_frame(media, &frame)) return false;
    return atomic_load_explicit(&media->end_generation, memory_order_acquire) == media->consumer_generation;
}

#endif /* _WIN32 */
