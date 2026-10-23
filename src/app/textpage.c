/*
 * A text file as a page: its colours, blocks, scrolling, decoding and opening (RFC-0005;
 * RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* ---- RFC-0005: a text file as a page ---- */

#define TEXT_MAX_BYTES (64u * 1024u * 1024u)   /* §2.3: bigger is refused for now (owner, 2026-10-17) */


bool is_text_path(proven_u8str_view_t path) {
    return g_text_on && path.size > 0 && rubraview_glob_match_list(rubraview_path_basename(path), PROVEN_LIT(TEXT_FILTER));
}

/* A page that is neither a picture nor played: the text ones. */
bool text_active(const app_state_t *app) {
    return app->text && app->text->page == current_page_index(app) && !app->picker_open;
}

double text_dpi(app_state_t *app) { return rubraview_pal_window_dpi_scale(app->window); }

/* The size in pixels, the line's height, from Settings › Text. */
double text_px(app_state_t *app) {
    double pt = rubraview_settings_get(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("size"));
    return (pt > 1.0 ? pt : 18.0) * 96.0 / 72.0 * text_dpi(app);
}

double text_spacing(app_state_t *app) {
    double pct = rubraview_settings_get(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("line_spacing"));
    return (pct >= 80.0 ? pct : 160.0) / 100.0;
}

double text_line_h(app_state_t *app) { return text_px(app) * text_spacing(app); }

bool text_wraps(app_state_t *app) { return rubraview_settings_get(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("wrap")) > 0.5; }

/* 1.25.0: "#RRGGBB" (or "RRGGBB") as an opaque colour; false for anything else. */
static bool hex_colour(proven_u8str_view_t text, uint32_t *out) {
    while (text.size > 0 && text.ptr[0] == ' ') { text.ptr++; text.size--; }
    while (text.size > 0 && text.ptr[text.size - 1] == ' ') text.size--;
    if (text.size > 0 && text.ptr[0] == '#') { text.ptr++; text.size--; }
    if (text.size != 6) return false;
    uint32_t v = 0;
    for (size_t i = 0; i < 6; ++i) {
        char c = text.ptr[i];
        uint32_t d = c >= '0' && c <= '9' ? (uint32_t)(c - '0') : c >= 'a' && c <= 'f' ? (uint32_t)(c - 'a' + 10)
                   : c >= 'A' && c <= 'F' ? (uint32_t)(c - 'A' + 10) : 16u;
        if (d > 15) return false;
        v = v * 16u + d;
    }
    *out = 0xFF000000u | v;
    return true;
}

/* A colour of Settings › Text, or `fallback` when it is empty or not a colour. */
uint32_t text_colour(app_state_t *app, const char *key, uint32_t fallback) {
    uint32_t c = 0;
    /* spelled out: check-settings.py finds reads by their literal keys */
    proven_u8str_view_t v = strcmp(key, "color_text") == 0 ? rubraview_settings_get_text(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("color_text"))
              : strcmp(key, "color_back") == 0 ? rubraview_settings_get_text(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("color_back"))
              : strcmp(key, "color_link") == 0 ? rubraview_settings_get_text(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("color_link"))
                                               : rubraview_settings_get_text(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("color_mark"));
    return hex_colour(v, &c) ? c : fallback;
}

uint32_t text_ink(app_state_t *app, uint32_t *paper) {
    static const uint32_t PAPER[] = { 0xFF1B1B1Bu, 0xFFF4ECD8u, 0xFFFFFFFFu, 0xFF000000u };
    static const uint32_t INK[]   = { 0xFFDADADAu, 0xFF433422u, 0xFF202020u, 0xFFC8C8C8u };
    int k = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("paper")));
    if (k < 0 || k > 3) k = 0;
    if (paper) *paper = text_colour(app, "color_back", PAPER[k]);   /* 1.25.0 (owner, 2026-10-20) */
    return text_colour(app, "color_text", INK[k]);
}


void text_free_blocks(text_page_t *t) {
    if (!t->blocks) return;
    for (size_t i = 0; i < t->view.count; ++i) {
        if (t->blocks[i]) { rubraview_pal_text_block_free(t->blocks[i]); t->blocks[i] = NULL; }
        if (t->carry) t->carry[i] = TEXT_CARRY_UNKNOWN;   /* laid out again: where lines end may differ */
    }
    t->laid_out = 0;
}

