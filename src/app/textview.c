/*
 * The text page laid out, drawn and moved: its blocks and column, its keys, wheel and pointer, find, copy
 * and encodings (RFC-0005; RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* ---- laying out and drawing ---- */

static uint64_t text_family_hash(proven_u8str_view_t s) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < s.size; ++i) h = (h ^ (uint8_t)s.ptr[i]) * 1099511628211ull;
    return h;
}

static rubraview_text_block_t *text_block_made(app_state_t *app, text_page_t *t, size_t i);

static rubraview_text_block_t *text_block(app_state_t *app, text_page_t *t, size_t i) {
    if (t->blocks[i]) return t->blocks[i];
    if (t->carry && t->view.paras[i].joined && t->carry[i] == TEXT_CARRY_UNKNOWN) {
        /* the pieces before it first, in order, back to one whose carry is known */
        size_t k = i;
        while (k > 0 && t->view.paras[k].joined && t->carry[k] == TEXT_CARRY_UNKNOWN) --k;
        if (t->carry[k] == TEXT_CARRY_UNKNOWN) t->carry[k] = 0;
        for (size_t j = k + 1; j <= i; ++j) {
            if (!t->view.paras[j].joined) { t->carry[j] = 0; continue; }
            rubraview_text_block_t *before = t->blocks[j - 1] ? t->blocks[j - 1] : text_block_made(app, t, j - 1);
            size_t local = 0;
            double top = 0.0;
            size_t end = t->view.paras[j - 1].start + t->view.paras[j - 1].len, from = text_block_start(t, j - 1);
            t->carry[j] = before && rubraview_pal_text_block_last_line(before, &local, &top) && from + local < end ? end - (from + local) : 0;
        }
    }
    return text_block_made(app, t, i);
}

static rubraview_text_block_t *text_block_made(app_state_t *app, text_page_t *t, size_t i) {
    if (t->blocks[i]) return t->blocks[i];
    if (t->carry && t->view.paras[i].joined && t->carry[i] == TEXT_CARRY_UNKNOWN) t->carry[i] = 0;   /* reached out of order */
    const rubraview_text_para_t *p = &t->view.paras[i];
    rubraview_font_spec_t face = rubraview_font_spec_parse(rubraview_settings_get_text(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("font")));
    rubraview_text_style_t style = {
        .family = face.family, .weight = face.weight, .italic = face.italic,   /* RV-136 */
        .size = t->lay_size, .line_spacing = t->lay_spacing, .width = t->lay_width, .wrap = t->lay_wrap,
    };
    size_t from = text_block_start(t, i);
    rubraview_text_block_t *b = rubraview_pal_text_block_make(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)t->view.text + from, .size = p->start + p->len - from }, &style);
    if (!b) return NULL;
    t->blocks[i] = b;
    t->laid_out++;
    double w = 0.0, h = 0.0;
    rubraview_pal_text_block_size(b, &w, &h);
    if (text_next_joined(t, i)) {   /* its last line is drawn by the next piece */
        size_t local = 0;
        double top = 0.0;
        if (rubraview_pal_text_block_last_line(b, &local, &top)) h = top;
    }
    double gap = p->gap ? t->lay_size * t->lay_spacing * 0.6 : 0.0;
    double line = t->lay_size * t->lay_spacing;
    if (h < line && !text_next_joined(t, i)) h = line;
    if (fabs(t->heights[i] - (h + gap)) > 0.5) { t->heights[i] = h + gap; t->tops_dirty = true; }
    return b;
}

/* The column: its left edge and width, and the space above the first line. */
static void text_column(app_state_t *app, double win_w, double *x, double *w) {
    double dpi = text_dpi(app);
    double margin = rubraview_settings_get(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("margin")) * dpi;
    double width = win_w - 2.0 * margin;
    double chars = rubraview_settings_get(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("width"));
    if (chars >= 10.0 && chars * text_px(app) * 0.55 < width) width = chars * text_px(app) * 0.55;
    if (width < 80.0 * dpi) width = win_w - 16.0 * dpi;
    *x = (win_w - width) * 0.5;
    double right = win_w - 16.0 * dpi;   /* RV-172: clear of the scroll bar */
    *w = *x + width > right && right - *x > 40.0 * dpi ? right - *x : width;
}

