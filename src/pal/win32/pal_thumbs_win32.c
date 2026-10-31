/*
 * The picker's thumbnails, made on a thread of their own (D-34). See
 * pal_thumbs.h. Everything this thread touches is its own: its COM
 * apartment, its WIC factory, its scratch arena, the file handles it
 * opens. The main thread and it share only the two queues below, under
 * one lock.
 */
#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <wincodec.h>
#include <objbase.h>
#include <shobjidl.h>
#include <stdlib.h>
#include <string.h>

#include "rubraview/stream.h"
#include "rubraview/pal/pal_thumbs.h"
#include "rubraview/pal/pal_fs.h"
#include "rubraview/pal/pal_time.h"
#include "rubraview/thumbq.h"
#include "rubraview/thumb.h"
#include "rubraview/pagesource.h"
#include "rubraview/glob.h"
#include "rubraview/path.h"
#include "rubraview/detail.h"
#include "rubraview/avif_read.h"
#include "rubraview/bigtiff.h"
#include "rubraview/pal/pal_image_wic_internal.h"
#include "rubraview/pal/pal_image.h"
#include "rubraview/textenc.h"
#include "rubraview/media_head.h"
#include <winioctl.h>

#define TEXT_TILE_W 192   /* RV-135: a text file's tile, a page */
#define TEXT_TILE_H 256
static uint8_t *text_page_small(proven_arena_t *scratch, proven_u8str_view_t path, double aspect, int32_t *out_w, int32_t *out_h);

/* Defined here so the link does not depend on which MinGW carries it. */
static const GUID RV_IID_IShellItemImageFactory_T = {0xBCC18B79, 0xBA16, 0x442F, {0x80, 0xC4, 0x8A, 0x59, 0xC3, 0x0C, 0x46, 0x3B}};

#define THUMB_JOBS 256
#define THUMB_RESULTS 64
#define THUMB_SCRATCH_BYTES (48u << 20)   /* a comic page inflated, plus its list */
#define THUMB_MAX_WIDTH 96
#define THUMB_BLUR 1
#define THUMB_KEEP 0.78
#define THUMB_DECODE_SIDE 256
#define FOLDER_TRIES 3
#define OWN_FILE_MAX (64u << 20)             /* an AVIF, a JPEG XL or a BigTIFF read whole for its small picture */

/* Owner, 2026-09-25: an archive's first page within a time limit, given up
   when the reading looks too slow to finish in it. */
#define ARCHIVE_7Z_MAX_FILE (24u << 20)        /* a solid block past this is streamed, not held */
#define ARCHIVE_MAX_ENTRY (24u << 20)
#define ARCHIVE_MAX_COST (64u << 20)           /* bytes decoded before the first page, at most */

struct rubraview_thumbs {
    SRWLOCK lock;
    CONDITION_VARIABLE wake_worker;
    HANDLE thread;
    bool stop;
    bool working;

    rubraview_thumbq_t queue;
    rubraview_thumb_job_t jobs[THUMB_JOBS];
    rubraview_thumb_result_t results[THUMB_RESULTS];
    size_t result_count;

    char pictures[512], films[512], archives[256];
    size_t pictures_len, films_len, archives_len;
    rubraview_thumbs_wake_fn wake;
    void *context;
};

/* ---- the shell's picture ---- */

uint8_t *rubraview_pal_shell_thumbnail_bgra(proven_u8str_view_t path, int32_t side, int32_t *out_w, int32_t *out_h) {
    uint8_t *result = NULL;
    char narrow[MAX_PATH * 4];
    WCHAR wide[MAX_PATH * 2];
    if (path.size == 0 || path.size >= sizeof(narrow)) return NULL;
    if (rubraview_stream_is_address(path)) return NULL;   /* RV-157: a station is not the shell's to look at */
    memcpy(narrow, path.ptr, path.size);
    narrow[path.size] = '\0';
    /* The shell's parser, unlike CreateFile, does not take '/' for a
       separator — and a listing joins the folder and the name with '/'. */
    for (size_t i = 0; i < path.size; ++i) {
        if (narrow[i] == '/') narrow[i] = '\\';
    }
    IShellItemImageFactory *factory = NULL;
    if (MultiByteToWideChar(CP_UTF8, 0, narrow, -1, wide, MAX_PATH * 2) > 0 &&
        SUCCEEDED(SHCreateItemFromParsingName(wide, NULL, &RV_IID_IShellItemImageFactory_T, (void**)&factory)) &&
        factory) {
        SIZE size = { side, side };
        HBITMAP bitmap = NULL;
        /* THUMBNAILONLY: the picture itself, never the generic icon. */
        if (SUCCEEDED(factory->lpVtbl->GetImage(factory, size, SIIGBF_BIGGERSIZEOK | SIIGBF_THUMBNAILONLY, &bitmap)) &&
            bitmap) {
            BITMAP info;
            if (GetObjectW(bitmap, sizeof(info), &info) && info.bmWidth > 0 && info.bmHeight > 0 &&
                info.bmWidth <= 4096 && info.bmHeight <= 4096) {
                int32_t w = info.bmWidth, h = info.bmHeight;
                uint8_t *pixels = (uint8_t*)rubraview_malloc_product((size_t)w, (size_t)h, 4u);
                BITMAPINFO bi = {0};
                bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bi.bmiHeader.biWidth = w;
                bi.bmiHeader.biHeight = -h;   /* top row first */
                bi.bmiHeader.biPlanes = 1;
                bi.bmiHeader.biBitCount = 32;
                bi.bmiHeader.biCompression = BI_RGB;
                HDC dc = GetDC(NULL);
                if (pixels && dc && GetDIBits(dc, bitmap, 0, (UINT)h, pixels, &bi, DIB_RGB_COLORS) == h) {
                    result = pixels;
                    pixels = NULL;
                    *out_w = w;
                    *out_h = h;
                }
                if (dc) ReleaseDC(NULL, dc);
                free(pixels);
            }
            DeleteObject(bitmap);
        }
        factory->lpVtbl->Release(factory);
    }
    return result;
}

