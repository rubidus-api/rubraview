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

/* ---- how a picture is printed (D-87) ---- */

rubraview_print_options_t rubraview_print_options_default(void) {
    return (rubraview_print_options_t){
        .turn = RUBRAVIEW_PRINT_TURN_AUTO, .size = RUBRAVIEW_PRINT_SIZE_FIT, .scale_percent = 100,
        .place = RUBRAVIEW_PRINT_PLACE_CENTRE, .margin_mm = 0, .per_sheet = 1,
    };
}

static const int32_t PER_SHEET[] = { 1, 2, 4, 6, 9 };
#define PER_SHEET_COUNT ((int32_t)(sizeof(PER_SHEET) / sizeof(PER_SHEET[0])))

int32_t rubraview_print_per_sheet_index(int32_t per_sheet) {
    int32_t index = 0;
    for (int32_t i = 0; i < PER_SHEET_COUNT; ++i) {
        if (PER_SHEET[i] <= per_sheet) index = i;
    }
    return index;
}

int32_t rubraview_print_per_sheet_of(int32_t index) {
    if (index < 0) index = 0;
    if (index >= PER_SHEET_COUNT) index = PER_SHEET_COUNT - 1;
    return PER_SHEET[index];
}

void rubraview_print_options_clamp(rubraview_print_options_t *o) {
    if (!o) return;
    if ((int)o->turn < 0 || o->turn >= RUBRAVIEW_PRINT_TURN_COUNT) o->turn = RUBRAVIEW_PRINT_TURN_AUTO;
    if ((int)o->size < 0 || o->size >= RUBRAVIEW_PRINT_SIZE_COUNT) o->size = RUBRAVIEW_PRINT_SIZE_FIT;
    if ((int)o->place < 0 || o->place >= RUBRAVIEW_PRINT_PLACE_COUNT) o->place = RUBRAVIEW_PRINT_PLACE_CENTRE;
    if (o->scale_percent < RUBRAVIEW_PRINT_SCALE_MIN) o->scale_percent = RUBRAVIEW_PRINT_SCALE_MIN;
    if (o->scale_percent > RUBRAVIEW_PRINT_SCALE_MAX) o->scale_percent = RUBRAVIEW_PRINT_SCALE_MAX;
    if (o->margin_mm < 0) o->margin_mm = 0;
    if (o->margin_mm > RUBRAVIEW_PRINT_MARGIN_MAX_MM) o->margin_mm = RUBRAVIEW_PRINT_MARGIN_MAX_MM;
    o->per_sheet = rubraview_print_per_sheet_of(rubraview_print_per_sheet_index(o->per_sheet));
}

const char *rubraview_print_turn_name(rubraview_print_turn_t turn) {
    static const char *const NAMES[RUBRAVIEW_PRINT_TURN_COUNT] = {
        "Auto", "None", "90\xC2\xB0 right", "180\xC2\xB0", "90\xC2\xB0 left",
    };
    return (int)turn >= 0 && turn < RUBRAVIEW_PRINT_TURN_COUNT ? NAMES[turn] : "";
}

const char *rubraview_print_size_name(rubraview_print_size_t size) {
    static const char *const NAMES[RUBRAVIEW_PRINT_SIZE_COUNT] = {
        "Fit the sheet", "Fill the sheet", "Stretch", "Actual size",
    };
    return (int)size >= 0 && size < RUBRAVIEW_PRINT_SIZE_COUNT ? NAMES[size] : "";
}

const char *rubraview_print_place_name(rubraview_print_place_t place) {
    static const char *const NAMES[RUBRAVIEW_PRINT_PLACE_COUNT] = {
        "Centre", "Top left", "Top", "Top right", "Left", "Right", "Bottom left", "Bottom", "Bottom right",
    };
    return (int)place >= 0 && place < RUBRAVIEW_PRINT_PLACE_COUNT ? NAMES[place] : "";
}

void rubraview_print_cell(int32_t sheet_w, int32_t sheet_h, int32_t dpi_x, int32_t dpi_y,
                          const rubraview_print_options_t *options, int32_t index,
                          int32_t *out_x, int32_t *out_y, int32_t *out_w, int32_t *out_h) {
    rubraview_print_options_t o = options ? *options : rubraview_print_options_default();
    rubraview_print_options_clamp(&o);
    int32_t x = 0, y = 0, w = 0, h = 0;
    if (sheet_w > 0 && sheet_h > 0) {
        /* The margin, in dots; never so much that less than a tenth of a side is left. */
        int32_t mx = dpi_x > 0 ? (int32_t)((int64_t)o.margin_mm * dpi_x * 10 / 254) : 0;
        int32_t my = dpi_y > 0 ? (int32_t)((int64_t)o.margin_mm * dpi_y * 10 / 254) : 0;
        if (mx > sheet_w * 9 / 20) mx = sheet_w * 9 / 20;
        if (my > sheet_h * 9 / 20) my = sheet_h * 9 / 20;
        int32_t area_w = sheet_w - 2 * mx, area_h = sheet_h - 2 * my;

        /* The longer side of the sheet takes the longer row of cells. */
        int32_t across = 1, down = 1;
        switch (o.per_sheet) {
            case 2: across = 1; down = 2; break;
            case 4: across = 2; down = 2; break;
            case 6: across = 2; down = 3; break;
            case 9: across = 3; down = 3; break;
            default: break;
        }
        if (area_w > area_h) { int32_t t = across; across = down; down = t; }

        int32_t shorter = area_w < area_h ? area_w : area_h;
        int32_t gap = o.per_sheet > 1 ? shorter / 50 : 0;
        w = (area_w - gap * (across - 1)) / across;
        h = (area_h - gap * (down - 1)) / down;
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        int32_t at = index < 0 ? 0 : index % (across * down);
        x = mx + (at % across) * (w + gap);
        y = my + (at / across) * (h + gap);
    }
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
}