/* Where a paragraph's block begins in the view's text: before the paragraph by what it carries. */
size_t text_block_start(const text_page_t *t, size_t i) {
    size_t c = t->carry && t->carry[i] != TEXT_CARRY_UNKNOWN ? t->carry[i] : 0;
    return t->view.paras[i].start - c;
}

/* The next paragraph goes on in this one's last line (a one-run piece). */
bool text_next_joined(const text_page_t *t, size_t i) {
    return i + 1 < t->view.count && t->view.paras[i + 1].joined;
}

/* A paragraph's height before it is laid out: its characters at about
   half the size each (CJK and Hangul at the whole size), over the width. */
static double text_guess_height(app_state_t *app, const text_page_t *t, size_t i) {
    const rubraview_text_para_t *p = &t->view.paras[i];
    double px = text_px(app), line = text_line_h(app);
    double gap = p->gap ? line * 0.6 : 0.0;
    if (!text_wraps(app) || t->lay_width < px) return line + gap;
    double cells = 0.0;
    for (size_t j = p->start; j < p->start + p->len;) {
        uint32_t cp = rubraview_text_next_cp(t->view.text, p->start + p->len, &j);
        cells += rubraview_codepoint_is_wide(cp) ? 2.0 : 1.0;
    }
    double lines = ceil(cells * px * 0.5 / t->lay_width);
    return (lines < 1.0 ? 1.0 : lines) * line + gap;
}

void text_reset_heights(app_state_t *app, text_page_t *t) {
    for (size_t i = 0; i < t->view.count; ++i) t->heights[i] = text_guess_height(app, t, i);
    t->tops_dirty = true;
}

void text_tops(text_page_t *t) {
    if (!t->tops_dirty) return;
    double y = 0.0;
    for (size_t i = 0; i < t->view.count; ++i) { t->tops[i] = y; y += t->heights[i]; }
    t->total = y;
    t->tops_dirty = false;
}

size_t text_para_at_y(text_page_t *t, double y) {
    text_tops(t);
    size_t lo = 0, hi = t->view.count ? t->view.count - 1 : 0;
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        if (t->tops[mid] <= y) lo = mid; else hi = mid - 1;
    }
    return lo;
}

double text_scroll_y(text_page_t *t) {
    text_tops(t);
    return t->view.count ? t->tops[t->anchor] + t->anchor_dy : 0.0;
}

double text_scroll_max(text_page_t *t) {
    text_tops(t);
    double m = t->total - t->view_h * 0.5;
    return m > 0.0 ? m : 0.0;
}

void text_scroll_to(text_page_t *t, double y) {
    double max = text_scroll_max(t);
    if (y > max) y = max;
    if (y < 0.0) y = 0.0;
    t->anchor = text_para_at_y(t, y);
    t->anchor_dy = y - t->tops[t->anchor];
}

void text_scroll_by(app_state_t *app, double dy) {
    if (!app->text) return;
    text_scroll_to(app->text, text_scroll_y(app->text) + dy);
}

/* The place kept for this file (§6): where the paragraph at the top began in the file. */
static void text_remember(app_state_t *app) {
    text_page_t *t = app->text;
    if (!t || t->key.size == 0 || t->view.count == 0) return;
    if (rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("reading_history")) < 0.5) return;
    if (recent_kept_out(app, t->key)) return;
    size_t source = t->view.paras[t->anchor].source;
    /* The history keeps the path it is given: a copy that outlives the page. */
    rubraview_history_record(app->arena, &app->history, app_keep(app, t->key), (int32_t)source, (int32_t)t->source_len, (int64_t)time(NULL));
    if (t->enc_by_hand) {
        char key[1100];
        int n = snprintf(key, sizeof(key), "%.*s|encoding", (int)t->key.size, t->key.ptr);
        if (n > 0 && (size_t)n < sizeof(key)) {
            rubraview_history_record(app->arena, &app->history, app_keep(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)key, .size = (size_t)n }),
                                     (int32_t)t->enc, 0, (int64_t)time(NULL));
        }
    }
}

void text_close(app_state_t *app) {
    text_page_t *t = app->text;
    if (!t) return;
    text_remember(app);
    text_free_blocks(t);
    rubraview_text_view_free(&t->view);
    free(t->blocks);
    free(t->carry);
    free(t->heights);
    free(t->tops);
    free(t->source);
    free((void *)t->key.ptr);
    free(t);
    app->text = NULL;
    if (app->rename_active && app->rename_is_find) rename_end(app);
}