/* ---- what the file is: its size as shown, its length (owner, 2026-10-16; RV-210: by our own parsers) ---- */

#define FILETIME_UNIX_EPOCH 116444736000000000LL

static int64_t unix_seconds(FILETIME ft) {
    int64_t ticks = (int64_t)(((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime);
    return (ticks - FILETIME_UNIX_EPOCH) / 10000000LL;   /* as pal_fs_win32 gives a listing's times */
}

/* The source's reads: at an offset, a handle opened for no particular order. */
static bool handle_read(void *context, uint64_t at, uint8_t *dst, size_t len, size_t *out_got) {
    HANDLE file = (HANDLE)context;
    size_t total = 0;
    while (total < len) {
        OVERLAPPED where = { .Offset = (DWORD)(at + total), .OffsetHigh = (DWORD)((at + total) >> 32) };
        DWORD want = len - total > (1u << 30) ? (1u << 30) : (DWORD)(len - total), got = 0;
        if (!ReadFile(file, dst + total, want, &got, &where) || got == 0) break;
        total += got;
    }
    *out_got = total;
    return total > 0;
}

static bool wide_path(proven_u8str_view_t path, WCHAR *wide, int cap) {
    char narrow[MAX_PATH * 4];
    if (path.size == 0 || path.size >= sizeof(narrow)) return false;
    memcpy(narrow, path.ptr, path.size);
    narrow[path.size] = '\0';
    for (size_t i = 0; i < path.size; ++i) if (narrow[i] == '/') narrow[i] = '\\';
    return MultiByteToWideChar(CP_UTF8, 0, narrow, -1, wide, cap) > 0;
}

/* A file's facts by media_head over `src` (the calling thread's own); its time too. False when it cannot be opened. */
static bool head_facts(rubraview_head_src_t *src, proven_u8str_view_t path, rubraview_media_facts_t *out, int64_t *out_mtime) {
    *out = (rubraview_media_facts_t){0};
    WCHAR wide[MAX_PATH * 2];
    if (!wide_path(path, wide, MAX_PATH * 2)) return false;
    HANDLE file = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                              FILE_FLAG_RANDOM_ACCESS, NULL);   /* not sequential: the cache would read on far past 64 KB */
    if (file == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info;
    bool ok = GetFileInformationByHandle(file, &info) != 0;
    if (ok) {
        if (out_mtime) *out_mtime = unix_seconds(info.ftLastWriteTime);
        rubraview_head_src_reset(src, handle_read, file, ((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow);
        (void)rubraview_media_head_read(src, rubraview_path_basename(path), out);
    }
    CloseHandle(file);
    return ok;
}

static _Thread_local rubraview_head_src_t g_thumb_head;   /* the thumbnail thread's */

/* ---- a picture in memory, decoded small ---- */

/* An archive's page is bytes, not a file the shell can see: WIC decodes
   it, scaled down on the way (a comic page at full size is tens of MB). */
static _Thread_local int32_t g_decoded_w, g_decoded_h;   /* the last decoded picture's own size, each thumbnail thread its own */

static uint8_t *decode_small_bgra(IWICImagingFactory *factory, const uint8_t *data, size_t size, int32_t side,
                                  int32_t *out_w, int32_t *out_h) {
    g_decoded_w = g_decoded_h = 0;
    if (!factory || !data || size == 0 || size > 0xFFFFFFFFu) return NULL;
    uint8_t *result = NULL;
    IWICStream *stream = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICBitmapScaler *scaler = NULL;
    IWICFormatConverter *converter = NULL;
    UINT w = 0, h = 0;
    /* RV-110: by the road a page takes, so an AVIF or a JPEG XL Windows has
       no codec for gets its small picture too. Ours decode at full size. */
    if ((decoder = rubraview_wic_decoder_for_bytes(factory, data, size, &stream)) != NULL &&
        SUCCEEDED(IWICBitmapDecoder_GetFrame(decoder, 0, &frame)) && frame &&
        SUCCEEDED(IWICBitmapFrameDecode_GetSize(frame, &w, &h)) && w > 0 && h > 0) {
        if (w < 0x7FFFFFFF && h < 0x7FFFFFFF) { g_decoded_w = (int32_t)w; g_decoded_h = (int32_t)h; }
        double scale = (double)side / (double)(w > h ? w : h);
        if (scale > 1.0) scale = 1.0;
        UINT sw = (UINT)((double)w * scale + 0.5), sh = (UINT)((double)h * scale + 0.5);
        if (sw < 1) sw = 1;
        if (sh < 1) sh = 1;
        if (SUCCEEDED(IWICImagingFactory_CreateBitmapScaler(factory, &scaler)) && scaler &&
            SUCCEEDED(IWICBitmapScaler_Initialize(scaler, (IWICBitmapSource*)frame, sw, sh, WICBitmapInterpolationModeFant)) &&
            SUCCEEDED(IWICImagingFactory_CreateFormatConverter(factory, &converter)) && converter &&
            SUCCEEDED(IWICFormatConverter_Initialize(converter, (IWICBitmapSource*)scaler, &GUID_WICPixelFormat32bppBGRA,
                                                     WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom))) {
            uint8_t *pixels = (uint8_t*)rubraview_malloc_product((size_t)sw, (size_t)sh, 4u);
            if (pixels && SUCCEEDED(IWICFormatConverter_CopyPixels(converter, NULL, sw * 4u, sw * sh * 4u, pixels))) {
                result = pixels;
                pixels = NULL;
                *out_w = (int32_t)sw;
                *out_h = (int32_t)sh;
            }
            free(pixels);
        }
    }
    if (converter) IWICFormatConverter_Release(converter);
    if (scaler) IWICBitmapScaler_Release(scaler);
    if (frame) IWICBitmapFrameDecode_Release(frame);
    if (decoder) IWICBitmapDecoder_Release(decoder);
    if (stream) IWICStream_Release(stream);
    return result;
}

/* RV-110: a file the shell has no picture for, when it is one the
   viewer reads without Windows' codec (the shell needs it; an AVIF, a
   JPEG XL) or by rewriting it first (a BigTIFF). */
static uint8_t *own_file_small(IWICImagingFactory *factory, proven_u8str_view_t path, int32_t *out_w, int32_t *out_h) {
    uint8_t *result = NULL;
    rubraview_fs_mapping_t map = {0};
    if (!rubraview_pal_fs_map(path, &map)) return NULL;
    if (map.size <= OWN_FILE_MAX &&
        (rubraview_avif_is(map.data, (size_t)map.size) || rubraview_image_is_jxl(map.data, (size_t)map.size) ||
         rubraview_bigtiff_is(map.data, (size_t)map.size))) {
        result = decode_small_bgra(factory, map.data, (size_t)map.size, THUMB_DECODE_SIDE, out_w, out_h);
    }
    rubraview_pal_fs_unmap(&map);
    return result;
}

/* ---- an archive's first page ----
   Mapped (owner, 2026-09-29): its index is read where it lies and only the
   first page's bytes are touched, so a book of several GB gets its cover
   as quickly as a small one. It used to be read in timed pieces into a
   buffer committed at the archive's whole size, which a large book did
   not finish within 1.5 s — or could not even commit. A first page that
   needs more than ARCHIVE_MAX_COST decoded before it (deep in a solid 7z
   block) is let go, so one book cannot hold the queue up. */
static uint8_t *archive_first_page(proven_arena_t *scratch, IWICImagingFactory *factory, proven_u8str_view_t path,
                                   proven_u8str_view_t picture_filter, int32_t *out_w, int32_t *out_h) {
    uint8_t *result = NULL;
    rubraview_fs_mapping_t map = {0};
    if (!rubraview_pal_fs_map(path, &map)) return NULL;
    if (map.size <= (uint64_t)SIZE_MAX) {
        rubraview_page_source_t source = rubraview_page_source_from_archive(
            scratch, map.data, (size_t)map.size, path, picture_filter, RUBRAVIEW_CODEPAGE_AUTO,
            ARCHIVE_MAX_ENTRY, ARCHIVE_7Z_MAX_FILE);
        if (source.page_count > 0 && rubraview_page_source_read_cost(&source, 0) <= ARCHIVE_MAX_COST) {
            rubraview_page_bytes_t page = rubraview_page_source_read(scratch, &source, 0, ARCHIVE_MAX_ENTRY);
            if (page.ok && page.data.size > 0) {
                result = decode_small_bgra(factory, (const uint8_t*)page.data.ptr, page.data.size,
                                           THUMB_DECODE_SIDE, out_w, out_h);
            }
        }
        rubraview_page_source_close(&source);
    }
    rubraview_pal_fs_unmap(&map);
    return result;
}

/* ---- one job ---- */

static uint8_t *make_one(rubraview_thumbs_t *t, proven_arena_t *scratch, IWICImagingFactory *factory,
                         const rubraview_thumb_job_t *job, int32_t *out_w, int32_t *out_h, rubraview_thumb_result_t *meta) {
    proven_u8str_view_t path = { .ptr = (const proven_byte_t *)job->path, .size = job->path_len };
    proven_u8str_view_t pictures = { .ptr = (const proven_byte_t *)t->pictures, .size = t->pictures_len };
    proven_u8str_view_t films = { .ptr = (const proven_byte_t *)t->films, .size = t->films_len };
    proven_u8str_view_t archives = { .ptr = (const proven_byte_t *)t->archives, .size = t->archives_len };
    int32_t w = 0, h = 0;
    uint8_t *raw = NULL;
    bool is_text = false;

    if (job->bytes) {
        /* a page inside a book: decoded small from its bytes */
        raw = decode_small_bgra(factory, job->bytes, job->bytes_len, THUMB_DECODE_SIDE, &w, &h);
        meta->source_width = g_decoded_w;
        meta->source_height = g_decoded_h;
    } else if (job->folder) {
        /* Its first picture in the order it would open, else its first film;
           when the shell has nothing for one, the next — three at most, so
           a folder of odd files does not hold the queue up. */
        rubraview_fs_listing_t inside = rubraview_pal_fs_list_dir(scratch, path);
        proven_u8str_view_t none = PROVEN_LIT("");
        proven_u8str_view_t kinds[2] = { pictures, films };
        int tries = 0;
        for (int k = 0; k < 2 && !raw && tries < FOLDER_TRIES; ++k) {
            rubraview_sibling_index_t found = rubraview_fs_index_siblings(scratch, &inside, none, kinds[k],
                                                                          RUBRAVIEW_SORT_NAME_NATURAL, true);
            for (size_t i = 0; i < found.count && !raw && tries < FOLDER_TRIES; ++i, ++tries) {
                raw = rubraview_pal_shell_thumbnail_bgra(found.paths[i], 192, &w, &h);
                if (!raw) raw = own_file_small(factory, found.paths[i], &w, &h);
            }
        }
    } else if (rubraview_glob_match_list(rubraview_path_basename(path), archives)) {
        raw = archive_first_page(scratch, factory, path, pictures, &w, &h);
    } else if (rubraview_glob_match_list(rubraview_path_basename(path), (proven_u8str_view_t){ .ptr = (const proven_byte_t *)"*.txt;*.log;*.nfo;*.md", .size = 22 })) {
        raw = text_page_small(scratch, path, job->aspect, &w, &h);   /* RV-135 */
        is_text = raw != NULL;
    } else {
        /* RV-202, RV-210: its own head, read by our parsers: its size as shown, its length */
        rubraview_media_facts_t facts;
        if (head_facts(&g_thumb_head, path, &facts, NULL)) {
            meta->source_width = facts.width;
            meta->source_height = facts.height;
            meta->seconds = facts.seconds;
        }
        raw = rubraview_pal_shell_thumbnail_bgra(path, 192, &w, &h);
        if (!raw) {
            raw = own_file_small(factory, path, &w, &h);
            if (raw && meta->source_width == 0) { meta->source_width = g_decoded_w; meta->source_height = g_decoded_h; }
        }
    }
    if (!raw) return NULL;

    rubraview_pixbuf_t shell = { .pixels = raw, .width = w, .height = h, .stride = w * 4,
                                 .format = RUBRAVIEW_PIXFMT_BGRA8 };
    /* a page of text keeps its size and its sharpness, only a little darker, so its words can be read */
    rubraview_pixbuf_t thumb = is_text ? rubraview_thumb_make(scratch, &shell, job->aspect, TEXT_TILE_W, 0, 0.92)
                                       : rubraview_thumb_make(scratch, &shell, job->aspect, THUMB_MAX_WIDTH, THUMB_BLUR, THUMB_KEEP);
    uint8_t *out = NULL;
    if (rubraview_pixbuf_is_valid(&thumb)) {
        size_t bytes = (size_t)thumb.width * (size_t)thumb.height * 4u;
        out = (uint8_t*)malloc(bytes);
        if (out) {
            for (int32_t y = 0; y < thumb.height; ++y) {
                memcpy(out + (size_t)y * (size_t)thumb.width * 4u, thumb.pixels + (ptrdiff_t)y * thumb.stride,
                       (size_t)thumb.width * 4u);
            }
            *out_w = thumb.width;
            *out_h = thumb.height;
        }
    }
    free(raw);
    return out;
}


/* RV-135 (2026-10-18): a text file's tile is a page of its first lines, dark on
   light paper, so text files are told apart at a glance. The first 4 KB are read,
   the encoding guessed as the viewer guesses it. */
static uint8_t *text_page_small(proven_arena_t *scratch, proven_u8str_view_t path, double aspect, int32_t *out_w, int32_t *out_h) {
    rubraview_fs_mapping_t map;
    if (!rubraview_pal_fs_map(path, &map)) return NULL;
    size_t n = map.size < 4096u ? (size_t)map.size : 4096u;
    size_t bom = 0;
    rubraview_textenc_t enc = rubraview_textenc_guess(map.data, n, NULL, &bom);
    const uint8_t *b = map.data + bom;
    size_t len = n - bom, raw_len = 0;
    char *raw = NULL;
    if (enc == RUBRAVIEW_TEXTENC_UTF8) {
        while (len > 0 && (b[len - 1] & 0xC0u) == 0x80u) --len;   /* a character cut by the 4 KB: left out */
        if (len > 0 && b[len - 1] >= 0xC0u) --len;
        raw = (char *)malloc(len + 1);
        if (raw) { memcpy(raw, b, len); raw[len] = '\0'; raw_len = len; }
    } else if (enc == RUBRAVIEW_TEXTENC_UTF16LE || enc == RUBRAVIEW_TEXTENC_UTF16BE) {
        raw = rubraview_textenc_utf16_to_utf8(b, len & ~(size_t)1, enc == RUBRAVIEW_TEXTENC_UTF16BE, &raw_len);
    } else {
        raw = rubraview_pal_text_decode(b, len, rubraview_textenc_codepage(enc), &raw_len);
    }
    rubraview_pal_fs_unmap(&map);
    if (!raw) return NULL;
    size_t clean_len = 0;
    char *clean = rubraview_text_clean(raw, raw_len, &clean_len);
    free(raw);
    if (!clean) return NULL;
    for (size_t i = 0; i < clean_len; ++i) if (clean[i] == '\t') clean[i] = ' ';
    /* The page is made in the tile's own shape, so nothing is cut from it: the first lines stay at its top. */
    if (!(aspect > 0.2 && aspect < 5.0)) aspect = (double)TEXT_TILE_W / TEXT_TILE_H;
    int32_t tw = TEXT_TILE_W, th = (int32_t)((double)TEXT_TILE_W / aspect + 0.5);
    if (th > TEXT_TILE_H * 2) { th = TEXT_TILE_H * 2; tw = (int32_t)((double)th * aspect + 0.5); }
    if (th < 48) th = 48;
    const int32_t pad = 10;
    rubraview_pixbuf_t mask = rubraview_pal_text_mask_box(scratch, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)clean, .size = clean_len }, PROVEN_LIT(""),
                                                          14, tw - 2 * pad, th - 2 * pad);
    free(clean);
    uint8_t *out = (uint8_t *)malloc((size_t)tw * (size_t)th * 4u);
    if (!out) return NULL;
    const uint8_t paper[3] = { 0xE6, 0xEB, 0xEE }, ink[3] = { 0x2A, 0x2A, 0x2A };   /* BGR */
    for (int32_t y = 0; y < th; ++y) {
        for (int32_t x = 0; x < tw; ++x) {
            uint8_t *q = out + ((size_t)y * (size_t)tw + (size_t)x) * 4u;
            int32_t mx = x - pad, my = y - pad;
            uint32_t a = mask.pixels && mx >= 0 && my >= 0 && mx < mask.width && my < mask.height
                       ? mask.pixels[(size_t)my * (size_t)mask.stride + (size_t)mx] : 0u;
            for (int c = 0; c < 3; ++c) q[c] = (uint8_t)((paper[c] * (255u - a) + ink[c] * a) / 255u);
            q[3] = 255;
        }
    }
    *out_w = tw;
    *out_h = th;
    return out;
}

static DWORD WINAPI thumbs_thread(LPVOID param) {
    rubraview_thumbs_t *t = (rubraview_thumbs_t*)param;
    bool com = SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
    IWICImagingFactory *factory = NULL;
    if (com) {
        (void)CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                               &IID_IWICImagingFactory, (void**)&factory);
    }
    void *memory = malloc(THUMB_SCRATCH_BYTES);

    for (;;) {
        rubraview_thumb_job_t job;
        AcquireSRWLockExclusive(&t->lock);
        while (!t->stop && t->queue.count == 0) SleepConditionVariableSRW(&t->wake_worker, &t->lock, INFINITE, 0);
        if (t->stop) { ReleaseSRWLockExclusive(&t->lock); break; }
        (void)rubraview_thumbq_take(&t->queue, &job);
        t->working = true;
        ReleaseSRWLockExclusive(&t->lock);

        rubraview_thumb_result_t result = { .generation = job.generation, .index = job.index };
        if (memory && com) {
            proven_arena_t scratch = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)memory,
                                                                             .size = THUMB_SCRATCH_BYTES });
            result.bgra = make_one(t, &scratch, factory, &job, &result.width, &result.height, &result);
        }
        free(job.bytes);   /* taken: the thread's */

        AcquireSRWLockExclusive(&t->lock);
        t->working = false;
        if (job.generation == t->queue.generation) {
            if (t->result_count == THUMB_RESULTS) {   /* nobody is reading: the oldest goes */
                free(t->results[0].bgra);
                memmove(&t->results[0], &t->results[1], (THUMB_RESULTS - 1) * sizeof(t->results[0]));
                t->result_count--;
            }
            t->results[t->result_count++] = result;
        } else {
            free(result.bgra);   /* made for a listing that is gone */
        }
        ReleaseSRWLockExclusive(&t->lock);
        if (t->wake) t->wake(t->context);
    }

    free(memory);
    rubraview_head_src_free(&g_thumb_head);
    if (factory) IWICImagingFactory_Release(factory);
    if (com) CoUninitialize();
    return 0;
}

