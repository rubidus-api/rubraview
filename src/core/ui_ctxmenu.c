#include "rubraview/ui_ctxmenu.h"
#include "rubraview/lang.h"

#include <math.h>

/* How many group changes come before row `index` (all of them for `count`). */
static size_t gaps_before(const int32_t *groups, size_t index) {
    size_t gaps = 0;
    for (size_t i = 1; i <= index; ++i) {
        if (groups[i] != groups[i - 1]) gaps++;
    }
    return gaps;
}

rubraview_rect_t rubraview_ctxmenu_frame(double window_width, double window_height, double x, double y,
                                         const rubraview_ctxmenu_metrics_t *m, const int32_t *groups, size_t count) {
    if (!m || !groups || count == 0) return (rubraview_rect_t){ x, y, 0.0, 0.0 };
    double height = m->pad * 2.0 + m->row_height * (double)count + m->gap * (double)gaps_before(groups, count - 1);
    double width = m->width;
    double left = x, top = y;
    if (left + width > window_width) left = x - width;         /* to the left of the point */
    if (top + height > window_height) top = y - height;        /* above it */
    if (left + width > window_width) left = window_width - width;
    if (top + height > window_height) top = window_height - height;
    if (left < 0.0) left = 0.0;
    if (top < 0.0) top = 0.0;
    return (rubraview_rect_t){ left, top, width, height };
}

rubraview_rect_t rubraview_ctxmenu_row(rubraview_rect_t frame, const rubraview_ctxmenu_metrics_t *m,
                                       const int32_t *groups, size_t index) {
    double top = frame.y + m->pad + m->row_height * (double)index + m->gap * (double)gaps_before(groups, index);
    return (rubraview_rect_t){ frame.x, top, frame.width, m->row_height };
}

int32_t rubraview_ctxmenu_row_at(rubraview_rect_t frame, const rubraview_ctxmenu_metrics_t *m,
                                 const int32_t *groups, size_t count, double x, double y) {
    if (!m || !groups || x < frame.x || x >= frame.x + frame.width) return -1;
    for (size_t i = 0; i < count; ++i) {
        rubraview_rect_t row = rubraview_ctxmenu_row(frame, m, groups, i);
        if (y >= row.y && y < row.y + row.height) return (int32_t)i;
    }
    return -1;
}

int32_t rubraview_ctxmenu_step(const bool *enabled, size_t count, int32_t from, int direction) {
    if (!enabled || count == 0) return -1;
    int32_t n = (int32_t)count;
    int32_t at = from;
    if (at < 0 || at >= n) at = direction > 0 ? -1 : n;   /* none chosen: from the end it starts at */
    for (int32_t k = 0; k < n; ++k) {
        at += direction > 0 ? 1 : -1;
        if (at >= n) at = 0;
        if (at < 0) at = n - 1;
        if (enabled[at]) return at;
    }
    return -1;
}

/* ---- RV-287: the ☰ laid as inline buttons ---- */

rubraview_rect_t rubraview_flowmenu_layout(double x, double y, double max_width, const rubraview_flowmenu_metrics_t *m,
                                           const double *widths, const int32_t *groups, size_t count, rubraview_rect_t *out_cells) {
    /* (x, y) as given, above the window too: a scrolled menu is laid out from above its top */
    if (!m || !widths || !groups || count == 0) return (rubraview_rect_t){ x, y, 0.0, 0.0 };
    double inner = max_width - m->pad * 2.0;
    if (inner < 1.0) inner = 1.0;
    double cx = 0.0, widest = 0.0;
    size_t line = 0;
    for (size_t i = 0; i < count; ++i) {
        double w = widths[i] < 0.0 ? 0.0 : (widths[i] > inner ? inner : widths[i]);
        bool starts = i == 0 || groups[i] != groups[i - 1];
        if (starts && cx > 0.0 && m->group_lines) { line++; cx = 0.0; }   /* RV-303: a line a group */
        double space = cx > 0.0 ? (starts ? m->group_gap : m->gap) : 0.0;
        if (starts && cx > 0.0) {   /* the whole group on the next line when it does not fit here but fits there */
            double gw = 0.0;
            for (size_t k = i; k < count && groups[k] == groups[i]; ++k)
                gw += (k > i ? m->gap : 0.0) + (widths[k] > inner ? inner : (widths[k] < 0.0 ? 0.0 : widths[k]));
            if (cx + space + gw > inner && gw <= inner) { line++; cx = 0.0; space = 0.0; }
        }
        if (cx > 0.0 && cx + space + w > inner) { line++; cx = 0.0; space = 0.0; }
        double left = cx + space;
        if (out_cells) out_cells[i] = (rubraview_rect_t){ x + m->pad + left, y + m->pad + (m->cell_height + m->gap) * (double)line, w, m->cell_height };
        cx = left + w;
        if (cx > widest) widest = cx;
    }
    double height = m->pad * 2.0 + m->cell_height * (double)(line + 1) + m->gap * (double)line;
    return (rubraview_rect_t){ x, y, widest + m->pad * 2.0, height };
}

