/*
 * Tiles of a page shown reduced, made on a thread of their own (D-40). See
 * pal_tiles.h. The thread owns its COM apartment, its WIC factory and the
 * page's bytes; the main thread and it share only what sits under `lock`.
 */
#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <wincodec.h>
#include <objbase.h>
#include <stdlib.h>
#include <string.h>

#include "rubraview/pal/pal_tiles.h"
#include "rubraview/resample.h"
#include "rubraview/pal/pal_image_wic_internal.h"

#define TILES_WANT_MAX 128
#define TILES_RESULTS 64
#define TILES_MAX_FILE ((uint64_t)1 << 30)

struct rubraview_tiles {
    SRWLOCK lock;
    CONDITION_VARIABLE wake_worker;
    HANDLE thread;
    bool stop;

    /* the page, as last set */
    uint32_t generation;
    char path[MAX_PATH * 4];
    size_t path_len;
    bool exif;
    int32_t picture_w, picture_h;
    bool source_changed;
    uint8_t *given;          /* an archive page's bytes, not yet taken by the thread (RV-085) */
    size_t given_size;

    /* the newest request */
    rubraview_tile_key_t want[TILES_WANT_MAX];
    size_t want_count;
    uint32_t want_serial;

    rubraview_tile_result_t results[TILES_RESULTS];
    size_t result_count;

    rubraview_tiles_wake_fn wake;
    void *context;
};

/* The whole file, read once per page with full sharing (as the page's own
   decode does: nothing the viewer holds may lock a user's file). */
static uint8_t *read_whole(const char *path, size_t path_len, size_t *out_size) {
    *out_size = 0;
    char narrow[MAX_PATH * 4];
    WCHAR wide[MAX_PATH * 2];
    if (path_len == 0 || path_len >= sizeof(narrow)) return NULL;
    memcpy(narrow, path, path_len);
    narrow[path_len] = '\0';
    if (MultiByteToWideChar(CP_UTF8, 0, narrow, -1, wide, (int)(sizeof(wide) / sizeof(wide[0]))) <= 0) return NULL;
    HANDLE file = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE) return NULL;
    LARGE_INTEGER size;
    uint8_t *bytes = NULL;
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && (uint64_t)size.QuadPart <= TILES_MAX_FILE) {
        bytes = (uint8_t*)malloc((size_t)size.QuadPart);
        size_t total = 0;
        while (bytes && total < (size_t)size.QuadPart) {
            DWORD got = 0;
            DWORD ask = (DWORD)((size_t)size.QuadPart - total > (1u << 30) ? (1u << 30) : (size_t)size.QuadPart - total);
            if (!ReadFile(file, bytes + total, ask, &got, NULL) || got == 0) break;
            total += got;
        }
        if (bytes && total == (size_t)size.QuadPart) {
            *out_size = total;
        } else {
            free(bytes);
            bytes = NULL;
        }
    }
    CloseHandle(file);
    return bytes;
}

static void post_result(rubraview_tiles_t *t, rubraview_tile_result_t result) {
    AcquireSRWLockExclusive(&t->lock);
    if (result.generation == t->generation) {
        if (t->result_count == TILES_RESULTS) {   /* nobody is reading: the oldest goes */
            free(t->results[0].bgra);
            memmove(&t->results[0], &t->results[1], (TILES_RESULTS - 1) * sizeof(t->results[0]));
            t->result_count--;
        }
        t->results[t->result_count++] = result;
        result.bgra = NULL;
    }
    ReleaseSRWLockExclusive(&t->lock);
    free(result.bgra);   /* made for a page that is gone */
    if (t->wake) t->wake(t->context);
}