/* ---- the main thread's side ---- */

static size_t copy_filter(char *to, size_t cap, proven_u8str_view_t from) {
    size_t n = from.size < cap ? from.size : cap;
    if (n) memcpy(to, from.ptr, n);
    return n;
}

rubraview_thumbs_t *rubraview_pal_thumbs_start(proven_u8str_view_t picture_filter, proven_u8str_view_t film_filter, proven_u8str_view_t archive_filter,
                                               rubraview_thumbs_wake_fn wake, void *context) {
    rubraview_thumbs_t *t = (rubraview_thumbs_t*)calloc(1, sizeof(*t));
    if (!t) return NULL;
    InitializeSRWLock(&t->lock);
    InitializeConditionVariable(&t->wake_worker);
    t->queue = (rubraview_thumbq_t){ .jobs = t->jobs, .capacity = THUMB_JOBS };
    t->pictures_len = copy_filter(t->pictures, sizeof(t->pictures), picture_filter);
    t->films_len = copy_filter(t->films, sizeof(t->films), film_filter);
    t->archives_len = copy_filter(t->archives, sizeof(t->archives), archive_filter);
    t->wake = wake;
    t->context = context;
    t->thread = CreateThread(NULL, 0, thumbs_thread, t, 0, NULL);
    if (!t->thread) { free(t); return NULL; }
    /* Below the viewer: a thumbnail must never slow a page turn or a film. */
    SetThreadPriority(t->thread, THREAD_PRIORITY_BELOW_NORMAL);
    return t;
}