int32_t rubraview_flowmenu_at(const rubraview_rect_t *cells, size_t count, double x, double y) {
    if (!cells) return -1;
    for (size_t i = 0; i < count; ++i) {
        rubraview_rect_t r = cells[i];
        if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) return (int32_t)i;
    }
    return -1;
}

int32_t rubraview_flowmenu_step_vertical(const rubraview_rect_t *cells, const bool *enabled, size_t count, int32_t from, int direction) {
    if (!cells || !enabled || count == 0) return -1;
    if (from < 0 || (size_t)from >= count) {
        for (size_t i = 0; i < count; ++i) if (enabled[i]) return (int32_t)i;
        return -1;
    }
    rubraview_rect_t at = cells[from];
    double mid = at.x + at.width * 0.5, sign = direction > 0 ? 1.0 : -1.0;
    double line_d = -1.0;   /* the nearest line that way with a button that can act */
    for (size_t i = 0; i < count; ++i) {
        double d = (cells[i].y - at.y) * sign;
        if (enabled[i] && d > 0.0 && (line_d < 0.0 || d < line_d)) line_d = d;
    }
    if (line_d < 0.0) return from;
    int32_t best = from;
    double best_d = 0.0;
    for (size_t i = 0; i < count; ++i) {
        if (!enabled[i] || (cells[i].y - at.y) * sign != line_d) continue;
        double d = fabs(cells[i].x + cells[i].width * 0.5 - mid);
        if (best == from || d < best_d) { best = (int32_t)i; best_d = d; }
    }
    return best;
}

double rubraview_flowmenu_scroll_clamp(double scroll, double content, double view, double pitch) {
    if (pitch <= 0.0 || content <= view) return 0.0;
    double most = ceil((content - view) / pitch - 1e-9) * pitch;
    double s = round(scroll / pitch) * pitch;
    return s < 0.0 ? 0.0 : (s > most ? most : s);
}

double rubraview_flowmenu_scroll_to(double scroll, double top, double content, double view, double pitch, double pad) {
    double s = rubraview_flowmenu_scroll_clamp(scroll, content, view, pitch);
    if (pitch <= 0.0) return s;
    double line_top = top - pad;   /* the line's place among whole lines */
    if (line_top < s) s = line_top;
    else if (line_top + pitch > s + (view - pad * 2.0) + 1e-9) s = line_top + pitch - (view - pad * 2.0);
    return rubraview_flowmenu_scroll_clamp(ceil(s / pitch - 1e-9) * pitch, content, view, pitch);
}

/* ---- RV-303 (RFC-0010 §6): the ☰'s categories ---- */

static const char *const MENUCAT_HEADING[RUBRAVIEW_MENUCAT_COUNT] = {
    [RUBRAVIEW_MENUCAT_OPEN]      = N_("Open and lists"),
    [RUBRAVIEW_MENUCAT_MOVE]      = N_("Move"),
    [RUBRAVIEW_MENUCAT_FILES]     = N_("File and folders"),
    [RUBRAVIEW_MENUCAT_VIEW]      = N_("View and reading"),
    [RUBRAVIEW_MENUCAT_PLAY]      = N_("Play and sound"),
    [RUBRAVIEW_MENUCAT_SUBTITLES] = N_("Subtitles"),
    [RUBRAVIEW_MENUCAT_EDIT]      = N_("Edit and output"),
    [RUBRAVIEW_MENUCAT_ARCHIVES]  = N_("Archives"),
    [RUBRAVIEW_MENUCAT_WIDGETS]   = N_("Widgets and help"),
};