static DWORD WINAPI tiles_thread(LPVOID param) {
    rubraview_tiles_t *t = (rubraview_tiles_t*)param;
    bool com = SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
    IWICImagingFactory *factory = NULL;
    if (com) {
        (void)CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                               &IID_IWICImagingFactory, (void**)&factory);
    }
    uint8_t *bytes = NULL;
    size_t byte_count = 0;
    uint32_t bytes_generation = 0;
    rubraview_tile_key_t work[TILES_WANT_MAX];
    bool finished[TILES_WANT_MAX];

    for (;;) {
        AcquireSRWLockExclusive(&t->lock);
        while (!t->stop && t->want_count == 0) SleepConditionVariableSRW(&t->wake_worker, &t->lock, INFINITE, 0);
        if (t->stop) { ReleaseSRWLockExclusive(&t->lock); break; }
        uint32_t generation = t->generation;
        uint32_t serial = t->want_serial;
        size_t count = t->want_count;
        memcpy(work, t->want, count * sizeof(work[0]));
        bool reload = t->source_changed || bytes_generation != generation;
        char path[MAX_PATH * 4];
        size_t path_len = t->path_len;
        memcpy(path, t->path, path_len);
        bool exif = t->exif;
        int32_t pw = t->picture_w, ph = t->picture_h;
        t->source_changed = false;
        uint8_t *given = NULL;
        size_t given_size = 0;
        if (reload) {
            given = t->given;
            given_size = t->given_size;
            t->given = NULL;
            t->given_size = 0;
        }
        ReleaseSRWLockExclusive(&t->lock);

        if (reload) {
            free(bytes);
            byte_count = given_size;
            bytes = given ? given : read_whole(path, path_len, &byte_count);
            bytes_generation = generation;
        }

        /* One band per tile row: the rows above it are decoded to reach it
           either way, so a row is read once and cut, not tile by tile. */
        bool superseded = false;
        memset(finished, 0, sizeof(finished));
        for (size_t i = 0; i < count && !superseded; ++i) {
            if (finished[i]) continue;   /* done as part of an earlier row */
            if (work[i].level < 0) {
                /* D-81: an enlarged tile. Its picture pixels, with a few
                   around them, are read at their own size and enlarged by
                   the core's resampler; the middle is the tile. */
                rubraview_tile_result_t result = { .generation = generation, .key = work[i] };
                int32_t x, y, w, h, ow, oh;
                if (bytes && factory && rubraview_tile_geometry(work[i], pw, ph, &x, &y, &w, &h, &ow, &oh)) {
                    int32_t factor = 1 << -work[i].level, m = RUBRAVIEW_ENLARGE_MARGIN;
                    int32_t sx0 = x - m < 0 ? 0 : x - m, sy0 = y - m < 0 ? 0 : y - m;
                    int32_t sx1 = x + w + m > pw ? pw : x + w + m, sy1 = y + h + m > ph ? ph : y + h + m;
                    int32_t sw = sx1 - sx0, sh = sy1 - sy0;
                    uint8_t *part = (uint8_t*)malloc((size_t)sw * (size_t)sh * 4u);
                    result.bgra = (uint8_t*)malloc((size_t)ow * (size_t)oh * 4u);
                    if (part && result.bgra &&
                        rubraview_wic_region_pbgra(factory, bytes, byte_count, exif, sx0, sy0, sw, sh, sw, sh,
                                                   part, (uint32_t)sw * 4u) &&
                        rubraview_enlarge_region(part, sw, sh, (size_t)sw * 4u, factor,
                                                 (x - sx0) * factor, (y - sy0) * factor, ow, oh,
                                                 result.bgra, (size_t)ow * 4u)) {
                        result.width = ow;
                        result.height = oh;
                    } else {
                        free(result.bgra);
                        result.bgra = NULL;
                    }
                    free(part);
                }
                post_result(t, result);
                finished[i] = true;
                AcquireSRWLockExclusive(&t->lock);
                superseded = t->stop || t->want_serial != serial || t->generation != generation;
                ReleaseSRWLockExclusive(&t->lock);
                continue;
            }
            int32_t level = work[i].level, ty = work[i].ty;
            int32_t tx0 = work[i].tx, tx1 = work[i].tx;
            for (size_t j = i; j < count; ++j) {
                if (work[j].level == level && work[j].ty == ty) {
                    if (work[j].tx < tx0) tx0 = work[j].tx;
                    if (work[j].tx > tx1) tx1 = work[j].tx;
                }
            }
            int32_t ax, ay, aw, ah, aow, aoh, bx, by, bw, bh, bow, boh;
            bool geometry =
                rubraview_tile_geometry((rubraview_tile_key_t){ level, tx0, ty }, pw, ph, &ax, &ay, &aw, &ah, &aow, &aoh) &&
                rubraview_tile_geometry((rubraview_tile_key_t){ level, tx1, ty }, pw, ph, &bx, &by, &bw, &bh, &bow, &boh);
            uint8_t *band = NULL;
            int32_t band_w = 0, band_h = aoh;
            if (geometry && bytes && factory) {
                /* Full tiles are exactly TILE_SIDE out; only the last one
                   in the row may be narrower. */
                band_w = (tx1 - tx0) * RUBRAVIEW_TILE_SIDE + bow;
                band = (uint8_t*)malloc((size_t)band_w * (size_t)band_h * 4u);
                if (band && !rubraview_wic_region_pbgra(factory, bytes, byte_count, exif,
                                                        ax, ay, bx + bw - ax, ah, band_w, band_h,
                                                        band, (uint32_t)band_w * 4u)) {
                    free(band);
                    band = NULL;
                }
            }
            for (size_t j = i; j < count; ++j) {
                if (finished[j] || work[j].level != level || work[j].ty != ty) continue;
                rubraview_tile_result_t result = { .generation = generation, .key = work[j] };
                int32_t x, y, w, h, ow, oh;
                if (band && rubraview_tile_geometry(work[j], pw, ph, &x, &y, &w, &h, &ow, &oh)) {
                    result.bgra = (uint8_t*)malloc((size_t)ow * (size_t)oh * 4u);
                    if (result.bgra) {
                        size_t left = (size_t)(work[j].tx - tx0) * RUBRAVIEW_TILE_SIDE * 4u;
                        for (int32_t r = 0; r < oh; ++r) {
                            memcpy(result.bgra + (size_t)r * (size_t)ow * 4u,
                                   band + (size_t)r * (size_t)band_w * 4u + left, (size_t)ow * 4u);
                        }
                        result.width = ow;
                        result.height = oh;
                    }
                }
                post_result(t, result);
                finished[j] = true;
            }
            free(band);

            AcquireSRWLockExclusive(&t->lock);
            superseded = t->stop || t->want_serial != serial || t->generation != generation;
            ReleaseSRWLockExclusive(&t->lock);
        }

        AcquireSRWLockExclusive(&t->lock);
        if (t->want_serial == serial && t->generation == generation) t->want_count = 0;   /* all of it done */
        ReleaseSRWLockExclusive(&t->lock);
    }

    free(bytes);
    if (factory) IWICImagingFactory_Release(factory);
    if (com) CoUninitialize();
    return 0;
}

