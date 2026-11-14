/* RV-281: the properties window's model (fileprops.h). */
#include "rubraview/fileprops.h"
#include "rubraview/lang.h"

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

/* ---- RV-281 phase 2: Digital Signatures ---- */

const char *rubraview_props_sig_status_words(int32_t status) {
    switch (status) {
        case RUBRAVIEW_SIG_OK:        return N_("This digital signature is OK.");
        case RUBRAVIEW_SIG_BAD:       return N_("This digital signature is not valid.");
        case RUBRAVIEW_SIG_UNTRUSTED: return N_("A certificate chain processed, but terminated in a root certificate which is not trusted.");
        default:                      return N_("This digital signature was not checked.");
    }
}

/* ---- RV-281 phase 2: Security ---- */

enum {
    FR_READ_DATA = 0x1, FR_WRITE_DATA = 0x2, FR_APPEND = 0x4, FR_READ_EA = 0x8, FR_WRITE_EA = 0x10, FR_EXECUTE = 0x20,
    FR_DELETE_CHILD = 0x40, FR_READ_ATTR = 0x80, FR_WRITE_ATTR = 0x100, FR_DELETE = 0x10000, FR_READ_CONTROL = 0x20000,
    FR_WRITE_DAC = 0x40000, FR_WRITE_OWNER = 0x80000, FR_SYNCHRONIZE = 0x100000,
};

static const uint32_t RIGHT_MASK[RUBRAVIEW_RIGHT_COUNT] = {
    0x1F01FFu,   /* Full control */
    0x1301BFu,   /* Modify */
    0x1200A9u,   /* Read & execute */
    0x1200A9u,   /* List folder contents */
    0x120089u,   /* Read */
    0x100116u,   /* Write */
    0u,          /* Special permissions */
};

const char *rubraview_props_right_name(int32_t right) {
    static const char *const NAMES[RUBRAVIEW_RIGHT_COUNT] = {
        N_("Full control"), N_("Modify"), N_("Read & execute"), N_("List folder contents"), N_("Read"), N_("Write"),
        N_("Special permissions"),
    };
    return right >= 0 && right < RUBRAVIEW_RIGHT_COUNT ? NAMES[right] : "";
}

bool rubraview_props_right_shown(int32_t right, bool folder) {
    if (right < 0 || right >= RUBRAVIEW_RIGHT_COUNT) return false;
    return right != RUBRAVIEW_RIGHT_LIST || folder;
}

uint32_t rubraview_props_right_mask(int32_t right) {
    return right >= 0 && right < RUBRAVIEW_RIGHT_COUNT ? RIGHT_MASK[right] : 0u;
}

bool rubraview_props_right_held(uint32_t mask, int32_t right, bool folder) {
    if (!rubraview_props_right_shown(right, folder)) return false;
    if (right != RUBRAVIEW_RIGHT_SPECIAL) return RIGHT_MASK[right] != 0 && (mask & RIGHT_MASK[right]) == RIGHT_MASK[right];
    uint32_t covered = FR_SYNCHRONIZE;
    for (int32_t r = 0; r < RUBRAVIEW_RIGHT_SPECIAL; ++r)
        if (rubraview_props_right_held(mask, r, folder)) covered |= RIGHT_MASK[r];
    return (mask & ~covered) != 0;
}

uint32_t rubraview_props_right_set(uint32_t mask, int32_t right, bool on, bool folder) {
    if (!rubraview_props_right_shown(right, folder) || right == RUBRAVIEW_RIGHT_SPECIAL) return mask;
    uint32_t m = RIGHT_MASK[right];
    if (on) return mask | m;
    uint32_t keep = 0, gone = m;
    for (int32_t r = 0; r < RUBRAVIEW_RIGHT_SPECIAL; ++r) {
        uint32_t o = RIGHT_MASK[r];
        if ((m & ~o) == 0) gone |= o;   /* a right holding this one goes with it (Modify off: Full control's own bits too) */
        else if ((o & ~m) == 0 && rubraview_props_right_held(mask, r, folder)) keep |= o;   /* one inside it stays */
    }
    uint32_t out = (mask & ~gone) | keep;
    return (out & ~(uint32_t)FR_SYNCHRONIZE) ? out : 0u;   /* SYNCHRONIZE alone is nothing */
}