void rubraview_pal_thumbs_stop(rubraview_thumbs_t *t) {
    if (!t) return;
    AcquireSRWLockExclusive(&t->lock);
    t->stop = true;
    ReleaseSRWLockExclusive(&t->lock);
    WakeAllConditionVariable(&t->wake_worker);
    /* A shell or disk call may be in flight; it is waited for, briefly. */
    if (WaitForSingleObject(t->thread, 3000) == WAIT_OBJECT_0) {
        CloseHandle(t->thread);
        for (size_t i = 0; i < t->result_count; ++i) free(t->results[i].bgra);
        rubraview_thumbq_set_generation(&t->queue, t->queue.generation + 1);   /* frees what is still queued */
        free(t);
    }
    /* Otherwise the thread still holds `t`; it is left, not freed under it. */
}

void rubraview_pal_thumbs_generation(rubraview_thumbs_t *t, uint32_t generation) {
    if (!t) return;
    AcquireSRWLockExclusive(&t->lock);
    rubraview_thumbq_set_generation(&t->queue, generation);
    for (size_t i = 0; i < t->result_count; ++i) free(t->results[i].bgra);
    t->result_count = 0;
    ReleaseSRWLockExclusive(&t->lock);
}

bool rubraview_pal_thumbs_request(rubraview_thumbs_t *t, uint32_t generation, size_t index,
                                  bool folder, double aspect, proven_u8str_view_t path) {
    if (!t || rubraview_stream_is_address(path)) return false;   /* RV-157: no picture made of a station */
    AcquireSRWLockExclusive(&t->lock);
    bool ok = rubraview_thumbq_push(&t->queue, generation, index, folder, aspect, path);
    ReleaseSRWLockExclusive(&t->lock);
    if (ok) WakeConditionVariable(&t->wake_worker);
    return ok;
}

