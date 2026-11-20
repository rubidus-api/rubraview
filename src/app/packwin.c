/* RV-294 (owner, 2026-11-16: "압축할 때 이것이 메인 창의 카드 창 형태로 뜨는데, 압축을 실행하는 분리된 프로세스에서 별개의 창으로
   떠야 할 것 같고, 그리고 화면 크기나 창 크기에 따라서 스크롤이 가능해야 할 것 같아요. 디자인 자체도 공간효율적으로"): the Compress
   window in the compression's own process. Its rows as the viewer's card had them (D-260, D-261, RV-248) — the name, the
   format, the level, the codec, solid, the split size, the password twice, hidden names — laid out tighter: a row's name
   beside its chips when there is room (over them when narrow), the chips flowing as inline words, the typed fields the
   one edit control (RV-293); taller than the window, it scrolls (the wheel, a bar, the field typed in brought into view);
   Compress and Cancel stay at its foot. */
#include "app.h"
#include "packwin.h"
#include "rubraview/archive_job.h"
#include "rubraview/fontspec.h"

static const char *const LEVEL_WORD[RUBRAVIEW_PACK_LEVEL_STEPS] = { N_("Store"), N_("Fastest"), N_("Fast"), N_("Normal"), N_("Maximum"), N_("Ultra") };
static const char *const SPLIT_WORD[] = { N_("None"), "10 MB", "100 MB", "700 MB", "4 GB" };
static const uint64_t SPLIT_MIB[] = { 0, 10, 100, 700, 4095 };
enum { SPLITS = 5 };

static const char *method_word(rubraview_pack_method_t m) {
    switch (m) {
        case RUBRAVIEW_PACK_METHOD_DEFLATE: return "Deflate";
        case RUBRAVIEW_PACK_METHOD_BZIP2:   return "BZip2";
        case RUBRAVIEW_PACK_METHOD_LZMA:    return "LZMA";
        case RUBRAVIEW_PACK_METHOD_LZMA2:   return "LZMA2";
        case RUBRAVIEW_PACK_METHOD_PPMD:    return "PPMd";
        case RUBRAVIEW_PACK_METHOD_ZSTD:    return "Zstandard";
        default:                            return "";
    }
}

static bool ends_ci(proven_u8str_view_t t, const char *tail) {
    size_t n = proven_cstr_len(tail);
    if (t.size < n) return false;
    for (size_t i = 0; i < n; ++i) if ((t.ptr[t.size - n + i] | 0x20) != (tail[i] | 0x20)) return false;
    return true;
}

bool packwin_7z(const packwin_t *p) { return ends_ci(rubraview_textedit_text(&p->field[PACKWIN_NAME].te), ".7z"); }

static rubraview_pack_format_t format_of(const packwin_t *p) { return packwin_7z(p) ? RUBRAVIEW_PACK_7Z : RUBRAVIEW_PACK_ZIP; }

rubraview_pack_method_t packwin_method(const packwin_t *p) {
    rubraview_pack_format_t f = format_of(p);
    if (p->method != RUBRAVIEW_PACK_METHOD_DEFAULT && rubraview_pack_method_ready(f, p->method)) return p->method;
    rubraview_pack_method_t first[8];
    return rubraview_pack_methods(f, first, 8) ? first[0] : RUBRAVIEW_PACK_METHOD_DEFAULT;
}

/* The name's ending made the format's (".zip" ⇄ ".7z"); a codec the format does not take goes back to its first. */
static void set_format(packwin_t *p, bool sevenz) {
    if (packwin_7z(p) == sevenz) return;
    proven_u8str_view_t t = rubraview_textedit_text(&p->field[PACKWIN_NAME].te);
    char s[sizeof p->buf[0]];
    if (t.size + 5 >= sizeof s) return;
    size_t n = t.size;
    if (ends_ci(t, ".zip")) n -= 4; else if (ends_ci(t, ".7z")) n -= 3;
    proven_sys_mem_copy(s, t.ptr, n);
    rubraview_fmt(s + n, sizeof s - n, sevenz ? ".7z" : ".zip");
    rubraview_editctl_set(&p->field[PACKWIN_NAME], proven_u8str_view_from_cstr(s));
    if (!rubraview_pack_method_fits(sevenz ? RUBRAVIEW_PACK_7Z : RUBRAVIEW_PACK_ZIP, p->method)) p->method = RUBRAVIEW_PACK_METHOD_DEFAULT;
}

