/* RV-268 (owner, 2026-11-10: "진행할 때 메인 창 안에 진행정도를 표시해주는 카드 창 만들면 좋을 것 같아요 ... 지금은
   위에 메시지로만 뜨는데 ... 용량기준 진행률이나 진행숫자(파일기준) 그리고 예상 남은시간 같은 것도 보여주고요"):
   a long job — an extraction, a compression, an archive written again, files copied or moved — shown in a card of
   the main window: what it is, a bar, the bytes and the percent, the file it is on of how many, the time left, and
   Stop where the job can be stopped (as Esc). The card is the input card's (D-323): moved by its title, kept inside
   the window and where it was left; it takes no keys, and the window works under it. */
#include "app.h"
#include "rubraview/picker.h"

/* Each pass the job running says where it is; the card follows. */
void progress_show(app_state_t *app, const progress_view_t *view) {
    double now = rubraview_pal_time_now_seconds();
    if (app->prog.source != view->source || app->prog_seen_at <= 0.0) rubraview_progress_begin(&app->prog_rate, now);
    app->prog = *view;
    app->prog_seen_at = now;
    rubraview_progress_sample(&app->prog_rate, view->bytes ? view->done : view->files_at, now);
}

/* The job ended: its card goes (its place kept for the next). */
void progress_end(app_state_t *app, int32_t source) {
    if (app->prog.source != source) return;
    app->prog.source = PROGRESS_NONE;
    app->prog_seen_at = 0.0;
    if (app->prog_card.open && app->prog_host_w > 0.0)
        app->card_kept[CARD_PROGRESS] = rubraview_card_share_of(&app->prog_card, app->prog_host_w, app->prog_host_h);
    rubraview_card_close(&app->prog_card);
    app->prog_stop = (rubraview_pal_rect_t){0};
}

/* Stop: what Esc does to the job shown. */
static void progress_stop(app_state_t *app) {
    if (app->prog.source == PROGRESS_ARCHWORK && archwork_busy(app)) { (void)archwork_stop(app); return; }
    if (app->prog.source == PROGRESS_ARCEDIT && app->arc_job && !atomic_load(&app->arc_job->job.cancel)) {
        atomic_store(&app->arc_job->job.cancel, true);
        osd_say(app, TU8("stopping..."));
    }
}

bool progress_card_press(app_state_t *app, double x, double y) {
    if (!app->prog_card.open) return false;
    if (app->prog.can_stop && !app->prog.stopping && pal_rect_has(app->prog_stop, x, y)) { progress_stop(app); return true; }
    bool on = rubraview_card_press(&app->prog_card, x, y);
    app->prog_card.focused = false;   /* it never takes the keys */
    return on;
}

static size_t time_left_text(char *out, size_t cap, double seconds) {
    if (seconds < 0.0) return (size_t)rubraview_fmt(out, cap, "{}", PROVEN_ARG(T("Working out the time left...")));
    int h = 0, m = 0, s = 0;
    rubraview_progress_split(seconds, &h, &m, &s);
    char t[64];
    if (h > 0) rubraview_fmt(t, sizeof t, T("{} h {} min"), PROVEN_ARG(h), PROVEN_ARG(m));
    else if (m > 0) rubraview_fmt(t, sizeof t, T("{} min {} s"), PROVEN_ARG(m), PROVEN_ARG(s));
    else rubraview_fmt(t, sizeof t, T("{} s"), PROVEN_ARG(s));
    int n = rubraview_fmt(out, cap, T("About {} left"), PROVEN_ARG(t));
    return n > 0 ? (size_t)n : 0;
}

