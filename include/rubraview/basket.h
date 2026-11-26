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
    char **paths;     /* UTF-8, each its own block (rubraview_strdup); for an archive's entry, the archive */
    char **entries;   /* RV-320: NULL for a file on disk; an archive's entry, its name inside ("root1/sub1/one.txt") */
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

/* RV-320 (D-402): an archive's entries (`names` inside `archive`, as the archive names them, no '/' at the ends) added to
   basket `which`; one it holds already (the same archive as rubraview_path_same compares, the same name) or an empty
   one is not added again. Returns how many were added. */
size_t rubraview_basket_add_entries(rubraview_baskets_t *b, int which, proven_u8str_view_t archive, const proven_u8str_view_t *names, size_t n);

/* The entry's name inside its archive at `index`; empty for a file on disk (rubraview_basket_path is its path, and the
   archive's for an entry). */
proven_u8str_view_t rubraview_basket_entry(const rubraview_baskets_t *b, int which, size_t index);

/* The folder an entry sits in, with its '/': "root1/sub1/" of "root1/sub1/one.txt"; empty at the archive's top. */
proven_u8str_view_t rubraview_basket_entry_folder(proven_u8str_view_t entry);

/* The basket's archive entries grouped by archive and the folder they sit in: each group one extraction straight into a
   folder, its `inner` left off their names. Archives in the order they first appear, folders within one so too;
   `members[first .. first + count)` are the basket's indices of a group's entries. Files on disk are in none. At most
   `cap` groups; `members` has room for the basket's count. Returns the groups written. */
typedef struct rubraview_basket_group {
    proven_u8str_view_t archive, inner;
    size_t first, count;
} rubraview_basket_group_t;
size_t rubraview_basket_groups(const rubraview_baskets_t *b, int which, rubraview_basket_group_t *groups, size_t cap, size_t *members);

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