void packwin_init(packwin_t *p, proven_u8str_view_t name, int level, rubraview_pack_method_t method, bool solid_off, uint64_t split, bool names_hidden) {
    *p = (packwin_t){ .level = level, .method = method, .solid_off = solid_off, .names_hidden = names_hidden, .focus = PACKWIN_NAME };
    for (int f = 0; f < PACKWIN_FIELDS; ++f) editbox_begin(&p->field[f], p->buf[f], sizeof p->buf[f], false);
    rubraview_editctl_set(&p->field[PACKWIN_NAME], name);
    rubraview_editctl_select_line(&p->field[PACKWIN_NAME], 0);
    char s[48];
    size_t n = rubraview_pack_split_text(s, sizeof s, split);
    rubraview_editctl_set(&p->field[PACKWIN_SPLIT], (proven_u8str_view_t){ .ptr = (const proven_byte_t *)s, .size = n });
    p->view[PACKWIN_PW].secret = p->view[PACKWIN_AGAIN].secret = true;
}

void packwin_free(packwin_t *p) {
    for (int f = 0; f < PACKWIN_FIELDS; ++f) {
        secure_wipe(rubraview_mem_mut(p->buf[f], sizeof p->buf[f]));
        rubraview_editctl_free(&p->field[f]);
    }
}

bool packwin_show(packwin_t *p, proven_arena_t *arena, proven_u8str_view_t ui_font) {
    rubraview_window_config_t config = { .title = T("Rubraview - compress"), .width = 620, .height = 460 };
    p->window = rubraview_pal_window_create(arena, &config);
    if (!p->window) return false;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(p->window, &w, &h);
    p->renderer = rubraview_pal_render_create(arena, rubraview_pal_window_native_handle(p->window), w, h);
    if (!p->renderer) { rubraview_pal_window_destroy(p->window); p->window = NULL; return false; }
    rubraview_font_spec_t face = rubraview_font_spec_parse(ui_font);
    rubraview_pal_render_set_ui_font(p->renderer, face.family, face.weight, face.italic);
    rubraview_pal_window_text_input(p->window, true);
    p->open = p->dirty = true;
    return true;
}

void packwin_close(packwin_t *p) {
    if (!p->open) return;
    if (p->renderer) rubraview_pal_render_destroy(p->renderer);
    if (p->window) rubraview_pal_window_destroy(p->window);
    p->renderer = NULL;
    p->window = NULL;
    p->open = false;
}

/* ---- the layout, drawn or only measured ---- */

#define PW_TEXT 0xFFE6E6E6u
#define PW_DIM  0xFF9AA2ACu
#define PW_ON   0xFF3E8BF0u

enum { ROW_FORMAT, ROW_LEVEL, ROW_CODEC, ROW_SOLID, ROW_SPLIT, ROW_PW, ROW_AGAIN, ROW_NAMES, ROWS };

static void say(packwin_t *p, const char *words) {
    rubraview_fmt(p->note, sizeof p->note, "{}", PROVEN_ARG(words));
    p->dirty = true;
}

/* One pass over the rows from `top` (the scroll taken off) in the window's width: their height; drawn with `draw`,
   their chips and fields kept for the pointer. */
