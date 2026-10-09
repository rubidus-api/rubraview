#include "rubraview/layout.h"
#include <string.h>

rubraview_layout_opts_t rubraview_layout_opts_default(rubraview_page_layout_t mode, rubraview_reading_dir_t direction) {
    return (rubraview_layout_opts_t){
        .mode = mode,
        .direction = direction,
        .spread_ar_threshold = 1.15,
        .portrait_collapse_ar = 1.0,
        .auto_split_wide_spreads = (mode == RUBRAVIEW_PAGE_LAYOUT_COMIC),
    };
}

rubraview_layout_opts_t rubraview_layout_opts_turned(rubraview_layout_opts_t opts, bool swaps_axes) {
    if (swaps_axes) opts.auto_split_wide_spreads = false;
    return opts;
}

static bool is_wide_spread(const rubraview_page_info_t *p, double threshold) {
    if (p->height <= 0.0) return false;
    return (p->width / p->height) >= threshold;
}

/* A page stands alone either because it is a pre-merged spread or
   because the archive's metadata tagged it as a cover (§3.8.5). */
static bool stands_alone(const rubraview_page_info_t *p, double threshold) {
    return p->force_standalone || is_wide_spread(p, threshold);
}

typedef struct spread_buf {
    rubraview_spread_t *data;
    size_t count;
    size_t capacity;
} spread_buf_t;

static bool spread_buf_reserve(proven_arena_t *arena, spread_buf_t *b, size_t min_capacity) {
    if (b->capacity >= min_capacity) return true;
    size_t new_cap = b->capacity == 0 ? 8 : b->capacity * 2;
    if (new_cap < min_capacity) new_cap = min_capacity;

    proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, new_cap, sizeof(rubraview_spread_t));
    if (!proven_is_ok(res.err)) return false;

    rubraview_spread_t *new_data = (rubraview_spread_t*)(void*)res.value.ptr;
    if (b->data && b->count > 0) memcpy(new_data, b->data, b->count * sizeof(rubraview_spread_t));
    b->data = new_data;
    b->capacity = new_cap;
    return true;
}

static bool spread_buf_push(proven_arena_t *arena, spread_buf_t *b, rubraview_spread_t s) {
    if (!spread_buf_reserve(arena, b, b->count + 1)) return false;
    b->data[b->count++] = s;
    return true;
}

static void push_single(proven_arena_t *arena, spread_buf_t *out, int32_t index, bool premerged) {
    spread_buf_push(arena, out, (rubraview_spread_t){
        .left_index = index, .right_index = -1,
        .left_half = RUBRAVIEW_SPREAD_WHOLE, .is_premerged_spread = premerged,
    });
}

static void push_split(proven_arena_t *arena, spread_buf_t *out, int32_t index, rubraview_reading_dir_t dir) {
    rubraview_spread_half_t first_half = (dir == RUBRAVIEW_READING_LTR) ? RUBRAVIEW_SPREAD_LEFT_HALF : RUBRAVIEW_SPREAD_RIGHT_HALF;
    rubraview_spread_half_t second_half = (dir == RUBRAVIEW_READING_LTR) ? RUBRAVIEW_SPREAD_RIGHT_HALF : RUBRAVIEW_SPREAD_LEFT_HALF;

    spread_buf_push(arena, out, (rubraview_spread_t){
        .left_index = index, .right_index = -1, .left_half = first_half, .is_premerged_spread = false,
    });
    spread_buf_push(arena, out, (rubraview_spread_t){
        .left_index = index, .right_index = -1, .left_half = second_half, .is_premerged_spread = false,
    });
}

static void push_pair(proven_arena_t *arena, spread_buf_t *out, int32_t a, int32_t b, rubraview_reading_dir_t dir) {
    /* `a` precedes `b` in page-number order; LTR places the lower page
       number on the left, RTL places it on the right. */
    int32_t left = (dir == RUBRAVIEW_READING_LTR) ? a : b;
    int32_t right = (dir == RUBRAVIEW_READING_LTR) ? b : a;
    spread_buf_push(arena, out, (rubraview_spread_t){
        .left_index = left, .right_index = right, .left_half = RUBRAVIEW_SPREAD_WHOLE, .is_premerged_spread = false,
    });
}

/* Walk pages [start, page_count) pairing consecutive non-wide pages, and
   isolating (or splitting) wide pre-merged spreads. Used for DUAL (start=0)
   and BOOK (start=1, after the cover has already been emitted alone). */
static bool paginate_pairs(proven_arena_t *arena, spread_buf_t *out, const rubraview_page_info_t *pages, size_t page_count, size_t start, const rubraview_layout_opts_t *opts) {
    size_t i = start;
    while (i < page_count) {
        if (stands_alone(&pages[i], opts->spread_ar_threshold)) {
            bool wide = is_wide_spread(&pages[i], opts->spread_ar_threshold);
            /* Splitting bisects a genuinely wide scan; a tagged cover is
               one page and is shown whole. */
            if (opts->auto_split_wide_spreads && wide) {
                push_split(arena, out, (int32_t)i, opts->direction);
            } else {
                push_single(arena, out, (int32_t)i, wide);
            }
            i += 1;
            continue;
        }

        bool next_is_pairable = (i + 1 < page_count) && !stands_alone(&pages[i + 1], opts->spread_ar_threshold);
        if (next_is_pairable) {
            push_pair(arena, out, (int32_t)i, (int32_t)(i + 1), opts->direction);
            i += 2;
        } else {
            push_single(arena, out, (int32_t)i, false);
            i += 1;
        }
    }
    return true;
}

