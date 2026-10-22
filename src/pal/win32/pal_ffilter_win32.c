#ifdef _WIN32
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/log.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include "rubraview/pal/pal_ffilter.h"
#include "rubraview/pal/pal_media.h"

/*
 * RFC-0009 D: avfilter, the sixth DLL of the FFmpeg the user named, loaded from the folder its avutil was
 * loaded from (never searched for elsewhere: half a set from one place and half from another is not one
 * FFmpeg). Nothing here is needed for FFmpeg to play: every failure only leaves the filters missing.
 */

#define MAX_SIDE 16384
#define MAX_PIXELS (1u << 28)

typedef struct ffl_api {
    rubraview_ffilter_state_t state;
    HMODULE tried_with;   /* the avutil the last load was tried beside */
    /* avfilter */
    unsigned (*avfilter_version)(void);
    const AVFilter *(*av_filter_iterate)(void **);
    const AVFilter *(*avfilter_get_by_name)(const char *);
    unsigned (*avfilter_filter_pad_count)(const AVFilter *, int);
    enum AVMediaType (*avfilter_pad_get_type)(const AVFilterPad *, int);
    AVFilterGraph *(*avfilter_graph_alloc)(void);
    void (*avfilter_graph_free)(AVFilterGraph **);
    AVFilterContext *(*avfilter_graph_alloc_filter)(AVFilterGraph *, const AVFilter *, const char *);
    int (*avfilter_graph_create_filter)(AVFilterContext **, const AVFilter *, const char *, const char *, void *, AVFilterGraph *);
    int (*avfilter_init_dict)(AVFilterContext *, AVDictionary **);
    AVFilterInOut *(*avfilter_inout_alloc)(void);
    void (*avfilter_inout_free)(AVFilterInOut **);
    int (*avfilter_graph_parse_ptr)(AVFilterGraph *, const char *, AVFilterInOut **, AVFilterInOut **, void *);
    int (*avfilter_graph_config)(AVFilterGraph *, void *);
    int (*av_buffersrc_add_frame_flags)(AVFilterContext *, AVFrame *, int);
    int (*av_buffersink_get_frame)(AVFilterContext *, AVFrame *);
    /* avutil */
    AVFrame *(*av_frame_alloc)(void);
    void (*av_frame_free)(AVFrame **);
    int (*av_frame_get_buffer)(AVFrame *, int);
    int (*av_opt_set)(void *, const char *, const char *, int);
    const AVOption *(*av_opt_next)(const void *, const AVOption *);
    char *(*av_strdup)(const char *);
    int (*av_strerror)(int, char *, size_t);
    void (*av_log_set_callback)(void (*)(void *, int, const char *, va_list));
    void (*av_log_default_callback)(void *, int, const char *, va_list);
} ffl_api_t;

static ffl_api_t g_ffl;
static SRWLOCK g_ffl_lock = SRWLOCK_INIT;

/* What FFmpeg says while this thread runs a graph: its errors, kept for the step's message. */
static _Thread_local char *g_ffl_said;
static _Thread_local size_t g_ffl_said_size;

static void ffl_log(void *avcl, int level, const char *fmt, va_list vl) {
    if (g_ffl_said && level <= AV_LOG_ERROR) {
        char line[256];
        va_list copy;
        va_copy(copy, vl);
        int n = vsnprintf(line, sizeof(line), fmt, copy);
        va_end(copy);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == ' ')) line[--n] = '\0';
        if (n > 0 && !g_ffl_said[0]) snprintf(g_ffl_said, g_ffl_said_size, "%s", line);   /* the first: the cause ("No such filter: 'eq'") */
        return;
    }
    if (g_ffl.av_log_default_callback) g_ffl.av_log_default_callback(avcl, level, fmt, vl);
}

static HMODULE ffl_load_beside(HMODULE util) {
    WCHAR path[MAX_PATH + 32];
    DWORD n = GetModuleFileNameW(util, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return NULL;
    while (n > 0 && path[n - 1] != L'\\') --n;
    static const WCHAR *const NAMES[2] = { L"avfilter-" AV_STRINGIFY(LIBAVFILTER_VERSION_MAJOR) L".dll", L"avfilter.dll" };
    for (int i = 0; i < 2; ++i) {
        size_t k = wcslen(NAMES[i]);
        memcpy(path + n, NAMES[i], (k + 1) * sizeof(WCHAR));
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) continue;
        HMODULE m = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (m) return m;
    }
    return NULL;
}