bool rubraview_pal_thumbs_request_bytes(rubraview_thumbs_t *t, uint32_t generation, size_t index,
                                        double aspect, uint8_t *bytes, size_t len) {
    if (!t) { free(bytes); return false; }
    AcquireSRWLockExclusive(&t->lock);
    bool ok = rubraview_thumbq_push_bytes(&t->queue, generation, index, aspect, bytes, len);
    ReleaseSRWLockExclusive(&t->lock);
    if (ok) WakeConditionVariable(&t->wake_worker);
    return ok;
}

bool rubraview_pal_thumbs_take(rubraview_thumbs_t *t, rubraview_thumb_result_t *out) {
    if (!t || !out) return false;
    bool got = false;
    AcquireSRWLockExclusive(&t->lock);
    if (t->result_count > 0) {
        *out = t->results[0];
        memmove(&t->results[0], &t->results[1], (t->result_count - 1) * sizeof(t->results[0]));
        t->result_count--;
        got = true;
    }
    ReleaseSRWLockExclusive(&t->lock);
    return got;
}

bool rubraview_pal_thumbs_busy(rubraview_thumbs_t *t) {
    if (!t) return false;
    AcquireSRWLockExclusive(&t->lock);
    bool busy = t->working || t->queue.count > 0 || t->result_count > 0;
    ReleaseSRWLockExclusive(&t->lock);
    return busy;
}