const char *rubraview_menu_category_heading(int category) {
    return category >= 0 && category < RUBRAVIEW_MENUCAT_COUNT ? MENUCAT_HEADING[category] : MENUCAT_HEADING[RUBRAVIEW_MENUCAT_WIDGETS];
}

/* The actions by name first; then by how their names begin; anything else is the last category's. */
static const struct { const char *action; int category; } MENUCAT_OF[] = {
    { "new_window", RUBRAVIEW_MENUCAT_OPEN }, { "open_picker", RUBRAVIEW_MENUCAT_OPEN }, { "open_folder", RUBRAVIEW_MENUCAT_OPEN },
    { "open_recent_list", RUBRAVIEW_MENUCAT_OPEN }, { "open_favorites", RUBRAVIEW_MENUCAT_OPEN }, { "toggle_playlist", RUBRAVIEW_MENUCAT_OPEN },
    { "toggle_favorite", RUBRAVIEW_MENUCAT_OPEN }, { "picker_favorite", RUBRAVIEW_MENUCAT_OPEN }, { "picker_open_item", RUBRAVIEW_MENUCAT_OPEN },
    { "picker_crumb_open", RUBRAVIEW_MENUCAT_OPEN }, { "picker_crumb_new_window", RUBRAVIEW_MENUCAT_OPEN }, { "picker_crumb_favorite", RUBRAVIEW_MENUCAT_OPEN },
    { "picker_add_to_list", RUBRAVIEW_MENUCAT_OPEN }, { "picker_make_playlist", RUBRAVIEW_MENUCAT_OPEN }, { "list_open", RUBRAVIEW_MENUCAT_OPEN },
    { "list_take_out", RUBRAVIEW_MENUCAT_OPEN }, { "recent_clear", RUBRAVIEW_MENUCAT_OPEN },
    { "next_page", RUBRAVIEW_MENUCAT_MOVE }, { "prev_page", RUBRAVIEW_MENUCAT_MOVE }, { "first_page", RUBRAVIEW_MENUCAT_MOVE },
    { "last_page", RUBRAVIEW_MENUCAT_MOVE }, { "next_archive", RUBRAVIEW_MENUCAT_MOVE }, { "prev_archive", RUBRAVIEW_MENUCAT_MOVE },
    { "page_over_back", RUBRAVIEW_MENUCAT_MOVE }, { "subpage_next", RUBRAVIEW_MENUCAT_MOVE }, { "subpage_prev", RUBRAVIEW_MENUCAT_MOVE },
    { "anim_step_back", RUBRAVIEW_MENUCAT_MOVE }, { "anim_step_forward", RUBRAVIEW_MENUCAT_MOVE },
    { "copy_to_folder", RUBRAVIEW_MENUCAT_FILES }, { "move_to_folder", RUBRAVIEW_MENUCAT_FILES }, { "copy_to_other_folder", RUBRAVIEW_MENUCAT_FILES },
    { "move_to_other_folder", RUBRAVIEW_MENUCAT_FILES }, { "copy_to_default", RUBRAVIEW_MENUCAT_FILES }, { "move_to_default", RUBRAVIEW_MENUCAT_FILES },
    { "undo", RUBRAVIEW_MENUCAT_FILES }, { "toggle_classify", RUBRAVIEW_MENUCAT_FILES }, { "delete_file", RUBRAVIEW_MENUCAT_FILES },
    { "rename_file", RUBRAVIEW_MENUCAT_FILES }, { "reveal_in_explorer", RUBRAVIEW_MENUCAT_FILES }, { "file_properties", RUBRAVIEW_MENUCAT_FILES },
    { "explorer_menu", RUBRAVIEW_MENUCAT_FILES }, { "explorer_apps", RUBRAVIEW_MENUCAT_FILES }, { "explorer_menu_classic", RUBRAVIEW_MENUCAT_FILES },
    { "list_copy_path", RUBRAVIEW_MENUCAT_FILES }, { "list_delete_file", RUBRAVIEW_MENUCAT_FILES }, { "list_reveal", RUBRAVIEW_MENUCAT_FILES },
    { "toggle_first_page_alone", RUBRAVIEW_MENUCAT_VIEW }, { "toggle_info", RUBRAVIEW_MENUCAT_VIEW }, { "toggle_fullscreen", RUBRAVIEW_MENUCAT_VIEW },
    { "toggle_miniplayer", RUBRAVIEW_MENUCAT_PLAY }, { "next_audio_track", RUBRAVIEW_MENUCAT_PLAY }, { "toggle_shuffle", RUBRAVIEW_MENUCAT_PLAY },
    { "toggle_slideshow", RUBRAVIEW_MENUCAT_PLAY },
    { "choose_subtitle_track", RUBRAVIEW_MENUCAT_SUBTITLES }, { "extract_subtitles", RUBRAVIEW_MENUCAT_SUBTITLES },
    { "toggle_subtitle_window", RUBRAVIEW_MENUCAT_SUBTITLES }, { "toggle_subtitles", RUBRAVIEW_MENUCAT_SUBTITLES },
    { "open_batch", RUBRAVIEW_MENUCAT_EDIT }, { "print", RUBRAVIEW_MENUCAT_EDIT }, { "copy_frame", RUBRAVIEW_MENUCAT_EDIT },
    { "save_frame", RUBRAVIEW_MENUCAT_EDIT }, { "open_edit", RUBRAVIEW_MENUCAT_EDIT }, { "quick_export", RUBRAVIEW_MENUCAT_EDIT },
    { "picker_look_inside", RUBRAVIEW_MENUCAT_ARCHIVES },
    { "align_widget", RUBRAVIEW_MENUCAT_WIDGETS }, { "open_settings", RUBRAVIEW_MENUCAT_WIDGETS }, { "open_log", RUBRAVIEW_MENUCAT_WIDGETS },
    { "toggle_help", RUBRAVIEW_MENUCAT_WIDGETS }, { "quit", RUBRAVIEW_MENUCAT_WIDGETS },
};
static const struct { const char *prefix; int category; } MENUCAT_PREFIX[] = {
    { "picker_extract", RUBRAVIEW_MENUCAT_ARCHIVES }, { "picker_compress", RUBRAVIEW_MENUCAT_ARCHIVES },
    { "picker_", RUBRAVIEW_MENUCAT_FILES }, { "subtitle_", RUBRAVIEW_MENUCAT_SUBTITLES }, { "media_", RUBRAVIEW_MENUCAT_PLAY },
    { "layout_", RUBRAVIEW_MENUCAT_VIEW }, { "text_", RUBRAVIEW_MENUCAT_VIEW }, { "zoom_", RUBRAVIEW_MENUCAT_VIEW }, { "fit_", RUBRAVIEW_MENUCAT_VIEW },
    { "rotate_", RUBRAVIEW_MENUCAT_VIEW }, { "flip_", RUBRAVIEW_MENUCAT_VIEW },
};

int rubraview_menu_category(proven_u8str_view_t action) {
    for (size_t i = 0; i < sizeof MENUCAT_OF / sizeof MENUCAT_OF[0]; ++i)
        if (proven_u8str_view_eq(action, proven_u8str_view_from_cstr(MENUCAT_OF[i].action))) return MENUCAT_OF[i].category;
    for (size_t i = 0; i < sizeof MENUCAT_PREFIX / sizeof MENUCAT_PREFIX[0]; ++i) {
        proven_u8str_view_t p = proven_u8str_view_from_cstr(MENUCAT_PREFIX[i].prefix);
        if (action.size >= p.size && proven_u8str_view_eq(proven_u8str_view_slice(action, 0, p.size), p)) return MENUCAT_PREFIX[i].category;
    }
    return RUBRAVIEW_MENUCAT_WIDGETS;
}
