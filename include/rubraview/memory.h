#ifndef RUBRAVIEW_MEMORY_H
#define RUBRAVIEW_MEMORY_H

#include "rubraview/core.h"
#include "proven/allocator.h"
#include "proven/pool.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RFC-0006: the program's memory by use. Everything here is on the heap;
 * nothing is a fixed block taken once and kept.
 */

/* ---- A region: heap chunks, each an arena; it moves on to a new one when full ---- */

typedef struct rubraview_chunk rubraview_chunk_t;

typedef struct rubraview_region {
    const char        *name;
    size_t             chunk_bytes;   /* a new chunk's size, unless more is asked for at once */
    size_t             limit;         /* the most its chunks may hold together */
    size_t             held;          /* what its chunks hold together */
    size_t             used_before;   /* used in the chunks before the newest */
    size_t             high_water;    /* the most ever used */
    rubraview_chunk_t *newest;
    proven_arena_t     arena;         /* the newest chunk: what callers allocate from */
} rubraview_region_t;

/** A region with its first chunk; false when the heap refuses it. */
bool rubraview_region_init(rubraview_region_t *region, const char *name, size_t chunk_bytes, size_t limit);
/**
 * Room for `need` bytes in `region->arena`: when the newest chunk cannot
 * hold them, a new chunk (of `need` or the chunk size, the larger) becomes
 * the arena. The older chunks stay, so what was handed out stays good.
 * False past the limit or when the heap refuses.
 */
bool rubraview_region_ensure(rubraview_region_t *region, size_t need);
size_t rubraview_region_used(const rubraview_region_t *region);
/** Every chunk but the first given back, that one emptied. */
void rubraview_region_reset(rubraview_region_t *region);
/** Every chunk given back. */
void rubraview_region_release(rubraview_region_t *region);
/** The region as an allocator: alloc after ensure; free does nothing (it is an arena's). */
proven_allocator_t rubraview_region_allocator(rubraview_region_t *region);

/* ---- Two regions in turn: what is made anew is made in the idle one, which then becomes the one in use ---- */

typedef struct rubraview_halves {
    rubraview_region_t half[2];
    int                live;
} rubraview_halves_t;

bool rubraview_halves_init(rubraview_halves_t *h, const char *name, size_t chunk_bytes, size_t limit);
void rubraview_halves_release(rubraview_halves_t *h);
/** The half in use, with room for `need`; NULL past its limit. */
proven_arena_t *rubraview_halves_live(rubraview_halves_t *h, size_t need);
/** The idle half, emptied (nobody holds what was in it), with room for `need`; NULL past its limit. */
proven_arena_t *rubraview_halves_next(rubraview_halves_t *h, size_t need);
/** The idle half made the one in use; the one before is emptied at the next `next`. */
void rubraview_halves_swap(rubraview_halves_t *h);
/** Both emptied: nothing made in either is held any more. */
void rubraview_halves_reset(rubraview_halves_t *h);
size_t rubraview_halves_used(const rubraview_halves_t *h);
size_t rubraview_halves_held(const rubraview_halves_t *h);

/* ---- A pool of one type: a stack of freed items in front of its base ---- */

typedef struct rubraview_type_pool {
    proven_pool_t pool;
    size_t        item_size;
    size_t        live, peak;     /* taken and not given back; the most ever */
    size_t        reused;         /* handed out from the stack, not the base */
} rubraview_type_pool_t;

bool  rubraview_type_pool_init(rubraview_type_pool_t *pool, proven_allocator_t base, size_t item_size, size_t stack_room);
void *rubraview_type_pool_take(rubraview_type_pool_t *pool);
void  rubraview_type_pool_give(rubraview_type_pool_t *pool, void *item);
void  rubraview_type_pool_release(rubraview_type_pool_t *pool);

/* ---- Blocks of one size from the heap, each an arena while it is taken ---- */

typedef struct rubraview_block_pool {
    rubraview_type_pool_t items;
    size_t                block_bytes;
} rubraview_block_pool_t;

bool rubraview_block_pool_init(rubraview_block_pool_t *pool, size_t block_bytes, size_t stack_room);
/** A block and an empty arena over it; false when the heap refuses. */
bool rubraview_block_take(rubraview_block_pool_t *pool, proven_arena_t *out_arena, void **out_block);
void rubraview_block_give(rubraview_block_pool_t *pool, void *block);
void rubraview_block_pool_release(rubraview_block_pool_t *pool);

/* ---- The program's memory by use ---- */

#define RUBRAVIEW_SCRATCH_CLASSES 4   /* 1, 4, 16 and 64 MB; larger ones come and go */

typedef struct rubraview_mem {
    rubraview_region_t     persist;   /* what lasts for the session */
    rubraview_region_t     media;     /* what a song or a film carries; reset as another opens */
    rubraview_region_t     bgm;       /* what the background music's track carries; reset with its next */
    rubraview_region_t     recent[2]; /* the recent list in two halves, parsed into the idle one */
    int                    recent_live;
    rubraview_halves_t     settings;  /* settings.ini as written and the folders read from it */
    rubraview_halves_t     subs;      /* the subtitle of the film: read into one half, saved into the other */
    rubraview_block_pool_t windows;   /* a window and its renderer, one block each */
    rubraview_block_pool_t scratch[RUBRAVIEW_SCRATCH_CLASSES];
    size_t                 scratch_big;      /* bytes of larger scratch out now */
    size_t                 restart_at;       /* persist's use that asks for a restart */
    size_t                 heap_restart_at;  /* all held that asks for one */
    bool                   full;             /* something could not be kept */
} rubraview_mem_t;

bool rubraview_mem_init(rubraview_mem_t *mem);
void rubraview_mem_release(rubraview_mem_t *mem);

/** A read that matters for a moment: an arena of at least `bytes`. */
typedef struct rubraview_scratch {
    proven_arena_t arena;
    void          *block;
    int            cls;      /* its class, or -1 for one larger than the classes */
    size_t         bytes;
} rubraview_scratch_t;

bool rubraview_scratch_take(rubraview_mem_t *mem, size_t bytes, rubraview_scratch_t *out);
void rubraview_scratch_give(rubraview_mem_t *mem, rubraview_scratch_t *scratch);

/** The recent list's idle half, emptied, to parse into; then made the list. */
proven_arena_t *rubraview_mem_recent_next(rubraview_mem_t *mem);
void            rubraview_mem_recent_swap(rubraview_mem_t *mem);

typedef enum rubraview_mem_pressure {
    RUBRAVIEW_MEM_OK = 0,
    RUBRAVIEW_MEM_HIGH,    /* a restart is worth offering */
    RUBRAVIEW_MEM_FULL,    /* something could not be kept */
} rubraview_mem_pressure_t;

rubraview_mem_pressure_t rubraview_mem_pressure(const rubraview_mem_t *mem);

typedef struct rubraview_mem_stats {
    size_t persist_used, persist_held, persist_peak;
    size_t media_used, media_peak;
    size_t recent_used;
    size_t settings_used, subs_used;
    size_t windows_live, windows_reused;
    size_t scratch_live;
    size_t total_held;
} rubraview_mem_stats_t;

rubraview_mem_stats_t rubraview_mem_stats(const rubraview_mem_t *mem);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_MEMORY_H */