/* ---- RV-192, RV-210: the facts of many files ---- */

#define SIZES_THREADS_MAX 4

struct rubraview_sizes {
    HANDLE threads[SIZES_THREADS_MAX];
    unsigned thread_count;
    volatile LONG stop, next, done;
    size_t count;
    char **paths;
    size_t *lens;
    int64_t *expect;          /* the time kept on disk, or 0 */
    int32_t *w, *h;
    double *seconds;
    int64_t *mtime;
    uint8_t *confirmed;
    volatile LONG *ready;     /* set after the rest of the answer, by a full barrier */
    rubraview_thumbs_wake_fn wake;
    void *context;
};

/* Readers side by side only where seeking costs nothing: a fixed drive that says it has no seek penalty. */
static unsigned sizes_threads_for(proven_u8str_view_t path) {
    WCHAR wide[MAX_PATH * 2], volume[MAX_PATH];
    if (!wide_path(path, wide, MAX_PATH * 2) || !GetVolumePathNameW(wide, volume, MAX_PATH)) return 1;
    if (GetDriveTypeW(volume) != DRIVE_FIXED || !(volume[0] && volume[1] == L':')) return 1;
    WCHAR device[8] = { L'\\', L'\\', L'.', L'\\', volume[0], L':', 0 };
    HANDLE drive = CreateFileW(device, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);   /* no rights needed to ask */
    if (drive == INVALID_HANDLE_VALUE) return 1;
    STORAGE_PROPERTY_QUERY query = { .PropertyId = StorageDeviceSeekPenaltyProperty, .QueryType = PropertyStandardQuery };
    DEVICE_SEEK_PENALTY_DESCRIPTOR answer = {0};
    DWORD got = 0;
    bool ok = DeviceIoControl(drive, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &answer, sizeof(answer), &got, NULL) &&
              got >= sizeof(answer);
    CloseHandle(drive);
    return ok && !answer.IncursSeekPenalty ? SIZES_THREADS_MAX : 1;
}

