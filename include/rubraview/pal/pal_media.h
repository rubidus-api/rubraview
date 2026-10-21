#ifndef RUBRAVIEW_PAL_MEDIA_H
#define RUBRAVIEW_PAL_MEDIA_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "rubraview/core.h"
#include "rubraview/detail.h"
#include "rubraview/mediaclock.h"
#include "rubraview/playback.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Media PAL (RFC-0001 §5, D-8, D-9). One interface, two backends: Windows
 * Media Foundation by default and FFmpeg as a replaceable second one.
 * No Win32 type appears here (D-1).
 *
 * Threads. `open` starts a decode thread that belongs to the media
 * object: it initialises COM and the decoder itself and never calls into
 * the imaging, drawing or arena code. Decoded frames reach the caller's
 * thread through a fixed ring of CPU buffers (rubraview_spsc_t); the
 * caller reads the oldest one, uploads it, and gives it back. Every
 * function below is called from that one caller thread.
 */

typedef struct rubraview_media rubraview_media_t;

typedef struct rubraview_media_info {
    rubraview_media_backend_t backend;   /* which backend opened it */
    double   duration_seconds;           /* 0 when the container does not say */
    double   frame_rate;                 /* 0 when unknown */
    int32_t  width, height;              /* decoded picture size */
    bool     has_video;
    bool     has_audio;
    bool     audio_output;               /* the sound reaches a device (false on a machine without one) */
    uint32_t video_fourcc;               /* the native codec, for messages (RUBRAVIEW_FOURCC order) */
    bool     hardware_decode;            /* RV-062: the pictures are decoded on the graphics card */
    /* For the information window (owner, 2026-09-29); 0 when unknown. */
    uint32_t audio_sample_rate, audio_channels;
    uint32_t bitrate_kbps;               /* the whole file's, or picture and sound added */
    /* 1.25.1 (owner, 2026-10-20: "어떤 백엔드나 다이렉트x 같은거 어떻게 하여 화면에 출력해 주는지"): how the
       pictures get to the screen, in the backend's words (English; the window translates the labels) */
    char     path_decoder[128];          /* what decodes the pictures, and where */
    char     path_colours[128];          /* what turns them into BGRA, and where */
    char     path_handover[128];         /* how a picture reaches the page's texture */
    char     path_why_software[128];     /* decoded in software: why (empty on the card) */
    int32_t  decode_threads;             /* the decoder's threads: 0 unknown, -1 one a core */
    /* RV-150 (D-180): an HDR film, and what was done with it */
    int32_t  hdr_transfer;               /* rubraview_hdr_transfer_t: 0 none, 1 PQ, 2 HLG */
    bool     hdr_tone_mapped;            /* its pictures are tone mapped to SDR on the graphics card */
    char     path_hdr_why[128];          /* HDR not tone mapped: why (an N_ literal; empty otherwise) */
    bool     hdr_output;                 /* RV-153: its pictures are for an HDR screen (FP16, 1.0 the SDR white) */
} rubraview_media_info_t;

/*
 * RV-062: what the caller offers for decoding on the graphics card.
 * `device` is the renderer's (rubraview_pal_render_video_device), opaque.
 * `mode`: 0 never, 1 when the card offers decoders, 2 always (diagnostic —
 * it takes the fall-back path on a card with none). A backend that cannot
 * use it, or finds it fails, decodes in software as before.
 */
typedef struct rubraview_media_gpu {
    void    *device;
    uint32_t decoder_profiles;
    int32_t  mode;
    /* RV-150: [video] hdr_tone_mapping (RUBRAVIEW_TONEMAP_*) and the device its shader runs on — the
       renderer's decode device, offered whether or not the card decodes */
    int32_t  tone_mapping;
    void    *shader_device;
    /* RV-153: an HDR film tone mapped for an HDR screen of this peak, its SDR white at that many nits;
       0: for an SDR screen */
    double   hdr_screen_peak;
    double   hdr_sdr_white;
} rubraview_media_gpu_t;

