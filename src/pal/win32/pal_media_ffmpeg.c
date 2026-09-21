#ifdef _WIN32
#include <windows.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/channel_layout.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include "rubraview/pal/pal_media_backend.h"
#include "rubraview/pal/pal_audio.h"

/*
 * The FFmpeg backend (D-8, RV-016). The DLLs are loaded at run time and
 * nothing of FFmpeg is linked in, so rubraview still runs where they are
 * absent — then this backend simply reports itself unavailable and files
 * it would have opened go to Media Foundation, or are reported (D-9).
 *
 * The vendored headers describe one FFmpeg version. A different major
 * version lays its structures out differently, so the loader asks each
 * library for its own version and refuses a mismatch rather than reading
 * another build's memory as if it were this one's.
 *
 * Threads are as in the Media Foundation backend: one decode thread owns
 * everything FFmpeg hands out and reaches the caller only through the
 * frame ring, the PCM ring and a few atomics.
 */

#define SLOT_COUNT 4
#define MAX_DIMENSION 16384
#define OPEN_TIMEOUT_MS 15000
#define NO_GENERATION UINT64_MAX
#define PCM_RING_SECONDS 2
#define VIDEO_ROOM_WAIT_MS 300   /* then the frame is dropped, so the sound never waits on the picture */

typedef struct ff_slot {
    uint8_t *pixels;
    double pts, duration;
    uint64_t generation;
} ff_slot_t;

typedef struct ff_media {
    HANDLE thread, opened;
    char path[MAX_PATH * 4];

    bool ok;
    rubraview_media_failure_t failure;
    rubraview_media_info_t info;

    int video_stream, audio_stream;
    int32_t width, height;
    uint32_t audio_rate, audio_channels;

    rubraview_spsc_t ring;
    ff_slot_t slots[SLOT_COUNT];
    rubraview_pcm_ring_t pcm;
    float *pcm_storage;
    rubraview_audio_out_t *audio;

    _Atomic bool quit, paused;
    _Atomic int32_t pending_audio_stream;   /* §3.16.2: -1 when nothing was asked */
    _Atomic int64_t seek_target_100ns;
    _Atomic uint64_t seek_generation, end_generation;
    uint64_t consumer_generation;

    /* §3.16.2: the container's tracks, read once while opening. */
    rubraview_track_set_t tracks;
    char track_text[RUBRAVIEW_MAX_TRACKS][3][64];   /* language, title, codec */
} ff_media_t;

/* ---- the DLLs ---- */

typedef struct ff_api {
    bool tried, ok;
    /* avformat */
    unsigned (*avformat_version)(void);
    int (*avformat_open_input)(AVFormatContext**, const char*, const AVInputFormat*, AVDictionary**);
    void (*avformat_close_input)(AVFormatContext**);
    int (*avformat_find_stream_info)(AVFormatContext*, AVDictionary**);
    int (*av_read_frame)(AVFormatContext*, AVPacket*);
    int (*av_seek_frame)(AVFormatContext*, int, int64_t, int);
    int (*av_find_best_stream)(AVFormatContext*, enum AVMediaType, int, int, const struct AVCodec**, int);
    /* avcodec */
    unsigned (*avcodec_version)(void);
    const AVCodec *(*avcodec_find_decoder)(enum AVCodecID);
    AVCodecContext *(*avcodec_alloc_context3)(const AVCodec*);
    void (*avcodec_free_context)(AVCodecContext**);
    int (*avcodec_parameters_to_context)(AVCodecContext*, const AVCodecParameters*);
    int (*avcodec_open2)(AVCodecContext*, const AVCodec*, AVDictionary**);
    int (*avcodec_send_packet)(AVCodecContext*, const AVPacket*);
    int (*avcodec_receive_frame)(AVCodecContext*, AVFrame*);
    void (*avcodec_flush_buffers)(AVCodecContext*);
    AVPacket *(*av_packet_alloc)(void);
    void (*av_packet_free)(AVPacket**);
    void (*av_packet_unref)(AVPacket*);
    /* avutil */
    unsigned (*avutil_version)(void);
    AVFrame *(*av_frame_alloc)(void);
    void (*av_frame_free)(AVFrame**);
    void (*av_frame_unref)(AVFrame*);
    void (*av_log_set_level)(int);
    void (*av_channel_layout_default)(AVChannelLayout*, int);
    AVDictionaryEntry *(*av_dict_get)(const AVDictionary*, const char*, const AVDictionaryEntry*, int);
    const char *(*avcodec_get_name)(enum AVCodecID);
    /* swscale, swresample */
    unsigned (*swscale_version)(void);
    struct SwsContext *(*sws_getContext)(int, int, enum AVPixelFormat, int, int, enum AVPixelFormat,
                                         int, SwsFilter*, SwsFilter*, const double*);
    int (*sws_scale)(struct SwsContext*, const uint8_t *const[], const int[], int, int,
                     uint8_t *const[], const int[]);
    void (*sws_freeContext)(struct SwsContext*);
    unsigned (*swresample_version)(void);
    int (*swr_alloc_set_opts2)(struct SwrContext**, const AVChannelLayout*, enum AVSampleFormat, int,
                               const AVChannelLayout*, enum AVSampleFormat, int, int, void*);
    int (*swr_init)(struct SwrContext*);
    int (*swr_convert)(struct SwrContext*, uint8_t *const*, int, const uint8_t *const*, int);
    void (*swr_free)(struct SwrContext**);
} ff_api_t;

