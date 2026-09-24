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

#include "rubraview/pal/pal_thumbs.h"
#include "rubraview/pal/pal_fs.h"
#include "rubraview/pal/pal_time.h"
#include "rubraview/thumbq.h"
#include "rubraview/thumb.h"
#include "rubraview/archive.h"
#include "rubraview/pagesource.h"
#include "rubraview/glob.h"
#include "rubraview/path.h"

/* Defined here so the link does not depend on which MinGW carries it. */
static const GUID RV_IID_IShellItemImageFactory_T = {0xBCC18B79, 0xBA16, 0x442F, {0x80, 0xC4, 0x8A, 0x59, 0xC3, 0x0C, 0x46, 0x3B}};

#define THUMB_JOBS 256
#define THUMB_RESULTS 64
#define THUMB_SCRATCH_BYTES (48u << 20)   /* a comic page inflated, plus its list */
#define THUMB_MAX_WIDTH 96
#define THUMB_BLUR 1
#define THUMB_KEEP 0.78
#define THUMB_DECODE_SIDE 256

/* Owner, 2026-09-25: an archive's first page within a time limit, given up
   when the reading looks too slow to finish in it. */
#define ARCHIVE_SECONDS 1.5
#define ARCHIVE_MAX_FILE (2ull << 30)          /* a bigger one is not tried */
#define ARCHIVE_7Z_MAX_FILE (24u << 20)        /* solid: its first page may need all of it */
#define ARCHIVE_MAX_ENTRY (24u << 20)
#define ARCHIVE_CHUNK (1u << 20)

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