void draw_text_page(app_state_t *app, int32_t win_w, int32_t win_h) {
    text_page_t *t = app->text;
    if (!t || t->view.count == 0) return;
    uint32_t paper = 0;
    uint32_t ink = text_ink(app, &paper);
    rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ 0, 0, (double)win_w, (double)win_h }, paper, 0.0);

    double col_x = 0.0, col_w = 0.0;
    text_column(app, (double)win_w, &col_x, &col_w);
    double dpi = text_dpi(app);
    t->top_pad = 40.0 * dpi;   /* clear of the title bar's band */
    t->view_h = (double)win_h - t->top_pad;
    t->col_x = col_x;
    t->col_w = col_w;

    /* Built again when Settings › Text changed its breaks or its indents. */
    if (t->built_breaks != (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("breaks"))) ||
        t->built_trim != (rubraview_settings_get(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("trim_lead")) > 0.5) ||
        t->built_headings != (rubraview_settings_get(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("headings")) > 0.5)) {
        if (!text_rebuild(app) || t->view.count == 0) return;
    }
    /* Laid out again when what they were laid out for has changed. */
    proven_u8str_view_t family = rubraview_settings_get_text(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("font"));
    double size = text_px(app), spacing = text_spacing(app);
    bool wrap = text_wraps(app);
    if (fabs(t->lay_width - col_w) > 0.5 || fabs(t->lay_size - size) > 0.01 || fabs(t->lay_spacing - spacing) > 0.001 ||
        t->lay_wrap != wrap || t->lay_family != text_family_hash(family)) {
        text_free_blocks(t);
        t->lay_width = col_w; t->lay_size = size; t->lay_spacing = spacing; t->lay_wrap = wrap;
        t->lay_family = text_family_hash(family);
        text_reset_heights(app, t);
        if (!wrap) t->x_scroll = 0.0;
    }

    /* From the paragraph at the top, a screen and a little more. */
    double y0 = text_scroll_y(t);
    size_t first = t->anchor;
    while (first > 0 && t->tops[first] > y0) --first;
    double y = t->tops[first] - y0 + t->top_pad;
    size_t sel_lo = t->sel_a < t->sel_b ? t->sel_a : t->sel_b, sel_hi = t->sel_a < t->sel_b ? t->sel_b : t->sel_a;
    for (size_t i = first; i < t->view.count && y < (double)win_h + t->lay_size * 4.0; ++i) {
        const rubraview_text_para_t *p = &t->view.paras[i];
        rubraview_text_block_t *b = text_block(app, t, i);
        double gap = p->gap ? size * spacing * 0.6 : 0.0;
        if (b) {
            rubraview_text_mark_t marks[10];
            size_t n = 0;
            size_t bs = text_block_start(t, i), be = p->start + p->len;   /* the block's own span, what it carries too */
            if (sel_hi > sel_lo && sel_hi > bs && sel_lo < be) {
                size_t a = sel_lo > bs ? sel_lo - bs : 0;
                size_t e = sel_hi < be ? sel_hi - bs : be - bs;
                marks[n++] = (rubraview_text_mark_t){ a, e - a, 0x903A6EA5u };
            }
            if (t->match && t->match_at >= bs && t->match_at < be) {
                marks[n++] = (rubraview_text_mark_t){ t->match_at - bs, t->match_len, (text_colour(app, "color_mark", 0xFFE0A030u) & 0x00FFFFFFu) | 0xC0000000u };
            }
            /* 1.25.0: web addresses tinted in the links' colour */
            uint32_t link = (text_colour(app, "color_link", 0xFF4A90D9u) & 0x00FFFFFFu) | 0x60000000u;
            for (size_t k = bs; k + 8 < be && n < 10; ++k) {
                const char *c = t->view.text + k;
                if (c[0] != 'h' || (memcmp(c, "http://", 7) != 0 && memcmp(c, "https://", 8) != 0)) continue;
                size_t e = k;
                while (e < be && (unsigned char)t->view.text[e] > ' ') ++e;
                marks[n++] = (rubraview_text_mark_t){ k - bs, e - k, link };
                k = e;
            }
            if (text_next_joined(t, i)) rubraview_pal_text_block_draw_upto(app->renderer, b, col_x - t->x_scroll, y + gap, t->heights[i] - gap, ink, marks, n);
            else rubraview_pal_text_block_draw(app->renderer, b, col_x - t->x_scroll, y + gap, ink, marks, n);
        }
        /* A height found now that differs from the guess: what is below moves, the top does not. */
        y += t->heights[i];
    }
    text_tops(t);
    /* Lay out what is just above too, so scrolling up does not jump. */
    for (size_t i = first, k = 0; i > 0 && k < 8; --i, ++k) (void)text_block(app, t, i - 1);
    /* Far ones go. */
    if (t->laid_out > TEXT_KEEP_BLOCKS) {
        for (size_t i = 0; i < t->view.count; ++i) {
            if (t->blocks[i] && (i + TEXT_KEEP_BLOCKS / 2 < first || i > first + TEXT_KEEP_BLOCKS / 2)) {
                rubraview_pal_text_block_free(t->blocks[i]);
                t->blocks[i] = NULL;
                t->laid_out--;
            }
        }
    }

    /* Where in the file, and moved there (RV-172): the lists' scroll bar, on a dark strip that any paper shows. */
    rubraview_scrollbar_t sb;
    if (scrollbar_of(app, RVSB_TEXT, &sb)) {
        rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ sb.area.x, sb.area.y, sb.area.width, sb.area.height },
                                       0x90303030u, sb.area.width * 0.5);
        draw_scrollbar(app, app->renderer, RVSB_TEXT, app->pointer_x, app->pointer_y);
    }
    if (t->autoscroll) {
        char line[64];
        int n = text_pages_mode(app)
            ? snprintf(line, sizeof(line), T("auto: a screen every %d s"), (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("autoscroll_page_seconds"))))
            : snprintf(line, sizeof(line), T("auto %d lines/min"), (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("autoscroll_speed"))));
        rubraview_pal_rect_t r = { (double)win_w - 220.0 * dpi, (double)win_h - 30.0 * dpi, 200.0 * dpi, 22.0 * dpi };
        if (n > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n }, r, 13.0 * dpi,
                                                  (ink & 0x00FFFFFFu) | 0x90000000u, RUBRAVIEW_TEXT_RIGHT);
    }
}