rubraview_layout_result_t rubraview_layout_compute(proven_arena_t *arena, const rubraview_page_info_t *pages, size_t page_count, double win_w, double win_h, rubraview_layout_opts_t opts) {
    rubraview_layout_result_t result = {0};
    if (!arena || !pages || page_count == 0) return result;

    /* §3.3.5 portrait auto-collapse: a narrow-tall window forces effective
       SINGLE, since side-by-side pages would be unreadably small. Webtoon
       is already a single continuous column and is unaffected. */
    rubraview_page_layout_t effective_mode = opts.mode;
    if (effective_mode != RUBRAVIEW_PAGE_LAYOUT_WEBTOON && win_w > 0.0 && win_h > 0.0) {
        if ((win_w / win_h) < opts.portrait_collapse_ar) {
            if (effective_mode != RUBRAVIEW_PAGE_LAYOUT_COMIC) effective_mode = RUBRAVIEW_PAGE_LAYOUT_SINGLE;
        }
    }

    spread_buf_t buf = {0};

    switch (effective_mode) {
        case RUBRAVIEW_PAGE_LAYOUT_WEBTOON:
            for (size_t i = 0; i < page_count; ++i) {
                push_single(arena, &buf, (int32_t)i, false);
            }
            break;

        case RUBRAVIEW_PAGE_LAYOUT_SINGLE:
        case RUBRAVIEW_PAGE_LAYOUT_COMIC:   /* the caller's options carry the splitting */
            for (size_t i = 0; i < page_count; ++i) {
                if (opts.auto_split_wide_spreads && is_wide_spread(&pages[i], opts.spread_ar_threshold)) {
                    push_split(arena, &buf, (int32_t)i, opts.direction);
                } else {
                    push_single(arena, &buf, (int32_t)i, is_wide_spread(&pages[i], opts.spread_ar_threshold));
                }
            }
            break;

        case RUBRAVIEW_PAGE_LAYOUT_DUAL:
            paginate_pairs(arena, &buf, pages, page_count, 0, &opts);
            break;

        case RUBRAVIEW_PAGE_LAYOUT_BOOK:
            /* Cover Page 1 Exception: page 0 always stands alone. */
            push_single(arena, &buf, 0, is_wide_spread(&pages[0], opts.spread_ar_threshold));
            paginate_pairs(arena, &buf, pages, page_count, 1, &opts);
            break;

        default:
            break;
    }

    result.spreads = buf.data;
    result.count = buf.count;
    return result;
}

/* ---- the strip ---- */

static double strip_height(rubraview_strip_height_fn height, void *context, size_t page) {
    double h = height(context, page);
    return h >= 1.0 ? h : 1.0;
}

/* Moves up by `amount` (>= 0); returns what could not be moved. */
static double strip_up(rubraview_strip_pos_t *pos, double amount, rubraview_strip_height_fn height, void *context) {
    pos->offset -= amount;
    while (pos->offset < 0.0 && pos->top > 0) {
        pos->top -= 1;
        pos->offset += strip_height(height, context, pos->top);
    }
    if (pos->offset < 0.0) {
        double left = -pos->offset;
        pos->offset = 0.0;
        return left;
    }
    return 0.0;
}

double rubraview_strip_scroll(rubraview_strip_pos_t *pos, double delta, size_t count, double view_height,
                              rubraview_strip_height_fn height, void *context) {
    if (!pos || !height || count == 0) return delta;
    if (pos->top >= count) { pos->top = count - 1; pos->offset = 0.0; }
    if (pos->offset < 0.0) pos->offset = 0.0;

    double left = 0.0;
    if (delta < 0.0) {
        left = -strip_up(pos, -delta, height, context);
    } else {
        pos->offset += delta;
    }
    /* Past the top page's bottom: the next page is the top one. */
    for (;;) {
        double h = strip_height(height, context, pos->top);
        if (pos->offset < h) break;
        if (pos->top + 1 >= count) {
            if (delta > 0.0) left += pos->offset - h;
            pos->offset = h;
            break;
        }
        pos->offset -= h;
        pos->top += 1;
    }
    /* What is left below the window's top edge must fill the window. */
    double below = strip_height(height, context, pos->top) - pos->offset;
    for (size_t i = pos->top + 1; i < count && below < view_height; ++i) below += strip_height(height, context, i);
    if (below < view_height) {
        double back = view_height - below;
        double stuck = strip_up(pos, back, height, context);
        if (delta > 0.0) {
            left += back - stuck;
            if (left > delta) left = delta;
        }
    }
    return left;
}