static double pass(packwin_t *p, double top, double width, double dpi, bool draw) {
    rubraview_renderer_t *r = p->renderer;
    bool sevenz = packwin_7z(p);
    rubraview_pack_format_t format = format_of(p);
    rubraview_pack_method_t methods[8], lit = packwin_method(p);
    size_t method_n = rubraview_pack_methods(format, methods, 8);
    double pad = 12.0 * dpi, line_h = 32.0 * dpi, gap = 5.0 * dpi, inner = width - 2.0 * pad - 14.0 * dpi;   /* room for the bar */
    bool wide = inner >= 400.0 * dpi;
    double label_w = wide ? 92.0 * dpi : 0.0, room = inner - label_w, fs = 13.0 * dpi;
    uint64_t split_now = 0;
    bool split_reads = rubraview_pack_split_parse(rubraview_textedit_text(&p->field[PACKWIN_SPLIT].te), &split_now);
    double y = top;
    /* the name: a whole line */
    if (draw) rubraview_pal_render_draw_text(r, TU8("Name (its ending says ZIP or 7z)"), (rubraview_pal_rect_t){ pad, y, inner, 18.0 * dpi }, 12.0 * dpi, PW_DIM, RUBRAVIEW_TEXT_LEFT);
    y += 18.0 * dpi;
    p->view[PACKWIN_NAME].box = (rubraview_pal_rect_t){ pad, y, inner, line_h - 4.0 * dpi };
    p->view[PACKWIN_NAME].size = 14.0 * dpi;
    p->field_y[PACKWIN_NAME] = y;
    y += line_h + 2.0 * dpi;
    if (draw) p->chips = 0;
    static const char *const ROW_WORD[ROWS] = { N_("Format"), N_("Level"), N_("Codec"), N_("Solid"), N_("Split"), N_("Password"), N_("Again"), N_("Names") };
    for (int row = 0; row < ROWS; ++row) {
        const char *words[8];
        bool on[8] = { 0 }, off[8] = { 0 };
        int n = 0, field = -1;
        switch (row) {
            case ROW_FORMAT: words[n] = "ZIP"; on[n++] = !sevenz; words[n] = "7z"; on[n++] = sevenz; break;
            case ROW_LEVEL: for (int k = 0; k < RUBRAVIEW_PACK_LEVEL_STEPS; ++k) { words[n] = rubraview_tr(LEVEL_WORD[k]); on[n++] = p->level == k; } break;
            case ROW_CODEC:
                for (size_t k = 0; k < method_n && n < 8; ++k) {
                    words[n] = method_word(methods[k]);
                    on[n] = p->level != 0 && methods[k] == lit;
                    off[n++] = p->level == 0 || !rubraview_pack_method_ready(format, methods[k]);
                }
                break;
            case ROW_SOLID:
                words[n] = T("Solid"); on[n] = sevenz && !p->solid_off; off[n++] = !sevenz;
                words[n] = T("File by file"); on[n] = !sevenz || p->solid_off; off[n++] = !sevenz;
                break;
            case ROW_SPLIT:
                field = PACKWIN_SPLIT;
                for (int k = 0; k < SPLITS; ++k) {
                    words[n] = k == 0 ? rubraview_tr(SPLIT_WORD[0]) : SPLIT_WORD[k];
                    on[n] = split_reads && split_now == (SPLIT_MIB[k] << 20);
                    off[n++] = k > 0 && !rubraview_pack_split_supported(format);
                }
                break;
            case ROW_PW: field = PACKWIN_PW; break;
            case ROW_AGAIN: field = PACKWIN_AGAIN; break;
            default:
                words[n] = T("Readable"); on[n] = !sevenz || !p->names_hidden; off[n++] = !sevenz;
                words[n] = T("Hidden by the password"); on[n] = sevenz && p->names_hidden; off[n++] = !sevenz || !rubraview_pack_names_hidden_supported(format);
                break;
        }
        double widths[10];
        int items = 0;
        if (field >= 0) widths[items++] = fmin((field == PACKWIN_SPLIT ? 120.0 : 260.0) * dpi, room);
        for (int k = 0; k < n; ++k) {
            double tw = 0.0;
            (void)rubraview_pal_render_measure_text(r, cstr(words[k]), fs, &tw);
            widths[items++] = fmin(fmax(tw + 18.0 * dpi, 48.0 * dpi), room);
        }
        uint32_t line_of[10];
        double x_of[10];
        size_t lines = rubraview_flow_inline(widths, (size_t)items, gap, room, line_of, x_of);
        if (draw) rubraview_pal_render_draw_text(r, cstr(rubraview_tr(ROW_WORD[row])),
                                                 (rubraview_pal_rect_t){ pad, y, wide ? label_w : inner, wide ? line_h - 4.0 * dpi : 18.0 * dpi },
                                                 12.0 * dpi, PW_DIM, RUBRAVIEW_TEXT_LEFT);
        if (!wide) y += 18.0 * dpi;
        for (int i = 0; i < items; ++i) {
            rubraview_pal_rect_t b = { pad + label_w + x_of[i], y + (double)line_of[i] * line_h, widths[i], line_h - 4.0 * dpi };
            if (field >= 0 && i == 0) {
                p->view[field].box = b;
                p->view[field].size = 14.0 * dpi;
                p->field_y[field] = b.y;
                continue;
            }
            int k = field >= 0 ? i - 1 : i;
            if (!draw) continue;
            bool over = pal_rect_has(b, p->px, p->py);
            rubraview_pal_render_fill_rect(r, b, on[k] ? PW_ON : over && !off[k] ? 0x40FFFFFFu : 0xFF2A2F36u, 3.0 * dpi);
            rubraview_pal_render_draw_text(r, cstr(words[k]), b, fs, off[k] ? 0x60F0F0F0u : PW_TEXT, RUBRAVIEW_TEXT_CENTER);
            if (p->chips < PACKWIN_CHIPS) { p->chip[p->chips] = b; p->chip_kind[p->chips] = row * 16 + k; p->chip_off[p->chips++] = off[k]; }
        }
        y += (double)(lines ? lines : 1) * line_h;
    }
    return y - top + pad;
}

