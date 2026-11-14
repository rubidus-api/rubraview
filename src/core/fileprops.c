/* RV-281: the properties window's model (fileprops.h). */
#include "rubraview/fileprops.h"

rubraview_tristate_t rubraview_fileprops_attr_state(const rubraview_fileprops_file_t *files, size_t count, uint32_t attr) {
    size_t on = 0;
    for (size_t i = 0; files && i < count; ++i) if (files[i].attributes & attr) on++;
    if (on == 0) return RUBRAVIEW_TRI_OFF;
    return on == count ? RUBRAVIEW_TRI_ON : RUBRAVIEW_TRI_MIXED;
}

rubraview_tristate_t rubraview_fileprops_attr_click(rubraview_tristate_t now) {
    return now == RUBRAVIEW_TRI_ON ? RUBRAVIEW_TRI_OFF : RUBRAVIEW_TRI_ON;
}

uint32_t rubraview_fileprops_attr_apply(uint32_t have, const rubraview_tristate_t wanted[6], uint32_t last_on) {
    uint32_t out = have & RUBRAVIEW_FATTR_ALL;
    for (uint32_t bit = 0; bit < 6; ++bit) {
        uint32_t a = 1u << bit;
        if (wanted[bit] == RUBRAVIEW_TRI_ON) out |= a;
        else if (wanted[bit] == RUBRAVIEW_TRI_OFF) out &= ~a;
    }
    const uint32_t both = RUBRAVIEW_FATTR_COMPRESSED | RUBRAVIEW_FATTR_ENCRYPTED;
    if ((out & both) == both)   /* the one turned on last stays; neither turned on: what the file had */
        out &= last_on == RUBRAVIEW_FATTR_COMPRESSED ? ~RUBRAVIEW_FATTR_ENCRYPTED
             : last_on == RUBRAVIEW_FATTR_ENCRYPTED ? ~RUBRAVIEW_FATTR_COMPRESSED
             : (have & RUBRAVIEW_FATTR_ENCRYPTED) ? ~RUBRAVIEW_FATTR_COMPRESSED : ~RUBRAVIEW_FATTR_ENCRYPTED;
    return out;
}

void rubraview_fileprops_sum(const rubraview_fileprops_file_t *files, size_t count, rubraview_fileprops_file_t *out) {
    if (!out) return;
    *out = (rubraview_fileprops_file_t){0};
    if (!files || count == 0) return;
    *out = files[0];
    if (count == 1) return;
    out->size = out->on_disk = 0;
    out->files = out->folders = 0;
    out->counted_all = true;
    for (size_t i = 0; i < count; ++i) {
        out->size += files[i].size;
        out->on_disk += files[i].on_disk;
        if (files[i].is_folder) { out->folders += 1 + files[i].folders; out->files += files[i].files; }
        else out->files += 1;
        out->counted_all = out->counted_all && (!files[i].is_folder || files[i].counted_all);
        out->blocked = out->blocked || files[i].blocked;
        out->can_compress = out->can_compress && files[i].can_compress;
        out->can_encrypt = out->can_encrypt && files[i].can_encrypt;
    }
    out->is_folder = false;
    out->created = out->modified = out->accessed = 0;   /* several: no one date */
}

/* ---- the Details lines ---- */

static void unescape_into(proven_u8str_view_t field, char *out, size_t cap) {
    size_t w = 0;
    for (size_t i = 0; i < field.size && w + 1 < cap; ++i) {
        char c = (char)field.ptr[i];
        if (c == '\\' && i + 1 < field.size) {
            char n = (char)field.ptr[++i];
            c = n == 't' ? '\t' : n == 'n' ? '\n' : n;
        }
        out[w++] = c;
    }
    out[w] = '\0';
}