static DWORD WINAPI sizes_thread(LPVOID param) {
    rubraview_sizes_t *s = (rubraview_sizes_t*)param;
    rubraview_head_src_t src = {0};
    double told = rubraview_pal_time_now_seconds();
    for (;;) {
        if (InterlockedCompareExchange(&s->stop, 0, 0)) break;
        size_t i = (size_t)InterlockedIncrement(&s->next) - 1;
        if (i >= s->count) break;
        proven_u8str_view_t path = { .ptr = (const proven_byte_t *)s->paths[i], .size = s->lens[i] };
        rubraview_media_facts_t facts = {0};
        int64_t mtime = 0;
        WCHAR wide[MAX_PATH * 2];
        WIN32_FILE_ATTRIBUTE_DATA attrs;
        if (s->lens[i] == 0) {
            /* nothing to read */
        } else if (s->expect && s->expect[i] && wide_path(path, wide, MAX_PATH * 2) &&
                   GetFileAttributesExW(wide, GetFileExInfoStandard, &attrs) && unix_seconds(attrs.ftLastWriteTime) == s->expect[i]) {
            mtime = s->expect[i];   /* as it was when its facts were kept: not read */
            s->confirmed[i] = 1;
        } else {
            (void)head_facts(&src, path, &facts, &mtime);
        }
        s->w[i] = facts.width;
        s->h[i] = facts.height;
        s->seconds[i] = facts.seconds;
        s->mtime[i] = mtime;
        InterlockedExchange(&s->ready[i], 1);   /* a full barrier: the answer is written before it is said ready */
        LONG done = InterlockedIncrement(&s->done);
        double now = rubraview_pal_time_now_seconds();
        if (s->wake && ((size_t)done == s->count || now - told > 0.2)) { told = now; s->wake(s->context); }
    }
    rubraview_head_src_free(&src);
    return 0;
}

static void sizes_free(rubraview_sizes_t *s) {
    for (size_t i = 0; s->paths && i < s->count; ++i) free(s->paths[i]);
    free(s->paths); free(s->lens); free(s->expect); free(s->w); free(s->h); free(s->seconds); free(s->mtime);
    free(s->confirmed); free((void *)s->ready);
    free(s);
}