typedef struct rubraview_media_open_result {
    rubraview_media_t        *media;     /* NULL on failure */
    rubraview_media_failure_t failure;   /* RUBRAVIEW_MEDIA_OPENED on success */
    rubraview_media_info_t    info;
} rubraview_media_open_result_t;

/** Whether a backend can be used on this machine (FFmpeg: are its DLLs here). */
bool rubraview_pal_media_backend_available(rubraview_media_backend_t backend);

/**
 * RV-110: a JPEG XL picture, read by the FFmpeg the user put beside the
 * program (which has libjxl in it or has not). Nothing of it is in
 * rubraview's own build.
 *
 * The state: 0, no usable FFmpeg here; 1, FFmpeg, with no JPEG XL decoder
 * in it; 2, it can read one.
 */
int rubraview_pal_ffmpeg_jxl_state(void);

/**
 * The folders FFmpeg's DLLs may also be in, after the program's own: for
 * the Store's package and Program Files, which cannot be added to.
 * `imported` is where Import copied them (the settings folder's ffmpeg),
 * `chosen` the one the user named (`video.ffmpeg_dir`); empty for none,
 * given clean (rubraview_path_typed_folder, rubraview_pal_fs_expand_env).
 * Each place is taken whole or not at all. While FFmpeg is not loaded the
 * places are looked in again at the next ask; once loaded it stays for the run.
 */
void rubraview_pal_ffmpeg_set_folders(u8str_t imported, u8str_t chosen);

/** Where the FFmpeg in use came from: 0 none, 1 beside the program, 2 imported, 3 the chosen folder. */
int rubraview_pal_ffmpeg_place(void);

/** The five DLL names this build takes, versioned ("avcodec-63.dll"), in the order they load. */
const char *rubraview_pal_ffmpeg_dll_name(size_t index);
#define RUBRAVIEW_FFMPEG_DLLS 5

/**
 * Whether FFmpeg's DLLs lie beside the program (or in the chosen folder) and were not loaded —
 * another version than this build takes, or this build's with one of the
 * five missing — so a notice can say that instead of "no FFmpeg".
 */
rubraview_ffmpeg_refusal_t rubraview_pal_ffmpeg_refused(void);

/**
 * Decode the first frame of the JPEG XL in `data` as R, G, B, A (alpha not
 * multiplied in), upright, top row first. `room` is asked once for
 * width * height * 4 bytes when the size is known, and may refuse (NULL).
 * False when it is not a JPEG XL, is damaged, is larger than `max_side`
 * on a side, or there is no decoder. May be called from any thread.
 */
/**
 * RV-139 (D-184, D-185): which pictures the user's FFmpeg can write — RUBRAVIEW_FFMPEG_WRITES_* — and the
 * encoders' names into `names` ("libjxl, libaom-av1").
 */
#define RUBRAVIEW_FFMPEG_WRITES_JXL        1u
#define RUBRAVIEW_FFMPEG_WRITES_AVIF       2u
#define RUBRAVIEW_FFMPEG_WRITES_JXL_ANIM   4u   /* libjxl_anim */
#define RUBRAVIEW_FFMPEG_WRITES_AVIF_ALPHA 8u   /* libaom: transparency as a second, grey picture */
uint32_t rubraview_pal_ffmpeg_image_encoders(char *names, size_t size);

typedef struct rubraview_ffmpeg_still {
    bool    avif;        /* AVIF, else JPEG XL */
    int32_t quality;     /* 1..100 (rubraview_export_jxl_distance / _av1_crf); 100 is lossless JPEG XL */
    bool    chroma444;   /* AVIF: 4:4:4 (libaom, rav1e), else 4:2:0 */
    int32_t bits;        /* AVIF: 8 or 10 */
    bool    moving;      /* set by the writer: more than one picture */
} rubraview_ffmpeg_still_t;

