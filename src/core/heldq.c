/* RV-256: requests held while the window asks or takes input, first in first out (D-308). */
#include "rubraview/heldq.h"

bool rubraview_heldq_push(rubraview_heldq_t *q, uint32_t kind, uint32_t verb, bool whole,
                          const proven_u8str_view_t *paths, size_t count) {
    if (!q || !paths || count == 0) return false;
    size_t len = 0;
    for (size_t i = 0; i < count; ++i) {
        if (paths[i].size > RUBRAVIEW_HELDQ_BYTES) return false;
        len += paths[i].size + 1;
    }
    if (len > RUBRAVIEW_HELDQ_BYTES || q->bytes + len > RUBRAVIEW_HELDQ_BYTES) return false;
    if (q->count == q->capacity) {   /* the ring grown, its items laid out from the head */
        size_t cap = q->capacity ? q->capacity * 2 : 8;
        rubraview_heldq_item_t *grown = (rubraview_heldq_item_t *)rubraview_alloc_zero(cap, sizeof *grown);
        if (!grown) return false;
        for (size_t i = 0; i < q->count; ++i) grown[i] = q->items[(q->head + i) % q->capacity];
        rubraview_free(q->items);
        q->items = grown;
        q->capacity = cap;
        q->head = 0;
    }
    char *text = (char *)rubraview_alloc(len ? len : 1);
    if (!text) return false;
    size_t at = 0;
    for (size_t i = 0; i < count; ++i) {
        if (paths[i].size) proven_sys_mem_copy(text + at, paths[i].ptr, paths[i].size);
        at += paths[i].size;
        text[at++] = '\0';
    }
    q->items[(q->head + q->count) % q->capacity] = (rubraview_heldq_item_t){ .kind = kind, .verb = verb, .whole = whole,
                                                                            .count = count, .text = text, .text_len = len };
    q->count++;
    q->bytes += len;
    return true;
}

bool rubraview_heldq_pop(rubraview_heldq_t *q, rubraview_heldq_item_t *out) {
    if (!q || q->count == 0 || !out) return false;
    *out = q->items[q->head];
    q->items[q->head] = (rubraview_heldq_item_t){ 0 };
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    q->bytes -= out->text_len;
    return true;
}

size_t rubraview_heldq_item_paths(const rubraview_heldq_item_t *item, proven_u8str_view_t *out, size_t cap) {
    if (!item || !item->text || !out) return 0;
    size_t n = 0, at = 0;
    while (n < item->count && n < cap && at < item->text_len) {
        size_t len = proven_cstr_len(item->text + at);
        out[n++] = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)item->text + at, .size = len };
        at += len + 1;
    }
    return n;
}

void rubraview_heldq_item_free(rubraview_heldq_item_t *item) {
    if (!item) return;
    rubraview_free(item->text);
    *item = (rubraview_heldq_item_t){ 0 };
}

void rubraview_heldq_free(rubraview_heldq_t *q) {
    if (!q) return;
    rubraview_heldq_item_t it;
    while (rubraview_heldq_pop(q, &it)) rubraview_heldq_item_free(&it);
    rubraview_free(q->items);
    *q = (rubraview_heldq_t){ 0 };
}