/* ---- moving ---- */

/* RV-172: the text's bar, in pixels; the end is where scrolling stops (half a screen past the last line). */
bool text_sb(app_state_t *app, rubraview_scrollbar_t *sb, double bw, double dpi) {
    text_page_t *t = app->text;
    if (!text_active(app) || t->view.count == 0 || t->view_h <= 0.0) return false;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    *sb = (rubraview_scrollbar_t){ .area = { (double)w - bw - 2.0 * dpi, t->top_pad + 2.0 * dpi, bw, t->view_h - 4.0 * dpi },
                                   .button = bw, .total = text_scroll_max(t) + t->view_h, .shown = t->view_h,
                                   .first = text_scroll_y(t), .step = text_line_h(app), .min_thumb = 24.0 * dpi };
    return true;
}

/* Moved by its bar: auto-scroll goes on from there, as after the wheel (owner, 2026-10-18). */
void text_sb_to(app_state_t *app, double first) {
    text_page_t *t = app->text;
    if (!t) return;
    text_scroll_to(t, first);
    t->autoscroll_carry = 0.0;
}

static bool text_at_end(text_page_t *t) { return text_scroll_y(t) >= text_scroll_max(t) - 0.5; }

/* How far through the text page on screen, 0-100; -1 when no text is on screen. */
int text_percent(const app_state_t *app) {
    text_page_t *t = app->text;
    if (!t || t->page != current_page_index(app)) return -1;
    double max = text_scroll_max(t);
    return max <= 0.5 ? 100 : (int)lround(100.0 * text_scroll_y(t) / max);
}

bool text_pages_mode(const app_state_t *app) {
    return lround(rubraview_settings_get(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("autoscroll_mode"))) == 1;
}

/* Auto-scroll (§5, the owner's first wish): each pass moves the text by
   the time since the last, so the speed holds whatever the frame rate. */