/* D-265, D-263, RV-294: what the choices mean, one line beside the buttons: the memory LZMA's top levels take, the
   password's kind, Store, who opens the format; while the split size is typed, how one is written, and the set's names. */
static const char *guide(const packwin_t *p) {
    bool sevenz = packwin_7z(p);
    rubraview_pack_method_t lit = packwin_method(p);
    bool lzma = lit == RUBRAVIEW_PACK_METHOD_LZMA || lit == RUBRAVIEW_PACK_METHOD_LZMA2;
    uint64_t split = 0;
    bool split_reads = rubraview_pack_split_parse(rubraview_textedit_text(&p->field[PACKWIN_SPLIT].te), &split);
    if (p->focus == PACKWIN_SPLIT) return T("Split: a size such as 700M, 4G or 10,000,000 (K, M, G, T, P count by 1024); empty for none");
    if (lzma && p->level == 5) return T("Ultra: a large file may take about 3 GB of memory to pack, as in 7-Zip");
    if (lzma && p->level == 4) return T("Maximum: a large file may take about 1.5 GB of memory to pack, as in 7-Zip");
    if (p->field[PACKWIN_PW].te.len || p->focus == PACKWIN_PW || p->focus == PACKWIN_AGAIN)
        return sevenz ? (p->names_hidden ? T("Hidden names: the archive is not even listed without its password") : T("Password: AES-256 (7-Zip's own)"))
                      : T("Password: AES-256 (WinZip's; Windows' own ZIP cannot open it)");
    if (split_reads && split > 0) return sevenz ? T("Split: name.7z.001, .002 ... (open the .001)") : T("Split: name.z01, .z02 ... name.zip (open the .zip)");
    if (p->level == 0) return T("Store: the files go in as they are, the codec unused");
    if (!sevenz) return rubraview_pack_method_plain(RUBRAVIEW_PACK_ZIP, lit) ? T("ZIP with Deflate: Windows opens it too")
                                                                             : T("ZIP with this codec: 7-Zip, Bandizip and this viewer open it, Windows' own does not");
    return T("7z: 7-Zip, Bandizip and this viewer open it");
}