uint32_t rubraview_props_special_bit(int32_t i) {
    static const uint32_t BITS[RUBRAVIEW_SPECIAL_COUNT] = {
        FR_EXECUTE, FR_READ_DATA, FR_READ_ATTR, FR_READ_EA, FR_WRITE_DATA, FR_APPEND, FR_WRITE_ATTR, FR_WRITE_EA,
        FR_DELETE_CHILD, FR_DELETE, FR_READ_CONTROL, FR_WRITE_DAC, FR_WRITE_OWNER,
    };
    return i >= 0 && i < RUBRAVIEW_SPECIAL_COUNT ? BITS[i] : 0u;
}

const char *rubraview_props_special_name(int32_t i, bool folder) {
    static const char *const FILE_NAMES[RUBRAVIEW_SPECIAL_COUNT] = {
        N_("Execute file"), N_("Read data"), N_("Read attributes"), N_("Read extended attributes"), N_("Write data"),
        N_("Append data"), N_("Write attributes"), N_("Write extended attributes"), N_("Delete subfolders and files"),
        N_("Delete"), N_("Read permissions"), N_("Change permissions"), N_("Take ownership"),
    };
    static const char *const FOLDER_NAMES[RUBRAVIEW_SPECIAL_COUNT] = {
        N_("Traverse folder"), N_("List folder"), N_("Read attributes"), N_("Read extended attributes"), N_("Create files"),
        N_("Create folders"), N_("Write attributes"), N_("Write extended attributes"), N_("Delete subfolders and files"),
        N_("Delete"), N_("Read permissions"), N_("Change permissions"), N_("Take ownership"),
    };
    if (i < 0 || i >= RUBRAVIEW_SPECIAL_COUNT) return "";
    return folder ? FOLDER_NAMES[i] : FILE_NAMES[i];
}

uint32_t rubraview_props_map_generic(uint32_t mask) {
    uint32_t out = mask & 0x00FFFFFFu;
    if (mask & 0x80000000u) out |= 0x120089u;   /* GENERIC_READ */
    if (mask & 0x40000000u) out |= 0x120116u;   /* GENERIC_WRITE */
    if (mask & 0x20000000u) out |= 0x1200A0u;   /* GENERIC_EXECUTE */
    if (mask & 0x10000000u) out |= 0x1F01FFu;   /* GENERIC_ALL */
    return out;
}

static bool same_sid(const char *a, const char *b) {
    return proven_u8str_view_eq(proven_u8str_view_from_cstr(a), proven_u8str_view_from_cstr(b));
}

static void copy_text(char *dst, size_t cap, const char *src) {
    (void)rubraview_fmt(dst, cap, "{}", PROVEN_ARG(src));
}

size_t rubraview_props_principals_from(const rubraview_props_ace_t *aces, size_t count, rubraview_props_principal_t *out, size_t cap) {
    size_t n = 0;
    for (size_t i = 0; i < count; ++i) {
        const rubraview_props_ace_t *a = &aces[i];
        size_t at = n;
        for (size_t k = 0; k < n; ++k) if (same_sid(out[k].sid, a->sid)) { at = k; break; }
        if (at == n) {
            if (n >= cap) continue;
            out[n] = (rubraview_props_principal_t){0};
            copy_text(out[n].sid, sizeof out[n].sid, a->sid);
            copy_text(out[n].name, sizeof out[n].name, a->name[0] ? a->name : a->sid);
            n++;
        }
        rubraview_props_principal_t *p = &out[at];
        uint32_t mask = rubraview_props_map_generic(a->mask);
        bool inherited = (a->flags & RUBRAVIEW_ACE_INHERITED) != 0;
        if (a->flags & RUBRAVIEW_ACE_INHERIT_ONLY) { p->elsewhere |= mask; continue; }   /* not this object's */
        if (inherited) { if (a->allow) p->allow_inherited |= mask; else p->deny_inherited |= mask; }
        else {
            if (a->allow) p->allow |= mask; else p->deny |= mask;
            if (p->flags == 0) p->flags = (uint8_t)(a->flags & 0x0F);
        }
    }
    return n;
}