#define BINDF(module, name) do { g_ffl.name = (void *)GetProcAddress(module, #name); if (!g_ffl.name) ok = false; } while (0)

static rubraview_ffilter_state_t ffl_load_locked(void) {
    if (g_ffl.state == RUBRAVIEW_FFILTER_READY) return g_ffl.state;
    if (rubraview_pal_ffmpeg_place() == 0) return RUBRAVIEW_FFILTER_NO_FFMPEG;
    const char *util_name = rubraview_pal_ffmpeg_dll_name(0);
    WCHAR wide[64];
    if (!util_name || MultiByteToWideChar(CP_UTF8, 0, util_name, -1, wide, 64) <= 0) return RUBRAVIEW_FFILTER_NO_FFMPEG;
    HMODULE util = GetModuleHandleW(wide);
    if (!util) return RUBRAVIEW_FFILTER_NO_FFMPEG;
    if (util == g_ffl.tried_with) return g_ffl.state;   /* tried beside this one already */
    g_ffl = (ffl_api_t){ .tried_with = util, .state = RUBRAVIEW_FFILTER_NO_DLL };
    HMODULE filter = ffl_load_beside(util);
    if (!filter) return g_ffl.state;
    bool ok = true;
    BINDF(filter, avfilter_version);
    if (ok && AV_VERSION_MAJOR(g_ffl.avfilter_version()) != LIBAVFILTER_VERSION_MAJOR) {
        FreeLibrary(filter);
        g_ffl = (ffl_api_t){ .tried_with = util, .state = RUBRAVIEW_FFILTER_OTHER_VERSION };
        return g_ffl.state;
    }
    BINDF(filter, av_filter_iterate); BINDF(filter, avfilter_get_by_name); BINDF(filter, avfilter_filter_pad_count);
    BINDF(filter, avfilter_pad_get_type); BINDF(filter, avfilter_graph_alloc); BINDF(filter, avfilter_graph_free);
    BINDF(filter, avfilter_graph_alloc_filter); BINDF(filter, avfilter_graph_create_filter); BINDF(filter, avfilter_init_dict);
    BINDF(filter, avfilter_inout_alloc); BINDF(filter, avfilter_inout_free); BINDF(filter, avfilter_graph_parse_ptr);
    BINDF(filter, avfilter_graph_config); BINDF(filter, av_buffersrc_add_frame_flags); BINDF(filter, av_buffersink_get_frame);
    BINDF(util, av_frame_alloc); BINDF(util, av_frame_free); BINDF(util, av_frame_get_buffer); BINDF(util, av_opt_set);
    BINDF(util, av_opt_next); BINDF(util, av_strdup); BINDF(util, av_strerror); BINDF(util, av_log_set_callback);
    BINDF(util, av_log_default_callback);
    if (!ok) {
        FreeLibrary(filter);
        g_ffl = (ffl_api_t){ .tried_with = util, .state = RUBRAVIEW_FFILTER_NO_DLL };
        return g_ffl.state;
    }
    g_ffl.av_log_set_callback(ffl_log);   /* for the whole process: others' messages go on to FFmpeg's own */
    g_ffl.state = RUBRAVIEW_FFILTER_READY;
    return g_ffl.state;
}

rubraview_ffilter_state_t rubraview_pal_ffilter_state(void) {
    AcquireSRWLockExclusive(&g_ffl_lock);
    rubraview_ffilter_state_t s = ffl_load_locked();
    ReleaseSRWLockExclusive(&g_ffl_lock);
    return s;
}

/* ---- the list ---- */

static bool video_one_to_one(const AVFilter *f) {
    if (f->flags & (AVFILTER_FLAG_DYNAMIC_INPUTS | AVFILTER_FLAG_DYNAMIC_OUTPUTS | AVFILTER_FLAG_HWDEVICE | AVFILTER_FLAG_METADATA_ONLY))
        return false;
    if (g_ffl.avfilter_filter_pad_count(f, 0) != 1 || g_ffl.avfilter_filter_pad_count(f, 1) != 1) return false;
    if (!f->inputs || !f->outputs) return false;
    if (g_ffl.avfilter_pad_get_type(f->inputs, 0) != AVMEDIA_TYPE_VIDEO || g_ffl.avfilter_pad_get_type(f->outputs, 0) != AVMEDIA_TYPE_VIDEO)
        return false;
    /* a graphics card's, which want its frames */
    static const char *const CARD[] = { "_opencl", "_vulkan", "_cuda", "_npp", "_qsv", "_vaapi", "_d3d11", "_d3d12", "_amf",
                                        "_videotoolbox", "_vt", "_metal", "_cuvid", "_libplacebo", "hwupload", "hwdownload", "hwmap" };
    for (size_t i = 0; i < sizeof(CARD) / sizeof(CARD[0]); ++i) if (strstr(f->name, CARD[i])) return false;
    return rubraview_ffilter_refused(proven_u8str_view_from_cstr(f->name)) == NULL;
}