/**
 * Pictures written as one AVIF (an AV1 encoder in FFmpeg's avif muxer; transparency as libaom's grey second
 * picture, or laid over white by another encoder) or JPEG XL (libjxl, libjxl_anim for more than one; alpha
 * kept). `picture` fills picture `index` as straight BGRA rows, width * 4 bytes apart, and says how long it
 * shows; it is asked for every picture twice (whether any is see-through, then to write). The file's bytes
 * (free()), or NULL and `why` (an N_ literal).
 */
uint8_t *rubraview_pal_ffmpeg_pictures_encode(int32_t width, int32_t height, size_t count,
                                              bool (*picture)(void *context, size_t index, uint8_t *bgra, double *seconds),
                                              void *context, const rubraview_ffmpeg_still_t *how,
                                              size_t *out_size, const char **why);

/** One picture (BGRA or RGBA rows, straight alpha). */
uint8_t *rubraview_pal_ffmpeg_image_encode(const rubraview_pixbuf_t *pixels, const rubraview_ffmpeg_still_t *how,
                                           size_t *out_size, const char **why);

bool rubraview_pal_ffmpeg_jxl_decode(const uint8_t *data, size_t size, int32_t max_side,
                                     uint8_t *(*room)(void *context, int32_t width, int32_t height),
                                     void *context, int32_t *out_width, int32_t *out_height);

/**
 * Every picture of a JPEG XL that moves, in order, each whole as it is to
 * be seen. `room` is asked for width * height * 4 bytes before each, told
 * how long the picture stays, and ends the reading by refusing (NULL).
 * True when at least one picture was written.
 */
bool rubraview_pal_ffmpeg_jxl_frames(const uint8_t *data, size_t size, int32_t max_side,
                                     uint8_t *(*room)(void *context, int32_t width, int32_t height, double seconds),
                                     void *context);

/**
 * Open a file with one backend and start decoding from its beginning.
 * Blocks until the backend has said yes or no. The caller tries the
 * backends in rubraview_media_backend_order and reports the last failure.
 */
rubraview_media_open_result_t rubraview_pal_media_open(u8str_t path, rubraview_media_backend_t backend,
                                                       const rubraview_media_gpu_t *gpu);

/** Stops the decode thread and frees everything. NULL is ignored. */
void rubraview_pal_media_close(rubraview_media_t *media);

/** A decoded picture: 32-bit BGRA rows, top row first. */
typedef struct rubraview_video_frame {
    const uint8_t *pixels;
    int32_t width, height;
    int32_t stride;           /* bytes from one row to the next */
    double  pts;              /* presentation time, seconds from the file's start */
    double  duration;         /* seconds; 0 when unknown */
    /* RV-062: a frame still on the graphics card — `pixels` is NULL and
       this texture (opaque) at `gpu_subresource` holds the picture, BGRA,
       at least width x height. Copy it with rubraview_pal_texture_copy_video_frame. */
    const void *gpu_texture;
    uint32_t    gpu_subresource;
} rubraview_video_frame_t;

/**
 * The oldest decoded frame, without taking it. False when none is ready.
 * The pixels stay valid until rubraview_pal_media_pop_frame.
 */
bool rubraview_pal_media_peek_frame(rubraview_media_t *media, rubraview_video_frame_t *out_frame);

/** Give the oldest frame back to the decoder. */
void rubraview_pal_media_pop_frame(rubraview_media_t *media);

/**
 * How many decoded frames are waiting. A late frame is skipped only when
 * another one stands behind it — on a machine that cannot decode in real
 * time, skipping the last one would leave nothing to show at all.
 */
size_t rubraview_pal_media_frames_ready(rubraview_media_t *media);

/**
 * Seek. Asynchronous: frames decoded before the request are discarded
 * as they are read, and the first frame after it is the one whose
 * interval holds `seconds` (the decoder runs forward from the keyframe
 * before it, so the landing is exact, not "the nearest keyframe").
 */
