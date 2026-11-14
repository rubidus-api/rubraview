#include "rubraview/core.h"
#include "rubraview/layout.h"
#include <string.h>

static const struct { rubraview_page_layout_t mode; rubraview_reading_dir_t direction; } CHOICES[RUBRAVIEW_LAYOUT_CHOICE_COUNT] = {
    [RUBRAVIEW_LAYOUT_CHOICE_SINGLE]    = { RUBRAVIEW_PAGE_LAYOUT_SINGLE,  RUBRAVIEW_READING_LTR },
    [RUBRAVIEW_LAYOUT_CHOICE_DUAL]      = { RUBRAVIEW_PAGE_LAYOUT_DUAL,    RUBRAVIEW_READING_LTR },
    [RUBRAVIEW_LAYOUT_CHOICE_DUAL_RTL]  = { RUBRAVIEW_PAGE_LAYOUT_DUAL,    RUBRAVIEW_READING_RTL },
    [RUBRAVIEW_LAYOUT_CHOICE_WEBTOON]   = { RUBRAVIEW_PAGE_LAYOUT_WEBTOON, RUBRAVIEW_READING_LTR },
    [RUBRAVIEW_LAYOUT_CHOICE_COMIC]     = { RUBRAVIEW_PAGE_LAYOUT_COMIC,   RUBRAVIEW_READING_LTR },
    [RUBRAVIEW_LAYOUT_CHOICE_COMIC_RTL] = { RUBRAVIEW_PAGE_LAYOUT_COMIC,   RUBRAVIEW_READING_RTL },
};

rubraview_page_layout_t rubraview_layout_choice_mode(int choice) {
    return choice >= 0 && choice < (int)RUBRAVIEW_LAYOUT_CHOICE_COUNT ? CHOICES[choice].mode : RUBRAVIEW_PAGE_LAYOUT_SINGLE;
}

rubraview_reading_dir_t rubraview_layout_choice_direction(int choice) {
    return choice >= 0 && choice < (int)RUBRAVIEW_LAYOUT_CHOICE_COUNT ? CHOICES[choice].direction : RUBRAVIEW_READING_LTR;
}

rubraview_layout_choice_t rubraview_layout_choice_of(rubraview_page_layout_t mode, rubraview_reading_dir_t direction) {
    for (int i = 0; i < (int)RUBRAVIEW_LAYOUT_CHOICE_COUNT; ++i) {
        if (CHOICES[i].mode == mode && CHOICES[i].direction == direction) return (rubraview_layout_choice_t)i;
    }
    for (int i = 0; i < (int)RUBRAVIEW_LAYOUT_CHOICE_COUNT; ++i) {
        if (CHOICES[i].mode == mode) return (rubraview_layout_choice_t)i;
    }
    return RUBRAVIEW_LAYOUT_CHOICE_SINGLE;
}