static proven_u8str_view_t copy_text(proven_arena_t *arena, const char *s) {
    if (!s) return PROVEN_LIT("");
    size_t n = strlen(s);
    if (n > 512) n = 512;
    proven_result_mem_mut_t r = proven_arena_alloc(arena, n + 1);
    if (!proven_is_ok(r.err)) return PROVEN_LIT("");
    memcpy(r.value.ptr, s, n);
    ((char *)r.value.ptr)[n] = '\0';
    return rubraview_u8(r.value.ptr, n);
}

static bool slider_range(double min, double max) {
    return min >= -1e6 && max <= 1e6 && max > min;
}

/* An option as a row; false when it is not one (a constant, deprecated, read-only, an alias). */
static bool option_of(proven_arena_t *arena, const AVFilter *f, const AVOption *o, rubraview_ffilter_option_t *out) {
    if (o->type == AV_OPT_TYPE_CONST || (o->flags & (AV_OPT_FLAG_DEPRECATED | AV_OPT_FLAG_READONLY))) return false;
    *out = (rubraview_ffilter_option_t){ .kind = RUBRAVIEW_FFILTER_TEXT, .name = copy_text(arena, o->name),
                                         .help = copy_text(arena, o->help), .min = o->min, .max = o->max };
    char num[64] = "";
    int type = (int)o->type;
    bool array = (type & AV_OPT_TYPE_FLAG_ARRAY) != 0;
    if (array) type &= ~AV_OPT_TYPE_FLAG_ARRAY;
    if (!array && o->unit && (type == AV_OPT_TYPE_INT || type == AV_OPT_TYPE_INT64 || type == AV_OPT_TYPE_UINT)) {
        /* its named constants, as a choice; the default's index */
        proven_u8str_view_t names[RUBRAVIEW_FFILTER_MAX_CHOICES];
        int32_t count = 0, at = -1;
        for (const AVOption *c = NULL; (c = g_ffl.av_opt_next(&f->priv_class, c)) && count < RUBRAVIEW_FFILTER_MAX_CHOICES;) {
            if (c->type != AV_OPT_TYPE_CONST || !c->unit || strcmp(c->unit, o->unit) != 0) continue;
            if (c->default_val.i64 == o->default_val.i64 && at < 0) at = count;
            names[count++] = copy_text(arena, c->name);
        }
        if (count > 0 && at >= 0) {
            proven_result_mem_mut_t r = rubraview_arena_alloc_array(arena, (size_t)count, sizeof(names[0]));
            if (proven_is_ok(r.err)) {
                memcpy(r.value.ptr, names, sizeof(names[0]) * (size_t)count);
                out->kind = RUBRAVIEW_FFILTER_CHOICE;
                out->choices = (proven_u8str_view_t *)r.value.ptr;
                out->choice_count = count;
                out->value = at;
                return true;
            }
        }
        snprintf(num, sizeof(num), "%lld", (long long)o->default_val.i64);
        out->text = copy_text(arena, num);
        return true;
    }
    if (array) return true;   /* typed */
    switch (type) {
        case AV_OPT_TYPE_BOOL:
            out->kind = RUBRAVIEW_FFILTER_BOOL;
            out->value = o->default_val.i64 > 0 ? 1 : 0;   /* -1 "auto": shown off, left as FFmpeg's unless changed */
            out->min = 0;
            out->max = 1;
            if (o->default_val.i64 < 0) { out->kind = RUBRAVIEW_FFILTER_TEXT; out->text = PROVEN_LIT("auto"); }
            return true;
        case AV_OPT_TYPE_INT: case AV_OPT_TYPE_INT64: case AV_OPT_TYPE_UINT:
            if (slider_range(o->min, o->max)) { out->kind = RUBRAVIEW_FFILTER_INT; out->value = (double)o->default_val.i64; return true; }
            snprintf(num, sizeof(num), "%lld", (long long)o->default_val.i64);
            out->text = copy_text(arena, num);
            return true;
        case AV_OPT_TYPE_DOUBLE: case AV_OPT_TYPE_FLOAT:
            if (slider_range(o->min, o->max)) { out->kind = RUBRAVIEW_FFILTER_FLOAT; out->value = o->default_val.dbl; return true; }
            snprintf(num, sizeof(num), "%.9g", o->default_val.dbl);
            out->text = copy_text(arena, num);
            return true;
        case AV_OPT_TYPE_STRING: case AV_OPT_TYPE_IMAGE_SIZE: case AV_OPT_TYPE_VIDEO_RATE: case AV_OPT_TYPE_COLOR:
            out->text = copy_text(arena, o->default_val.str);
            return true;
        default:   /* flags, rationals, durations, formats, ...: typed, FFmpeg's default unless typed */
            return true;
    }
}

