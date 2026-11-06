#ifndef RUBRAVIEW_BASKET_H
#define RUBRAVIEW_BASKET_H

#include "rubraview/core.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RV-227 (owner, 2026-11-06: "여러개 중 하나의 장바구니에 담는 기능"): five baskets of paths picked in the open-file
 * screen, later copied or moved together into the folder on screen. Only the paths are kept, for this run (the
 * owner's answer); the files stay where they are until a move. A copy keeps a basket; a move takes out what moved.
 */
enum { RUBRAVIEW_BASKETS = 5 };

typedef struct rubraview_basket {
    char **paths;     /* UTF-8, each its own block (rubraview_strdup) */
    size_t count, capacity;
} rubraview_basket_t;

typedef struct rubraview_baskets {
    rubraview_basket_t basket[RUBRAVIEW_BASKETS];
} rubraview_baskets_t;

/* Every basket emptied and its memory given back. A zeroed rubraview_baskets_t is five empty baskets. */
void rubraview_baskets_free(rubraview_baskets_t *b);

/* Adds `paths` to basket `which`; a path it holds already (as rubraview_path_same compares) or an empty one is not
   added again. Returns how many were added. */
size_t rubraview_basket_add(rubraview_baskets_t *b, int which, const proven_u8str_view_t *paths, size_t n);

size_t rubraview_basket_count(const rubraview_baskets_t *b, int which);
/* The path at `index`, empty past the end. */
proven_u8str_view_t rubraview_basket_path(const rubraview_baskets_t *b, int which, size_t index);

/* Takes the path at `index` out; false past the end. */
bool rubraview_basket_remove(rubraview_baskets_t *b, int which, size_t index);
void rubraview_basket_empty(rubraview_baskets_t *b, int which);

/* After a move into a folder: the paths whose `moved[i]` is true (i over the basket as it was) are taken out, the
   rest kept in their order. Returns how many are left. */
size_t rubraview_basket_keep_unmoved(rubraview_baskets_t *b, int which, const bool *moved);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_BASKET_H */