rubraview_sizes_t *rubraview_pal_sizes_start(const proven_u8str_view_t *paths, const int64_t *expect, size_t count,
                                             bool background, rubraview_thumbs_wake_fn wake, void *context) {
    if (count > 0 && !paths) return NULL;
    rubraview_sizes_t *s = (rubraview_sizes_t*)calloc(1, sizeof(*s));
    if (!s) return NULL;
    size_t n = count ? count : 1;
    s->paths = (char**)calloc(n, sizeof(char*));
    s->lens = (size_t*)calloc(n, sizeof(size_t));
    s->expect = expect ? (int64_t*)calloc(n, sizeof(int64_t)) : NULL;
    s->w = (int32_t*)calloc(n, sizeof(int32_t));
    s->h = (int32_t*)calloc(n, sizeof(int32_t));
    s->seconds = (double*)calloc(n, sizeof(double));
    s->mtime = (int64_t*)calloc(n, sizeof(int64_t));
    s->confirmed = (uint8_t*)calloc(n, 1);
    s->ready = (volatile LONG*)calloc(n, sizeof(LONG));
    s->count = count;
    bool ok = s->paths && s->lens && (!expect || s->expect) && s->w && s->h && s->seconds && s->mtime && s->confirmed && s->ready;
    for (size_t i = 0; ok && i < count; ++i) {
        if (expect) s->expect[i] = expect[i];
        if (paths[i].size == 0) continue;
        s->paths[i] = (char*)malloc(paths[i].size);
        if (!s->paths[i]) { ok = false; break; }
        memcpy(s->paths[i], paths[i].ptr, paths[i].size);
        s->lens[i] = paths[i].size;
    }
    if (!ok) { sizes_free(s); return NULL; }
    s->wake = wake;
    s->context = context;
    unsigned want = 1;
    for (size_t i = 0; i < count; ++i) if (s->lens[i]) { want = sizes_threads_for(paths[i]); break; }
    if ((size_t)want > count) want = count ? (unsigned)count : 1;
    for (unsigned k = 0; k < want; ++k) {
        s->threads[k] = CreateThread(NULL, 0, sizes_thread, s, 0, NULL);
        if (!s->threads[k]) break;
        /* Ahead, the lowest CPU priority, but not Windows' background mode: its disk priority let a film playing
           beside it hold 388 heads back past 4 s, where they take 0.45 s (the VM, 2026-10-31). */
        SetThreadPriority(s->threads[k], background ? THREAD_PRIORITY_LOWEST : THREAD_PRIORITY_BELOW_NORMAL);
        s->thread_count++;
    }
    if (s->thread_count == 0) { sizes_free(s); return NULL; }
    if (count == 0 && wake) wake(context);
    return s;
}

size_t rubraview_pal_sizes_done(rubraview_sizes_t *s) {
    return s ? (size_t)InterlockedCompareExchange(&s->done, 0, 0) : 0;
}

size_t rubraview_pal_sizes_count(const rubraview_sizes_t *s) {
    return s ? s->count : 0;
}

bool rubraview_pal_sizes_get(const rubraview_sizes_t *s, size_t index, int32_t *out_width, int32_t *out_height,
                             double *out_seconds, int64_t *out_mtime, bool *out_confirmed) {
    if (!s || index >= s->count || !InterlockedCompareExchange((volatile LONG*)&s->ready[index], 0, 0)) return false;
    if (out_width) *out_width = s->w[index];
    if (out_height) *out_height = s->h[index];
    if (out_seconds) *out_seconds = s->seconds[index];
    if (out_mtime) *out_mtime = s->mtime[index];
    if (out_confirmed) *out_confirmed = s->confirmed[index] != 0;
    return true;
}

proven_u8str_view_t rubraview_pal_sizes_path(const rubraview_sizes_t *s, size_t index) {
    if (!s || index >= s->count || !s->paths[index]) return (proven_u8str_view_t){0};
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)s->paths[index], .size = s->lens[index] };
}

void rubraview_pal_sizes_hurry(rubraview_sizes_t *s) {
    if (!s) return;
    for (unsigned k = 0; k < s->thread_count; ++k) SetThreadPriority(s->threads[k], THREAD_PRIORITY_BELOW_NORMAL);
}

void rubraview_pal_sizes_stop(rubraview_sizes_t *s) {
    if (!s) return;
    InterlockedExchange(&s->stop, 1);
    /* A read may be in flight on each; they are waited for, briefly. */
    if (WaitForMultipleObjects(s->thread_count, s->threads, TRUE, 3000) == WAIT_OBJECT_0) {
        for (unsigned k = 0; k < s->thread_count; ++k) CloseHandle(s->threads[k]);
        sizes_free(s);
    }
    /* Otherwise a thread still holds `s`; it is left, not freed under it. */
}

#endif /* _WIN32 */