static bool push_ace(rubraview_props_ace_t *out, size_t cap, size_t *n, const char *sid, const char *name, bool allow, uint32_t mask, uint8_t flags) {
    if (mask == 0) return true;
    if (*n >= cap) return false;
    out[*n] = (rubraview_props_ace_t){ .allow = allow, .mask = mask, .flags = flags };
    copy_text(out[*n].sid, sizeof out[*n].sid, sid);
    copy_text(out[*n].name, sizeof out[*n].name, name);
    (*n)++;
    return true;
}

size_t rubraview_props_explicit_aces(const rubraview_props_ace_t *original, size_t original_count,
                                     const rubraview_props_principal_t *list, size_t count, bool folder,
                                     rubraview_props_ace_t *out, size_t cap) {
    size_t n = 0;
    uint8_t fresh = folder ? (uint8_t)(RUBRAVIEW_ACE_OBJECT_INHERIT | RUBRAVIEW_ACE_CONTAINER_INHERIT) : 0;
    for (int pass = 0; pass < 2; ++pass) {   /* 0: Deny, 1: Allow */
        bool allow = pass == 1;
        for (size_t i = 0; i < count; ++i) {
            const rubraview_props_principal_t *p = &list[i];
            for (size_t k = 0; k < original_count; ++k) {   /* what it had as it was, or its inherit-only ones */
                const rubraview_props_ace_t *a = &original[k];
                if (a->allow != allow || (a->flags & RUBRAVIEW_ACE_INHERITED) || !same_sid(a->sid, p->sid)) continue;
                if (p->changed && !(a->flags & RUBRAVIEW_ACE_INHERIT_ONLY)) continue;
                (void)push_ace(out, cap, &n, a->sid, a->name, a->allow, a->mask, a->flags);
            }
            if (p->changed) {
                uint8_t flags = p->flags ? p->flags : fresh;
                (void)push_ace(out, cap, &n, p->sid, p->name, allow, allow ? p->allow : p->deny, flags);
            }
        }
    }
    return n;
}

static bool put(char *out, size_t cap, size_t *at, const char *text) {
    size_t n = proven_cstr_len(text);
    if (*at + n + 1 > cap) return false;
    proven_sys_mem_copy(out + *at, text, n);
    *at += n;
    out[*at] = '\0';
    return true;
}

