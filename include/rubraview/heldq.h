#ifndef RUBRAVIEW_HELDQ_H
#define RUBRAVIEW_HELDQ_H

#include "rubraview/core.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RV-256 (owner, 2026-11-08: "뭔가 다른 입력을 받고 있거나 할 때 이렇게 메인 쓰레드에 작업이 오면 잠시 스택 같은데
 * 쌓이면서 보류하고 현재 작업 마무리 다음에 차근차근 처리할 수 있게 하자"; asked, "먼저 온 것부터"): requests that
 * arrive while the window asks or takes input — Explorer's, another launch's, a drop — wait here, first in first
 * out, and are taken one at a time once it is free. Each item's paths are one heap block of their own; the queue's
 * memory is the heap's, never an arena's, and its paths together are capped (RUBRAVIEW_HELDQ_BYTES).
 */
enum { RUBRAVIEW_HELDQ_SHELL = 0, RUBRAVIEW_HELDQ_DROP = 1 };
#define RUBRAVIEW_HELDQ_BYTES ((size_t)4u << 20)

typedef struct rubraview_heldq_item {
    uint32_t kind;     /* RUBRAVIEW_HELDQ_SHELL or _DROP */
    uint32_t verb;     /* a shell request's rubraview_shell_verb_t */
    bool     whole;    /* a shell request's "the set as one" */
    size_t   count;    /* paths */
    char    *text;     /* the paths, each ended by NUL, one after another */
    size_t   text_len;
} rubraview_heldq_item_t;

typedef struct rubraview_heldq {
    rubraview_heldq_item_t *items;   /* a ring */
    size_t head, count, capacity;
    size_t bytes;                    /* the paths' bytes held */
} rubraview_heldq_t;

/* A request kept at the end. False — nothing kept — with no paths, no memory, or past the cap. */
bool rubraview_heldq_push(rubraview_heldq_t *q, uint32_t kind, uint32_t verb, bool whole,
                          const proven_u8str_view_t *paths, size_t count);

/* The oldest request taken out into *out (its text the caller's, given back by rubraview_heldq_item_free). False
   when none waits. */
bool rubraview_heldq_pop(rubraview_heldq_t *q, rubraview_heldq_item_t *out);

/* An item's paths as views into its text, at most `cap`; how many. */
size_t rubraview_heldq_item_paths(const rubraview_heldq_item_t *item, proven_u8str_view_t *out, size_t cap);

void rubraview_heldq_item_free(rubraview_heldq_item_t *item);

static inline size_t rubraview_heldq_count(const rubraview_heldq_t *q) { return q ? q->count : 0; }

/* Every request dropped and the queue's memory given back. A zeroed queue is empty. */
void rubraview_heldq_free(rubraview_heldq_t *q);

#ifdef __cplusplus
}
#endif

#endif