/* The view again after its breaks or leading spaces changed: the same place in the file at the top. */
bool text_rebuild(app_state_t *app) {
    text_page_t *t = app->text;
    size_t source = t->view.count ? t->view.paras[t->anchor].source : 0;
    text_free_blocks(t);
    free(t->blocks); free(t->heights); free(t->tops); free(t->carry);
    t->blocks = NULL; t->heights = NULL; t->tops = NULL; t->carry = NULL;
    rubraview_text_view_free(&t->view);
    int breaks = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("breaks")));
    bool trim = rubraview_settings_get(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("trim_lead")) > 0.5;
    if (breaks < 0 || breaks >= RUBRAVIEW_TEXT_BREAKS_COUNT) breaks = 0;
    bool headings = rubraview_settings_get(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("headings")) > 0.5;
    if (!rubraview_text_view_build_ex(&t->view, t->source, t->source_len, (rubraview_text_breaks_t)breaks, trim, headings)) return false;
    t->built_breaks = breaks;
    t->built_trim = trim;
    t->built_headings = headings;
    t->blocks = (rubraview_text_block_t **)calloc(t->view.count, sizeof(*t->blocks));
    t->heights = (double *)calloc(t->view.count, sizeof(double));
    t->tops = (double *)calloc(t->view.count, sizeof(double));
    t->carry = (size_t *)malloc(t->view.count * sizeof(size_t));
    if (!t->blocks || !t->heights || !t->tops || !t->carry) return false;
    for (size_t i = 0; i < t->view.count; ++i) t->carry[i] = TEXT_CARRY_UNKNOWN;
    t->lay_width = 0.0;   /* laid out again on the next frame */
    text_reset_heights(app, t);
    t->anchor = rubraview_text_view_para_of_source(&t->view, source);
    t->anchor_dy = 0.0;
    t->selecting = false;
    t->sel_a = t->sel_b = 0;
    t->match = false;
    return true;
}

/* §2: the bytes read in an encoding, made ready to show. */
static bool text_decode(text_page_t *t, const uint8_t *bytes, size_t size, rubraview_textenc_t enc, size_t bom) {
    char *raw = NULL;
    size_t raw_len = 0;
    bytes += bom; size -= bom;
    if (enc == RUBRAVIEW_TEXTENC_UTF8) {
        raw = (char *)malloc(size + 1);
        if (raw) { memcpy(raw, bytes, size); raw[size] = '\0'; raw_len = size; }
    } else if (enc == RUBRAVIEW_TEXTENC_UTF16LE || enc == RUBRAVIEW_TEXTENC_UTF16BE) {
        raw = rubraview_textenc_utf16_to_utf8(bytes, size, enc == RUBRAVIEW_TEXTENC_UTF16BE, &raw_len);
    } else {
        raw = rubraview_pal_text_decode(bytes, size, rubraview_textenc_codepage(enc), &raw_len);
    }
    if (!raw) return false;
    char *clean = rubraview_text_clean(raw, raw_len, &t->source_len);
    free(raw);
    if (!clean) return false;
    free(t->source);
    t->source = clean;
    t->enc = enc;
    return true;
}