size_t rubraview_props_rows_read(proven_u8str_view_t text, rubraview_props_row_t *rows, size_t cap) {
    size_t n = 0, start = 0;
    for (size_t i = 0; i <= text.size && n < cap; ++i) {
        if (i < text.size && text.ptr[i] != '\n') continue;
        proven_u8str_view_t line = proven_u8str_view_slice(text, start, i - start);
        start = i + 1;
        if (line.size > 0 && line.ptr[line.size - 1] == '\r') line.size--;
        proven_u8str_view_t f[5] = {0};
        size_t count = 0, from = 0;
        for (size_t k = 0; k <= line.size && count < 5; ++k) {
            if (k < line.size && (line.ptr[k] != '\t' || count == 4)) continue;
            f[count++] = proven_u8str_view_slice(line, from, k - from);
            from = k + 1;
        }
        if (count == 2 && f[0].size == 1 && f[0].ptr[0] == 'H') {
            rows[n] = (rubraview_props_row_t){ .flags = RUBRAVIEW_PROPS_ROW_HEADING };
            unescape_into(f[1], rows[n].label, sizeof rows[n].label);
            n++;
        } else if (count == 5 && f[0].size == 1 && f[0].ptr[0] == 'P') {
            rows[n] = (rubraview_props_row_t){0};
            unescape_into(f[1], rows[n].name, sizeof rows[n].name);
            unescape_into(f[2], rows[n].label, sizeof rows[n].label);
            for (size_t k = 0; k < f[3].size; ++k) {
                char c = (char)f[3].ptr[k];
                if (c == 'e') rows[n].flags |= RUBRAVIEW_PROPS_ROW_EDITABLE;
                if (c == 'm') rows[n].flags |= RUBRAVIEW_PROPS_ROW_MULTI;
                if (c == 'r') rows[n].flags |= RUBRAVIEW_PROPS_ROW_RATING;
            }
            unescape_into(f[4], rows[n].value, sizeof rows[n].value);
            n++;
        }
        if (i == text.size) break;
    }
    return n;
}

bool rubraview_props_escape(proven_u8str_view_t text, char *out, size_t cap) {
    size_t w = 0;
    if (!out || cap == 0) return false;
    for (size_t i = 0; i < text.size; ++i) {
        char c = (char)text.ptr[i];
        const char *rep = c == '\t' ? "\\t" : c == '\n' ? "\\n" : c == '\\' ? "\\\\" : c == '\r' ? "" : NULL;
        size_t len = rep ? proven_cstr_len(rep) : 1;
        if (w + len + 1 > cap) { out[w] = '\0'; return false; }
        if (rep) { proven_sys_mem_copy(out + w, rep, len); w += len; }
        else out[w++] = c;
    }
    out[w] = '\0';
    return true;
}

bool rubraview_props_change_line(const char *name, const char *value, char *out, size_t cap) {
    char n_esc[256], v_esc[1100];
    if (!rubraview_props_escape(proven_u8str_view_from_cstr(name ? name : ""), n_esc, sizeof n_esc) ||
        !rubraview_props_escape(proven_u8str_view_from_cstr(value ? value : ""), v_esc, sizeof v_esc)) return false;
    int w = rubraview_fmt(out, cap, "{}\t{}\n", PROVEN_ARG(n_esc), PROVEN_ARG(v_esc));
    return w >= 0 && (size_t)w < cap;
}

size_t rubraview_props_changes_read(proven_u8str_view_t text, rubraview_props_row_t *rows, size_t cap) {
    size_t n = 0, start = 0;
    for (size_t i = 0; i <= text.size && n < cap; ++i) {
        if (i < text.size && text.ptr[i] != '\n') continue;
        proven_u8str_view_t line = proven_u8str_view_slice(text, start, i - start);
        start = i + 1;
        size_t tab = 0;
        while (tab < line.size && line.ptr[tab] != '\t') ++tab;
        if (tab > 0 && tab < line.size) {
            rows[n] = (rubraview_props_row_t){ .flags = RUBRAVIEW_PROPS_ROW_EDITABLE };
            unescape_into(proven_u8str_view_slice(line, 0, tab), rows[n].name, sizeof rows[n].name);
            unescape_into(proven_u8str_view_slice(line, tab + 1, line.size - tab - 1), rows[n].value, sizeof rows[n].value);
            n++;
        }
        if (i == text.size) break;
    }
    return n;
}

int32_t rubraview_props_rating_stars(int32_t rating) {
    if (rating <= 0) return 0;
    if (rating <= 12) return 1;
    if (rating <= 37) return 2;
    if (rating <= 62) return 3;
    if (rating <= 87) return 4;
    return 5;
}

int32_t rubraview_props_rating_value(int32_t stars) {
    static const int32_t VALUES[6] = { 0, 1, 25, 50, 75, 99 };
    return VALUES[stars < 0 ? 0 : stars > 5 ? 5 : stars];
}
