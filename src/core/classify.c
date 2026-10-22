#include "rubraview/classify.h"
#include <string.h>

static size_t find(const rubraview_classify_t *c, proven_u8str_view_t path) {
    for (size_t i = 0; i < c->count; ++i) if (proven_u8str_view_eq(c->paths[i], path)) return i;
    return SIZE_MAX;
}

int32_t rubraview_classify_folder_of(const rubraview_classify_t *c, proven_u8str_view_t path) {
    if (!c || path.size == 0) return 0;
    size_t i = find(c, path);
    return i == SIZE_MAX ? 0 : c->folders[i];
}

bool rubraview_classify_set(rubraview_classify_t *c, proven_u8str_view_t path, int32_t folder) {
    if (!c || path.size == 0 || folder < 0 || folder > 9) return false;
    size_t i = find(c, path);
    if (folder == 0) {
        if (i != SIZE_MAX) rubraview_classify_remove_at(c, i);
        return true;
    }
    if (i != SIZE_MAX) { c->folders[i] = (uint8_t)folder; return true; }
    if (c->count >= RUBRAVIEW_CLASSIFY_MAX) return false;
    c->paths[c->count] = path;
    c->folders[c->count] = (uint8_t)folder;
    c->count++;
    return true;
}

size_t rubraview_classify_count(const rubraview_classify_t *c, int32_t folder) {
    if (!c) return 0;
    if (folder == 0) return c->count;
    size_t n = 0;
    for (size_t i = 0; i < c->count; ++i) n += c->folders[i] == (uint8_t)folder;
    return n;
}

void rubraview_classify_remove_at(rubraview_classify_t *c, size_t index) {
    if (!c || index >= c->count) return;
    memmove(c->paths + index, c->paths + index + 1, (c->count - index - 1) * sizeof(c->paths[0]));
    memmove(c->folders + index, c->folders + index + 1, (c->count - index - 1) * sizeof(c->folders[0]));
    c->count--;
}

void rubraview_classify_clear(rubraview_classify_t *c) {
    if (c) c->count = 0;
}