/* The bytes of page `index`: mapped from the folder, or read out of the archive. */
bool text_with_bytes(app_state_t *app, int32_t index, rubraview_textenc_t want, bool by_hand) {
    text_page_t *t = app->text;
    proven_u8str_view_t path = app->source.pages[index].path;
    rubraview_fs_mapping_t map = {0};
    void *mem = NULL;
    const uint8_t *bytes = NULL;
    size_t size = 0;
    if (app->source.kind == RUBRAVIEW_PAGE_SOURCE_FOLDER && path.size > 0) {
        if (!rubraview_pal_fs_map(path, &map)) return false;
        if (map.size > TEXT_MAX_BYTES) { rubraview_pal_fs_unmap(&map); osd_say(app, TU8("this text file is over 64 MB: not shown")); return false; }
        bytes = (const uint8_t *)map.data;
        size = (size_t)map.size;
    } else {
        rubraview_page_bytes_t b = page_read_owned(&app->source, &app->source_lock, (size_t)index, &mem);
        if (!b.ok) { free(mem); return false; }
        bytes = (const uint8_t *)b.data.ptr;
        size = b.data.size;
    }
    size_t bom = 0;
    char tag[96];
    const char *lang = rubraview_pal_process_ui_language(tag, sizeof(tag)) ? rubraview_lang_pick(tag, LANG_PACKS, 5) : NULL;
    rubraview_textenc_t guessed = rubraview_textenc_guess(bytes, size, lang, &bom);
    rubraview_textenc_t enc = want != RUBRAVIEW_TEXTENC_AUTO ? want : guessed;
    if (want != RUBRAVIEW_TEXTENC_AUTO && enc != guessed) bom = 0;
    /* A .nfo's art is drawn in DOS's box characters. */
    if (want == RUBRAVIEW_TEXTENC_AUTO && enc == RUBRAVIEW_TEXTENC_CP1252 && rubraview_path_has_ext(path.size ? path : app->source.pages[index].name, ".nfo")) {
        enc = RUBRAVIEW_TEXTENC_CP437;
    }
    bool ok = text_decode(t, bytes, size, enc, bom);
    if (map.data) rubraview_pal_fs_unmap(&map);
    free(mem);
    t->enc_by_hand = by_hand;
    return ok;
}

static void text_open(app_state_t *app, int32_t index) {
    text_page_t *t = (text_page_t *)calloc(1, sizeof(*t));
    if (!t) return;
    app->text = t;
    t->page = index;
    proven_u8str_view_t path = app->source.pages[index].path.size > 0 ? app->source.pages[index].path : app->source.pages[index].name;
    proven_u8str_view_t key = app->source.archive_path.size > 0 && app->source.pages[index].path.size == 0
        ? rubraview_path_join(app->arena, app->source.archive_path, app->source.pages[index].name) : path;
    char *k = (char *)malloc(key.size + 1);
    if (k) { memcpy(k, key.ptr, key.size); k[key.size] = '\0'; t->key = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)k, .size = key.size }; }

    /* An encoding chosen by hand for this file, else the setting's, else the guess. */
    rubraview_textenc_t want = RUBRAVIEW_TEXTENC_AUTO;
    bool by_hand = false;
    char hk[1100];
    int hn = snprintf(hk, sizeof(hk), "%.*s|encoding", (int)key.size, key.ptr);
    const rubraview_history_entry_t *chosen = hn > 0 && (size_t)hn < sizeof(hk)
        ? rubraview_history_find(&app->history, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)hk, .size = (size_t)hn }) : NULL;
    if (chosen && chosen->page > 0 && chosen->page < RUBRAVIEW_TEXTENC_COUNT) { want = (rubraview_textenc_t)chosen->page; by_hand = true; }
    else {
        int s = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("encoding")));
        if (s > 0 && s < RUBRAVIEW_TEXTENC_COUNT) want = (rubraview_textenc_t)s;
    }
    if (!text_with_bytes(app, index, want, by_hand) || !text_rebuild(app)) {
        osd_say(app, TU8("this text file could not be read"));
        text_close(app);
        return;
    }
    /* Where it was left (§6). */
    const rubraview_history_entry_t *was = rubraview_history_find(&app->history, key);
    if (was && was->page > 0 && (size_t)was->total == t->source_len) {
        t->anchor = rubraview_text_view_para_of_source(&t->view, (size_t)was->page);
        osd_say(app, TU8("back where it was left (Home: the start)"));
    }
    update_window_title(app);
    app->needs_relayout = true;
}

/* media_prepare's other half: the text page on screen is the one open. */
void text_prepare(app_state_t *app) {
    int32_t index = current_page_index(app);
    if (app->text && app->text->page == index && index >= 0 && (size_t)index < page_count(app)) {
        /* The same page of the same set: a new set may have a text page at the same place. */
        proven_u8str_view_t path = app->source.pages[index].path.size > 0 ? app->source.pages[index].path : app->source.pages[index].name;
        size_t n = path.size;
        if (app->text->key.size >= n && memcmp(app->text->key.ptr + app->text->key.size - n, path.ptr, n) == 0) return;
    }
    text_close(app);
    if (index >= 0 && (size_t)index < page_count(app)) {
        proven_u8str_view_t path = app->source.pages[index].path.size > 0 ? app->source.pages[index].path : app->source.pages[index].name;
        if (is_text_path(path)) text_open(app, index);
    }
    toolbox_refresh(app);
}