void text_tick(app_state_t *app, double now) {
    text_page_t *t = app->text;
    if (!t || !t->autoscroll) return;
    if (!text_active(app)) { t->autoscroll = false; return; }
    double dt = now - t->autoscroll_last;
    t->autoscroll_last = now;
    if (dt <= 0.0 || dt > 0.25) return;
    if (text_pages_mode(app)) {   /* RV-123 (RFC-0005 §5): a screen at a time, every N seconds */
        t->autoscroll_carry += dt;
        double every = rubraview_settings_get(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("autoscroll_page_seconds"));
        if (t->autoscroll_carry < (every >= 1.0 ? every : 10.0)) return;
        t->autoscroll_carry = 0.0;
        double step = t->view_h - text_line_h(app);   /* one line kept on screen, to read on from */
        text_scroll_by(app, step > text_line_h(app) ? step : text_line_h(app));
        if (text_at_end(t)) { t->autoscroll = false; osd_say(app, TU8("auto-scroll: the end")); }
        return;
    }
    double lpm = rubraview_settings_get(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("autoscroll_speed"));
    double px = (lpm > 0.0 ? lpm : 30.0) / 60.0 * text_line_h(app) * dt + t->autoscroll_carry;
    double whole = floor(px * 4.0) / 4.0;   /* quarter pixels: smooth, and no drift */
    t->autoscroll_carry = px - whole;
    text_scroll_by(app, whole);
    if (text_at_end(t)) {
        t->autoscroll = false;
        osd_say(app, TU8("auto-scroll: the end"));
    }
}

bool text_moving(const app_state_t *app) { return app->text && app->text->autoscroll; }

const char *text_encoding_shown(const app_state_t *app) {
    return app->text && app->text->page == current_page_index(app) ? rubraview_textenc_name(app->text->enc) : NULL;
}

static void text_speed(app_state_t *app, double factor) {
    if (text_pages_mode(app)) {   /* faster is fewer seconds a screen */
        double s = rubraview_settings_get(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("autoscroll_page_seconds"));
        double next = round((s >= 1.0 ? s : 10.0) / factor);
        if (next == s) next = s + (factor > 1.0 ? -1.0 : 1.0);
        if (next < 2.0) next = 2.0;
        if (next > 120.0) next = 120.0;
        rubraview_settings_set(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("autoscroll_page_seconds"), next);
        char line[80];
        int n = snprintf(line, sizeof(line), T("auto-scroll: a screen every %d s"), (int)next);
        if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
        return;
    }
    double lpm = rubraview_settings_get(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("autoscroll_speed"));
    double next = round((lpm > 0.0 ? lpm : 30.0) * factor);
    if (next == lpm) next = lpm + (factor > 1.0 ? 1.0 : -1.0);
    if (next < 2.0) next = 2.0;
    if (next > 600.0) next = 600.0;
    rubraview_settings_set(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("autoscroll_speed"), next);
    char line[80];
    int n = snprintf(line, sizeof(line), T("auto-scroll: %d lines a minute"), (int)next);
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
}

static void text_set(app_state_t *app, const char *key, double value, const char *format_text) {
    rubraview_settings_set(&app->settings, PROVEN_LIT("text"), cstr(key), value);
    double now = rubraview_settings_get(&app->settings, PROVEN_LIT("text"), cstr(key));
    char line[80];
    int n = snprintf(line, sizeof(line), format_text, (int)lround(now));
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
}

void text_find_step(app_state_t *app, bool forward) {
    text_page_t *t = app->text;
    if (!t) return;
    proven_u8str_view_t needle = app->rename_active && app->rename_is_find ? rubraview_textedit_text(&app->rename_edit)
                                                                : (proven_u8str_view_t){ .ptr = (const proven_byte_t *)t->find, .size = t->find_len };
    if (needle.size == 0) { osd_say(app, TU8("type what to find (Ctrl+F)")); return; }
    if ((const char *)needle.ptr != t->find && needle.size < sizeof(t->find)) { memcpy(t->find, needle.ptr, needle.size); t->find_len = needle.size; }
    size_t from = t->match ? (forward ? t->match_at + 1 : t->match_at)
                           : t->view.paras[t->anchor].start;
    size_t at = 0, len = 0;
    if (!rubraview_text_find(&t->view, needle, from, !forward, &at, &len)) {
        t->match = false;
        osd_say(app, TU8("not found"));
        return;
    }
    t->match = true;
    t->match_at = at;
    t->match_len = len;
    /* Its line a third of the way down. */
    size_t i = rubraview_text_view_para_at(&t->view, at);
    rubraview_text_block_t *b = text_block(app, t, i);
    double top = 0.0, h = 0.0;
    if (b) rubraview_pal_text_block_line_of(b, at - text_block_start(t, i), &top, &h);
    text_tops(t);
    text_scroll_to(t, t->tops[i] + top - t->view_h / 3.0);
    size_t which = 0, count = rubraview_text_count(&t->view, needle, at, &which);
    char line[64];
    int n = snprintf(line, sizeof(line), T("found %zu of %zu"), which, count);
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
}

