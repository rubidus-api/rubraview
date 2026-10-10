#include "rubraview/shellreq.h"
#include "rubraview/filemanage.h"
#include <stdlib.h>
#include <string.h>

static const char *const VERB_FLAGS[RUBRAVIEW_SHELL_VERB_COUNT] = {
    "", "--open", "--open-only", "--add", "--browse", "--convert", "--print",
};

const char *rubraview_shell_verb_flag(rubraview_shell_verb_t verb) {
    return verb > RUBRAVIEW_SHELL_VERB_NONE && verb < RUBRAVIEW_SHELL_VERB_COUNT ? VERB_FLAGS[verb] : "";
}

rubraview_shell_verb_t rubraview_shell_verb_for_flag(u8str_t arg) {
    for (int verb = RUBRAVIEW_SHELL_VERB_OPEN; verb < RUBRAVIEW_SHELL_VERB_COUNT; ++verb) {
        if (rubraview_u8_eq_lit(arg, VERB_FLAGS[verb])) return (rubraview_shell_verb_t)verb;
    }
    return RUBRAVIEW_SHELL_VERB_NONE;
}

#define PLAYABLE (RUBRAVIEW_SHELL_PICTURES | RUBRAVIEW_SHELL_VIDEO | RUBRAVIEW_SHELL_MUSIC)
#define BOOKS    (RUBRAVIEW_SHELL_COMICS | RUBRAVIEW_SHELL_ARCHIVES)

/* The ids sort in the menu's order: the classic menu lists a sub-menu's
   verbs by key name. */
static const rubraview_shell_menu_item_t MENU_ITEMS[] = {
    { RUBRAVIEW_SHELL_VERB_OPEN,      "1open",    "Open",                 "\xEC\x97\xB4\xEA\xB8\xB0",
      PLAYABLE | BOOKS, false },
    { RUBRAVIEW_SHELL_VERB_OPEN_ONLY, "2only",    "Open these only",      "\xEC\x9D\xB4\xEA\xB2\x83\xEB\xA7\x8C \xEC\x97\xB4\xEA\xB8\xB0",
      PLAYABLE, false },
    { RUBRAVIEW_SHELL_VERB_ADD,       "3add",     "Add to the list",      "\xEC\x9E\xAC\xEC\x83\x9D\xEB\xAA\xA9\xEB\xA1\x9D\xEC\x97\x90 \xEB\x84\xA3\xEA\xB8\xB0",
      PLAYABLE, false },
    { RUBRAVIEW_SHELL_VERB_BROWSE,    "4browse",  "Browse the archive",   "\xEC\x95\x95\xEC\xB6\x95 \xED\x8C\x8C\xEC\x9D\xBC \xEB\x82\xB4\xEB\xB6\x80 \xEB\xB3\xB4\xEA\xB8\xB0",
      BOOKS, true },
    { RUBRAVIEW_SHELL_VERB_CONVERT,   "5convert", "Convert...",           "\xEB\xB3\x80\xED\x99\x98...",
      RUBRAVIEW_SHELL_PICTURES, false },
    { RUBRAVIEW_SHELL_VERB_PRINT,     "6print",   "Print...",             "\xEC\x9D\xB8\xEC\x87\x84...",
      RUBRAVIEW_SHELL_PICTURES, false },
};

const rubraview_shell_menu_item_t *rubraview_shell_menu_items(size_t *out_count) {
    if (out_count) *out_count = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);
    return MENU_ITEMS;
}

/* ---- a request, packed ---- */

size_t rubraview_shellreq_pack(rubraview_shell_verb_t verb, bool whole, const u8str_t *paths, size_t count,
                               char *dst, size_t cap) {
    size_t need = 2;
    for (size_t i = 0; i < count; ++i) need += paths[i].len + 1;
    if (!dst || cap < need) return need;
    dst[0] = (char)('A' + (int)verb);
    dst[1] = whole ? '1' : '0';
    size_t at = 2;
    for (size_t i = 0; i < count; ++i) {
        if (paths[i].len) memcpy(dst + at, paths[i].ptr, paths[i].len);
        at += paths[i].len;
        dst[at++] = '\0';
    }
    return need;
}

bool rubraview_shellreq_unpack(const char *blob, size_t len, rubraview_shell_verb_t *out_verb, bool *out_whole,
                               u8str_t *paths, size_t cap, size_t *out_count) {
    if (out_count) *out_count = 0;
    if (!blob || len < 2) return false;
    int verb = blob[0] - 'A';
    if (verb < 0 || verb >= RUBRAVIEW_SHELL_VERB_COUNT) return false;
    if (blob[1] != '0' && blob[1] != '1') return false;
    /* Every path ends inside the bytes: the last byte is a path's NUL. */
    if (len > 2 && blob[len - 1] != '\0') return false;

    size_t count = 0;
    for (size_t at = 2; at < len;) {
        size_t n = strlen(blob + at);
        if (n > 0) {
            if (paths && count < cap) paths[count] = (u8str_t){ .ptr = blob + at, .len = n };
            count++;
        }
        at += n + 1;
    }
    if (out_verb) *out_verb = (rubraview_shell_verb_t)verb;
    if (out_whole) *out_whole = blob[1] == '1';
    if (out_count) *out_count = count;
    return true;
}