uint8_t *rubraview_pal_shell_thumbnail_bgra(u8str_t path, int32_t side, int32_t *out_w, int32_t *out_h) {
    uint8_t *result = NULL;
    char narrow[MAX_PATH * 4];
    WCHAR wide[MAX_PATH * 2];
    if (path.len == 0 || path.len >= sizeof(narrow)) return NULL;
    memcpy(narrow, path.ptr, path.len);
    narrow[path.len] = '\0';
    /* The shell's parser, unlike CreateFile, does not take '/' for a
       separator — and a listing joins the folder and the name with '/'. */
    for (size_t i = 0; i < path.len; ++i) {
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
                uint8_t *pixels = (uint8_t*)malloc((size_t)w * (size_t)h * 4u);
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

/* ---- a picture in memory, decoded small ---- */

/* An archive's page is bytes, not a file the shell can see: WIC decodes
   it, scaled down on the way (a comic page at full size is tens of MB). */
static uint8_t *decode_small_bgra(IWICImagingFactory *factory, const uint8_t *data, size_t size, int32_t side,
                                  int32_t *out_w, int32_t *out_h) {
    if (!factory || !data || size == 0 || size > 0xFFFFFFFFu) return NULL;
    uint8_t *result = NULL;
    IWICStream *stream = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICBitmapScaler *scaler = NULL;
    IWICFormatConverter *converter = NULL;
    UINT w = 0, h = 0;
    if (SUCCEEDED(IWICImagingFactory_CreateStream(factory, &stream)) && stream &&
        SUCCEEDED(IWICStream_InitializeFromMemory(stream, (BYTE*)(uintptr_t)data, (DWORD)size)) &&
        SUCCEEDED(IWICImagingFactory_CreateDecoderFromStream(factory, (IStream*)stream, NULL,
                                                             WICDecodeMetadataCacheOnDemand, &decoder)) && decoder &&
        SUCCEEDED(IWICBitmapDecoder_GetFrame(decoder, 0, &frame)) && frame &&
        SUCCEEDED(IWICBitmapFrameDecode_GetSize(frame, &w, &h)) && w > 0 && h > 0) {
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
            uint8_t *pixels = (uint8_t*)malloc((size_t)sw * (size_t)sh * 4u);
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

/* ---- an archive's first page, read in pieces under a time limit ---- */

typedef struct timed_reader {
    HANDLE file;
    double started;
    uint64_t done;       /* bytes read so far, all pieces together */
} timed_reader_t;

/* Reads [offset, offset + length) into `buffer + offset`, a megabyte at a
   time, giving up as soon as the whole plan (`planned` bytes) could not be
   finished within ARCHIVE_SECONDS at the speed seen so far. */
static bool timed_read(timed_reader_t *r, uint8_t *buffer, uint64_t offset, uint64_t length, uint64_t planned) {
    uint64_t end = offset + length;
    while (offset < end) {
        double elapsed = rubraview_pal_time_now_seconds() - r->started;
        if (!rubraview_read_budget_ok(r->done, planned, elapsed, ARCHIVE_SECONDS)) return false;
        DWORD want = (DWORD)(end - offset < ARCHIVE_CHUNK ? end - offset : ARCHIVE_CHUNK);
        LARGE_INTEGER at;
        at.QuadPart = (LONGLONG)offset;
        DWORD got = 0;
        if (!SetFilePointerEx(r->file, at, NULL, FILE_BEGIN) ||
            !ReadFile(r->file, buffer + offset, want, &got, NULL) || got == 0) {
            return false;
        }
        offset += got;
        r->done += got;
    }
    return true;
}

static HANDLE open_utf8(u8str_t path) {
    char narrow[MAX_PATH * 4];
    WCHAR wide[MAX_PATH * 2];
    if (path.len == 0 || path.len >= sizeof(narrow)) return INVALID_HANDLE_VALUE;
    memcpy(narrow, path.ptr, path.len);
    narrow[path.len] = '\0';
    if (MultiByteToWideChar(CP_UTF8, 0, narrow, -1, wide, MAX_PATH * 2) <= 0) return INVALID_HANDLE_VALUE;
    return CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                       OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
}

/*
 * The file gets a zero-filled buffer of its own size (committed memory the
 * system backs only where it is written), and only the pieces a reader
 * needs are read into it: for a ZIP the directory at the end and then the
 * first page's entry; a 7z, being solid, whole and only when small. The
 * page-source code then reads it as if it were all there — an entry that
 * was not read has no local header and is refused (test_archive).
 */
static uint8_t *archive_first_page(proven_arena_t *scratch, IWICImagingFactory *factory, u8str_t path,
                                   u8str_t picture_filter, int32_t *out_w, int32_t *out_h) {
    uint8_t *result = NULL;
    HANDLE file = open_utf8(path);
    if (file == INVALID_HANDLE_VALUE) return NULL;
    LARGE_INTEGER size_li;
    uint8_t *buffer = NULL;
    uint64_t size = 0;
    bool seven = rubraview_path_has_ext(path, ".7z") || rubraview_path_has_ext(path, ".cb7");
    if (!GetFileSizeEx(file, &size_li) || size_li.QuadPart <= 0) goto done;
    size = (uint64_t)size_li.QuadPart;
    if (size > ARCHIVE_MAX_FILE || (seven && size > ARCHIVE_7Z_MAX_FILE)) goto done;
    buffer = (uint8_t*)VirtualAlloc(NULL, (SIZE_T)size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!buffer) goto done;

    timed_reader_t reader = { .file = file, .started = rubraview_pal_time_now_seconds() };
    if (seven) {
        if (!timed_read(&reader, buffer, 0, size, size)) goto done;
    } else {
        /* The end: the EOCD sits in the last 22 + 65535 bytes. */
        uint64_t tail = size < 65557u ? size : 65557u;
        if (!timed_read(&reader, buffer, size - tail, tail, tail)) goto done;
        uint64_t cd_offset = 0, cd_size = 0;
        if (!rubraview_zip_locate_directory(buffer + (size - tail), (size_t)tail, size, &cd_offset, &cd_size)) goto done;
        uint64_t have_from = size - tail;
        if (cd_offset < have_from &&
            !timed_read(&reader, buffer, cd_offset, have_from - cd_offset, reader.done + (have_from - cd_offset))) {
            goto done;
        }
    }

    rubraview_page_source_t source = rubraview_page_source_from_archive(
        scratch, buffer, (size_t)size, path, picture_filter, RUBRAVIEW_CODEPAGE_AUTO, ARCHIVE_MAX_ENTRY, ARCHIVE_7Z_MAX_FILE);
    if (source.page_count > 0) {
        bool ready = true;
        if (!seven && source.kind == RUBRAVIEW_PAGE_SOURCE_ARCHIVE) {
            /* The first page's entry: its local header, then the rest. */
            size_t entry = source.pages[0].entry_index;
            if (entry < source.archive.entry_count) {
                const rubraview_zip_entry_t *e = &source.archive.entries[entry];
                uint64_t at = e->local_header_offset, span = 0;
                ready = e->compressed_size <= ARCHIVE_MAX_ENTRY && at + 30 <= size &&
                        timed_read(&reader, buffer, at, 30, reader.done + 30 + e->compressed_size) &&
                        rubraview_zip_local_span(buffer + at, e->compressed_size, &span) && at + span <= size &&
                        timed_read(&reader, buffer, at + 30, span - 30, reader.done + (span - 30));
            } else {
                ready = false;
            }
        }
        if (ready && rubraview_pal_time_now_seconds() - reader.started < ARCHIVE_SECONDS) {
            rubraview_page_bytes_t page = rubraview_page_source_read(scratch, &source, 0, ARCHIVE_MAX_ENTRY);
            if (page.ok && page.data.len > 0) {
                result = decode_small_bgra(factory, (const uint8_t*)page.data.ptr, page.data.len,
                                           THUMB_DECODE_SIDE, out_w, out_h);
            }
        }
    }
    rubraview_page_source_close(&source);

done:
    if (buffer) VirtualFree(buffer, 0, MEM_RELEASE);
    CloseHandle(file);
    return result;
}

/* ---- one job ---- */

static uint8_t *make_one(rubraview_thumbs_t *t, proven_arena_t *scratch, IWICImagingFactory *factory,
                         const rubraview_thumb_job_t *job, int32_t *out_w, int32_t *out_h) {
    u8str_t path = { .ptr = job->path, .len = job->path_len };
    u8str_t pictures = { .ptr = t->pictures, .len = t->pictures_len };
    u8str_t films = { .ptr = t->films, .len = t->films_len };
    u8str_t archives = { .ptr = t->archives, .len = t->archives_len };
    int32_t w = 0, h = 0;
    uint8_t *raw = NULL;

    if (job->folder) {
        /* Its first picture in the order it would open, else its first film. */
        rubraview_fs_listing_t inside = rubraview_pal_fs_list_dir(scratch, path);
        u8str_t none = { .ptr = "", .len = 0 };
        rubraview_sibling_index_t first = rubraview_fs_index_siblings(scratch, &inside, none, pictures,
                                                                      RUBRAVIEW_SORT_NAME_NATURAL, true);
        if (first.count == 0) {
            first = rubraview_fs_index_siblings(scratch, &inside, none, films, RUBRAVIEW_SORT_NAME_NATURAL, true);
        }
        if (first.count > 0) raw = rubraview_pal_shell_thumbnail_bgra(first.paths[0], 192, &w, &h);
    } else if (rubraview_glob_match_list(rubraview_path_basename(path), archives)) {
        raw = archive_first_page(scratch, factory, path, pictures, &w, &h);
    } else {
        raw = rubraview_pal_shell_thumbnail_bgra(path, 192, &w, &h);
    }
    if (!raw) return NULL;

    rubraview_pixbuf_t shell = { .pixels = raw, .width = w, .height = h, .stride = w * 4,
                                 .format = RUBRAVIEW_PIXFMT_BGRA8 };
    rubraview_pixbuf_t thumb = rubraview_thumb_make(scratch, &shell, job->aspect, THUMB_MAX_WIDTH, THUMB_BLUR, THUMB_KEEP);
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
            result.bgra = make_one(t, &scratch, factory, &job, &result.width, &result.height);
        }

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
    if (factory) IWICImagingFactory_Release(factory);
    if (com) CoUninitialize();
    return 0;
}

/* ---- the main thread's side ---- */

static size_t copy_filter(char *to, size_t cap, u8str_t from) {
    size_t n = from.len < cap ? from.len : cap;
    if (n) memcpy(to, from.ptr, n);
    return n;
}

rubraview_thumbs_t *rubraview_pal_thumbs_start(u8str_t picture_filter, u8str_t film_filter, u8str_t archive_filter,
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
                                  bool folder, double aspect, u8str_t path) {
    if (!t) return false;
    AcquireSRWLockExclusive(&t->lock);
    bool ok = rubraview_thumbq_push(&t->queue, generation, index, folder, aspect, path);
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

#endif /* _WIN32 */