bool rubraview_pal_ffilter_list(proven_arena_t *arena, rubraview_ffilter_catalog_t *out) {
    *out = (rubraview_ffilter_catalog_t){0};
    if (rubraview_pal_ffilter_state() != RUBRAVIEW_FFILTER_READY) return false;
    int32_t count = 0;
    void *it = NULL;
    for (const AVFilter *f; (f = g_ffl.av_filter_iterate(&it));) if (video_one_to_one(f)) count++;
    if (count == 0) return false;
    proven_result_mem_mut_t r = rubraview_arena_alloc_array(arena, (size_t)count, sizeof(rubraview_ffilter_t));
    if (!proven_is_ok(r.err)) return false;
    rubraview_ffilter_t *filters = (rubraview_ffilter_t *)r.value.ptr;
    int32_t n = 0;
    it = NULL;
    for (const AVFilter *f; (f = g_ffl.av_filter_iterate(&it)) && n < count;) {
        if (!video_one_to_one(f)) continue;
        rubraview_ffilter_t *x = &filters[n];
        *x = (rubraview_ffilter_t){ .name = copy_text(arena, f->name), .description = copy_text(arena, f->description) };
        if (f->priv_class) {
            rubraview_ffilter_option_t rows[RUBRAVIEW_FFILTER_MAX_SHOWN];
            int offsets[RUBRAVIEW_FFILTER_MAX_SHOWN];
            int32_t k = 0;
            for (const AVOption *o = NULL; (o = g_ffl.av_opt_next(&f->priv_class, o)) && k < RUBRAVIEW_FFILTER_MAX_SHOWN;) {
                bool alias = false;   /* "w" and "width": the same field, the first name kept */
                for (int32_t j = 0; j < k && !alias; ++j) alias = offsets[j] == o->offset;
                if (alias || !option_of(arena, f, o, &rows[k])) continue;
                offsets[k++] = o->offset;
            }
            proven_result_mem_mut_t m = k ? rubraview_arena_alloc_array(arena, (size_t)k, sizeof(rows[0]))
                                          : (proven_result_mem_mut_t){0};
            if (k && proven_is_ok(m.err)) {
                memcpy(m.value.ptr, rows, sizeof(rows[0]) * (size_t)k);
                x->options = (rubraview_ffilter_option_t *)m.value.ptr;
                x->option_count = k;
            }
        }
        if (x->name.size) n++;
    }
    out->filters = filters;
    out->filter_count = n;
    rubraview_ffilter_catalog_sort(out);
    return n > 0;
}

/* ---- a run ---- */

static void say(char *said, size_t size, const char *text) {
    if (said && size) snprintf(said, size, "%s", text);
}

static void say_error(char *said, size_t size, int err) {
    if (!said || !size || said[0]) return;   /* FFmpeg's own message, when it gave one, says more */
    char text[128] = "";
    if (g_ffl.av_strerror(err, text, sizeof(text)) < 0) snprintf(text, sizeof(text), "error %d", err);
    snprintf(said, size, "%s", text);
}