size_t rubraview_shellreq_split_lines(u8str_t text, u8str_t *paths, size_t cap) {
    size_t at = 0, count = 0;
    if (text.len >= 3 && (unsigned char)text.ptr[0] == 0xEF && (unsigned char)text.ptr[1] == 0xBB &&
        (unsigned char)text.ptr[2] == 0xBF) {
        at = 3;
    }
    while (at < text.len) {
        size_t end = at;
        while (end < text.len && text.ptr[end] != '\n' && text.ptr[end] != '\r') end++;
        if (end > at) {
            if (paths && count < cap) paths[count] = (u8str_t){ .ptr = text.ptr + at, .len = end - at };
            count++;
        }
        at = end + 1;
    }
    return count;
}

/* ---- gathering ---- */

bool rubraview_shellreq_collect(rubraview_shellreq_collector_t *c, rubraview_shell_verb_t verb, bool whole,
                                const u8str_t *paths, size_t count, double now) {
    if (!c) return false;
    if (c->pending && c->verb != verb) return false;

    size_t need = c->pending ? c->text_len : 0;
    for (size_t i = 0; i < count; ++i) need += paths[i].len + 1;
    if (need > c->text_cap) {
        size_t grown = c->text_cap ? c->text_cap : 1024;
        while (grown < need) grown *= 2;
        char *more = (char*)realloc(c->text, grown);
        if (!more) return false;
        c->text = more;
        c->text_cap = grown;
    }
    if (!c->pending) {
        c->text_len = 0;
        c->count = 0;
        c->whole = false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (paths[i].len == 0) continue;
        memcpy(c->text + c->text_len, paths[i].ptr, paths[i].len);
        c->text_len += paths[i].len;
        c->text[c->text_len++] = '\0';
        c->count++;
    }
    c->verb = verb;
    c->pending = true;
    c->whole = c->whole || whole;
    c->last = now;
    return true;
}

bool rubraview_shellreq_ready(const rubraview_shellreq_collector_t *c, double now) {
    if (!c || !c->pending) return false;
    return c->whole || now - c->last >= RUBRAVIEW_SHELLREQ_GATHER_SECONDS;
}

const char *rubraview_shellreq_next(const rubraview_shellreq_collector_t *c, const char *prev) {
    if (!c || !c->pending || c->text_len == 0) return NULL;
    const char *next = prev ? prev + strlen(prev) + 1 : c->text;
    return next < c->text + c->text_len ? next : NULL;
}

void rubraview_shellreq_clear(rubraview_shellreq_collector_t *c) {
    if (!c) return;
    c->pending = false;
    c->whole = false;
    c->text_len = 0;
    c->count = 0;
}

void rubraview_shellreq_free(rubraview_shellreq_collector_t *c) {
    if (!c) return;
    free(c->text);
    *c = (rubraview_shellreq_collector_t){ 0 };
}

/* ---- a picture on a sheet ---- */

static void fit(int32_t sheet_w, int32_t sheet_h, int32_t w, int32_t h, int32_t *out_w, int32_t *out_h) {
    /* The side that runs out first decides; 64 bits, a sheet at 1200 dpi
       times a large picture's side passes 32. */
    if ((int64_t)w * sheet_h >= (int64_t)h * sheet_w) {
        *out_w = sheet_w;
        *out_h = (int32_t)((int64_t)h * sheet_w / w);
    } else {
        *out_h = sheet_h;
        *out_w = (int32_t)((int64_t)w * sheet_h / h);
    }
    if (*out_w < 1) *out_w = 1;
    if (*out_h < 1) *out_h = 1;
}

void rubraview_print_fit(int32_t sheet_w, int32_t sheet_h, int32_t picture_w, int32_t picture_h,
                         int32_t *out_x, int32_t *out_y, int32_t *out_w, int32_t *out_h, bool *out_turn) {
    int32_t w = 0, h = 0;
    bool turn = false;
    if (sheet_w > 0 && sheet_h > 0 && picture_w > 0 && picture_h > 0) {
        int32_t tw = 0, th = 0;
        fit(sheet_w, sheet_h, picture_w, picture_h, &w, &h);
        fit(sheet_w, sheet_h, picture_h, picture_w, &tw, &th);
        if ((int64_t)tw * th > (int64_t)w * h) {
            turn = true;
            w = tw;
            h = th;
        }
    }
    if (out_x) *out_x = (sheet_w - w) / 2;
    if (out_y) *out_y = (sheet_h - h) / 2;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    if (out_turn) *out_turn = turn;
}