bool rubraview_props_sec_lines(const rubraview_props_sec_t *sec, const rubraview_props_ace_t *aces, size_t count, char *out, size_t cap) {
    if (!out || cap == 0) return false;
    size_t at = 0;
    out[0] = '\0';
    char line[900], name[600];
    if (sec && sec->owner_sid[0]) {
        if (rubraview_fmt(line, sizeof line, "O\t{}\n", PROVEN_ARG(sec->owner_sid)) < 0 || !put(out, cap, &at, line)) return false;
    }
    if (sec && sec->protect >= 0) {
        if (rubraview_fmt(line, sizeof line, "P\t{}\n", PROVEN_ARG(sec->protect)) < 0 || !put(out, cap, &at, line)) return false;
    }
    if (sec && sec->write_dacl && !put(out, cap, &at, "D\t1\n")) return false;
    if (sec && sec->error[0]) {
        if (!rubraview_props_escape(proven_u8str_view_from_cstr(sec->error), name, sizeof name)) return false;
        if (rubraview_fmt(line, sizeof line, "E\t{}\n", PROVEN_ARG(name)) < 0 || !put(out, cap, &at, line)) return false;
    }
    for (size_t i = 0; i < count; ++i) {
        const rubraview_props_ace_t *a = &aces[i];
        if (!rubraview_props_escape(proven_u8str_view_from_cstr(a->name), name, sizeof name)) return false;
        const char *kind = a->audit ? (a->audit == 3 ? "b" : a->audit == 2 ? "f" : "s") : (a->allow ? "a" : "d");
        if (rubraview_fmt(line, sizeof line, "{}\t{}\t{}\t{:x}\t{:x}\t{}\n", PROVEN_ARG(a->audit ? "U" : "A"), PROVEN_ARG(kind),
                          PROVEN_ARG(a->sid), PROVEN_ARG(a->mask), PROVEN_ARG((uint32_t)a->flags), PROVEN_ARG(name)) < 0 ||
            !put(out, cap, &at, line))
            return false;
    }
    return true;
}

static uint32_t hex_of(proven_u8str_view_t t) {
    uint32_t v = 0;
    for (size_t i = 0; i < t.size; ++i) {
        uint8_t c = t.ptr[i];
        uint32_t d = c >= '0' && c <= '9' ? (uint32_t)(c - '0') : c >= 'a' && c <= 'f' ? (uint32_t)(c - 'a' + 10)
                   : c >= 'A' && c <= 'F' ? (uint32_t)(c - 'A' + 10) : 16u;
        if (d > 15) break;
        v = v * 16u + d;
    }
    return v;
}

size_t rubraview_props_sec_read(proven_u8str_view_t text, rubraview_props_sec_t *sec, rubraview_props_ace_t *aces, size_t cap) {
    if (sec) { *sec = (rubraview_props_sec_t){0}; sec->protect = -1; }
    size_t n = 0, start = 0;
    for (size_t i = 0; i <= text.size; ++i) {
        if (i < text.size && text.ptr[i] != '\n') continue;
        proven_u8str_view_t line = proven_u8str_view_slice(text, start, i - start);
        start = i + 1;
        proven_u8str_view_t f[6] = {0};
        size_t nf = 0, from = 0;
        for (size_t k = 0; k <= line.size && nf < 6; ++k) {
            if (k < line.size && line.ptr[k] != '\t') continue;
            f[nf++] = proven_u8str_view_slice(line, from, k - from);
            from = k + 1;
        }
        if (nf >= 2 && f[0].size == 1) {
            uint8_t tag = f[0].ptr[0];
            if (tag == 'O' && sec) unescape_into(f[1], sec->owner_sid, sizeof sec->owner_sid);
            else if (tag == 'P' && sec) sec->protect = f[1].size == 1 && f[1].ptr[0] == '1' ? 1 : 0;
            else if (tag == 'E' && sec) unescape_into(f[1], sec->error, sizeof sec->error);
            else if (tag == 'D' && sec) sec->write_dacl = f[1].size == 1 && f[1].ptr[0] == '1';
            else if ((tag == 'A' || tag == 'U') && nf >= 5 && f[1].size == 1 && n < cap) {
                rubraview_props_ace_t *a = &aces[n];
                *a = (rubraview_props_ace_t){0};
                uint8_t k = f[1].ptr[0];
                if (tag == 'A') a->allow = k == 'a';
                else { a->audit = k == 'b' ? 3 : k == 'f' ? 2 : 1; a->allow = true; }
                unescape_into(f[2], a->sid, sizeof a->sid);
                a->mask = hex_of(f[3]);
                a->flags = (uint8_t)hex_of(f[4]);
                if (nf >= 6) unescape_into(f[5], a->name, sizeof a->name);
                if (a->sid[0]) n++;
            }
        }
        if (i == text.size) break;
    }
    return n;
}