bool rubraview_pal_ffilter_run(const char *graph_text, const uint8_t *bgra, int32_t width, int32_t height,
                               uint8_t **out, int32_t *out_width, int32_t *out_height, char *said, size_t said_size) {
    *out = NULL;
    *out_width = *out_height = 0;
    if (said && said_size) said[0] = '\0';
    if (rubraview_pal_ffilter_state() != RUBRAVIEW_FFILTER_READY) { say(said, said_size, "avfilter is not loaded"); return false; }
    const char *refused = rubraview_ffilter_refused(proven_u8str_view_from_cstr(graph_text));
    if (refused) { snprintf(said, said_size, "the filter %s is not allowed here", refused); return false; }
    if (width <= 0 || height <= 0 || width > MAX_SIDE || height > MAX_SIDE) { say(said, said_size, "the picture is too large"); return false; }

    g_ffl_said = said;
    g_ffl_said_size = said_size;
    bool ok = false;
    AVFilterGraph *graph = g_ffl.avfilter_graph_alloc();
    AVFilterInOut *ins = g_ffl.avfilter_inout_alloc(), *outs = g_ffl.avfilter_inout_alloc();
    AVFrame *frame = g_ffl.av_frame_alloc(), *done = g_ffl.av_frame_alloc();
    AVFilterContext *src = NULL, *sink = NULL;
    int err = 0;
    if (!graph || !ins || !outs || !frame || !done) { say(said, said_size, "out of memory"); goto end; }

    char args[160];
    snprintf(args, sizeof(args), "video_size=%dx%d:pix_fmt=%d:time_base=1/25:pixel_aspect=1/1", width, height, (int)AV_PIX_FMT_BGRA);
    if ((err = g_ffl.avfilter_graph_create_filter(&src, g_ffl.avfilter_get_by_name("buffer"), "in", args, NULL, graph)) < 0) goto fail;
    sink = g_ffl.avfilter_graph_alloc_filter(graph, g_ffl.avfilter_get_by_name("buffersink"), "out");
    if (!sink) { err = AVERROR(ENOMEM); goto fail; }
    if ((err = g_ffl.av_opt_set(sink, "pixel_formats", "bgra", AV_OPT_SEARCH_CHILDREN)) < 0) goto fail;
    if ((err = g_ffl.avfilter_init_dict(sink, NULL)) < 0) goto fail;
    /* the graph's open input is fed by `in`, its open output feeds `out` */
    outs->name = g_ffl.av_strdup("in");
    outs->filter_ctx = src;
    outs->pad_idx = 0;
    outs->next = NULL;
    ins->name = g_ffl.av_strdup("out");
    ins->filter_ctx = sink;
    ins->pad_idx = 0;
    ins->next = NULL;
    if (!outs->name || !ins->name) { err = AVERROR(ENOMEM); goto fail; }
    if ((err = g_ffl.avfilter_graph_parse_ptr(graph, graph_text, &ins, &outs, NULL)) < 0) goto fail;
    if ((err = g_ffl.avfilter_graph_config(graph, NULL)) < 0) goto fail;

    frame->width = width;
    frame->height = height;
    frame->format = AV_PIX_FMT_BGRA;
    frame->pts = 0;
    frame->sample_aspect_ratio = (AVRational){ 1, 1 };
    if ((err = g_ffl.av_frame_get_buffer(frame, 0)) < 0) goto fail;
    for (int32_t y = 0; y < height; ++y)
        memcpy(frame->data[0] + (size_t)y * (size_t)frame->linesize[0], bgra + (size_t)y * (size_t)width * 4u, (size_t)width * 4u);
    if ((err = g_ffl.av_buffersrc_add_frame_flags(src, frame, 0)) < 0) goto fail;
    if ((err = g_ffl.av_buffersrc_add_frame_flags(src, NULL, 0)) < 0) goto fail;   /* the end: filters that wait for more give what they have */
    if ((err = g_ffl.av_buffersink_get_frame(sink, done)) < 0) goto fail;

    if (done->format != AV_PIX_FMT_BGRA || done->width <= 0 || done->height <= 0 || done->width > MAX_SIDE ||
        done->height > MAX_SIDE || (uint64_t)done->width * (uint64_t)done->height > MAX_PIXELS || !done->data[0]) {
        say(said, said_size, "FFmpeg gave back a picture that cannot be used");
        goto end;
    }
    size_t row = (size_t)done->width * 4u;
    uint8_t *pixels = (uint8_t *)malloc(row * (size_t)done->height);
    if (!pixels) { say(said, said_size, "out of memory"); goto end; }
    for (int32_t y = 0; y < done->height; ++y)
        memcpy(pixels + (size_t)y * row, done->data[0] + (ptrdiff_t)y * done->linesize[0], row);   /* vflip's run upwards */
    *out = pixels;
    *out_width = done->width;
    *out_height = done->height;
    ok = true;
    goto end;
fail:
    say_error(said, said_size, err);
end:
    g_ffl_said = NULL;
    if (frame) g_ffl.av_frame_free(&frame);
    if (done) g_ffl.av_frame_free(&done);
    g_ffl.avfilter_inout_free(&ins);
    g_ffl.avfilter_inout_free(&outs);
    g_ffl.avfilter_graph_free(&graph);
    return ok;
}

#endif /* _WIN32 */