rubraview_tiles_t *rubraview_pal_tiles_start(rubraview_tiles_wake_fn wake, void *context) {
    rubraview_tiles_t *t = (rubraview_tiles_t*)calloc(1, sizeof(*t));
    if (!t) return NULL;
    InitializeSRWLock(&t->lock);
    InitializeConditionVariable(&t->wake_worker);
    t->wake = wake;
    t->context = context;
    t->thread = CreateThread(NULL, 0, tiles_thread, t, 0, NULL);
    if (!t->thread) { free(t); return NULL; }
    /* Below the viewer: a tile must never slow a page turn. */
    SetThreadPriority(t->thread, THREAD_PRIORITY_BELOW_NORMAL);
    return t;
}

void rubraview_pal_tiles_stop(rubraview_tiles_t *t) {
    if (!t) return;
    AcquireSRWLockExclusive(&t->lock);
    t->stop = true;
    ReleaseSRWLockExclusive(&t->lock);
    WakeAllConditionVariable(&t->wake_worker);
    /* A band may be decoding; it is waited for, briefly. */
    if (WaitForSingleObject(t->thread, 3000) == WAIT_OBJECT_0) {
        CloseHandle(t->thread);
        for (size_t i = 0; i < t->result_count; ++i) free(t->results[i].bgra);
        free(t->given);
        free(t);
    }
    /* Otherwise the thread still holds `t`; it is left, not freed under it. */
}

static void set_source(rubraview_tiles_t *t, uint32_t generation, u8str_t path, uint8_t *bytes, size_t size,
                       bool apply_exif_orientation, int32_t picture_w, int32_t picture_h) {
    AcquireSRWLockExclusive(&t->lock);
    t->generation = generation;
    t->path_len = path.len < sizeof(t->path) ? path.len : 0;
    if (t->path_len) memcpy(t->path, path.ptr, t->path_len);
    free(t->given);   /* a page never taken: the thread was still on another */
    t->given = bytes;
    t->given_size = bytes ? size : 0;
    t->exif = apply_exif_orientation;
    t->picture_w = picture_w;
    t->picture_h = picture_h;
    t->source_changed = true;
    t->want_count = 0;
    t->want_serial++;
    for (size_t i = 0; i < t->result_count; ++i) free(t->results[i].bgra);
    t->result_count = 0;
    ReleaseSRWLockExclusive(&t->lock);
}

void rubraview_pal_tiles_source(rubraview_tiles_t *t, uint32_t generation, u8str_t path,
                                bool apply_exif_orientation, int32_t picture_w, int32_t picture_h) {
    if (!t) return;
    set_source(t, generation, path, NULL, 0, apply_exif_orientation, picture_w, picture_h);
}

void rubraview_pal_tiles_source_bytes(rubraview_tiles_t *t, uint32_t generation, uint8_t *bytes, size_t size,
                                      bool apply_exif_orientation, int32_t picture_w, int32_t picture_h) {
    if (!t) { free(bytes); return; }
    set_source(t, generation, (u8str_t){ .ptr = "", .len = 0 }, bytes, size,
               apply_exif_orientation, picture_w, picture_h);
}

void rubraview_pal_tiles_want(rubraview_tiles_t *t, uint32_t generation,
                              const rubraview_tile_key_t *keys, size_t count) {
    if (!t) return;
    if (count > TILES_WANT_MAX) count = TILES_WANT_MAX;
    AcquireSRWLockExclusive(&t->lock);
    bool ok = generation == t->generation;
    if (ok) {
        if (count) memcpy(t->want, keys, count * sizeof(t->want[0]));
        t->want_count = count;
        t->want_serial++;
    }
    ReleaseSRWLockExclusive(&t->lock);
    if (ok && count) WakeConditionVariable(&t->wake_worker);
}

bool rubraview_pal_tiles_take(rubraview_tiles_t *t, rubraview_tile_result_t *out) {
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

#endif /* _WIN32 */