static void text_copy(app_state_t *app) {
    text_page_t *t = app->text;
    size_t lo = t->sel_a < t->sel_b ? t->sel_a : t->sel_b, hi = t->sel_a < t->sel_b ? t->sel_b : t->sel_a;
    if (hi <= lo) { osd_say(app, TU8("nothing is selected: drag over the text, or Ctrl+A")); return; }
    osd_say(app, rubraview_pal_clipboard_set_text(app->window, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)t->view.text + lo, .size = hi - lo })
                     ? TU8("copied") : TU8("could not copy"));
}

void text_encoding_set(app_state_t *app, rubraview_textenc_t next) {
    text_page_t *t = app->text;
    if (!t) return;
    size_t source = t->view.count ? t->view.paras[t->anchor].source : 0;
    if (!text_with_bytes(app, t->page, next, true) || !text_rebuild(app)) {
        osd_say(app, TU8("that encoding is not on this computer"));
        return;
    }
    t->anchor = rubraview_text_view_para_of_source(&t->view, source < t->source_len ? source : 0);
    char line[96];
    int n = snprintf(line, sizeof(line), T("read as %s (E: the next)"), rubraview_textenc_name(t->enc));
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
    update_window_title(app);
}

int text_enc_now(const app_state_t *app) {
    return app->text ? (int)app->text->enc : 0;
}

static void text_encoding_next(app_state_t *app) {
    if (!app->text) return;
    rubraview_textenc_t next = (rubraview_textenc_t)((app->text->enc + 1) % RUBRAVIEW_TEXTENC_COUNT);
    text_encoding_set(app, next == RUBRAVIEW_TEXTENC_AUTO ? RUBRAVIEW_TEXTENC_UTF8 : next);
}