/* The part of a length `full` laid at `at` that falls inside 0..room, and
   the part of `pixels` it shows. */
static void cut(int32_t at, int32_t full, int32_t room, int32_t pixels,
                int32_t *dst_at, int32_t *dst_len, int32_t *src_at, int32_t *src_len) {
    int32_t from = at < 0 ? 0 : at;
    int32_t to = at + full > room ? room : at + full;
    if (to <= from) { from = 0; to = 1; }
    int64_t s0 = (int64_t)(from - at) * pixels / full;
    int64_t s1 = ((int64_t)(to - at) * pixels + full - 1) / full;
    if (s0 < 0) s0 = 0;
    if (s1 > pixels) s1 = pixels;
    if (s1 <= s0) { if (s0 >= pixels) s0 = pixels - 1; s1 = s0 + 1; }
    *dst_at = from;
    *dst_len = to - from;
    *src_at = (int32_t)s0;
    *src_len = (int32_t)(s1 - s0);
}

bool rubraview_print_place(int32_t cell_x, int32_t cell_y, int32_t cell_w, int32_t cell_h,
                           int32_t dpi_x, int32_t dpi_y, int32_t picture_w, int32_t picture_h,
                           const rubraview_print_options_t *options, rubraview_print_placement_t *out) {
    if (!out) return false;
    *out = (rubraview_print_placement_t){ 0 };
    if (cell_w <= 0 || cell_h <= 0 || picture_w <= 0 || picture_h <= 0) return false;
    rubraview_print_options_t o = options ? *options : rubraview_print_options_default();
    rubraview_print_options_clamp(&o);

    int32_t turns = 0;
    switch (o.turn) {
        case RUBRAVIEW_PRINT_TURN_RIGHT: turns = 1; break;
        case RUBRAVIEW_PRINT_TURN_HALF:  turns = 2; break;
        case RUBRAVIEW_PRINT_TURN_LEFT:  turns = 3; break;
        case RUBRAVIEW_PRINT_TURN_AUTO: {
            /* Turned when all of it is then larger on the sheet. */
            int32_t w = 0, h = 0, tw = 0, th = 0;
            fit(cell_w, cell_h, picture_w, picture_h, &w, &h);
            fit(cell_w, cell_h, picture_h, picture_w, &tw, &th);
            if ((int64_t)tw * th > (int64_t)w * h) turns = 1;
            break;
        }
        default: break;
    }
    int32_t pw = (turns & 1) ? picture_h : picture_w;
    int32_t ph = (turns & 1) ? picture_w : picture_h;

    /* The whole picture's size on the sheet, 64 bits wide until it is cut. */
    int64_t w = 0, h = 0;
    switch (o.size) {
        case RUBRAVIEW_PRINT_SIZE_FILL:
            if ((int64_t)pw * cell_h >= (int64_t)ph * cell_w) { h = cell_h; w = (int64_t)pw * cell_h / ph; }
            else { w = cell_w; h = (int64_t)ph * cell_w / pw; }
            break;
        case RUBRAVIEW_PRINT_SIZE_STRETCH:
            w = cell_w;
            h = cell_h;
            break;
        case RUBRAVIEW_PRINT_SIZE_ACTUAL:
            w = (int64_t)pw * (dpi_x > 0 ? dpi_x : 96) / 96;
            h = (int64_t)ph * (dpi_y > 0 ? dpi_y : 96) / 96;
            break;
        default: {
            int32_t fw = 0, fh = 0;
            fit(cell_w, cell_h, pw, ph, &fw, &fh);
            w = fw;
            h = fh;
            break;
        }
    }
    w = w * o.scale_percent / 100;
    h = h * o.scale_percent / 100;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    /* Far past the sheet nothing more shows; kept inside 32 bits. */
    const int64_t most = (int64_t)1 << 28;
    if (w > most) w = most;
    if (h > most) h = most;

    int64_t x = (cell_w - w) / 2, y = (cell_h - h) / 2;
    switch (o.place) {
        case RUBRAVIEW_PRINT_PLACE_TOP_LEFT: case RUBRAVIEW_PRINT_PLACE_LEFT: case RUBRAVIEW_PRINT_PLACE_BOTTOM_LEFT:
            x = 0; break;
        case RUBRAVIEW_PRINT_PLACE_TOP_RIGHT: case RUBRAVIEW_PRINT_PLACE_RIGHT: case RUBRAVIEW_PRINT_PLACE_BOTTOM_RIGHT:
            x = cell_w - w; break;
        default: break;
    }
    switch (o.place) {
        case RUBRAVIEW_PRINT_PLACE_TOP_LEFT: case RUBRAVIEW_PRINT_PLACE_TOP: case RUBRAVIEW_PRINT_PLACE_TOP_RIGHT:
            y = 0; break;
        case RUBRAVIEW_PRINT_PLACE_BOTTOM_LEFT: case RUBRAVIEW_PRINT_PLACE_BOTTOM: case RUBRAVIEW_PRINT_PLACE_BOTTOM_RIGHT:
            y = cell_h - h; break;
        default: break;
    }

    out->quarter_turns = turns;
    out->turned_w = pw;
    out->turned_h = ph;
    cut((int32_t)x, (int32_t)w, cell_w, pw, &out->dst_x, &out->dst_w, &out->src_x, &out->src_w);
    cut((int32_t)y, (int32_t)h, cell_h, ph, &out->dst_y, &out->dst_h, &out->src_y, &out->src_h);
    out->dst_x += cell_x;
    out->dst_y += cell_y;
    return true;
}