void rubraview_pal_media_seek(rubraview_media_t *media, double seconds);

/** The decoder has reached the end and every decoded frame has been taken. */
bool rubraview_pal_media_finished(rubraview_media_t *media);

/** Whether the sound device has played all it was given and holds nothing
    more: the sound has run out (a film whose picture runs longer than its
    sound) or is late. */
bool rubraview_pal_media_sound_over(rubraview_media_t *media);

/** Pause or resume the sound (the picture is paused by the caller's clock). */
void rubraview_pal_media_set_paused(rubraview_media_t *media, bool paused);

/**
 * RV-075: how loud this player is, 0.0 to 1.0, on top of the program's
 * volume — what a crossfade between two tracks is made of. A player with
 * no sound, or none reaching a device, takes it and does nothing.
 */
void rubraview_pal_media_set_gain(rubraview_media_t *media, double gain);

/**
 * Where the listener is and when that was measured — the §5.3 master
 * clock. False when the file's sound is not playing on a device.
 */
bool rubraview_pal_media_audio_position(rubraview_media_t *media, double *out_position, double *out_wall);

/**
 * D-120: the rate and channels the sound is played at now. A track of
 * another format chosen, or a format that changed while decoding, makes
 * these differ from what `rubraview_media_info_t` said at opening. False
 * when no sound is played.
 */
bool rubraview_pal_media_audio_format(rubraview_media_t *media, uint32_t *out_rate, uint32_t *out_channels);

/**
 * D-120: how many pictures a second the film really has, measured from the
 * pictures' own times once a few have come. False until then, and for a
 * backend whose opening figure is to be trusted.
 */
bool rubraview_pal_media_picture_rate(rubraview_media_t *media, double *out_rate);

/** RV-150: the peak an HDR film is tone mapped from, in nits — its metadata's, read from its pictures. False
    while it is not tone mapped. */
bool rubraview_pal_media_hdr_peak(rubraview_media_t *media, double *out_nits);

/**
 * §3.16.2 / R135: the tracks the container holds. The set is worked out
 * once, on the decode thread, while the file is being opened; this call
 * copies it. The strings in it belong to the media object and stay
 * valid until it is closed.
 *
 * Text subtitle streams are not listed: neither backend decodes them
 * yet (D-10), and a track nobody can display is not a choice.
 */
bool rubraview_pal_media_tracks(rubraview_media_t *media, rubraview_track_set_t *out_set);

/**
 * §3.16.2: play a different sound track of the same file, by its
 * `stream_index` from the set above. The request is handed to the decode
 * thread, which owns the decoder; it takes effect within a frame or two.
 *
 * Seek to where the film is straight afterwards: that is what flushes
 * what the old track had already decoded, so picture and sound restart
 * together.
 *
 * False when the backend cannot: no sound is being played on this
 * machine at all, the index is not an audio track of this file, or the
 * new track cannot be decoded (the old one keeps playing).
 */
bool rubraview_pal_media_select_audio_track(rubraview_media_t *media, int32_t stream_index);

/*
 * Everything the container and its codecs say about themselves, for the
 * information window: a heading a stream, then label / value lines. It
 * is written once, on the decode thread, while the file is being opened;
 * the pointer stays valid until the media object is closed.
 */
const rubraview_details_t *rubraview_pal_media_details(rubraview_media_t *media);

/**
 * §3.16.1 / D-12: a text subtitle stream carried inside the container,
 * handed over as SubRip text for the core parser. Empty when the
 * backend cannot read it — Media Foundation never can, and no backend
 * offers the picture-based subtitle formats, which is why those are not
 * listed as tracks at all.
 *
 * Reads the file a second time, on the calling thread, and returns when
 * it has the whole stream: the decode thread's own reading is not
 * disturbed. Long films take a moment.
 */
u8str_t rubraview_pal_media_read_subtitle_stream(rubraview_media_t *media, proven_arena_t *arena,
                                                 int32_t stream_index);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_MEDIA_H */