/* The text page's own actions; false for one that is not its. */
bool text_action(app_state_t *app, proven_u8str_view_t action) {
    if (action.size < 5 || memcmp(action.ptr, "text_", 5) != 0) return false;
    text_page_t *t = app->text;
    if (!t || !text_active(app)) { osd_say(app, TU8("for a text file")); return true; }
    double line = text_line_h(app);
    double screen = t->view_h > line * 2.0 ? t->view_h - line * 1.5 : line;
    if (proven_u8str_view_eq(action, PROVEN_LIT("text_autoscroll"))) {
        t->autoscroll = !t->autoscroll;
        t->autoscroll_last = rubraview_pal_time_now_seconds();
        t->autoscroll_carry = 0.0;
        if (t->autoscroll) text_speed(app, 1.0); else osd_say(app, TU8("auto-scroll stopped"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("text_faster"))) {
        text_speed(app, 1.1);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("text_slower"))) {
        text_speed(app, 1.0 / 1.1);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("text_line_down")) || proven_u8str_view_eq(action, PROVEN_LIT("text_line_up"))) {
        bool down = proven_u8str_view_eq(action, PROVEN_LIT("text_line_down"));
        if (t->autoscroll) text_speed(app, down ? 1.1 : 1.0 / 1.1);   /* while it runs, the arrows are its speed */
        else text_scroll_by(app, down ? line : -line);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("text_screen_down"))) {
        if (text_at_end(t)) { apply_intent(app, RUBRAVIEW_INTENT_NEXT_PAGE); return true; }   /* as a book's last page */
        text_scroll_by(app, screen);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("text_screen_up"))) {
        if (text_scroll_y(t) <= 0.5) { apply_intent(app, RUBRAVIEW_INTENT_PREV_PAGE); return true; }
        text_scroll_by(app, -screen);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("text_top"))) {
        text_scroll_to(t, 0.0);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("text_bottom"))) {
        text_scroll_to(t, 1e18);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("text_bigger")) || proven_u8str_view_eq(action, PROVEN_LIT("text_smaller"))) {
        double pt = rubraview_settings_get(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("size"));
        text_set(app, "size", pt + (proven_u8str_view_eq(action, PROVEN_LIT("text_bigger")) ? 1.0 : -1.0), T("text size %d pt"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("text_spacing_more")) || proven_u8str_view_eq(action, PROVEN_LIT("text_spacing_less"))) {
        double pct = rubraview_settings_get(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("line_spacing"));
        text_set(app, "line_spacing", pct + (proven_u8str_view_eq(action, PROVEN_LIT("text_spacing_more")) ? 10.0 : -10.0), T("line spacing %d %%"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("text_breaks"))) {
        int b = ((int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("breaks"))) + 1) % RUBRAVIEW_TEXT_BREAKS_COUNT;
        rubraview_settings_set(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("breaks"), (double)b);
        if (!text_rebuild(app)) { osd_say(app, TU8("out of memory")); return true; }
        osd_say(app, b == 0 ? TU8("line breaks as they are") : b == 1 ? TU8("all in one run, no line breaks")
                     : b == 2 ? TU8("blank lines dropped") : TU8("paragraphs found (a sentence's end and more line breaks)"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("text_indent"))) {
        bool trim = rubraview_settings_get(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("trim_lead")) < 0.5;
        rubraview_settings_set(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("trim_lead"), trim ? 1.0 : 0.0);
        if (!text_rebuild(app)) { osd_say(app, TU8("out of memory")); return true; }
        osd_say(app, trim ? TU8("indents removed") : TU8("indents kept"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("text_wrap"))) {
        bool wrap = !text_wraps(app);
        rubraview_settings_set(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("wrap"), wrap ? 1.0 : 0.0);
        osd_say(app, wrap ? TU8("word wrap: to the window") : TU8("word wrap off: long lines run on (Shift+wheel)"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("text_find"))) {
        if (app->rename_active) rename_end(app);
        app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
        rubraview_textedit_set(&app->rename_edit, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)t->find, .size = t->find_len });
        rubraview_textedit_select_all(&app->rename_edit);
        app->rename_active = true;
        app->rename_is_find = true;
        app->rename_composing_length = 0;
        rubraview_pal_window_text_input(app->window, true);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("text_find_next")) || proven_u8str_view_eq(action, PROVEN_LIT("text_find_prev"))) {
        text_find_step(app, proven_u8str_view_eq(action, PROVEN_LIT("text_find_next")));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("text_encoding"))) {
        text_encoding_next(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("text_edit_external"))) {
        /* 1.25.0 (owner, 2026-10-20): the file in the editor Settings › Text names, Notepad without one */
        proven_u8str_view_t path = current_file_path(app);
        if (path.size == 0) { osd_say(app, TU8("a page inside an archive has no file of its own to edit")); return true; }
        proven_u8str_view_t editor = rubraview_settings_get_text(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("editor"));
        char line[4096];
        size_t pos = 0;
        line[0] = '\0';
        append_arg(line, sizeof(line), &pos, editor.size > 0 ? editor : PROVEN_LIT("notepad.exe"));
        append_arg(line, sizeof(line), &pos, path);
        if (!rubraview_pal_process_start((proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = pos })) osd_say(app, TU8("the editor could not be started"));
        else osd_say(app, TU8("opened in the editor: Reload reads it again"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("text_reload"))) {
        /* the file read again, in the encoding it is read in, at the same place */
        size_t source = t->view.count ? t->view.paras[t->anchor].source : 0;
        if (!text_with_bytes(app, t->page, t->enc, false) || !text_rebuild(app)) { osd_say(app, TU8("could not read it again")); return true; }
        t->anchor = rubraview_text_view_para_of_source(&t->view, source);
        osd_say(app, TU8("read again"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("text_copy"))) {
        text_copy(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("text_select_all"))) {
        t->sel_a = 0;
        t->sel_b = t->view.len;
        osd_say(app, TU8("all selected: Ctrl+C copies"));
    } else {
        return false;
    }
    note_activity(app);
    return true;
}

/* The wheel over a text page: lines; with Ctrl the size; while auto-scroll runs, its speed. */
bool text_wheel(app_state_t *app, double notches, uint32_t mods) {
    if (!text_active(app)) return false;
    text_page_t *t = app->text;
    if (mods & RUBRAVIEW_MOD_CTRL) { text_action(app, notches > 0 ? PROVEN_LIT("text_bigger") : PROVEN_LIT("text_smaller")); return true; }
    if (mods & RUBRAVIEW_MOD_ALT) { text_action(app, notches > 0 ? PROVEN_LIT("text_spacing_more") : PROVEN_LIT("text_spacing_less")); return true; }
    if (mods & RUBRAVIEW_MOD_SHIFT) {
        if (!text_wraps(app)) { t->x_scroll -= notches * text_px(app) * 4.0; if (t->x_scroll < 0.0) t->x_scroll = 0.0; }
        return true;
    }
    /* The wheel moves the text, auto-scroll or not; it does not change the speed (owner, 2026-10-18). */
    text_scroll_by(app, -notches * text_line_h(app) * 3.0);
    if (t->autoscroll) t->autoscroll_carry = 0.0;
    note_activity(app);
    return true;
}

/* The place in the view's text under a point of the window. */
static size_t text_offset_at(app_state_t *app, double x, double y) {
    text_page_t *t = app->text;
    double doc_y = text_scroll_y(t) + (y - t->top_pad);
    if (doc_y < 0.0) doc_y = 0.0;
    size_t i = text_para_at_y(t, doc_y);
    rubraview_text_block_t *b = text_block(app, t, i);
    const rubraview_text_para_t *p = &t->view.paras[i];
    double gap = p->gap ? t->lay_size * t->lay_spacing * 0.6 : 0.0;
    size_t in = b ? rubraview_pal_text_block_hit(b, x - t->col_x + t->x_scroll, doc_y - t->tops[i] - gap) : 0;
    size_t bs = text_block_start(t, i);
    return bs + (bs + in < p->start + p->len ? in : p->start + p->len - bs);
}

/* A press on the text starts a selection. A click (no drag) in the middle of
   the window starts or stops auto-scroll (owner, 2026-10-18); a drag stops it. */
bool text_press(app_state_t *app, double x, double y, uint32_t mods) {
    if (!text_active(app)) return false;
    if (scrollbar_press(app, RVSB_TEXT, x, y)) return true;   /* RV-172: its bar */
    text_page_t *t = app->text;
    t->press_x = x;
    t->press_y = y;
    size_t at = text_offset_at(app, x, y);
    if (!(mods & RUBRAVIEW_MOD_SHIFT)) t->sel_a = at;
    t->sel_b = at;
    t->selecting = true;
    return true;
}

void text_drag(app_state_t *app, double x, double y) {
    text_page_t *t = app->text;
    if (!t || !t->selecting) return;
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    /* Past the top or the bottom the text comes to the pointer. */
    if (y < t->top_pad) text_scroll_by(app, -text_line_h(app) * 0.5);
    else if (y > (double)win_h - 4.0) text_scroll_by(app, text_line_h(app) * 0.5);
    t->sel_b = text_offset_at(app, x, y);
}

bool text_release(app_state_t *app, double x, double y) {
    text_page_t *t = app->text;
    if (!t || !t->selecting) return false;
    t->selecting = false;
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    bool click = t->sel_a == t->sel_b && fabs(x - t->press_x) < 6.0 && fabs(y - t->press_y) < 6.0;
    bool middle = t->press_x > win_w / 3.0 && t->press_x < win_w * 2.0 / 3.0 && t->press_y > win_h / 3.0 && t->press_y < win_h * 2.0 / 3.0;
    if (click && middle) handle_action(app, PROVEN_LIT("text_autoscroll"));
    else if (!click && t->autoscroll) { t->autoscroll = false; osd_say(app, TU8("auto-scroll stopped")); }   /* a selection */
    return true;
}
