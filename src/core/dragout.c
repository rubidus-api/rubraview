/* RV-223 (D-278): files dragged, copied and cut out of the open-file screen — the part with no Windows in it. */
#include "rubraview/dragout.h"
#include "rubraview/archive_job.h"

uint32_t rubraview_dragout_effects(bool from_archive, bool movable, bool right_button) {
    if (!right_button) return RUBRAVIEW_DROP_COPY;   /* a left drag copies, whatever the drive */
    if (!from_archive) return RUBRAVIEW_DROP_COPY | RUBRAVIEW_DROP_MOVE | RUBRAVIEW_DROP_LINK;
    return RUBRAVIEW_DROP_COPY | (movable ? RUBRAVIEW_DROP_MOVE : 0u);
}

static bool ends_with_ci(proven_u8str_view_t s, const char *tail) {
    proven_u8str_view_t t = proven_u8str_view_from_cstr(tail);
    return s.size > t.size && proven_u8str_view_eq_ascii_ci((proven_u8str_view_t){ .ptr = s.ptr + s.size - t.size, .size = t.size }, t);
}

bool rubraview_dragout_archive_movable(proven_u8str_view_t archive_path, bool any_encrypted) {
    if (any_encrypted) return false;
    return ends_with_ci(archive_path, ".zip") || ends_with_ci(archive_path, ".cbz") || ends_with_ci(archive_path, ".7z") ||
           ends_with_ci(archive_path, ".cb7");
}

size_t rubraview_dragout_path(proven_u8str_view_t dest, proven_u8str_view_t name, char *out, size_t cap) {
    while (dest.size > 0 && (dest.ptr[dest.size - 1] == '\\' || dest.ptr[dest.size - 1] == '/')) dest.size--;
    if (!out || cap == 0 || dest.size == 0 || name.size == 0) return 0;
    char part[1024];
    size_t n = rubraview_archive_safe_part(name, part, sizeof part);
    if (n == 0 || dest.size + 1 + n + 1 > cap) return 0;
    proven_sys_mem_copy(out, dest.ptr, dest.size);
    out[dest.size] = '\\';
    proven_sys_mem_copy(out + dest.size + 1, part, n);
    out[dest.size + 1 + n] = '\0';
    return dest.size + 1 + n;
}

uint32_t rubraview_dragout_performed(uint32_t returned, uint32_t said) {
    uint32_t mask = RUBRAVIEW_DROP_COPY | RUBRAVIEW_DROP_MOVE | RUBRAVIEW_DROP_LINK;
    return (said & mask) ? (said & mask) : (returned & mask);
}