static ff_api_t g_ff;

static HMODULE load_one(const char *versioned, const char *plain) {
    WCHAR wide[64];
    HMODULE module = NULL;
    if (MultiByteToWideChar(CP_UTF8, 0, versioned, -1, wide, 64) > 0) {
        module = LoadLibraryExW(wide, NULL, LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                                            LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    }
    if (!module && MultiByteToWideChar(CP_UTF8, 0, plain, -1, wide, 64) > 0) {
        module = LoadLibraryExW(wide, NULL, LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                                            LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    }
    return module;
}

#define BIND(module, name) do { \
    g_ff.name = (void*)GetProcAddress(module, #name); \
    if (!g_ff.name) return false; \
} while (0)

static bool bind_all(HMODULE fmt, HMODULE codec, HMODULE util, HMODULE sws, HMODULE swr) {
    BIND(fmt, avformat_version); BIND(fmt, avformat_open_input); BIND(fmt, avformat_close_input);
    BIND(fmt, avformat_find_stream_info); BIND(fmt, av_read_frame); BIND(fmt, av_seek_frame);
    BIND(fmt, av_find_best_stream);
    BIND(codec, avcodec_version); BIND(codec, avcodec_find_decoder); BIND(codec, avcodec_alloc_context3);
    BIND(codec, avcodec_free_context); BIND(codec, avcodec_parameters_to_context); BIND(codec, avcodec_open2);
    BIND(codec, avcodec_send_packet); BIND(codec, avcodec_receive_frame); BIND(codec, avcodec_flush_buffers);
    BIND(codec, av_packet_alloc); BIND(codec, av_packet_free); BIND(codec, av_packet_unref);
    BIND(util, avutil_version); BIND(util, av_frame_alloc); BIND(util, av_frame_free);
    BIND(util, av_frame_unref); BIND(util, av_log_set_level); BIND(util, av_channel_layout_default);
    BIND(util, av_dict_get); BIND(codec, avcodec_get_name);
    BIND(sws, swscale_version); BIND(sws, sws_getContext); BIND(sws, sws_scale); BIND(sws, sws_freeContext);
    BIND(swr, swresample_version); BIND(swr, swr_alloc_set_opts2); BIND(swr, swr_init);
    BIND(swr, swr_convert); BIND(swr, swr_free);
    return true;
}

/* The headers here describe one version; a different major lays its
   structures out differently, and reading those would be reading another
   program's idea of memory. */
static bool versions_match(void) {
    return AV_VERSION_MAJOR(g_ff.avformat_version()) == LIBAVFORMAT_VERSION_MAJOR &&
           AV_VERSION_MAJOR(g_ff.avcodec_version()) == LIBAVCODEC_VERSION_MAJOR &&
           AV_VERSION_MAJOR(g_ff.avutil_version()) == LIBAVUTIL_VERSION_MAJOR &&
           AV_VERSION_MAJOR(g_ff.swscale_version()) == LIBSWSCALE_VERSION_MAJOR &&
           AV_VERSION_MAJOR(g_ff.swresample_version()) == LIBSWRESAMPLE_VERSION_MAJOR;
}

static bool ff_load(void) {
    if (g_ff.tried) return g_ff.ok;
    g_ff.tried = true;
    HMODULE util = load_one("avutil-" AV_STRINGIFY(LIBAVUTIL_VERSION_MAJOR) ".dll", "avutil.dll");
    HMODULE codec = load_one("avcodec-" AV_STRINGIFY(LIBAVCODEC_VERSION_MAJOR) ".dll", "avcodec.dll");
    HMODULE fmt = load_one("avformat-" AV_STRINGIFY(LIBAVFORMAT_VERSION_MAJOR) ".dll", "avformat.dll");
    HMODULE sws = load_one("swscale-" AV_STRINGIFY(LIBSWSCALE_VERSION_MAJOR) ".dll", "swscale.dll");
    HMODULE swr = load_one("swresample-" AV_STRINGIFY(LIBSWRESAMPLE_VERSION_MAJOR) ".dll", "swresample.dll");
    if (!util || !codec || !fmt || !sws || !swr) return false;
    if (!bind_all(fmt, codec, util, sws, swr)) return false;
    if (!versions_match()) return false;
    g_ff.av_log_set_level(AV_LOG_ERROR);
    g_ff.ok = true;
    return true;
}

static bool ff_available(void) { return ff_load(); }

/* ---- decode thread ---- */

typedef struct ff_decode {
    AVFormatContext *format;
    AVCodecContext *video, *audio;
    struct SwsContext *sws;
    struct SwrContext *swr;
    AVPacket *packet;
    AVFrame *frame;
} ff_decode_t;

static double stream_seconds(AVFormatContext *format, int index, int64_t pts) {
    if (pts == AV_NOPTS_VALUE || index < 0) return 0.0;
    return (double)pts * av_q2d(format->streams[index]->time_base);
}

/* Opens one audio stream's decoder and the resampler that turns it into
   the float samples the output wants. Used when the file is opened and
   again when another sound track is chosen (§3.16.2). */
static bool ff_open_audio_stream(ff_media_t *m, ff_decode_t *d, int stream_index) {
    if (stream_index < 0 || (unsigned)stream_index >= d->format->nb_streams) return false;
    AVCodecParameters *par = d->format->streams[stream_index]->codecpar;
    const AVCodec *decoder = g_ff.avcodec_find_decoder(par->codec_id);
    if (!decoder || par->sample_rate <= 0 || par->ch_layout.nb_channels <= 0 ||
        par->ch_layout.nb_channels > 8) return false;

    AVCodecContext *context = g_ff.avcodec_alloc_context3(decoder);
    if (!context || g_ff.avcodec_parameters_to_context(context, par) < 0 ||
        g_ff.avcodec_open2(context, decoder, NULL) < 0) {
        if (context) g_ff.avcodec_free_context(&context);
        return false;
    }

    struct SwrContext *swr = NULL;
    AVChannelLayout out_layout;
    g_ff.av_channel_layout_default(&out_layout, par->ch_layout.nb_channels);
    if (g_ff.swr_alloc_set_opts2(&swr, &out_layout, AV_SAMPLE_FMT_FLT, par->sample_rate,
                                 &context->ch_layout, context->sample_fmt,
                                 context->sample_rate, 0, NULL) < 0 || !swr ||
        g_ff.swr_init(swr) < 0) {
        if (swr) g_ff.swr_free(&swr);
        g_ff.avcodec_free_context(&context);
        return false;
    }

    if (d->audio) g_ff.avcodec_free_context(&d->audio);
    if (d->swr) g_ff.swr_free(&d->swr);
    d->audio = context;
    d->swr = swr;
    m->audio_rate = (uint32_t)par->sample_rate;
    m->audio_channels = (uint32_t)par->ch_layout.nb_channels;
    return true;
}

/* §3.16.1 / D-12: the subtitle formats that are text. A picture-based
   one (DVD, Blu-ray, DVB) would need a second image path to draw, so it
   is not offered at all rather than offered and then refused. */
static bool ff_subtitle_is_text(enum AVCodecID id) {
    return id == AV_CODEC_ID_SUBRIP || id == AV_CODEC_ID_TEXT ||
           id == AV_CODEC_ID_WEBVTT || id == AV_CODEC_ID_MOV_TEXT ||
           id == AV_CODEC_ID_ASS || id == AV_CODEC_ID_SSA;
}

/* ---- what the container holds (§3.16.2) ---- */

static u8str_t ff_copy(char *buffer, size_t capacity, const char *text) {
    buffer[0] = '\0';
    if (!text || !text[0]) return (u8str_t){ .ptr = buffer, .len = 0 };
    size_t len = strlen(text);
    if (len >= capacity) len = capacity - 1;
    memcpy(buffer, text, len);
    buffer[len] = '\0';
    return (u8str_t){ .ptr = buffer, .len = len };
}

/* Text subtitle streams are deliberately left out: nothing displays
   them yet (D-10), and a track nobody can pick is not a choice. */
static void ff_collect_tracks(ff_media_t *m, AVFormatContext *format) {
    m->tracks = rubraview_tracks_create();
    for (unsigned i = 0; i < format->nb_streams && m->tracks.count < RUBRAVIEW_MAX_TRACKS; ++i) {
        const AVStream *stream = format->streams[i];
        const AVCodecParameters *par = stream->codecpar;
        rubraview_track_kind_t kind;
        if (par->codec_type == AVMEDIA_TYPE_VIDEO) kind = RUBRAVIEW_TRACK_VIDEO;
        else if (par->codec_type == AVMEDIA_TYPE_AUDIO) kind = RUBRAVIEW_TRACK_AUDIO;
        else if (par->codec_type == AVMEDIA_TYPE_SUBTITLE && ff_subtitle_is_text(par->codec_id))
            kind = RUBRAVIEW_TRACK_SUBTITLE;
        else continue;   /* picture-based subtitles are not listed: nothing can draw them (D-12) */

        size_t slot = m->tracks.count;
        const AVDictionaryEntry *language = g_ff.av_dict_get(stream->metadata, "language", NULL, 0);
        const AVDictionaryEntry *title = g_ff.av_dict_get(stream->metadata, "title", NULL, 0);
        rubraview_track_t track = {
            .kind = kind,
            .stream_index = (int32_t)i,
            .language = ff_copy(m->track_text[slot][0], sizeof(m->track_text[slot][0]),
                                language ? language->value : NULL),
            .title = ff_copy(m->track_text[slot][1], sizeof(m->track_text[slot][1]),
                             title ? title->value : NULL),
            .codec = ff_copy(m->track_text[slot][2], sizeof(m->track_text[slot][2]),
                             g_ff.avcodec_get_name(par->codec_id)),
            .channels = kind == RUBRAVIEW_TRACK_AUDIO ? par->ch_layout.nb_channels : 0,
            .is_default = (stream->disposition & AV_DISPOSITION_DEFAULT) != 0,
            .is_forced = (stream->disposition & AV_DISPOSITION_FORCED) != 0,
        };
        rubraview_tracks_add(&m->tracks, track);
    }
    for (size_t i = 0; i < m->tracks.count; ++i) {
        if (m->tracks.tracks[i].stream_index == m->video_stream &&
            m->tracks.tracks[i].kind == RUBRAVIEW_TRACK_VIDEO) m->tracks.current_video = (int32_t)i;
        if (m->tracks.tracks[i].stream_index == m->audio_stream &&
            m->tracks.tracks[i].kind == RUBRAVIEW_TRACK_AUDIO) m->tracks.current_audio = (int32_t)i;
    }
}

static bool open_input(ff_media_t *m, ff_decode_t *d) {
    if (g_ff.avformat_open_input(&d->format, m->path, NULL, NULL) < 0 || !d->format) {
        m->failure = GetFileAttributesA(m->path) != INVALID_FILE_ATTRIBUTES
            ? RUBRAVIEW_MEDIA_FAIL_CONTAINER : RUBRAVIEW_MEDIA_FAIL_FILE;
        return false;
    }
    if (g_ff.avformat_find_stream_info(d->format, NULL) < 0) {
        m->failure = RUBRAVIEW_MEDIA_FAIL_CONTAINER;
        return false;
    }

    m->video_stream = g_ff.av_find_best_stream(d->format, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    m->audio_stream = g_ff.av_find_best_stream(d->format, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (m->video_stream < 0 && m->audio_stream < 0) {
        m->failure = RUBRAVIEW_MEDIA_FAIL_CODEC;
        return false;
    }
    ff_collect_tracks(m, d->format);

    if (m->video_stream >= 0) {
        AVCodecParameters *par = d->format->streams[m->video_stream]->codecpar;
        const AVCodec *decoder = g_ff.avcodec_find_decoder(par->codec_id);
        uint32_t fourcc = par->codec_tag ? (uint32_t)par->codec_tag
                        : (par->codec_id == AV_CODEC_ID_HEVC ? RUBRAVIEW_FOURCC('H', 'E', 'V', 'C') : 0);
        if (!decoder || par->width <= 0 || par->height <= 0 ||
            par->width > MAX_DIMENSION || par->height > MAX_DIMENSION) {
            m->failure = rubraview_media_classify(true, true, false, fourcc);
            return false;
        }
        d->video = g_ff.avcodec_alloc_context3(decoder);
        if (!d->video || g_ff.avcodec_parameters_to_context(d->video, par) < 0 ||
            g_ff.avcodec_open2(d->video, decoder, NULL) < 0) {
            m->failure = rubraview_media_classify(true, true, false, fourcc);
            return false;
        }
        m->width = par->width;
        m->height = par->height;
        for (int i = 0; i < SLOT_COUNT; ++i) {
            m->slots[i].pixels = (uint8_t*)malloc((size_t)m->width * (size_t)m->height * 4u);
            if (!m->slots[i].pixels) { m->failure = RUBRAVIEW_MEDIA_FAIL_FILE; return false; }
        }
    }

    if (m->audio_stream >= 0) {
        bool ready = ff_open_audio_stream(m, d, m->audio_stream);
        if (ready) {
            size_t capacity = (size_t)m->audio_rate * m->audio_channels * PCM_RING_SECONDS;
            m->pcm_storage = (float*)malloc(capacity * sizeof(float));
            if (m->pcm_storage && rubraview_pcm_ring_init(&m->pcm, m->pcm_storage, capacity)) {
                m->audio = rubraview_pal_audio_open(m->audio_rate, m->audio_channels, &m->pcm);
            }
        }
        if (!m->audio) {
            /* No device, or a sound we cannot decode: the picture plays alone. */
            m->audio_stream = -1;
            free(m->pcm_storage);
            m->pcm_storage = NULL;
        }
    }

    AVRational rate = m->video_stream >= 0 ? d->format->streams[m->video_stream]->avg_frame_rate
                                           : (AVRational){0, 1};
    m->ok = true;
    m->failure = RUBRAVIEW_MEDIA_OPENED;
    m->info = (rubraview_media_info_t){
        .backend = RUBRAVIEW_BACKEND_FFMPEG,
        .duration_seconds = d->format->duration > 0 ? (double)d->format->duration / AV_TIME_BASE : 0.0,
        .frame_rate = rate.den > 0 ? av_q2d(rate) : 0.0,
        .width = m->width,
        .height = m->height,
        .has_video = m->video_stream >= 0,
        .has_audio = m->audio_stream >= 0 || g_ff.av_find_best_stream(d->format, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0) >= 0,
        .audio_output = m->audio != NULL,
        .video_fourcc = m->video_stream >= 0 ? (uint32_t)d->format->streams[m->video_stream]->codecpar->codec_tag : 0,
    };
    return true;
}

static void free_decode(ff_decode_t *d) {
    if (d->sws) g_ff.sws_freeContext(d->sws);
    if (d->swr) g_ff.swr_free(&d->swr);
    if (d->video) g_ff.avcodec_free_context(&d->video);
    if (d->audio) g_ff.avcodec_free_context(&d->audio);
    if (d->frame) g_ff.av_frame_free(&d->frame);
    if (d->packet) g_ff.av_packet_free(&d->packet);
    if (d->format) g_ff.avformat_close_input(&d->format);
}

/* Puts one decoded picture in the ring, waiting for room while playing
   but never longer than VIDEO_ROOM_WAIT_MS: the sound must not end up
   waiting on the picture. Returns false when the caller should stop. */
static bool store_video(ff_media_t *m, ff_decode_t *d, double pts, double duration, uint64_t generation) {
    size_t slot = 0;
    for (int waited = 0; !rubraview_spsc_acquire_write(&m->ring, &slot); waited += 5) {
        if (atomic_load_explicit(&m->quit, memory_order_acquire) ||
            atomic_load_explicit(&m->seek_generation, memory_order_acquire) != generation) return false;
        bool paused = atomic_load_explicit(&m->paused, memory_order_acquire);
        if (!paused && waited >= VIDEO_ROOM_WAIT_MS) return true;   /* drop this picture */
        Sleep(5);
    }

    d->sws = g_ff.sws_getContext(d->frame->width, d->frame->height, (enum AVPixelFormat)d->frame->format,
                                 m->width, m->height, AV_PIX_FMT_BGRA, SWS_BILINEAR, NULL, NULL, NULL);
    if (!d->sws) return true;
    uint8_t *dst[4] = { m->slots[slot].pixels, NULL, NULL, NULL };
    int stride[4] = { m->width * 4, 0, 0, 0 };
    g_ff.sws_scale(d->sws, (const uint8_t *const*)d->frame->data, d->frame->linesize, 0, d->frame->height,
                   dst, stride);
    g_ff.sws_freeContext(d->sws);
    d->sws = NULL;

    m->slots[slot].pts = pts;
    m->slots[slot].duration = duration;
    m->slots[slot].generation = generation;
    rubraview_spsc_commit_write(&m->ring);
    return true;
}

static void store_audio(ff_media_t *m, ff_decode_t *d, double pts, double *skip_until, uint64_t generation) {
    int max_out = d->frame->nb_samples + 256;
    float *converted = (float*)malloc((size_t)max_out * m->audio_channels * sizeof(float));
    if (!converted) return;
    uint8_t *out[1] = { (uint8_t*)(void*)converted };
    int frames = g_ff.swr_convert(d->swr, out, max_out, (const uint8_t *const*)d->frame->extended_data,
                                  d->frame->nb_samples);
    if (frames > 0) {
        size_t skip = 0;
        if (*skip_until >= 0.0) {
            double end = pts + (double)frames / (double)m->audio_rate;
            if (end <= *skip_until + 1e-4) skip = (size_t)frames;
            else {
                if (pts < *skip_until) skip = (size_t)((*skip_until - pts) * (double)m->audio_rate);
                *skip_until = -1.0;
            }
        }
        const float *data = converted + skip * m->audio_channels;
        size_t count = ((size_t)frames > skip ? (size_t)frames - skip : 0) * m->audio_channels;
        while (count > 0) {
            size_t n = rubraview_pcm_ring_write(&m->pcm, data, count);
            data += n;
            count -= n;
            if (count == 0) break;
            if (atomic_load_explicit(&m->quit, memory_order_acquire) ||
                atomic_load_explicit(&m->seek_generation, memory_order_acquire) != generation) break;
            Sleep(2);
        }
    }
    free(converted);
}

static void decode_loop(ff_media_t *m, ff_decode_t *d) {
    uint64_t generation = atomic_load_explicit(&m->seek_generation, memory_order_acquire);
    double skip_video = -1.0, skip_audio = -1.0;
    bool ended = false;

    while (!atomic_load_explicit(&m->quit, memory_order_acquire)) {
        /* §3.16.2: another sound track was asked for. Only this thread
           may touch the decoder, so the request is applied here. */
        int32_t asked = atomic_exchange_explicit(&m->pending_audio_stream, -1, memory_order_acq_rel);
        if (asked >= 0 && asked != m->audio_stream) {
            uint32_t old_rate = m->audio_rate, old_channels = m->audio_channels;
            if (ff_open_audio_stream(m, d, asked)) {
                m->audio_stream = asked;
                for (size_t i = 0; i < m->tracks.count; ++i) {
                    if (m->tracks.tracks[i].kind == RUBRAVIEW_TRACK_AUDIO &&
                        m->tracks.tracks[i].stream_index == asked) m->tracks.current_audio = (int32_t)i;
                }
                if (m->audio_rate != old_rate || m->audio_channels != old_channels) {
                    /* A different format is a different device and a
                       different ring; the old sound is dropped, not mixed. */
                    rubraview_pal_audio_close(m->audio);
                    m->audio = NULL;
                    free(m->pcm_storage);
                    size_t capacity = (size_t)m->audio_rate * m->audio_channels * PCM_RING_SECONDS;
                    m->pcm_storage = (float*)malloc(capacity * sizeof(float));
                    if (m->pcm_storage && rubraview_pcm_ring_init(&m->pcm, m->pcm_storage, capacity)) {
                        m->audio = rubraview_pal_audio_open(m->audio_rate, m->audio_channels, &m->pcm);
                    }
                    if (!m->audio) {
                        free(m->pcm_storage);
                        m->pcm_storage = NULL;
                        m->audio_stream = -1;
                    }
                }
            }
        }

        uint64_t wanted = atomic_load_explicit(&m->seek_generation, memory_order_acquire);
        if (wanted != generation) {
            generation = wanted;
            int64_t target = atomic_load_explicit(&m->seek_target_100ns, memory_order_relaxed);
            double seconds = (double)target / 1e7;
            if (m->audio) rubraview_pal_audio_flush(m->audio, seconds);
            g_ff.av_seek_frame(d->format, -1, (int64_t)(seconds * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD);
            if (d->video) g_ff.avcodec_flush_buffers(d->video);
            if (d->audio) g_ff.avcodec_flush_buffers(d->audio);
            skip_video = skip_audio = seconds;
            ended = false;
        }
        if (ended) { Sleep(5); continue; }

        int read = g_ff.av_read_frame(d->format, d->packet);
        if (read < 0) {
            atomic_store_explicit(&m->end_generation, generation, memory_order_release);
            ended = true;
            continue;
        }

        bool is_video = d->packet->stream_index == m->video_stream && d->video;
        bool is_audio = d->packet->stream_index == m->audio_stream && d->audio;
        AVCodecContext *decoder = is_video ? d->video : is_audio ? d->audio : NULL;
        if (decoder && g_ff.avcodec_send_packet(decoder, d->packet) >= 0) {
            while (g_ff.avcodec_receive_frame(decoder, d->frame) >= 0) {
                double pts = stream_seconds(d->format, d->packet->stream_index, d->frame->best_effort_timestamp);
                if (is_video) {
                    double duration = d->frame->duration > 0
                        ? stream_seconds(d->format, m->video_stream, d->frame->duration)
                        : (m->info.frame_rate > 0.0 ? 1.0 / m->info.frame_rate : 1.0 / 30.0);
                    bool before = skip_video >= 0.0 && pts + duration <= skip_video + 1e-4;
                    if (!before) {
                        skip_video = -1.0;
                        if (!store_video(m, d, pts, duration, generation)) { g_ff.av_frame_unref(d->frame); break; }
                    }
                } else {
                    store_audio(m, d, pts, &skip_audio, generation);
                }
                g_ff.av_frame_unref(d->frame);
            }
        }
        g_ff.av_packet_unref(d->packet);
    }
}

static DWORD WINAPI decode_thread(LPVOID arg) {
    ff_media_t *m = (ff_media_t*)arg;
    ff_decode_t d = {0};
    d.packet = g_ff.av_packet_alloc();
    d.frame = g_ff.av_frame_alloc();
    bool opened = d.packet && d.frame && open_input(m, &d);
    SetEvent(m->opened);
    if (opened) decode_loop(m, &d);
    free_decode(&d);
    return 0;
}

/* ---- the caller's thread ---- */

static void ff_close(void *handle) {
    ff_media_t *m = (ff_media_t*)handle;
    if (!m) return;
    atomic_store_explicit(&m->quit, true, memory_order_release);
    if (m->thread) {
        WaitForSingleObject(m->thread, 10000);
        CloseHandle(m->thread);
    }
    if (m->opened) CloseHandle(m->opened);
    rubraview_pal_audio_close(m->audio);
    free(m->pcm_storage);
    for (int i = 0; i < SLOT_COUNT; ++i) free(m->slots[i].pixels);
    free(m);
}

static void *ff_open(u8str_t path, rubraview_media_failure_t *out_failure, rubraview_media_info_t *out_info,
                     const rubraview_media_gpu_t *gpu) {
    /* RV-062: D3D11VA hands back NV12 textures, which need a colour
       conversion on the card of their own; FFmpeg decodes in software. */
    (void)gpu;
    *out_failure = RUBRAVIEW_MEDIA_FAIL_FILE;
    if (!ff_load() || path.len == 0 || path.len >= MAX_PATH * 4) return NULL;

    ff_media_t *m = (ff_media_t*)calloc(1, sizeof(*m));
    if (!m) return NULL;
    memcpy(m->path, path.ptr, path.len);   /* FFmpeg takes UTF-8 paths on Windows */
    m->path[path.len] = '\0';
    m->video_stream = -1;
    m->audio_stream = -1;
    m->failure = RUBRAVIEW_MEDIA_FAIL_FILE;
    atomic_init(&m->quit, false);
    atomic_init(&m->paused, false);
    atomic_init(&m->seek_target_100ns, 0);
    atomic_init(&m->seek_generation, 0);
    atomic_init(&m->end_generation, NO_GENERATION);
    atomic_init(&m->pending_audio_stream, -1);
    rubraview_spsc_init(&m->ring, SLOT_COUNT);

    m->opened = CreateEventW(NULL, TRUE, FALSE, NULL);
    m->thread = m->opened ? CreateThread(NULL, 0, decode_thread, m, 0, NULL) : NULL;
    if (!m->thread || WaitForSingleObject(m->opened, OPEN_TIMEOUT_MS) != WAIT_OBJECT_0) {
        ff_close(m);
        return NULL;
    }
    *out_failure = m->failure;
    if (!m->ok) {
        ff_close(m);
        return NULL;
    }
    *out_info = m->info;
    rubraview_pal_audio_set_playing(m->audio, true);
    return m;
}

static bool ff_peek_frame(void *handle, rubraview_video_frame_t *out_frame) {
    ff_media_t *m = (ff_media_t*)handle;
    if (!m || !out_frame) return false;
    for (;;) {
        size_t slot;
        if (!rubraview_spsc_peek_read(&m->ring, &slot)) return false;
        const ff_slot_t *s = &m->slots[slot];
        if (s->generation != m->consumer_generation) {
            rubraview_spsc_release_read(&m->ring);   /* decoded before the latest seek */
            continue;
        }
        *out_frame = (rubraview_video_frame_t){
            .pixels = s->pixels, .width = m->width, .height = m->height,
            .stride = m->width * 4, .pts = s->pts, .duration = s->duration,
        };
        return true;
    }
}

static void ff_pop_frame(void *handle) {
    ff_media_t *m = (ff_media_t*)handle;
    size_t slot;
    if (m && rubraview_spsc_peek_read(&m->ring, &slot)) rubraview_spsc_release_read(&m->ring);
}

static size_t ff_frames_ready(void *handle) {
    ff_media_t *m = (ff_media_t*)handle;
    return m ? rubraview_spsc_count(&m->ring) : 0;
}

static void ff_seek(void *handle, double seconds) {
    ff_media_t *m = (ff_media_t*)handle;
    if (!m) return;
    if (!(seconds > 0.0)) seconds = 0.0;
    atomic_store_explicit(&m->seek_target_100ns, (int64_t)llround(seconds * 1e7), memory_order_relaxed);
    m->consumer_generation = atomic_fetch_add_explicit(&m->seek_generation, 1, memory_order_release) + 1;
}

static bool ff_finished(void *handle) {
    ff_media_t *m = (ff_media_t*)handle;
    if (!m) return true;
    rubraview_video_frame_t frame;
    if (ff_peek_frame(m, &frame)) return false;
    return atomic_load_explicit(&m->end_generation, memory_order_acquire) == m->consumer_generation &&
           rubraview_pal_audio_drained(m->audio);
}

static void ff_set_paused(void *handle, bool paused) {
    ff_media_t *m = (ff_media_t*)handle;
    if (!m) return;
    atomic_store_explicit(&m->paused, paused, memory_order_release);
    rubraview_pal_audio_set_playing(m->audio, !paused);
}

static bool ff_audio_position(void *handle, double *out_position, double *out_wall) {
    ff_media_t *m = (ff_media_t*)handle;
    return m && m->audio && rubraview_pal_audio_position(m->audio, out_position, out_wall);
}

/* §3.16.1 / D-12: read one text subtitle stream out of the file and
   write it as SubRip, which the core parser already knows. No subtitle
   *decoder* is involved: in every text format the packet's payload is
   the line itself (Matroska's S_TEXT/UTF8 verbatim, ASS as its comma
   fields, MP4 text behind a two-byte length).

   The file is opened a second time on purpose. The decode thread owns
   the first context and is reading from it; a second reader walking the
   same one would move its position under it. */
#define SUBTITLE_BLOB_MAX (4u * 1024u * 1024u)
#define SUBTITLE_MAX_CUE_SECONDS 10.0

static size_t ff_timecode(char *out, size_t capacity, double seconds) {
    if (seconds < 0.0) seconds = 0.0;
    int total_ms = (int)(seconds * 1000.0 + 0.5);
    int ms = total_ms % 1000, total_s = total_ms / 1000;
    int sec = total_s % 60, total_m = total_s / 60;
    int min = total_m % 60, hour = total_m / 60;
    int written = snprintf(out, capacity, "%02d:%02d:%02d,%03d", hour, min, sec, ms);
    return written > 0 ? (size_t)written : 0;
}

/* The text inside one packet, whatever wrapping its format uses. */
static void ff_subtitle_payload(enum AVCodecID id, const uint8_t *data, int size,
                                const uint8_t **out_text, int *out_len) {
    *out_text = data;
    *out_len = size;
    if (size <= 0) { *out_len = 0; return; }

    if (id == AV_CODEC_ID_MOV_TEXT) {
        /* Two bytes of length, then the text. */
        if (size < 2) { *out_len = 0; return; }
        int inner = (data[0] << 8) | data[1];
        if (inner > size - 2) inner = size - 2;
        *out_text = data + 2;
        *out_len = inner;
        return;
    }
    if (id == AV_CODEC_ID_ASS || id == AV_CODEC_ID_SSA) {
        /* ReadOrder,Layer,Style,Name,MarginL,MarginR,MarginV,Effect,Text
           — eight commas, then the line. */
        int commas = 0, i = 0;
        for (; i < size && commas < 8; ++i) {
            if (data[i] == ',') commas++;
        }
        if (commas == 8) {
            *out_text = data + i;
            *out_len = size - i;
        }
    }
}

static u8str_t ff_read_subtitle_stream(void *handle, proven_arena_t *arena, int32_t stream_index) {
    u8str_t empty = { .ptr = "", .len = 0 };
    ff_media_t *m = (ff_media_t*)handle;
    if (!m || !arena || stream_index < 0 || !g_ff.ok) return empty;

    AVFormatContext *format = NULL;
    if (g_ff.avformat_open_input(&format, m->path, NULL, NULL) < 0 || !format) return empty;
    if (g_ff.avformat_find_stream_info(format, NULL) < 0 ||
        (unsigned)stream_index >= format->nb_streams) {
        g_ff.avformat_close_input(&format);
        return empty;
    }
    const AVStream *stream = format->streams[stream_index];
    if (stream->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE ||
        !ff_subtitle_is_text(stream->codecpar->codec_id)) {
        g_ff.avformat_close_input(&format);
        return empty;
    }

    proven_result_mem_mut_t res = proven_arena_alloc(arena, SUBTITLE_BLOB_MAX);
    if (!proven_is_ok(res.err)) {
        g_ff.avformat_close_input(&format);
        return empty;
    }
    char *blob = (char*)(void*)res.value.ptr;
    size_t used = 0;
    int32_t number = 0;

    AVPacket *packet = g_ff.av_packet_alloc();
    while (packet && g_ff.av_read_frame(format, packet) >= 0) {
        if (packet->stream_index != stream_index || packet->size <= 0 ||
            packet->pts == AV_NOPTS_VALUE) {
            g_ff.av_packet_unref(packet);
            continue;
        }
        double start = stream_seconds(format, stream_index, packet->pts);
        double length = packet->duration > 0
            ? (double)packet->duration * av_q2d(stream->time_base) : SUBTITLE_MAX_CUE_SECONDS;
        const uint8_t *text = NULL;
        int text_len = 0;
        ff_subtitle_payload(stream->codecpar->codec_id, packet->data, packet->size, &text, &text_len);

        char from[32], to[32];
        size_t from_len = ff_timecode(from, sizeof(from), start);
        size_t to_len = ff_timecode(to, sizeof(to), start + length);
        /* number, times, the line, a blank line — and the line itself
           must hold no blank line of its own or the parser would read
           one cue as two. */
        if (text_len > 0 && used + (size_t)text_len + 64 < SUBTITLE_BLOB_MAX) {
            used += (size_t)snprintf(blob + used, SUBTITLE_BLOB_MAX - used, "%d\n%.*s --> %.*s\n",
                                     ++number, (int)from_len, from, (int)to_len, to);
            for (int i = 0; i < text_len; ++i) {
                char c = (char)text[i];
                if (c == '\r') continue;
                if (c == '\n' && used > 0 && blob[used - 1] == '\n') continue;
                blob[used++] = c;
            }
            if (used > 0 && blob[used - 1] != '\n') blob[used++] = '\n';
            blob[used++] = '\n';
        }
        g_ff.av_packet_unref(packet);
    }
    if (packet) g_ff.av_packet_free(&packet);
    g_ff.avformat_close_input(&format);

    if (used == 0) return empty;
    blob[used] = '\0';   /* §7.2.3: allocated with room for it */
    return (u8str_t){ .ptr = blob, .len = used };
}

static bool ff_select_audio_track(void *handle, int32_t stream_index) {
    ff_media_t *m = (ff_media_t*)handle;
    if (!m || stream_index < 0) return false;
    if (!m->audio || m->audio_stream < 0) return false;   /* nothing is being played */
    if (stream_index == m->audio_stream) return true;
    bool known = false;
    for (size_t i = 0; i < m->tracks.count; ++i) {
        if (m->tracks.tracks[i].kind == RUBRAVIEW_TRACK_AUDIO &&
            m->tracks.tracks[i].stream_index == stream_index) known = true;
    }
    if (!known) return false;
    atomic_store_explicit(&m->pending_audio_stream, stream_index, memory_order_release);
    return true;
}

static bool ff_tracks(void *handle, rubraview_track_set_t *out_set) {
    ff_media_t *m = (ff_media_t*)handle;
    if (!m || !out_set) return false;
    /* Written on the decode thread before `opened` was signalled. */
    *out_set = m->tracks;
    return m->tracks.count > 0;
}

static const rubraview_media_backend_api_t FFMPEG_API = {
    .available = ff_available,
    .open = ff_open,
    .close = ff_close,
    .peek_frame = ff_peek_frame,
    .pop_frame = ff_pop_frame,
    .frames_ready = ff_frames_ready,
    .seek = ff_seek,
    .finished = ff_finished,
    .set_paused = ff_set_paused,
    .audio_position = ff_audio_position,
    .tracks = ff_tracks,
    .select_audio_track = ff_select_audio_track,
    .read_subtitle_stream = ff_read_subtitle_stream,
};

const rubraview_media_backend_api_t *rubraview_media_backend_ffmpeg(void) {
    return &FFMPEG_API;
}

#endif /* _WIN32 */