rubraview_layout_opts_t rubraview_layout_opts_default(rubraview_page_layout_t mode, rubraview_reading_dir_t direction) {
    return (rubraview_layout_opts_t){
        .mode = mode,
        .direction = direction,
        .spread_ar_threshold = 1.15,
        .portrait_collapse_ar = 1.0,
        .auto_split_wide_spreads = (mode == RUBRAVIEW_PAGE_LAYOUT_COMIC),
        .anchor = -1,
        .first_alone = false,
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
    if (b->data && b->count > 0) proven_sys_mem_copy(new_data, b->data, b->count * sizeof(rubraview_spread_t));
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

/* A page shown by itself: a wide scan whole or in its halves, any other page whole. */
static void push_alone(proven_arena_t *arena, spread_buf_t *out, const rubraview_page_info_t *pages, size_t i, const rubraview_layout_opts_t *opts) {
    bool wide = is_wide_spread(&pages[i], opts->spread_ar_threshold);
    /* Splitting bisects a genuinely wide scan; a tagged cover is
       one page and is shown whole. */
    if (opts->auto_split_wide_spreads && wide) {
        push_split(arena, out, (int32_t)i, opts->direction);
    } else {
        push_single(arena, out, (int32_t)i, wide);
    }
}

/* Two pages (RV-224, D-279): what never pairs — a wide scan, the first page when the reader chose it alone
   (Settings, the book); RV-283: an archive's metadata no longer makes a cover stand alone. */
static bool pairs_not(const rubraview_page_info_t *pages, size_t i, const rubraview_layout_opts_t *opts) {
    return stands_alone(&pages[i], opts->spread_ar_threshold) || (opts->first_alone && i == 0);
}

/* Two pages paired from the anchor (RV-224, D-279): forward from it as a reader turns them, so the page chosen is
   the first one shown. RV-283 (owner, 2026-11-14: "첫 페이지가 무조건 1장씩 보이는게 조금 그렇네요. 2장씩 보기는 첫 페이지부터
   2장씩 보이는 것으로 해 주세요." "물론 가로로 넓으면 1장씩 보이는 것도 마찬가지로"): the pages before it pair from the
   first page forward (they were paired back from the anchor, which left the first page alone whenever the count was
   odd); one left over stands alone just before the anchor. The last page chosen (reached going back from the next
   book, End) pairs the whole from the first page, so it is not left alone at the end. */
static void paginate_pairs(proven_arena_t *arena, spread_buf_t *out, const rubraview_page_info_t *pages, size_t page_count, size_t anchor, const rubraview_layout_opts_t *opts) {
    if (anchor + 1 >= page_count) anchor = 0;
    for (size_t j = 0; j < anchor; ) {
        if (pairs_not(pages, j, opts)) { push_alone(arena, out, pages, j, opts); j += 1; continue; }
        if (j + 1 < anchor && !pairs_not(pages, j + 1, opts)) {
            push_pair(arena, out, (int32_t)j, (int32_t)(j + 1), opts->direction);
            j += 2;
        } else {
            push_alone(arena, out, pages, j, opts);
            j += 1;
        }
    }

    size_t i = anchor;
    while (i < page_count) {
        if (pairs_not(pages, i, opts)) {
            push_alone(arena, out, pages, i, opts);
            i += 1;
            continue;
        }

        bool next_is_pairable = (i + 1 < page_count) && !pairs_not(pages, i + 1, opts);
        if (next_is_pairable) {
            push_pair(arena, out, (int32_t)i, (int32_t)(i + 1), opts->direction);
            i += 2;
        } else {
            push_single(arena, out, (int32_t)i, false);
            i += 1;
        }
    }
}

static rubraview_layout_result_t layout_compute_all(proven_arena_t *arena, const rubraview_page_info_t *pages, size_t page_count, double win_w, double win_h, rubraview_layout_opts_t opts);

rubraview_layout_result_t rubraview_layout_compute(proven_arena_t *arena, const rubraview_page_info_t *pages, size_t page_count, double win_w, double win_h, rubraview_layout_opts_t opts) {
    rubraview_layout_result_t none = {0};
    if (!arena || !pages || page_count == 0) return none;

    /* D-98: the pages that are no still pictures are left out of every
       layout but SINGLE (the layout chosen, not the one a narrow window
       falls back to). The strip keeps a place for every page — it is
       measured page by page — and its caller gives those no height. */
    size_t kept = 0;
    for (size_t i = 0; i < page_count; ++i) if (!pages[i].skip) kept++;
    if (opts.mode == RUBRAVIEW_PAGE_LAYOUT_SINGLE || opts.mode == RUBRAVIEW_PAGE_LAYOUT_WEBTOON ||
        kept == page_count || kept == 0) {
        return layout_compute_all(arena, pages, page_count, win_w, win_h, opts);
    }
    proven_result_mem_mut_t pr = rubraview_arena_alloc_array(arena, kept, sizeof(rubraview_page_info_t));
    proven_result_mem_mut_t ir = rubraview_arena_alloc_array(arena, kept, sizeof(int32_t));
    if (!proven_is_ok(pr.err) || !proven_is_ok(ir.err)) return layout_compute_all(arena, pages, page_count, win_w, win_h, opts);
    rubraview_page_info_t *some = (rubraview_page_info_t*)(void*)pr.value.ptr;
    int32_t *index_of = (int32_t*)(void*)ir.value.ptr;
    int32_t anchor = -1;   /* the first page kept at or after the anchor, counted among those kept */
    for (size_t i = 0, k = 0; i < page_count; ++i) {
        if (pages[i].skip) continue;
        if (anchor < 0 && opts.anchor >= 0 && (int32_t)i >= opts.anchor) anchor = (int32_t)k;
        some[k] = pages[i];
        index_of[k++] = (int32_t)i;
    }
    if (anchor < 0 && opts.anchor >= 0) anchor = (int32_t)kept - 1;   /* past the last kept: the last */
    opts.anchor = anchor;
    rubraview_layout_result_t result = layout_compute_all(arena, some, kept, win_w, win_h, opts);
    for (size_t i = 0; i < result.count; ++i) {
        rubraview_spread_t *sp = &result.spreads[i];
        if (sp->left_index >= 0 && (size_t)sp->left_index < kept) sp->left_index = index_of[sp->left_index];
        if (sp->right_index >= 0 && (size_t)sp->right_index < kept) sp->right_index = index_of[sp->right_index];
    }
    return result;
}

static rubraview_layout_result_t layout_compute_all(proven_arena_t *arena, const rubraview_page_info_t *pages, size_t page_count, double win_w, double win_h, rubraview_layout_opts_t opts) {
    rubraview_layout_result_t result = {0};

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

        case RUBRAVIEW_PAGE_LAYOUT_DUAL: {
            size_t anchor = opts.anchor > 0 ? (size_t)opts.anchor : 0;
            if (anchor >= page_count) anchor = page_count - 1;
            paginate_pairs(arena, &buf, pages, page_count, anchor, &opts);
            break;
        }

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