void draw_progress_card(app_state_t *app, double win_w, double win_h, double dpi) {
    if (app->prog.source == PROGRESS_NONE) return;
    const progress_view_t *v = &app->prog;
    double pad = 20.0 * dpi, line_h = 22.0 * dpi, bar_h = 14.0 * dpi, btn_h = 38.0 * dpi;
    int lines = (v->bytes ? 1 : 0) + (v->files_total > 0 ? 1 : 0) + 1;
    double min_h = pad + 30.0 * dpi + 22.0 * dpi + 12.0 * dpi + bar_h + 10.0 * dpi + line_h * lines + (v->can_stop ? 14.0 * dpi + btn_h : 0.0) + pad;
    rubraview_card_t *cd = &app->prog_card;
    app->prog_host_w = win_w;
    app->prog_host_h = win_h;
    if (!cd->open) {
        cd->margin = 8.0 * dpi;
        cd->edge = 6.0 * dpi;
        cd->title_height = 46.0 * dpi;
        rubraview_card_open(cd, win_w, win_h, 560.0 * dpi, min_h, 0.5, &app->card_kept[CARD_PROGRESS]);
        cd->focused = false;
    }
    rubraview_card_fit(cd, win_w, win_h, 360.0 * dpi, min_h);
    rubraview_pal_rect_t c = { cd->rect.x, cd->rect.y, cd->rect.width, cd->rect.height };

    const char *note = v->stopping ? T("stopping...") : v->note[0] ? v->note : "";
    double y = draw_card_frame(app, c, v->title, note, true, dpi);
    uint64_t done = v->bytes ? v->done : v->files_at, total = v->bytes ? v->total : v->files_total;
    int pct = rubraview_progress_percent(done, total);
    rubraview_pal_rect_t bar = { c.x + pad, y, c.width - 2.0 * pad, bar_h };
    rubraview_pal_render_fill_rect(app->renderer, bar, 0xFF141414u, bar_h * 0.5);
    if (total > 0 && done > 0) {
        double f = (double)done / (double)total;
        if (f > 1.0) f = 1.0;
        rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ bar.x, bar.y, fmax(bar_h, bar.width * f), bar.height }, COLOR_TILE_CURRENT, bar_h * 0.5);
    }
    rubraview_pal_render_stroke_rect(app->renderer, bar, COLOR_BOX_BORDER, 1.0, bar_h * 0.5);
    y += bar_h + 10.0 * dpi;

    char line[300];
    rubraview_pal_rect_t row = { c.x + pad, y, c.width - 2.0 * pad, line_h };
    if (v->bytes) {
        char a[32], b[32];
        rubraview_picker_size_text(a, sizeof a, v->done);
        rubraview_picker_size_text(b, sizeof b, v->total);
        int n = rubraview_fmt(line, sizeof line, T("{} of {}  ·  {} %"), PROVEN_ARG(a), PROVEN_ARG(b), PROVEN_ARG(pct));
        if (n > 0) rubraview_pal_render_draw_text(app->renderer, cstr(line), row, 15.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
        row.y += line_h;
    }
    if (v->files_total > 0) {
        uint32_t at = v->files_at > v->files_total ? v->files_total : v->files_at;
        int n = v->bytes ? rubraview_fmt(line, sizeof line, T("Files: {} of {}"), PROVEN_ARG((size_t)at), PROVEN_ARG((size_t)v->files_total))
                         : rubraview_fmt(line, sizeof line, T("Files: {} of {}  ·  {} %"), PROVEN_ARG((size_t)at), PROVEN_ARG((size_t)v->files_total), PROVEN_ARG(pct));
        if (n > 0) rubraview_pal_render_draw_text(app->renderer, cstr(line), row, 15.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
        row.y += line_h;
    }
    double left = rubraview_progress_left(&app->prog_rate, done, total, rubraview_pal_time_now_seconds());
    if (time_left_text(line, sizeof line, left) > 0)
        rubraview_pal_render_draw_text(app->renderer, cstr(line), row, 15.0 * dpi, 0xB0F0F0F0u, RUBRAVIEW_TEXT_LEFT);
    app->prog_stop = (rubraview_pal_rect_t){0};
    if (v->can_stop && !v->stopping) {
        const char *words[1] = { T("Stop") };
        draw_card_buttons(app, c, words, 1, &app->prog_stop, dpi);
    }
}