void packwin_draw(packwin_t *p) {
    if (!p->open || !p->dirty) return;
    p->dirty = false;
    rubraview_renderer_t *r = p->renderer;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(p->window, &w, &h);
    double dpi = rubraview_pal_window_dpi_scale(p->window), pad = 12.0 * dpi;
    /* RV-315: the buttons in rows that wrap when the window is narrow; the foot as tall as they make it */
    const char *bw[2] = { T("Compress"), T("Cancel") };
    double bh = 34.0 * dpi;
    double block = dlgpage_buttons(r, bw, 2, pad, (double)w - pad, 0.0, 14.0 * dpi, 18.0 * dpi, 72.0 * dpi, bh, 8.0 * dpi, p->button);
    double foot = block + 2.0 * pad + (p->note[0] ? 20.0 * dpi : 0.0);
    double head = 30.0 * dpi, view_h = (double)h - head - foot;
    rubraview_pal_render_begin(r, 0xFF1B1E23u);
    /* measured, the scroll kept within it, then drawn from where it is */
    p->content = pass(p, 0.0, (double)w, dpi, false);
    double most = fmax(0.0, p->content - view_h);
    if (p->follow >= 0) {   /* the field typed in brought into view */
        double fy = p->field_y[p->follow], fh = 32.0 * dpi;
        if (fy - p->scroll < 0.0) p->scroll = fy;
        if (fy + fh - p->scroll > view_h) p->scroll = fy + fh - view_h;
        p->follow = -1;
    }
    p->scroll = fmin(fmax(p->scroll, 0.0), most);
    (void)pass(p, head - p->scroll, (double)w, dpi, true);
    for (int f = 0; f < PACKWIN_FIELDS; ++f) {
        editbox_view_t *v = &p->view[f];
        if (v->box.y + v->box.height <= head || v->box.y >= head + view_h) continue;   /* scrolled away: no clip, not drawn */
        rubraview_pal_render_fill_rect(r, v->box, 0xFF101215u, 3.0 * dpi);
        rubraview_pal_render_stroke_rect(r, v->box, f == p->focus ? PW_ON : 0xFF4A525Cu, 1.0, 3.0 * dpi);
        if (f != p->focus && p->field[f].te.len == 0) {
            const char *hint = f == PACKWIN_PW ? T("none") : f == PACKWIN_SPLIT ? T("None") : "";
            rubraview_pal_render_draw_text(r, cstr(hint), (rubraview_pal_rect_t){ v->box.x + 8.0 * dpi, v->box.y, v->box.width, v->box.height }, 12.0 * dpi, 0x80F0F0F0u, RUBRAVIEW_TEXT_LEFT);
            continue;
        }
        editbox_draw(r, &p->field[f], v, f == p->focus ? cstr(p->composing) : PROVEN_LIT(""), f == p->focus, PW_TEXT, dpi);
    }
    /* what is over and under the rows: the title, the bar, the notes and the buttons */
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ 0.0, 0.0, (double)w, head }, 0xFF1B1E23u, 0.0);
    char title[200];
    int tn = rubraview_fmt(title, sizeof title, T("Compress {} into a new archive"), PROVEN_ARG(p->what));
    if (tn > 0) rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)title, .size = (size_t)tn },
                                               (rubraview_pal_rect_t){ pad, 4.0 * dpi, (double)w - 2.0 * pad, head - 6.0 * dpi }, 15.0 * dpi, PW_TEXT, RUBRAVIEW_TEXT_LEFT);
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ 0.0, (double)h - foot, (double)w, foot }, 0xFF1B1E23u, 0.0);
    if (most > 0.5) {   /* a bar: where the rows shown are among all */
        double track = view_h, thumb = fmax(20.0 * dpi, track * view_h / p->content), at = head + (track - thumb) * (p->scroll / most);
        rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ (double)w - 10.0 * dpi, head, 6.0 * dpi, track }, 0x20FFFFFFu, 3.0 * dpi);
        rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ (double)w - 10.0 * dpi, at, 6.0 * dpi, thumb }, 0x90F0F0F0u, 3.0 * dpi);
        p->bar = (rubraview_pal_rect_t){ (double)w - 14.0 * dpi, head, 14.0 * dpi, track };
        p->bar_most = most;
    } else {
        p->bar = (rubraview_pal_rect_t){0};
    }
    if (p->note[0]) rubraview_pal_render_draw_text(r, cstr(p->note), (rubraview_pal_rect_t){ pad, (double)h - foot + 4.0 * dpi, (double)w - 2.0 * pad, 18.0 * dpi },
                                                   12.0 * dpi, 0xFFE8C46Fu, RUBRAVIEW_TEXT_LEFT);
    double by = (double)h - pad - block, bx = p->button[0].y == p->button[1].y ? p->button[0].x - 8.0 * dpi : pad;   /* the guide beside one row */
    for (int i = 0; i < 2; ++i) {
        p->button[i].y += by;
        rubraview_pal_rect_t b = p->button[i];
        bool over = pal_rect_has(b, p->px, p->py);
        rubraview_pal_render_fill_rect(r, b, i == 0 ? (over ? 0xFF4F9CF7u : PW_ON) : over ? 0x50FFFFFFu : 0xFF2A2F36u, 4.0 * dpi);
        rubraview_pal_render_draw_text(r, cstr(bw[i]), b, 14.0 * dpi, PW_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
    if (bx - pad > 80.0 * dpi) {   /* the guide in the room the buttons leave, smaller rather than cut off */
        proven_u8str_view_t g = cstr(guide(p));
        double gs = 12.0 * dpi, gw = 0.0, room = bx - pad;
        if (rubraview_pal_render_measure_text(r, g, gs, &gw) && gw > room) gs = fmax(9.0 * dpi, gs * room / gw);
        rubraview_pal_render_draw_text(r, g, (rubraview_pal_rect_t){ pad, by, room, bh }, gs, PW_DIM, RUBRAVIEW_TEXT_LEFT);
    }
    if (!rubraview_pal_render_end(r)) p->dirty = true;
}

/* ---- input ---- */

static void focus_on(packwin_t *p, int f) {
    p->focus = f;
    p->composing[0] = '\0';
    p->follow = f;
    rubraview_pal_window_text_ime(p->window, f == PACKWIN_NAME);   /* D-262: a new archive's password in plain letters */
    p->dirty = true;
}

static void chip(packwin_t *p, int row, int k, bool off) {
    bool sevenz = packwin_7z(p);
    if (off) {
        say(p, row == ROW_SOLID ? T("ZIP packs each file on its own") : row == ROW_NAMES && !sevenz ? T("ZIP keeps its names readable; 7z can hide them")
               : row == ROW_CODEC && p->level == 0 ? T("Store keeps the files as they are: no codec") : T("FultaArc does not write that yet"));
        return;
    }
    switch (row) {
        case ROW_FORMAT: set_format(p, k == 1); break;
        case ROW_LEVEL: p->level = k; break;
        case ROW_CODEC: {
            rubraview_pack_method_t m[8];
            size_t n = rubraview_pack_methods(sevenz ? RUBRAVIEW_PACK_7Z : RUBRAVIEW_PACK_ZIP, m, 8);
            if ((size_t)k < n) p->method = m[k];
            break;
        }
        case ROW_SOLID: p->solid_off = k == 1; break;
        case ROW_SPLIT: {
            char s[48];
            size_t n = rubraview_pack_split_text(s, sizeof s, k > 0 && k < SPLITS ? SPLIT_MIB[k] << 20 : 0u);
            rubraview_editctl_set(&p->field[PACKWIN_SPLIT], (proven_u8str_view_t){ .ptr = (const proven_byte_t *)s, .size = n });
            break;
        }
        case ROW_NAMES: p->names_hidden = k == 1; break;
        default: break;
    }
    p->note[0] = '\0';
    p->dirty = true;
}

/* Enter or Compress: the passwords the same, a hidden list with a password, a split size that reads. */
static bool ready(packwin_t *p) {
    proven_u8str_view_t a = rubraview_textedit_text(&p->field[PACKWIN_PW].te), b = rubraview_textedit_text(&p->field[PACKWIN_AGAIN].te);
    if (rubraview_textedit_text(&p->field[PACKWIN_NAME].te).size == 0) { say(p, T("not compressed: no name")); focus_on(p, PACKWIN_NAME); return false; }
    if (p->names_hidden && packwin_7z(p) && a.size == 0) { say(p, T("hidden names need a password: type one")); focus_on(p, PACKWIN_PW); return false; }
    if (a.size != b.size || proven_sys_mem_cmp(a.ptr, b.ptr, a.size) != 0) { say(p, T("the two passwords differ: type it again")); focus_on(p, a.size ? PACKWIN_AGAIN : PACKWIN_PW); return false; }
    if (!rubraview_pack_split_parse(rubraview_textedit_text(&p->field[PACKWIN_SPLIT].te), &p->split)) {
        say(p, T("the split size is a number with K, M, G, T or P: 700M, 4G, 10,000,000")); focus_on(p, PACKWIN_SPLIT); return false;
    }
    if (p->split > 0 && p->split < 65536u) { say(p, T("a split part is 64K at least")); focus_on(p, PACKWIN_SPLIT); return false; }
    return true;
}

static void text_in(packwin_t *p, proven_u8str_view_t t) {
    if (p->focus == PACKWIN_PW || p->focus == PACKWIN_AGAIN) {   /* D-262: plain letters only, or the archive could not be opened elsewhere */
        char kept[512];
        bool dropped = false;
        size_t n = rubraview_password_keep_plain(t, kept, sizeof kept, &dropped);
        editbox_text(&p->field[p->focus], (proven_u8str_view_t){ .ptr = (const proven_byte_t *)kept, .size = n }, true);
        secure_wipe(rubraview_mem_mut(kept, sizeof kept));
        if (dropped) say(p, T("a new archive's password takes English letters, digits and symbols only"));
    } else {
        editbox_text(&p->field[p->focus], t, true);
    }
}

int packwin_pump(packwin_t *p) {
    int verdict = 0;
    rubraview_window_event_t e;
    while (p->open && rubraview_pal_window_poll_event(p->window, &e)) {
        switch (e.kind) {
            case RUBRAVIEW_WINDOW_EVENT_CLOSE: verdict = -1; break;
            case RUBRAVIEW_WINDOW_EVENT_RESIZE: rubraview_pal_render_resize(p->renderer, e.resize.width, e.resize.height); p->dirty = true; break;
            case RUBRAVIEW_WINDOW_EVENT_PAINT: p->dirty = true; break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_WHEEL: p->scroll -= e.mouse.wheel_delta * 48.0 * rubraview_pal_window_dpi_scale(p->window); p->dirty = true; break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE:
                p->px = e.mouse.x; p->py = e.mouse.y;
                if (p->bar_drag) p->scroll = p->bar_most * (e.mouse.y - p->bar.y) / fmax(1.0, p->bar.height);
                else (void)editbox_pointer_move(p->renderer, &p->field[p->focus], &p->view[p->focus], e.mouse.x);
                p->dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_UP: p->bar_drag = false; p->view[p->focus].dragging = false; break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN: {
                if (e.mouse.button != RUBRAVIEW_MOUSE_LEFT) break;
                double x = e.mouse.x, y = e.mouse.y;
                if (p->bar.width > 0.0 && pal_rect_has(p->bar, x, y)) { p->bar_drag = true; p->scroll = p->bar_most * (y - p->bar.y) / fmax(1.0, p->bar.height); p->dirty = true; break; }
                if (pal_rect_has(p->button[0], x, y)) { if (ready(p)) verdict = 1; break; }
                if (pal_rect_has(p->button[1], x, y)) { verdict = -1; break; }
                for (int f = 0; f < PACKWIN_FIELDS; ++f)
                    if (pal_rect_has(p->view[f].box, x, y)) {
                        if (f != p->focus) focus_on(p, f);
                        (void)editbox_pointer_down(p->renderer, &p->field[f], &p->view[f], x, y, (e.mouse.modifiers & RUBRAVIEW_MOD_SHIFT) != 0);
                        p->dirty = true;
                        goto pressed;
                    }
                for (int i = 0; i < p->chips; ++i)
                    if (pal_rect_has(p->chip[i], x, y)) { chip(p, p->chip_kind[i] / 16, p->chip_kind[i] % 16, p->chip_off[i]); break; }
            pressed:
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_TEXT:
                p->composing[0] = '\0';
                text_in(p, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)e.text.utf8, .size = e.text.length });
                p->dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_COMPOSITION: {
                size_t n = e.text.length < sizeof p->composing - 1 ? e.text.length : sizeof p->composing - 1;
                proven_sys_mem_copy(p->composing, e.text.utf8, n);
                p->composing[n] = '\0';
                p->dirty = true;
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN: {
                rubraview_key_combo_t k = e.key.combo;
                if (key_is(k, "Escape")) { verdict = -1; break; }
                if (key_is(k, "Enter")) { if (ready(p)) verdict = 1; break; }
                if (key_is(k, "Tab")) {   /* the name, the split size, the password, it again */
                    static const int ORDER[PACKWIN_FIELDS] = { PACKWIN_NAME, PACKWIN_SPLIT, PACKWIN_PW, PACKWIN_AGAIN };
                    int at = 0;
                    for (int i = 0; i < PACKWIN_FIELDS; ++i) if (ORDER[i] == p->focus) at = i;
                    at = (at + ((k.modifiers & RUBRAVIEW_MOD_SHIFT) ? PACKWIN_FIELDS - 1 : 1)) % PACKWIN_FIELDS;
                    focus_on(p, ORDER[at]);
                    break;
                }
                if (key_is(k, "PageDown")) { p->scroll += 200.0; p->dirty = true; break; }
                if (key_is(k, "PageUp")) { p->scroll -= 200.0; p->dirty = true; break; }
                (void)editbox_key(p->window, &p->field[p->focus], k, p->focus == PACKWIN_PW || p->focus == PACKWIN_AGAIN);
                p->follow = p->focus;
                p->dirty = true;
                break;
            }
            default: break;
        }
    }
    return verdict;
}
