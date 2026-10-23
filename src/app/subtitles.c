/*
 * Subtitles: the Sub tile and its list, taken out of a film, the subtitle window, put into a film
 * (RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* ---- D-33: the Sub tile and its list of tracks ---- */

/* The OSD line for the subtitles now in use. */
void subtitle_say(app_state_t *app) {
    char label[160];
    proven_u8str_view_t text = app->tracks.current_subtitle >= 0
                       ? rubraview_track_label(label, sizeof(label), &app->tracks, app->tracks.current_subtitle)
                       : TU8("off");
    char line[192];
    int n = snprintf(line, sizeof(line), T("subtitles %.*s"), (int)text.size, text.ptr);
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
}

/* The subtitle tracks in list order; returns how many. */
size_t subtitle_track_indices(const app_state_t *app, int32_t *out, size_t capacity) {
    size_t n = 0;
    for (size_t i = 0; i < app->tracks.count && n < capacity; ++i) {
        if (app->tracks.tracks[i].kind == RUBRAVIEW_TRACK_SUBTITLE) out[n++] = (int32_t)i;
    }
    return n;
}

/* A tap on the Sub tile: off, or back on to the track last shown (the
   first one when none was). */
void subtitles_toggle(app_state_t *app) {
    if (rubraview_tracks_count(&app->tracks, RUBRAVIEW_TRACK_SUBTITLE) == 0) {
        osd_say(app, TU8("this video has no subtitles, in it or beside it"));
        return;
    }
    if (!subs_may_leave(app)) return;
    if (app->tracks.current_subtitle >= 0) {
        app->subtitle_last = app->tracks.current_subtitle;
        subtitle_select(app, -1);
        app->subbox.selected = false;
    } else {
        int32_t want = app->subtitle_last;
        if (want < 0 || (size_t)want >= app->tracks.count ||
            app->tracks.tracks[want].kind != RUBRAVIEW_TRACK_SUBTITLE) {
            want = rubraview_tracks_next(&app->tracks, RUBRAVIEW_TRACK_SUBTITLE, -1);
        }
        subtitle_select(app, want);
    }
    subtitle_say(app);
}

#define SUB_LIST_WIDTH 340.0

rubraview_rect_t sub_list_rect(const app_state_t *app, size_t *out_rows) {
    int32_t tracks[SUB_LIST_MAX];
    size_t rows = 1 + subtitle_track_indices(app, tracks, SUB_LIST_MAX);   /* Off, then each track */
    if (out_rows) *out_rows = rows;
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    return rubraview_choice_list_rect((double)win_w, (double)win_h, rows, SUB_LIST_ROW * dpi,
                                      SUB_LIST_WIDTH * dpi, app->sub_list_anchor_x, app->sub_list_anchor_y);
}

/* A double tap or a held press: the list, above where it was asked for. */
void sub_list_open_at(app_state_t *app, double x, double y) {
    if (rubraview_tracks_count(&app->tracks, RUBRAVIEW_TRACK_SUBTITLE) == 0) {
        osd_say(app, TU8("this video has no subtitles, in it or beside it"));
        return;
    }
    app->sub_list_anchor_x = x;
    app->sub_list_anchor_y = y;
    app->sub_list_open = true;
}

/* Row 0 is Off; row k the k-th subtitle track. */
void sub_list_choose(app_state_t *app, int32_t row) {
    int32_t tracks[SUB_LIST_MAX];
    size_t n = subtitle_track_indices(app, tracks, SUB_LIST_MAX);
    app->sub_list_open = false;
    if (!subs_may_leave(app)) return;
    if (row == 0) {
        if (app->tracks.current_subtitle >= 0) app->subtitle_last = app->tracks.current_subtitle;
        subtitle_select(app, -1);
        app->subbox.selected = false;
    } else if (row > 0 && (size_t)(row - 1) < n) {
        subtitle_select(app, tracks[row - 1]);
    }
    subtitle_say(app);
}

/* Where the Sub tile is on screen, for the list to open above it. */
bool sub_tile_anchor(app_state_t *app, double *x, double *y);

/* The Sub tile was pressed or let go (from the toolbox in the window or
   detached — both come here). */
void sub_tile_result(app_state_t *app, rubraview_tap_result_t r) {
    if (r == RUBRAVIEW_TAP_SINGLE) {
        subtitles_toggle(app);
    } else if (r == RUBRAVIEW_TAP_CHOOSE) {
        double x = 0.0, y = 0.0;
        if (!sub_tile_anchor(app, &x, &y)) {
            int32_t w = 0, h = 0;
            rubraview_pal_window_get_size(app->window, &w, &h);
            x = w * 0.5; y = h * 0.75;
        }
        sub_list_open_at(app, x, y);
    }
}

/* ---- Subtitles taken out of a Matroska film (RV-115 stage 3, D-127) ----
   Our own reader (`matroska.c`) walks the film's blocks and the text ones
   of each subtitle track are written as `film.kor.srt` / `film.eng.ass`
   beside it. A film is long: the walk is done a slice a frame, says how
   far it is, and the same action stops it. Nothing of the film is changed. */

void takeout_end(app_state_t *app) {
    for (size_t i = 0; i < RUBRAVIEW_MKV_MAX_TRACKS; ++i) {
        free(app->takeout.out[i].data);
        app->takeout.out[i].data = NULL;
        app->takeout.out[i].len = app->takeout.out[i].cap = 0;
        app->takeout.out[i].cues = 0;
    }
    if (app->takeout.map.os) rubraview_pal_fs_unmap(&app->takeout.map);
    app->takeout.map = (rubraview_fs_mapping_t){0};
    app->takeout.running = false;
    app->subs_dirty = true;
}

/* Room for `need` more bytes of a track's file; false when there is none. */
static bool takeout_room(app_state_t *app, size_t track, size_t need) {
    size_t want = 0;
    if (PROVEN_CKD_ADD(&want, app->takeout.out[track].len, need)) return false;
    if (want <= app->takeout.out[track].cap) return true;
    size_t cap = app->takeout.out[track].cap ? app->takeout.out[track].cap : 4096;
    while (cap < want) {
        if (PROVEN_CKD_MUL(&cap, cap, (size_t)2)) return false;
    }
    if (cap > SUBTITLE_MAX_BYTES) return false;       /* more than the viewer would read back */
    char *data = realloc(app->takeout.out[track].data, cap);
    if (!data) return false;
    app->takeout.out[track].data = data;
    app->takeout.out[track].cap = cap;
    return true;
}

static bool takeout_wanted(const rubraview_mkv_track_t *track) {
    return !track->encoded && (track->subtitle == RUBRAVIEW_MKV_SUB_SRT || track->subtitle == RUBRAVIEW_MKV_SUB_ASS);
}

/* D-129: an MP4's subtitles. Their samples are few and the tables say where
   each is, so they are read at once, not walked; each 3GPP text track is
   written as SubRip under the name a Matroska track of its language would
   have (its tracks are described in `takeout.mkv`'s place for that). */
static void takeout_write(app_state_t *app);

static void takeout_mp4(app_state_t *app, proven_u8str_view_t film) {
    const char *why = NULL;
    if (!rubraview_mp4_open(&app->takeout.mp4, app->takeout.map.data, app->takeout.map.size, &why)) {
        takeout_end(app);
        char line[200];
        int n = snprintf(line, sizeof(line), T("subtitles are taken out of Matroska and MP4 films only (%s)"), why ? why : T("not one of them"));
        if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1 });
        return;
    }
    app->takeout.mkv = (rubraview_mkv_t){0};
    size_t wanted = 0, other = 0;
    for (size_t i = 0; i < app->takeout.mp4.track_count && i < RUBRAVIEW_MKV_MAX_TRACKS; ++i) {
        const rubraview_mp4_track_t *t = &app->takeout.mp4.tracks[i];
        rubraview_mkv_track_t *as = &app->takeout.mkv.tracks[app->takeout.mkv.track_count++];
        *as = (rubraview_mkv_track_t){ .number = t->id, .type = t->subtitle ? 0x11 : 0,
                                       .language = { .ptr = (const proven_byte_t *)t->language, .size = strlen(t->language) },
                                       .subtitle = t->subtitle == RUBRAVIEW_MP4_SUB_TX3G ? RUBRAVIEW_MKV_SUB_SRT
                                                 : t->subtitle == RUBRAVIEW_MP4_SUB_OTHER ? RUBRAVIEW_MKV_SUB_OTHER : RUBRAVIEW_MKV_SUB_NONE };
        if (t->subtitle == RUBRAVIEW_MP4_SUB_OTHER) other++;
        if (t->subtitle != RUBRAVIEW_MP4_SUB_TX3G) continue;
        wanted++;
        rubraview_mp4_walk_t walk = {0};
        rubraview_mp4_sample_t sample;
        while (rubraview_mp4_sample_next(&app->takeout.mp4, t, &walk, &sample)) {
            size_t need = rubraview_mp4_subtitle_cue(&app->takeout.mp4, t, app->takeout.out[i].cues + 1, &sample, NULL, 0);
            if (need == 0) continue;
            if (!takeout_room(app, i, need + 1)) { takeout_end(app); osd_say(app, TU8("the film's subtitles are too long to take out")); return; }
            app->takeout.out[i].len += rubraview_mp4_subtitle_cue(&app->takeout.mp4, t, app->takeout.out[i].cues + 1, &sample,
                                                                  app->takeout.out[i].data + app->takeout.out[i].len,
                                                                  app->takeout.out[i].cap - app->takeout.out[i].len);
            app->takeout.out[i].cues++;
        }
    }
    if (wanted == 0) {
        takeout_end(app);
        osd_say(app, other > 0 ? TU8("the film's subtitles are of a kind not taken out yet (only 3GPP text is)")
                               : TU8("the film holds no subtitles"));
        return;
    }
    memcpy(app->takeout.film, film.ptr, film.size);
    app->takeout.film[film.size] = '\0';
    takeout_write(app);
}

void takeout_begin(app_state_t *app) {
    if (app->takeout.running) {                       /* the same action stops it */
        takeout_end(app);
        osd_say(app, TU8("taking the subtitles out was stopped; nothing was written"));
        return;
    }
    if (!app->media || app->media_page < 0 || (size_t)app->media_page >= page_count(app) || app->source.archive_path.size > 0) {
        osd_say(app, TU8("no film file is open to take subtitles out of"));
        return;
    }
    proven_u8str_view_t film = app->source.pages[app->media_page].path;
    if (film.size == 0 || film.size >= sizeof(app->takeout.film)) return;
    if (!rubraview_pal_fs_map(film, &app->takeout.map)) {
        osd_say(app, TU8("the film could not be read"));
        return;
    }
    const char *why = NULL;
    if (!rubraview_mkv_open(&app->takeout.mkv, app->takeout.map.data, app->takeout.map.size, &why)) {
        takeout_mp4(app, film);
        return;
    }
    size_t wanted = 0, pictures = 0, packed = 0;
    for (size_t i = 0; i < app->takeout.mkv.track_count; ++i) {
        const rubraview_mkv_track_t *t = &app->takeout.mkv.tracks[i];
        if (takeout_wanted(t)) {
            size_t head = rubraview_mkv_subtitle_head(t, NULL, 0);
            if (!takeout_room(app, i, head + 1)) { takeout_end(app); osd_say(app, TU8("no memory to take the subtitles out")); return; }
            app->takeout.out[i].len = rubraview_mkv_subtitle_head(t, app->takeout.out[i].data, app->takeout.out[i].cap);
            wanted++;
        } else if (t->subtitle == RUBRAVIEW_MKV_SUB_PICTURE) {
            pictures++;
        } else if (t->subtitle != RUBRAVIEW_MKV_SUB_NONE) {
            packed++;
        }
    }
    if (wanted == 0) {
        takeout_end(app);
        osd_say(app, pictures > 0 ? TU8("the film's subtitles are pictures: they are not taken out")
                   : packed > 0 ? TU8("the film's subtitles are compressed or of a kind not taken out yet")
                   : TU8("the film holds no subtitles"));
        return;
    }
    memcpy(app->takeout.film, film.ptr, film.size);
    app->takeout.film[film.size] = '\0';
    rubraview_mkv_walk_begin(&app->takeout.mkv, &app->takeout.walk);
    app->takeout.running = true;
    app->takeout.said_at = 0.0;
    app->takeout.percent = -1;
    osd_say(app, TU8("taking the subtitles out of the film..."));
}

/* The walk is over: each track's text goes to its file. One already there
   with the same bytes is left; another is kept under a numbered name (D-126). */
static void takeout_write(app_state_t *app) {
    proven_u8str_view_t film = { .ptr = (const proven_byte_t *)app->takeout.film, .size = strlen(app->takeout.film) };
    char said[400];
    size_t said_len = 0, written = 0, same = 0, failed = 0, empty = 0;
    proven_u8str_view_t first = PROVEN_LIT("");
    for (size_t i = 0; i < app->takeout.mkv.track_count; ++i) {
        const rubraview_mkv_track_t *t = &app->takeout.mkv.tracks[i];
        if (!takeout_wanted(t)) continue;
        if (app->takeout.out[i].cues == 0) { empty++; continue; }
        unsigned ordinal = 0;                         /* earlier tracks of the same language */
        for (size_t k = 0; k < i; ++k) {
            const rubraview_mkv_track_t *o = &app->takeout.mkv.tracks[k];
            if (takeout_wanted(o) && app->takeout.out[k].cues > 0 && proven_u8str_view_eq(o->language, t->language) &&
                strcmp(rubraview_mkv_subtitle_ext(o), rubraview_mkv_subtitle_ext(t)) == 0) ordinal++;
        }
        char name[1100];
        size_t name_len = rubraview_mkv_subtitle_name(t, film, ordinal, name, sizeof(name));
        if (name_len == 0) { failed++; continue; }
        proven_u8str_view_t path = { .ptr = (const proven_byte_t *)name, .size = name_len };
        proven_u8str_view_t text = { .ptr = (const proven_byte_t *)app->takeout.out[i].data, .size = app->takeout.out[i].len };
        bool ok = true;
        if (rubraview_pal_fs_exists(path)) {
            proven_u8str_view_t there = rubraview_pal_fs_read_file(app->arena, path, SUBTITLE_MAX_BYTES);
            if (proven_u8str_view_eq(there, text)) { same++; ok = false; }
            else {
                proven_u8str_view_t kept = PROVEN_LIT("");
                for (unsigned number = 1; number < 10000 && kept.size == 0; ++number) {
                    proven_u8str_view_t candidate = rubraview_subtitle_backup_path(app->arena, path, number);
                    if (candidate.size == 0) break;
                    if (!rubraview_pal_fs_exists(candidate)) kept = candidate;
                }
                if (kept.size == 0 || !rubraview_pal_fs_move(path, kept)) { failed++; ok = false; }
            }
        }
        if (ok && !rubraview_pal_fs_write_file(path, text)) { failed++; ok = false; }
        if (!ok) continue;
        written++;
        proven_u8str_view_t base = rubraview_path_basename(path);
        if (first.size == 0) first = app_keep(app, path);
        int n = snprintf(said + said_len, sizeof(said) - said_len, "%s%.*s", said_len ? ", " : "", (int)(base.size < 120 ? base.size : 120), base.ptr);
        if (n > 0 && (size_t)n < sizeof(said) - said_len) said_len += (size_t)n;
    }
    char line[520];
    int n;
    if (written > 0) n = snprintf(line, sizeof(line), T("taken out: %.*s%s"), (int)said_len, said, failed ? T(" (some could not be written)") : "");
    else if (same > 0) n = snprintf(line, sizeof(line), T("the film's subtitles are beside it already"));
    else if (failed > 0) n = snprintf(line, sizeof(line), T("the subtitles could not be written beside the film"));
    else n = snprintf(line, sizeof(line), "%s", empty ? T("the film's subtitle tracks are empty") : T("nothing was taken out"));
    app->takeout.mp4 = (rubraview_mp4_t){0};
    takeout_end(app);
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1 });

    /* The film on screen is still that film: its list of subtitles is made
       again, and with none on (or none changed) the first new one is shown. */
    if (written == 0 || !app->media || app->media_page < 0 || (size_t)app->media_page >= page_count(app)) return;
    if (!proven_u8str_view_eq(app->source.pages[app->media_page].path, film) || rubraview_subtitle_changed(&app->subtitle)) return;
    int32_t shown = app->tracks.current_subtitle;
    proven_u8str_view_t shown_path = app->subtitle.source_path;
    tracks_prepare(app, film);
    int32_t pick = -1;
    for (size_t i = 0; i < app->tracks.count; ++i) {
        const rubraview_track_t *tr = &app->tracks.tracks[i];
        if (tr->kind != RUBRAVIEW_TRACK_SUBTITLE || !tr->is_external || tr->stream_index < 0 ||
            (size_t)tr->stream_index >= app->subtitle_count) continue;
        proven_u8str_view_t p = app->subtitle_files[tr->stream_index].path;
        if (shown >= 0 && shown_path.size > 0 ? proven_u8str_view_eq(p, shown_path) : proven_u8str_view_eq(p, first)) { pick = (int32_t)i; break; }
    }
    if (pick >= 0) subtitle_select(app, pick);
}

/* A slice of the walk: until eight milliseconds have gone, then the frame is drawn. */
void takeout_step(app_state_t *app) {
    if (!app->takeout.running) return;
    double began = rubraview_pal_time_now_seconds();
    rubraview_mkv_block_t block;
    bool more = true;
    for (unsigned turn = 0; more; ++turn) {
        if ((turn & 63u) == 63u && rubraview_pal_time_now_seconds() - began > 0.008) break;
        more = rubraview_mkv_walk_next(&app->takeout.mkv, &app->takeout.walk, &block);
        if (!more) break;
        int32_t index = rubraview_mkv_track_index(&app->takeout.mkv, block.track);
        if (index < 0 || !takeout_wanted(&app->takeout.mkv.tracks[index])) continue;
        const rubraview_mkv_track_t *t = &app->takeout.mkv.tracks[index];
        size_t need = rubraview_mkv_subtitle_cue(t, app->takeout.out[index].cues + 1, &block, NULL, 0);
        if (need == 0) continue;
        if (!takeout_room(app, (size_t)index, need + 1)) {
            takeout_end(app);
            osd_say(app, TU8("the film's subtitles are too long to take out"));
            return;
        }
        app->takeout.out[index].len += rubraview_mkv_subtitle_cue(t, app->takeout.out[index].cues + 1, &block,
                                                                  app->takeout.out[index].data + app->takeout.out[index].len,
                                                                  app->takeout.out[index].cap - app->takeout.out[index].len);
        app->takeout.out[index].cues++;
    }
    if (rubraview_pal_fs_mapping_lost(&app->takeout.map)) {
        takeout_end(app);
        osd_say(app, TU8("the film went away while its subtitles were taken out; nothing was written"));
        return;
    }
    if (!more) { takeout_write(app); return; }
    int percent = (int)(rubraview_mkv_walk_share(&app->takeout.mkv, &app->takeout.walk) * 100.0);
    if (percent != app->takeout.percent && began - app->takeout.said_at > 0.5) {
        app->takeout.percent = percent;
        app->takeout.said_at = began;
        char line[64];
        int n = snprintf(line, sizeof(line), T("taking the subtitles out... %d %%"), percent);
        if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
        app->subs_dirty = true;
    }
}

/* ---- The subtitle window (owner, 2026-10-14, RV-115) ----
   "자막을 한 줄에 한 항목씩 보여주면서 목록에서 특정 자막을 클릭하면 그 위치로
   이동. 창 외부에 새창으로 … 개별 자막 선택하는 체크박스를 각 항목 맨 앞에
   두고 위에 전체선택 해제 박스도 … 뒤로 앞으로 버튼과 이동할 시간을 적는 칸을
   목록 위쪽에". A window of its own, owned by the viewer: the header with
   the box for all, a bar with Earlier, the seconds and Later, then a row a
   subtitle — its box, when it comes and goes, what it says. The model is
   `ui_subwin`; a DVD's or a Blu-ray's pictures are listed by their times
   only. Moving changes the cues in memory; writing them to the file is
   the next stage of RV-115. */

enum { SUBS_NONE = 0, SUBS_TEXT, SUBS_VOBSUB, SUBS_PGS };

static int subs_kind(const app_state_t *app) {
    if (!app->media) return SUBS_NONE;
    if (app->subtitle.count > 0) return SUBS_TEXT;
    if (app->vobsub.count > 0) return SUBS_VOBSUB;
    if (app->pgs.count > 0) return SUBS_PGS;
    return SUBS_NONE;
}

/* When a row's subtitle comes, on the film's clock (the track's offset counted). */
/* RV-115: the subtitle on screen was moved or edited and not saved. Going
   where it is lost (another file, another subtitle, closing) is held once
   and said; the same within a few seconds goes on and drops the changes. */
bool subs_may_leave(app_state_t *app) {
    if (!rubraview_subtitle_changed(&app->subtitle)) return true;
    double now = rubraview_pal_time_now_seconds();
    if (now < app->subs_leave_through) return true;   /* one going passes through here more than once */
    if (app->subs_leave_unasked) {                     /* nobody pressed, so there is no second press to wait for */
        osd_say(app, app->subtitle.source_path.size ? TU8("subtitle changes not saved: stopped here; Save keeps them")
                                                   : TU8("subtitle changes not saved: stopped here"));
        return false;
    }
    if (rubraview_confirm_press(&app->subs_leave, 1, now)) { app->subs_leave_through = now + 1.0; return true; }
    /* a subtitle inside the film cannot be saved (yet): only the way on is said */
    osd_say(app, app->subtitle.source_path.size ? TU8("subtitle changes not saved: Save keeps them, again leaves them")
                                               : TU8("subtitle changes not saved: again leaves them"));
    app->notice_seconds = RUBRAVIEW_CONFIRM_SECONDS;   /* said for as long as the second try counts */
    return false;
}

static double subs_row_start(const app_state_t *app, size_t row, double *out_end) {
    double start = 0.0, end = -1.0;
    switch (subs_kind(app)) {
        case SUBS_TEXT:
            if (row < app->subs_count && app->subs_rows[row] < app->subtitle.count) {
                start = app->subtitle.cues[app->subs_rows[row]].start_seconds;
                end = app->subtitle.cues[app->subs_rows[row]].end_seconds;
            }
            break;
        case SUBS_VOBSUB:
            if (row < app->vobsub.count) start = app->vobsub.entries[row].start_seconds;   /* its end is in the picture */
            break;
        case SUBS_PGS:
            if (row < app->pgs.count) { start = app->pgs.entries[row].start_seconds; end = app->pgs.entries[row].end_seconds; }
            break;
        default: break;
    }
    if (out_end) *out_end = end < 0.0 ? -1.0 : end + app->subtitle.offset_seconds;
    return start + app->subtitle.offset_seconds;
}

/* The rows for the track on screen now; made again when it is another one. */
static void subs_sync(app_state_t *app) {
    int kind = subs_kind(app);
    const void *of = kind == SUBS_TEXT ? (const void*)app->subtitle.cues
                   : kind == SUBS_VOBSUB ? (const void*)app->vobsub.entries
                   : kind == SUBS_PGS ? (const void*)app->pgs.entries : NULL;
    size_t of_count = kind == SUBS_TEXT ? app->subtitle.count : kind == SUBS_VOBSUB ? app->vobsub.count
                    : kind == SUBS_PGS ? app->pgs.count : 0;
    const char *language = kind == SUBS_TEXT ? (const char *)app->subtitle.shown_language.ptr : NULL;
    if (of == app->subs_for && of_count == app->subs_for_count && language == app->subs_for_language &&
        app->subs_for_generation == g_subs_generation) return;
    app->subs_for_generation = g_subs_generation;
    app->subs_for = of;
    app->subs_for_count = of_count;
    app->subs_for_language = language;
    app->subs_count = 0;
    app->subs_anchor = SIZE_MAX;
    app->subs_hover = app->subs_now = -1;
    app->subs_edit_row = -1;
    app->subs_edit_composing_len = 0;
    app->subs_click_row = -1;
    app->subs_list.first = 0;
    app->subs_scrolled_at = 0.0;
    app->subs_dirty = true;
    if (of_count == 0) return;
    if (of_count > app->subs_room) {
        uint32_t *rows = realloc(app->subs_rows, of_count * sizeof(rows[0]));
        if (rows) app->subs_rows = rows;
        bool *chosen = realloc(app->subs_chosen, of_count * sizeof(chosen[0]));
        if (chosen) app->subs_chosen = chosen;
        bool *marks = realloc(app->subs_marks, of_count * sizeof(marks[0]));
        if (marks) app->subs_marks = marks;
        if (!rows || !chosen || !marks) return;      /* no room: an empty list, and the old room is kept */
        app->subs_room = of_count;
    }
    app->subs_count = kind == SUBS_TEXT ? rubraview_subwin_rows(&app->subtitle, app->subs_rows, of_count) : of_count;
    rubraview_subwin_choose_all(app->subs_chosen, app->subs_room, false);
}

/* The row of the moment playing: the last that has begun. */
static int32_t subs_row_now(const app_state_t *app) {
    if (app->subs_count == 0) return -1;
    if (subs_kind(app) == SUBS_TEXT) {
        return rubraview_subwin_row_at(&app->subtitle, app->subs_rows, app->subs_count, app->media_position);
    }
    size_t low = 0, high = app->subs_count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (subs_row_start(app, mid, NULL) <= app->media_position) low = mid + 1; else high = mid;
    }
    return (int32_t)low - 1;
}

static void subs_focus_amount(app_state_t *app, bool on) {
    if (app->subs_amount_focus == on) return;
    app->subs_amount_focus = on;
    if (app->subs_window) rubraview_pal_window_text_input(app->subs_window, on || app->subs_edit_row >= 0);
    if (!on && app->subs_amount_len == 0) {           /* left empty: the tenth it came with */
        memcpy(app->subs_amount, "0.1", 3);
        app->subs_amount_len = 3;
    }
    app->subs_dirty = true;
}

/* ---- A subtitle put into a Matroska film (RV-115 stage 4, D-128) ---- */

static proven_u8str_view_t putin_temp(const app_state_t *app) { return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->putin.temp, .size = strlen(app->putin.temp) }; }

/* Done, stopped or failed: everything let go; unless the swap happened, the temporary file goes. */
void putin_end(app_state_t *app, bool swapped) {
    if (app->putin.phase == 0 && !app->putin.map.os && !app->putin.made_map.os && !app->putin.writer.os) return;
    if (app->putin.writer.os) rubraview_pal_fs_writer_abort(&app->putin.writer, putin_temp(app));
    if (app->putin.made_map.os) rubraview_pal_fs_unmap(&app->putin.made_map);
    if (app->putin.map.os) rubraview_pal_fs_unmap(&app->putin.map);
    app->putin.made_map = app->putin.map = (rubraview_fs_mapping_t){0};
    if (!swapped && app->putin.temp[0] && rubraview_pal_fs_exists(putin_temp(app))) (void)rubraview_pal_fs_delete(putin_temp(app));
    rubraview_mkv_put_free(&app->putin.put);
    rubraview_mp4_put_free(&app->putin.mp4_put);
    app->putin.phase = 0;
    app->subs_dirty = true;
}

static void putin_fail(app_state_t *app, const char *why) {
    putin_end(app, false);
    char line[300];
    int n = snprintf(line, sizeof(line), T("the subtitle was not put in: %s; the film is as it was"), why ? why : T("it could not be done"));
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1 });
}

/* Why the subtitle on screen cannot go into the film, or NULL when it can be tried. */
static const char *putin_refusal(app_state_t *app) {
    if (!app->media || !app->media_info.has_video || app->media_page < 0 || (size_t)app->media_page >= page_count(app) ||
        app->source.archive_path.size > 0) return T("no film file is open");
    if (subs_kind(app) != SUBS_TEXT) return T("no text subtitle is on");
    if (app->subtitle.source_path.size == 0) return T("the subtitle on is already inside the film");
    if (rubraview_subtitle_changed(&app->subtitle)) return T("save the subtitle's changes first");
    if (app->takeout.running) return T("subtitles are being taken out");
    /* said before the first press arms it; the film's own bytes decide again in `putin_begin` */
    proven_u8str_view_t film = rubraview_path_basename(app->source.pages[app->media_page].path);
    if (rubraview_path_has_ext(film, ".webm")) return T("a WebM film holds no SubRip");
    if ((rubraview_path_has_ext(film, ".mkv") || rubraview_path_has_ext(film, ".mk3d")) &&
app->subtitle.format != RUBRAVIEW_SUBTITLE_SRT && app->subtitle.format != RUBRAVIEW_SUBTITLE_ASS)
        return T("only SubRip and SubStation subtitles are put into a Matroska film");
    return NULL;
}

static bool putin_emit(void *ctx, const void *bytes, size_t size) {
    return rubraview_pal_fs_writer_write((rubraview_fs_writer_t*)ctx, bytes, size);
}

static void putin_begin(app_state_t *app) {
    const char *why = putin_refusal(app);
    if (why) { putin_fail(app, why); return; }
    proven_u8str_view_t film = app->source.pages[app->media_page].path;
    if (film.size == 0 || film.size >= sizeof(app->putin.film)) { putin_fail(app, T("the film's name is too long")); return; }
    memcpy(app->putin.film, film.ptr, film.size);
    app->putin.film[film.size] = '\0';
    int tn = snprintf(app->putin.temp, sizeof(app->putin.temp), "%s.rubraview-part", app->putin.film);
    if (tn <= 0 || (size_t)tn >= sizeof(app->putin.temp)) { putin_fail(app, T("the film's name is too long")); return; }
    if (!rubraview_pal_fs_map(film, &app->putin.map)) { putin_fail(app, T("the film could not be read")); return; }
    proven_u8str_view_t tag = rubraview_subtitle_language_tag(film, app->subtitle.source_path);
    proven_u8str_view_t name = rubraview_path_basename(app->subtitle.source_path);
    uint64_t total = 0;
    app->putin.is_mp4 = false;
    if (!rubraview_mkv_open(&app->putin.mkv, app->putin.map.data, app->putin.map.size, &why)) {
        /* D-130: an MP4 — the subtitle's words, any format, as 3GPP text after the film's own bytes */
        if (!rubraview_mp4_open(&app->putin.mp4, app->putin.map.data, app->putin.map.size, &why)) {
            putin_fail(app, T("subtitles are put into Matroska and MP4 films only"));
            return;
        }
        if (!rubraview_mp4_put_plan(&app->putin.mp4_put, &app->putin.mp4, &app->subtitle, tag.size == 3 ? tag : PROVEN_LIT(""),
                                    name, &why)) {
            putin_fail(app, why);
            return;
        }
        app->putin.is_mp4 = true;
        total = app->putin.mp4_put.total;
    } else if (app->subtitle.format != RUBRAVIEW_SUBTITLE_SRT && app->subtitle.format != RUBRAVIEW_SUBTITLE_ASS) {
        putin_fail(app, T("only SubRip and SubStation subtitles are put into a Matroska film"));
        return;
    }
    if (!app->putin.is_mp4) {
    rubraview_mkv_put_track_t track;
    if (!rubraview_mkv_put_track_from_subtitle(app->arena, &app->subtitle, &track, &why)) { putin_fail(app, why); return; }
    if (tag.size == 3) track.language = tag;                   /* `film.kor.srt`: a three-letter code is Matroska's own */
    track.name = name;
    uint64_t uid = (uint64_t)(rubraview_pal_time_now_seconds() * 1000003.0) ^ 0x5DEECE66Du;
    if (!rubraview_mkv_put_plan(&app->putin.put, &app->putin.mkv, &track, uid, &why)) { putin_fail(app, why); return; }
    total = app->putin.put.total;
    }
    uint64_t free_bytes = 0;
    if (rubraview_pal_fs_free_bytes(rubraview_path_dirname(film), &free_bytes) && free_bytes < total + (64u << 20)) {
        putin_fail(app, T("there is not room on the disk for the film written again"));
        return;
    }
    if (rubraview_pal_fs_exists(putin_temp(app))) (void)rubraview_pal_fs_delete(putin_temp(app));   /* ours, left by a stop */
    if (!rubraview_pal_fs_writer_open(&app->putin.writer, putin_temp(app))) { putin_fail(app, T("the new film could not be made beside the old")); return; }
    app->putin.phase = 1;
    app->putin.percent = -1;
    app->putin.said_at = 0.0;
    osd_say(app, TU8("writing the film with the subtitle in it..."));
}

/* The temporary film is sound: the film takes a numbered name and stays in its folder; the new one takes the film's name. */
static void putin_swap(app_state_t *app) {
    if (app->putin.made_map.os) rubraview_pal_fs_unmap(&app->putin.made_map);
    if (app->putin.map.os) rubraview_pal_fs_unmap(&app->putin.map);
    app->putin.made_map = app->putin.map = (rubraview_fs_mapping_t){0};
    proven_u8str_view_t film = { .ptr = (const proven_byte_t *)app->putin.film, .size = strlen(app->putin.film) };
    proven_u8str_view_t kept = PROVEN_LIT("");
    for (unsigned number = 1; number < 10000 && kept.size == 0; ++number) {
        proven_u8str_view_t name = rubraview_subtitle_backup_path(app->arena, film, number);
        if (name.size == 0) break;
        if (!rubraview_pal_fs_exists(name)) kept = name;
    }
    if (kept.size == 0) { putin_fail(app, T("no free name to keep the film under")); return; }
    bool on_screen = app->media && app->media_page >= 0 && (size_t)app->media_page < page_count(app) &&
                     proven_u8str_view_eq(app->source.pages[app->media_page].path, film);
    int32_t page = current_page_index(app);
    if (!rubraview_pal_fs_move(film, kept)) {
        if (on_screen) media_close(app);                         /* the player may hold it: let go and try again */
        if (!rubraview_pal_fs_move(film, kept)) { putin_fail(app, T("the film could not be renamed")); if (on_screen) reopen_after_removal(app, page >= 0 ? (size_t)page : 0); return; }
    }
    if (!rubraview_pal_fs_move(putin_temp(app), film)) {
        (void)rubraview_pal_fs_move(kept, film);                /* put it back */
        putin_fail(app, T("the new film could not take the film's name"));
        if (on_screen) reopen_after_removal(app, page >= 0 ? (size_t)page : 0);
        return;
    }
    putin_end(app, true);
    char line[400];
    proven_u8str_view_t kept_name = rubraview_path_basename(kept);
    int n = snprintf(line, sizeof(line), T("the subtitle is in the film now; the film as it was is %.*s"), (int)(kept_name.size < 300 ? kept_name.size : 300), kept_name.ptr);
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1 });
    if (on_screen) reopen_on_path(app, film, page >= 0 ? (size_t)page : 0);
}

void putin_step(app_state_t *app) {
    if (app->putin.phase == 0) return;
    double began = rubraview_pal_time_now_seconds();
    if (rubraview_pal_fs_mapping_lost(&app->putin.map)) { putin_fail(app, T("the film went away while it was read")); return; }
    if (app->putin.phase == 1) {
        int r = 1;
        while (r == 1 && rubraview_pal_time_now_seconds() - began < 0.008) {
            r = app->putin.is_mp4 ? rubraview_mp4_put_step(&app->putin.mp4_put, putin_emit, &app->putin.writer, 4u << 20)
                                  : rubraview_mkv_put_step(&app->putin.put, putin_emit, &app->putin.writer, 4u << 20);
        }
        if (r < 0) { putin_fail(app, T("the new film could not be written (is the disk full?)")); return; }
        if (r == 0) {
            if (!rubraview_pal_fs_writer_close(&app->putin.writer)) { putin_fail(app, T("the new film could not be written whole")); return; }
            bool mapped = rubraview_pal_fs_map(putin_temp(app), &app->putin.made_map);
            bool begun = mapped && (app->putin.is_mp4
                ? rubraview_mp4_check_begin(&app->putin.mp4_check, &app->putin.mp4_put, app->putin.made_map.data, app->putin.made_map.size)
                : rubraview_mkv_check_begin(&app->putin.check, &app->putin.put, app->putin.made_map.data, app->putin.made_map.size));
            if (!begun) {
                const char *why = app->putin.is_mp4 ? app->putin.mp4_check.why : app->putin.check.why;
                putin_fail(app, why ? why : T("the new film could not be read back"));
                return;
            }
            app->putin.phase = 2;
        }
    } else {
        int r = 1;
        while (r == 1 && rubraview_pal_time_now_seconds() - began < 0.008) {
            r = app->putin.is_mp4 ? rubraview_mp4_check_step(&app->putin.mp4_check, &app->putin.mp4_put, app->putin.made_map.data, 8u << 20)
                                  : rubraview_mkv_check_step(&app->putin.check, &app->putin.put, 2000);
        }
        if (r < 0) { putin_fail(app, app->putin.is_mp4 ? app->putin.mp4_check.why : app->putin.check.why); return; }
        if (r == 0) { putin_swap(app); return; }
    }
    uint64_t total = app->putin.is_mp4 ? app->putin.mp4_put.total : app->putin.put.total;
    double share = app->putin.phase == 1 ? (total ? (double)app->putin.writer.written / (double)total : 0.0) * 0.5
                 : app->putin.is_mp4 ? 0.5 + (app->putin.mp4.size ? (double)app->putin.mp4_check.at / (double)app->putin.mp4.size : 0.0) * 0.5
                 : 0.5 + rubraview_mkv_walk_share(&app->putin.mkv, &app->putin.check.old_walk) * 0.5;
    int percent = (int)(share * 100.0);
    if (percent != app->putin.percent && began - app->putin.said_at > 0.5) {
        app->putin.percent = percent;
        app->putin.said_at = began;
        char line[80];
        int n = snprintf(line, sizeof(line), "%s... %d %%", app->putin.phase == 1 ? T("writing the film with the subtitle in it") : T("checking the new film against the old"), percent);
        if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
        app->subs_dirty = true;
    }
}

/* The button: a first press says what will happen and asks for a second; the second goes on; during the work it stops. */
static void putin_press(app_state_t *app) {
    double now = rubraview_pal_time_now_seconds();
    if (app->putin.phase != 0) {
        putin_end(app, false);
        osd_say(app, TU8("putting the subtitle in was stopped; the film is as it was"));
        return;
    }
    const char *why = putin_refusal(app);
    if (why) {
        char line[200];
        int n = snprintf(line, sizeof(line), T("cannot put the subtitle in: %s"), why);
        if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1 });
        return;
    }
    if (app->putin.armed_at > 0.0 && now - app->putin.armed_at < 6.0) {
        app->putin.armed_at = 0.0;
        putin_begin(app);
        return;
    }
    app->putin.armed_at = now;
    proven_u8str_view_t film = app->source.pages[app->media_page].path;
    proven_u8str_view_t kept = rubraview_path_basename(rubraview_subtitle_backup_path(app->arena, film, 1));
    proven_u8str_view_t sub = rubraview_path_basename(app->subtitle.source_path);
    char line[600];
    int n = snprintf(line, sizeof(line), T("put %.*s into the film? It is written again and checked; the film as it is stays as %.*s. Press again to go on"),
                     (int)(sub.size < 200 ? sub.size : 200), sub.ptr, (int)(kept.size < 200 ? kept.size : 200), kept.ptr);
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1 });
    app->subs_dirty = true;
}

void subs_close(app_state_t *app) {
    if (!app->subs_open) return;
    rubraview_pal_window_get_frame(app->subs_window, &app->subs_frame[0], &app->subs_frame[1],
                                   &app->subs_frame[2], &app->subs_frame[3]);
    if (app->subs_renderer) rubraview_pal_render_destroy(app->subs_renderer);
    if (app->subs_window) rubraview_pal_window_destroy(app->subs_window);
    window_block_give(app, &app->subs_block);
    app->subs_renderer = NULL;
    app->subs_window = NULL;
    app->subs_open = false;
    app->subs_amount_focus = false;
    app->subs_edit_row = -1;                     /* a text half typed is given up with the window */
}

/* The header and the bar are the list's title; the rows fill the rest. */
static void subs_layout(app_state_t *app) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->subs_window, &w, &h);
    double dpi = rubraview_pal_window_dpi_scale(app->subs_window);
    rubraview_listwin_t *lw = &app->subs_list;
    lw->open = lw->placed = true;
    lw->x = lw->y = 0.0;
    lw->width = (double)w;
    lw->height = (double)h;
    lw->row_height = 24.0 * dpi;
    lw->title_height = 60.0 * dpi;
    app->subs_dirty = true;
}

void subs_show(app_state_t *app) {
    if (app->subs_open) { subs_close(app); return; }   /* the same key puts it away */
    if (!app->media) {
        osd_say(app, TU8("no film is open to list subtitles of"));
        return;
    }
    rubraview_window_config_t config = { .title = T("Rubraview subtitles"), .width = 620, .height = 560, .owner = app->window };
    app->subs_window = rubraview_pal_window_create(window_block_take(app, &app->subs_block), &config);
    if (!app->subs_window) {
        osd_say(app, TU8("the subtitle list could not be opened"));
        return;
    }
    if (app->subs_frame[2] > 0 && app->subs_frame[3] > 0) {
        rubraview_pal_window_set_frame(app->subs_window, app->subs_frame[0], app->subs_frame[1],
                                       app->subs_frame[2], app->subs_frame[3]);
    }
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->subs_window, &w, &h);
    app->subs_renderer = rubraview_pal_render_create(window_block_arena(app, &app->subs_block), rubraview_pal_window_native_handle(app->subs_window), w, h);
    ui_font_apply_all(app);
    if (!app->subs_renderer) {
        rubraview_pal_window_destroy(app->subs_window); window_block_give(app, &app->subs_block);
        app->subs_window = NULL;
        osd_say(app, TU8("the subtitle list could not be drawn"));
        return;
    }
    app->subs_open = true;
    app->subs_list = (rubraview_listwin_t){0};
    app->subs_for = NULL;                        /* the first frame takes the track as new */
    app->subs_for_count = 0;
    app->subs_amount_focus = false;
    app->subs_edit_row = app->subs_click_row = -1;
    if (app->subs_amount_len == 0) { memcpy(app->subs_amount, "0.1", 3); app->subs_amount_len = 3; }
    subs_layout(app);
}

/* Where the parts above the rows are, in the window's own pixels. */
typedef enum { SUBS_PART_ALL = 0, SUBS_PART_EARLIER, SUBS_PART_AMOUNT, SUBS_PART_LATER, SUBS_PART_SAVE, SUBS_PART_TAKEOUT, SUBS_PART_PUTIN } subs_part_t;

static rubraview_pal_rect_t subs_part_rect(const app_state_t *app, subs_part_t part) {
    const rubraview_listwin_t *lw = &app->subs_list;
    double band = lw->title_height * 0.5, rh = lw->row_height;
    double box = rh * 0.6, pad = rh * 0.3, button = rh * 3.6, field = rh * 3.0;
    double y = band + (band - rh * 0.9) * 0.5;
    switch (part) {
        case SUBS_PART_ALL:     return (rubraview_pal_rect_t){ pad, (band - box) * 0.5, box, box };
        case SUBS_PART_EARLIER: return (rubraview_pal_rect_t){ pad, y, button, rh * 0.9 };
        case SUBS_PART_AMOUNT:  return (rubraview_pal_rect_t){ pad * 2.0 + button, y, field, rh * 0.9 };
        case SUBS_PART_LATER:   return (rubraview_pal_rect_t){ pad * 4.0 + button + field + rh * 0.6, y, button, rh * 0.9 };
        case SUBS_PART_SAVE:    return (rubraview_pal_rect_t){ lw->width - pad - button, y, button, rh * 0.9 };   /* at the right */
        case SUBS_PART_TAKEOUT: return (rubraview_pal_rect_t){ lw->width - pad - button, (band - rh * 0.9) * 0.5, button, rh * 0.9 };   /* above Save */
        default:                return (rubraview_pal_rect_t){ lw->width - 2.0 * (pad + button), (band - rh * 0.9) * 0.5, button, rh * 0.9 };   /* Put in, left of Take out */
    }
}

static bool subs_in(rubraview_pal_rect_t r, double x, double y) {
    return x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
}

/* A row's box: at its left, as tall as the text. */
static rubraview_pal_rect_t subs_row_box(const app_state_t *app, rubraview_rect_t row) {
    double box = app->subs_list.row_height * 0.6;
    return (rubraview_pal_rect_t){ row.x + app->subs_list.row_height * 0.3, row.y + (row.height - box) * 0.5, box, box };
}

static void subs_go_to(app_state_t *app, size_t row) {
    if (row >= app->subs_count) return;
    /* a hair inside, so the subtitle itself is on screen */
    play_seek(app, subs_row_start(app, row, NULL) + 0.01);
    app->subs_scrolled_at = 0.0;
    app->subs_dirty = true;
}

/* Earlier (-1) or Later (+1): the rows with their boxes ticked, by the seconds in the field. */
static void subs_move(app_state_t *app, int sign) {
    double amount = 0.0;
    if (subs_kind(app) != SUBS_TEXT) {
        osd_say(app, TU8("only text subtitles can be moved"));
        return;
    }
    if (!rubraview_subwin_amount((proven_u8str_view_t){ (const proven_byte_t *)app->subs_amount, app->subs_amount_len }, &amount)) {
        osd_say(app, TU8("the seconds to move by are not a number above 0"));
        return;
    }
    size_t picked = rubraview_subwin_chosen_count(app->subs_chosen, app->subs_count);
    if (picked == 0) {
        osd_say(app, TU8("tick the subtitles to move"));
        return;
    }
    if (app->subtitle.count > app->subs_room) return;
    size_t moved = rubraview_subwin_shift(&app->subtitle, app->subs_rows, app->subs_chosen, app->subs_count,
                                          sign < 0 ? -amount : amount, app->subs_marks);
    char line[96];
    int n = snprintf(line, sizeof(line), T("%zu subtitle%s %.*s s %s"), moved, moved == 1 ? "" : TC("plural", "s"),
                     (int)app->subs_amount_len, app->subs_amount, sign < 0 ? "earlier" : "later");
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
    app->subs_dirty = true;
}

/* A row's text typed over (RV-115 stage 2): what the file holds for it,
   markup and all, on one line — `\N` is a line break. Enter keeps it, Esc
   gives it up; a press elsewhere keeps it too. */
/* RV-115: the rows made again after a cue came, went or moved; the row of `cue` (-1: none) is the one chosen. */
static void subs_rows_again(app_state_t *app, int32_t cue) {
    app->subs_for = NULL;
    subs_sync(app);
    for (size_t r = 0; cue >= 0 && r < app->subs_count; ++r) {
        if (app->subs_rows[r] != (uint32_t)cue) continue;
        app->subs_anchor = r;
        rubraview_listwin_reveal(&app->subs_list, r, app->subs_count);
        break;
    }
    app->subs_dirty = true;
}

static void subs_edit_end(app_state_t *app, bool keep) {
    if (app->subs_edit_row < 0) return;
    size_t row = (size_t)app->subs_edit_row;
    bool times = app->subs_edit_time;
    app->subs_edit_row = -1;
    app->subs_edit_time = false;
    app->subs_edit_composing_len = 0;
    if (app->subs_window && !app->subs_amount_focus) rubraview_pal_window_text_input(app->subs_window, false);
    app->subs_dirty = true;
    if (!keep || subs_kind(app) != SUBS_TEXT || row >= app->subs_count) return;
    size_t cue = app->subs_rows[row];
    proven_u8str_view_t typed = rubraview_textedit_text(&app->subs_edit);
    if (times) {
        /* RV-115: `0:01:02.50 --> 0:01:04.00`, as the row showed them */
        size_t arrow = SIZE_MAX;
        for (size_t i = 0; i + 2 < typed.size && arrow == SIZE_MAX; ++i) {
            if (typed.ptr[i] == '-' && typed.ptr[i + 1] == '-' && typed.ptr[i + 2] == '>') arrow = i;
        }
        double start = 0.0, end = 0.0;
        if (arrow == SIZE_MAX ||
            !rubraview_subtitle_parse_time((proven_u8str_view_t){ .ptr = typed.ptr, .size = arrow }, &start) ||
            !rubraview_subtitle_parse_time((proven_u8str_view_t){ .ptr = typed.ptr + arrow + 3, .size = typed.size - arrow - 3 }, &end)) {
            osd_say(app, TU8("type the times as 0:01:02.50 --> 0:01:04.00"));
            return;
        }
        int32_t at = rubraview_subtitle_cue_set_time(&app->subtitle, cue, start, end);
        if (at < 0) { osd_say(app, TU8("a subtitle ends after it starts")); return; }
        subs_rows_again(app, at);
        return;
    }
    proven_u8str_view_t was = rubraview_subtitle_cue_typed(subs_arena(app, 256u << 10), &app->subtitle, cue);
    if (typed.size == was.size && memcmp(typed.ptr, was.ptr, typed.size) == 0) return;
    if (!rubraview_subtitle_cue_retype(subs_arena(app, 256u << 10), &app->subtitle, cue, typed)) {
        osd_say(app, TU8("a subtitle cannot be left with nothing to show"));
    }
}

static void subs_edit_begin_as(app_state_t *app, size_t row, bool times) {
    if (subs_kind(app) != SUBS_TEXT || row >= app->subs_count) return;
    subs_edit_end(app, true);
    proven_u8str_view_t typed = rubraview_subtitle_cue_typed(subs_arena(app, 256u << 10), &app->subtitle, app->subs_rows[row]);
    char clock[96];
    if (times) {   /* RV-115: the two times, to type over */
        const rubraview_subtitle_cue_t *c = &app->subtitle.cues[app->subs_rows[row]];
        char a[32], b[32];
        proven_u8str_view_t ta = rubraview_subwin_time(a, sizeof(a), c->start_seconds), tb = rubraview_subwin_time(b, sizeof(b), c->end_seconds);
        int n = snprintf(clock, sizeof(clock), "%.*s --> %.*s", (int)ta.size, ta.ptr, (int)tb.size, tb.ptr);
        typed = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)clock, .size = n > 0 ? (size_t)n : 0 };
    }
    if (typed.size == 0) {
        osd_say(app, TU8("this subtitle's text has no place in a file to edit"));
        return;
    }
    if (typed.size + 1 >= sizeof(app->subs_edit_buffer)) {
        osd_say(app, TU8("this subtitle is too long to edit here"));
        return;
    }
    subs_focus_amount(app, false);
    app->subs_edit = rubraview_textedit_make(app->subs_edit_buffer, sizeof(app->subs_edit_buffer));
    rubraview_textedit_set(&app->subs_edit, typed);
    rubraview_textedit_move(&app->subs_edit, RUBRAVIEW_TEXTEDIT_END, false);
    app->subs_edit_row = (int32_t)row;
    app->subs_edit_time = times;
    app->subs_edit_composing_len = 0;
    rubraview_pal_window_text_input(app->subs_window, true);
    app->subs_dirty = true;
}

static void subs_edit_begin(app_state_t *app, size_t row) {
    subs_edit_begin_as(app, row, false);
}

/* RV-115: Insert — a new subtitle where the film is, two seconds long, its words typed at once. */
static void subs_add(app_state_t *app) {
    if (subs_kind(app) != SUBS_TEXT) { osd_say(app, TU8("only a text subtitle takes new lines")); return; }
    subs_edit_end(app, true);
    double at = app->media_position - app->subtitle.offset_seconds;
    if (!(at >= 0.0)) at = 0.0;
    int32_t cue = rubraview_subtitle_cue_add(subs_arena(app, subs_edit_room(app)), &app->subtitle, at, at + 2.0, PROVEN_LIT("..."));
    if (cue < 0) { osd_say(app, TU8("a new subtitle is written like one already in the file, and there is none")); return; }
    subs_rows_again(app, cue);
    if (app->subs_anchor < app->subs_count) subs_edit_begin(app, app->subs_anchor);
    osd_say(app, TU8("a new subtitle here, two seconds long: type its words, Enter keeps them (Ctrl+T its times)"));
}

/* RV-115: Delete — the ticked rows, or the one last clicked, out (in the file when saved). */
static void subs_remove(app_state_t *app) {
    if (subs_kind(app) != SUBS_TEXT) return;
    subs_edit_end(app, true);
    size_t gone = 0, count = app->subs_count;
    bool any = rubraview_subwin_chosen_count(app->subs_chosen, count) > 0;
    for (size_t r = 0; r < count; ++r) {
        if (any ? !app->subs_chosen[r] : r != app->subs_anchor) continue;
        if (rubraview_subtitle_cue_remove(&app->subtitle, app->subs_rows[r])) gone++;
    }
    if (gone == 0) { osd_say(app, TU8("click a row, or tick some, to take out")); return; }
    subs_rows_again(app, -1);
    char line[96];
    int n = snprintf(line, sizeof(line), T("%zu taken out; Save writes it, and the file keeps them until then"), gone);
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
}

/* Save: the file written again in its own format with only the changed
   places new (D-126). The new file takes the subtitle's name; the one
   before is kept beside it as `name.old1.ext`, the first number free. */
static void subs_save(app_state_t *app) {
    subs_edit_end(app, true);
    if (subs_kind(app) != SUBS_TEXT) { osd_say(app, TU8("only text subtitles can be saved")); return; }
    if (!rubraview_subtitle_changed(&app->subtitle) && app->subtitle.offset_seconds == 0.0) { osd_say(app, TU8("nothing was changed")); return; }
    if (app->subtitle.source_path.size == 0) {
        osd_say(app, TU8("this subtitle is inside the film: it cannot be saved yet"));
        return;
    }
    /* Z / X's offset goes into the times written, and is 0 after (RV-115). */
    rubraview_subtitle_track_t folded = rubraview_subtitle_offset_folded(subs_arena(app, subs_edit_room(app)), &app->subtitle);
    if (folded.count == 0) { osd_say(app, TU8("not saved: no memory")); return; }
    const rubraview_subtitle_track_t *track = &folded;
    /* RFC-0006: the file written and read back is made in the idle half of `subs`, which
       then becomes the one in use; the half before (the track as it was, its edits) is
       emptied at the next save. */
    /* the file made, read back and re-encoded, and its cues parsed twice (as saved, and with a mark) */
    size_t save_room = track->source.size * 8u + 2u * subs_edit_room(app);
    proven_arena_t *to = app->mem ? rubraview_halves_next(&app->mem->subs, save_room) : NULL;
    if (!to) to = app->arena;
    const char *why = NULL;
    proven_u8str_view_t next = rubraview_subtitle_patched(to, track, &why);
    if (next.size == 0) {
        char line[160];
        int n = snprintf(line, sizeof(line), T("not saved: %s"), why ? why : T("the file could not be written again"));
        if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
        return;
    }
    /* The file must still be what was read, and read back the same in its own encoding. */
    proven_u8str_view_t disk = rubraview_pal_fs_read_file(to, track->source_path, SUBTITLE_MAX_BYTES);
    proven_u8str_view_t was = track->source_transcoded ? rubraview_pal_encode_codepage(to, track->source, 0) : track->source;
    if (disk.size == 0 || was.size != disk.size || memcmp(was.ptr, disk.ptr, disk.size) != 0) {
        osd_say(app, track->source_transcoded && was.size == 0
                         ? TU8("not saved: the file does not read back the same in its encoding")
                         : TU8("not saved: the file was changed on disk since it was read"));
        return;
    }
    proven_u8str_view_t bytes = next;
    bool now_utf8 = false;
    if (track->source_transcoded) {
        bytes = rubraview_pal_encode_codepage(to, next, 0);
        if (bytes.size == 0) {
            /* RV-115: the file's code page has no such character — UTF-8 is offered, and a second Save takes it */
            double now = rubraview_pal_time_now_seconds();
            if (!(app->subs_utf8_armed > 0.0 && now - app->subs_utf8_armed < 6.0)) {
                app->subs_utf8_armed = now;
                osd_say(app, TU8("a character typed is not in this file's encoding: Save again within 6 s writes the file as UTF-8"));
                return;
            }
            app->subs_utf8_armed = 0.0;
            proven_result_mem_mut_t res = proven_arena_alloc(to, next.size + 4);
            if (!proven_is_ok(res.err)) { osd_say(app, TU8("not saved: no memory")); return; }
            char *with_bom = (char*)(void*)res.value.ptr;
            memcpy(with_bom, "\xEF\xBB\xBF", 3);   /* a mark players read as UTF-8 */
            memcpy(with_bom + 3, next.ptr, next.size);
            bytes = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)with_bom, .size = next.size + 3 };
            now_utf8 = true;
        }
    }
    proven_u8str_view_t kept = PROVEN_LIT("");
    for (unsigned number = 1; number < 10000; ++number) {
        proven_u8str_view_t name = rubraview_subtitle_backup_path(to, track->source_path, number);
        if (name.size == 0) break;
        if (!rubraview_pal_fs_exists(name)) { kept = name; break; }
    }
    if (kept.size == 0 || !rubraview_pal_fs_move(track->source_path, kept)) {
        osd_say(app, TU8("not saved: the file before could not be renamed"));
        return;
    }
    if (!rubraview_pal_fs_write_file(track->source_path, bytes)) {
        (void)rubraview_pal_fs_move(kept, track->source_path);   /* put it back */
        osd_say(app, TU8("not saved: the new file could not be written"));
        return;
    }
    /* Read as saved: the places are the new file's, and nothing is changed. */
    rubraview_subtitle_track_t fresh = rubraview_subtitle_parse(to, next, track->format);
    /* the language shown is a name in the track before: kept with the new one */
    proven_u8str_view_t shown = PROVEN_LIT("");
    if (track->shown_language.size > 0) {
        proven_result_mem_mut_t res = proven_arena_alloc(to, track->shown_language.size + 1);
        if (proven_is_ok(res.err)) {
            memcpy(res.value.ptr, track->shown_language.ptr, track->shown_language.size);
            ((char*)(void*)res.value.ptr)[track->shown_language.size] = '\0';
            shown = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(const char*)(void*)res.value.ptr, .size = track->shown_language.size };
        } else {
            fresh.count = 0;   /* not read back whole: kept as it was (below) */
        }
    }
    if (fresh.count > 0) {
        fresh.offset_seconds = 0.0;
        fresh.shown_language = shown;
        fresh.source_path = track->source_path;
        fresh.source_transcoded = track->source_transcoded && !now_utf8;
        if (now_utf8) {   /* read as it is on the disk now: the mark first */
            rubraview_subtitle_track_t marked = rubraview_subtitle_parse(to, bytes, track->format);
            if (marked.count > 0) {
                marked.shown_language = shown;
                marked.source_path = track->source_path;
                fresh = marked;
            }
        }
        app->subtitle = fresh;
        if (app->mem && to != app->arena) { rubraview_halves_swap(&app->mem->subs); g_subs_generation++; }
    } else {
        app->subtitle = folded;   /* its times are the file's now */
        for (size_t i = 0; i < folded.count; ++i) folded.cues[i].time_changed = folded.cues[i].text_changed = false;
    }
    app->subtitle_offset_seconds = 0.0;   /* written in: this film's remembered offset is spent */
    char line[400];
    proven_u8str_view_t kept_name = rubraview_path_basename(kept);
    int n = snprintf(line, sizeof(line), T("saved; the file before is %.*s"), (int)(kept_name.size < 300 ? kept_name.size : 300), kept_name.ptr);
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
    app->subs_dirty = true;
}

static void subs_draw_box(rubraview_renderer_t *r, rubraview_pal_rect_t box, bool on, bool some) {
    rubraview_pal_render_stroke_rect(r, box, 0xB0F0F0F0u, 1.0, 2.0);
    double inset = on ? box.width * 0.22 : box.width * 0.34;
    rubraview_pal_rect_t in = { box.x + inset, box.y + inset, box.width - 2.0 * inset, box.height - 2.0 * inset };
    if (on) rubraview_pal_render_fill_rect(r, in, COLOR_TILE_CURRENT, 1.0);
    else if (some) rubraview_pal_render_fill_rect(r, in, 0x90F0F0F0u, 1.0);
}

void draw_subs_window(app_state_t *app) {
    if (!app->subs_open) return;
    subs_sync(app);
    int kind = subs_kind(app);
    size_t count = app->subs_count;
    int32_t now_row = subs_row_now(app);
    if (now_row != app->subs_now) {
        app->subs_now = now_row;
        /* follow the film, unless the reader has just moved the list */
        if (now_row >= 0 && rubraview_pal_time_now_seconds() - app->subs_scrolled_at > 4.0) {
            rubraview_listwin_reveal(&app->subs_list, (size_t)now_row, count);
        }
        app->subs_dirty = true;
    }
    if (!app->subs_dirty) return;
    app->subs_dirty = false;

    rubraview_renderer_t *r = app->subs_renderer;
    const rubraview_listwin_t *lw = &app->subs_list;
    double font = lw->row_height * 0.5, band = lw->title_height * 0.5, pad = lw->row_height * 0.3;
    rubraview_pal_render_begin(r, FILES_BACKGROUND);
    rubraview_pal_rect_t head_box = { 0.0, 0.0, lw->width, lw->title_height };
    rubraview_pal_render_fill_rect(r, head_box, COLOR_TILE_FILL, 0.0);

    /* The header: the box for all, how many there are and how many are ticked, whose they are. */
    size_t picked = kind == SUBS_TEXT ? rubraview_subwin_chosen_count(app->subs_chosen, count) : 0;
    char head[400], cut[640];
    int hn;
    if (kind == SUBS_NONE) {
        hn = snprintf(head, sizeof(head), "%s", app->media ? T("no subtitle is on") : T("no film is open"));
    } else {
        proven_u8str_view_t name = app->subtitle_name;
        hn = snprintf(head, sizeof(head), T("%zu subtitle%s   %zu ticked   %.*s%s"), count, count == 1 ? "" : TC("plural", "s"), picked,
                      (int)(name.size < 200 ? name.size : 200), name.ptr, kind == SUBS_TEXT && rubraview_subtitle_changed(&app->subtitle) ? T("   (changed, not saved)") : "");
    }
    rubraview_pal_rect_t all = subs_part_rect(app, SUBS_PART_ALL);
    double head_x = pad;
    if (kind == SUBS_TEXT) {
        subs_draw_box(r, all, count > 0 && picked == count, picked > 0);
        head_x = all.x + all.width + pad;
    }
    /* Take out: the film's own subtitles written beside it (a Matroska film; it says so of another). */
    rubraview_pal_rect_t takeout = subs_part_rect(app, SUBS_PART_TAKEOUT);
    bool takeout_shown = app->media && app->media_info.has_video && takeout.x > head_x + lw->row_height * 4.0;
    if (takeout_shown) {
        char label[32];
        int ln = app->takeout.running ? snprintf(label, sizeof(label), T("Stop %d %%"), app->takeout.percent < 0 ? 0 : app->takeout.percent)
                                      : snprintf(label, sizeof(label), T("Take out"));
        rubraview_pal_render_fill_rect(r, takeout, app->takeout.running ? COLOR_TILE_CURRENT : COLOR_BOX_FILL, 3.0);
        rubraview_pal_render_stroke_rect(r, takeout, COLOR_BOX_BORDER, 1.0, 3.0);
        if (ln > 0) rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ (const proven_byte_t *)label, (size_t)ln }, takeout, font, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
    rubraview_pal_rect_t putin = subs_part_rect(app, SUBS_PART_PUTIN);
    bool putin_shown = takeout_shown && kind == SUBS_TEXT && putin.x > head_x + lw->row_height * 4.0;
    if (putin_shown) {
        double now = rubraview_pal_time_now_seconds();
        bool armed = app->putin.armed_at > 0.0 && now - app->putin.armed_at < 6.0;
        char label[32];
        int ln = app->putin.phase ? snprintf(label, sizeof(label), T("Stop %d %%"), app->putin.percent < 0 ? 0 : app->putin.percent)
                                  : snprintf(label, sizeof(label), "%s", armed ? T("Sure? Put in") : T("Put in film"));
        rubraview_pal_render_fill_rect(r, putin, app->putin.phase || armed ? COLOR_TILE_CURRENT : COLOR_BOX_FILL, 3.0);
        rubraview_pal_render_stroke_rect(r, putin, COLOR_BOX_BORDER, 1.0, 3.0);
        if (ln > 0) rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ (const proven_byte_t *)label, (size_t)ln }, putin, font * 0.95, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        if (armed) app->subs_dirty = true;            /* the question goes away by itself */
    }
    rubraview_pal_rect_t head_text = { head_x, 0.0, (putin_shown ? putin.x : takeout_shown ? takeout.x : lw->width) - head_x - pad, band };
    if (hn > 0) rubraview_pal_render_draw_text(r, files_fit_text(cut, sizeof(cut), (proven_u8str_view_t){ (const proven_byte_t *)head, (size_t)(hn < (int)sizeof(head) ? hn : (int)sizeof(head) - 1) },
                                                                 head_text.width, font), head_text, font, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);

    /* The bar: Earlier, the seconds, Later. */
    if (kind == SUBS_TEXT) {
        rubraview_pal_rect_t earlier = subs_part_rect(app, SUBS_PART_EARLIER), amount = subs_part_rect(app, SUBS_PART_AMOUNT),
                             later = subs_part_rect(app, SUBS_PART_LATER);
        rubraview_pal_render_fill_rect(r, earlier, COLOR_BOX_FILL, 3.0);
        rubraview_pal_render_stroke_rect(r, earlier, COLOR_BOX_BORDER, 1.0, 3.0);
        rubraview_pal_render_draw_text(r, TU8("< Earlier"), earlier, font, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        rubraview_pal_render_fill_rect(r, later, COLOR_BOX_FILL, 3.0);
        rubraview_pal_render_stroke_rect(r, later, COLOR_BOX_BORDER, 1.0, 3.0);
        rubraview_pal_render_draw_text(r, TU8("Later >"), later, font, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        rubraview_pal_render_fill_rect(r, amount, 0xFF101010u, 2.0);
        rubraview_pal_render_stroke_rect(r, amount, app->subs_amount_focus ? COLOR_TILE_CURRENT : COLOR_BOX_BORDER, 1.0, 2.0);
        char typed[24];
        int tn = snprintf(typed, sizeof(typed), "%.*s%s", (int)app->subs_amount_len, app->subs_amount, app->subs_amount_focus ? "|" : "");
        if (tn > 0) rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ (const proven_byte_t *)typed, (size_t)tn }, amount, font, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        rubraview_pal_rect_t unit = { amount.x + amount.width + pad * 0.5, amount.y, lw->row_height * 0.8, amount.height };
        rubraview_pal_render_draw_text(r, PROVEN_LIT("s"), unit, font, 0xB0F0F0F0u, RUBRAVIEW_TEXT_LEFT);
        rubraview_pal_rect_t save = subs_part_rect(app, SUBS_PART_SAVE);
        if (save.x > later.x + later.width + pad) {          /* room for it beside Later */
            bool changed = rubraview_subtitle_changed(&app->subtitle) || app->subtitle.offset_seconds != 0.0;   /* Z / X is saved too */
            rubraview_pal_render_fill_rect(r, save, changed ? COLOR_TILE_CURRENT : COLOR_BOX_FILL, 3.0);
            rubraview_pal_render_stroke_rect(r, save, COLOR_BOX_BORDER, 1.0, 3.0);
            rubraview_pal_render_draw_text(r, TU8("Save"), save, font, changed ? COLOR_TEXT : 0x70F0F0F0u, RUBRAVIEW_TEXT_CENTER);
        }
    } else if (kind != SUBS_NONE) {
        rubraview_pal_rect_t note = { pad, band, lw->width - 2.0 * pad, band };
        rubraview_pal_render_draw_text(r, TU8("picture subtitles: a click goes there; they cannot be moved or edited"), note,
                                       font * 0.9, 0xB0F0F0F0u, RUBRAVIEW_TEXT_LEFT);
    }

    /* The rows: the box, when it comes and when it goes, what it says. */
    size_t rows = rubraview_listwin_rows_visible(lw);
    double time_w = font * 0.56 * 11.0;
    for (size_t i = lw->first; i < count && i < lw->first + rows; ++i) {
        rubraview_rect_t rr = rubraview_listwin_row_rect(lw, i, count);
        rubraview_pal_rect_t row = { rr.x + 2.0, rr.y, rr.width - 8.0, rr.height };
        if ((int32_t)i == app->subs_now) rubraview_pal_render_fill_rect(r, row, (COLOR_TILE_CURRENT & 0x00FFFFFFu) | 0x90000000u, 2.0);
        else if ((int32_t)i == app->subs_hover) rubraview_pal_render_fill_rect(r, row, COLOR_TILE_FILL, 2.0);
        double x = row.x + pad;
        if (kind == SUBS_TEXT) {
            rubraview_pal_rect_t box = subs_row_box(app, rr);
            subs_draw_box(r, box, app->subs_chosen[i], false);
            x = box.x + box.width + pad;
        }
        char clock[32], line[1200];   /* a text being typed, the IME's part and the caret fit */
        double end = -1.0, start = subs_row_start(app, i, &end);
        rubraview_pal_rect_t start_box = { x, row.y, time_w, row.height };
        rubraview_pal_render_draw_text(r, rubraview_subwin_time(clock, sizeof(clock), start), start_box, font * 0.9, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
        x += time_w;
        if (end >= 0.0) {
            rubraview_pal_rect_t end_box = { x, row.y, time_w, row.height };
            rubraview_pal_render_draw_text(r, rubraview_subwin_time(clock, sizeof(clock), end), end_box, font * 0.9, 0xB0F0F0F0u, RUBRAVIEW_TEXT_LEFT);
        }
        x += time_w;
        proven_u8str_view_t said = kind == SUBS_TEXT ? rubraview_subwin_line(line, sizeof(line), app->subtitle.cues[app->subs_rows[i]].text)
                                         : TU8("(picture)");
        rubraview_pal_rect_t text = { x, row.y, row.x + row.width - x - pad, row.height };
        if (kind == SUBS_TEXT && (app->subtitle.cues[app->subs_rows[i]].time_changed || app->subtitle.cues[app->subs_rows[i]].text_changed ||
                                  app->subtitle.cues[app->subs_rows[i]].added)) {
            rubraview_pal_rect_t mark = { row.x, row.y + 3.0, 3.0, row.height - 6.0 };   /* changed, not saved */
            rubraview_pal_render_fill_rect(r, mark, 0xFFF0C040u, 1.0);
        }
        if ((int32_t)i == app->subs_edit_row && text.width > 0.0) {
            /* The text being typed: the file's own words with the caret, the IME's part before it. */
            rubraview_pal_rect_t field = { text.x - pad * 0.5, row.y + 2.0, text.width + pad * 0.5, row.height - 4.0 };
            rubraview_pal_render_fill_rect(r, field, 0xFF101010u, 2.0);
            rubraview_pal_render_stroke_rect(r, field, COLOR_TILE_CURRENT, 1.0, 2.0);
            const rubraview_textedit_t *te = &app->subs_edit;
            size_t caret = te->caret <= te->len ? te->caret : te->len, comp = app->subs_edit_composing_len;
            /* Begin far enough on that the caret is in the field. */
            size_t room = font > 0.0 ? (size_t)(text.width / (font * 0.56)) : 0, from = caret, used = comp + 2;
            while (from > 0) {
                size_t prev = from - 1;
                while (prev > 0 && ((unsigned char)te->buf[prev] & 0xC0) == 0x80) prev--;
                size_t wide = (unsigned char)te->buf[prev] >= 0xE0 ? 2 : 1;
                if (used + wide > room) break;
                used += wide;
                from = prev;
            }
            size_t n = 0;
            memcpy(line + n, te->buf + from, caret - from); n += caret - from;
            memcpy(line + n, app->subs_edit_composing, comp); n += comp;
            line[n++] = '|';
            memcpy(line + n, te->buf + caret, te->len - caret); n += te->len - caret;
            rubraview_pal_render_draw_text(r, files_fit_text(cut, sizeof(cut), (proven_u8str_view_t){ (const proven_byte_t *)line, n }, text.width, font), text, font,
                                           COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
        } else
        if (text.width > 0.0) rubraview_pal_render_draw_text(r, files_fit_text(cut, sizeof(cut), said, text.width, font), text, font,
                                                             kind == SUBS_TEXT ? COLOR_TEXT : 0xB0F0F0F0u, RUBRAVIEW_TEXT_LEFT);
    }
    if (count > rows && rows > 0) {              /* where in a long list the rows are */
        double track = lw->height - lw->title_height;
        double bh = track * (double)rows / (double)count;
        if (bh < lw->row_height * 0.5) bh = lw->row_height * 0.5;
        double by = lw->y + lw->title_height + (track - bh) * (double)lw->first / (double)(count - rows);
        rubraview_pal_rect_t bar = { lw->x + lw->width - 5.0, by, 3.0, bh };
        rubraview_pal_render_fill_rect(r, bar, COLOR_BOX_BORDER | 0x60000000u, 1.5);
    }
    if (!rubraview_pal_render_end(r)) subs_close(app);   /* its device went: the key opens it again */
}

static void subs_press(app_state_t *app, double x, double y, uint32_t modifiers) {
    int kind = subs_kind(app);
    size_t count = app->subs_count, index = 0;
    bool on_amount = kind == SUBS_TEXT && subs_in(subs_part_rect(app, SUBS_PART_AMOUNT), x, y);
    subs_edit_end(app, true);                    /* a press anywhere keeps what was typed */
    subs_focus_amount(app, on_amount);
    if (on_amount) return;
    /* The two buttons answer whatever is on: a press that does nothing says why (a film not up yet). */
    if (subs_in(subs_part_rect(app, SUBS_PART_PUTIN), x, y) && y < app->subs_list.title_height * 0.5) {
        putin_press(app);
        app->subs_dirty = true;
        return;
    }
    if (subs_in(subs_part_rect(app, SUBS_PART_TAKEOUT), x, y)) {
        takeout_begin(app);
        app->subs_dirty = true;
        return;
    }
    if (kind == SUBS_TEXT && y < app->subs_list.title_height) {
        rubraview_pal_rect_t all = subs_part_rect(app, SUBS_PART_ALL);
        /* the box for all, and a little around it: it is small */
        rubraview_pal_rect_t all_zone = { 0.0, 0.0, all.x + all.width + app->subs_list.row_height * 0.3, app->subs_list.title_height * 0.5 };
        if (subs_in(all_zone, x, y)) {
            bool every = count > 0 && rubraview_subwin_chosen_count(app->subs_chosen, count) == count;
            rubraview_subwin_choose_all(app->subs_chosen, count, !every);
        } else if (subs_in(subs_part_rect(app, SUBS_PART_EARLIER), x, y)) {
            subs_move(app, -1);
        } else if (subs_in(subs_part_rect(app, SUBS_PART_LATER), x, y)) {
            subs_move(app, +1);
        } else if (subs_in(subs_part_rect(app, SUBS_PART_SAVE), x, y)) {
            subs_save(app);
        }
        app->subs_dirty = true;
        return;
    }
    if (rubraview_listwin_hit(&app->subs_list, x, y, count, &index) != RUBRAVIEW_LISTWIN_ROW) return;
    if (kind != SUBS_TEXT) { subs_go_to(app, index); return; }
    rubraview_rect_t rr = rubraview_listwin_row_rect(&app->subs_list, index, count);
    rubraview_pal_rect_t box = subs_row_box(app, rr);
    bool on_box = x < box.x + box.width + app->subs_list.row_height * 0.3;
    bool ctrl = on_box || (modifiers & RUBRAVIEW_MOD_CTRL) != 0;
    if (rubraview_subwin_click(app->subs_chosen, count, &app->subs_anchor, index, ctrl, (modifiers & RUBRAVIEW_MOD_SHIFT) != 0)) {
        /* The first click goes there; a second one soon after edits the text. */
        double now = rubraview_pal_time_now_seconds();
        if (app->subs_click_row == (int32_t)index && now - app->subs_click_at < 0.45) {
            app->subs_click_row = -1;
            /* RV-115: on the two times, they are typed; on the words, the words */
            double pad = app->subs_list.row_height * 0.3, time_w = app->subs_list.row_height * 0.5 * 0.56 * 11.0;
            subs_edit_begin_as(app, index, x < box.x + box.width + pad + time_w * 2.0);
        } else {
            app->subs_click_row = (int32_t)index;
            app->subs_click_at = now;
            subs_go_to(app, index);
        }
    }
    app->subs_dirty = true;
}

size_t subs_pump(app_state_t *app) {
    size_t handled = 0;
    rubraview_window_event_t event;
    while (app->subs_open && rubraview_pal_window_poll_event(app->subs_window, &event)) {
        handled++;
        size_t count = app->subs_count, index = 0;
        switch (event.kind) {
            case RUBRAVIEW_WINDOW_EVENT_CLOSE:
                subs_close(app);
                return handled;
            case RUBRAVIEW_WINDOW_EVENT_RESIZE:
                rubraview_pal_render_resize(app->subs_renderer, event.resize.width, event.resize.height);
                subs_layout(app);
                break;
            case RUBRAVIEW_WINDOW_EVENT_PAINT:
            case RUBRAVIEW_WINDOW_EVENT_DPI_CHANGED:
                subs_layout(app);
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE: {
                int32_t was = app->subs_hover;
                app->subs_hover = rubraview_listwin_hit(&app->subs_list, event.mouse.x, event.mouse.y, count, &index) == RUBRAVIEW_LISTWIN_ROW
                                    ? (int32_t)index : -1;
                if (app->subs_hover != was) app->subs_dirty = true;
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_LEAVE:
                if (app->subs_hover != -1) { app->subs_hover = -1; app->subs_dirty = true; }
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN:
                if (event.mouse.button == RUBRAVIEW_MOUSE_LEFT) subs_press(app, event.mouse.x, event.mouse.y, event.mouse.modifiers);
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_WHEEL:
                rubraview_listwin_scroll(&app->subs_list, event.mouse.wheel_delta > 0 ? -3 : 3, count);
                app->subs_scrolled_at = rubraview_pal_time_now_seconds();
                app->subs_hover = rubraview_listwin_hit(&app->subs_list, event.mouse.x, event.mouse.y, count, &index) == RUBRAVIEW_LISTWIN_ROW
                                    ? (int32_t)index : -1;
                app->subs_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_COMPOSITION:
                if (app->subs_edit_row < 0) break;
                app->subs_edit_composing_len = event.text.length < sizeof(app->subs_edit_composing) ? event.text.length : 0;
                memcpy(app->subs_edit_composing, event.text.utf8, app->subs_edit_composing_len);
                app->subs_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_TEXT:
                if (app->subs_edit_row >= 0) {
                    app->subs_edit_composing_len = 0;
                    (void)rubraview_textedit_insert(&app->subs_edit, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)event.text.utf8, .size = event.text.length });
                    app->subs_dirty = true;
                    break;
                }
                if (!app->subs_amount_focus) break;
                for (size_t i = 0; i < event.text.length; ++i) {     /* digits and one point; nothing else is a time */
                    char c = event.text.utf8[i];
                    bool point = (c == '.' || c == ',') && !memchr(app->subs_amount, '.', app->subs_amount_len);
                    if (!((c >= '0' && c <= '9') || point) || app->subs_amount_len + 1 >= 8) continue;
                    app->subs_amount[app->subs_amount_len++] = point ? '.' : c;
                }
                app->subs_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN: {
                rubraview_key_combo_t combo = event.key.combo;
                long rows = (long)rubraview_listwin_rows_visible(&app->subs_list);
                long step = 0;
                if (app->subs_amount_focus) {
                    /* the field has the keys: Backspace takes a character, Enter or Esc leaves it */
                    if (key_is(combo, "Backspace")) { if (app->subs_amount_len > 0) app->subs_amount_len--; }
                    else if (key_is(combo, "Enter") || key_is(combo, "Escape") || key_is(combo, "Tab")) subs_focus_amount(app, false);
                    app->subs_dirty = true;
                    break;
                }
                if (app->subs_edit_row >= 0) {
                    /* the text being typed has the keys */
                    bool extend = (combo.modifiers & RUBRAVIEW_MOD_SHIFT) != 0;
                    if (key_is(combo, "Enter")) subs_edit_end(app, true);
                    else if (key_is(combo, "Escape")) subs_edit_end(app, false);
                    else if (key_is(combo, "Backspace")) rubraview_textedit_backspace(&app->subs_edit);
                    else if (key_is(combo, "Delete")) rubraview_textedit_delete(&app->subs_edit);
                    else if (key_is(combo, "Left")) rubraview_textedit_move(&app->subs_edit, RUBRAVIEW_TEXTEDIT_LEFT, extend);
                    else if (key_is(combo, "Right")) rubraview_textedit_move(&app->subs_edit, RUBRAVIEW_TEXTEDIT_RIGHT, extend);
                    else if (key_is(combo, "Home")) rubraview_textedit_move(&app->subs_edit, RUBRAVIEW_TEXTEDIT_HOME, extend);
                    else if (key_is(combo, "End")) rubraview_textedit_move(&app->subs_edit, RUBRAVIEW_TEXTEDIT_END, extend);
                    app->subs_dirty = true;
                    break;
                }
                if (key_is(combo, "Escape")) { subs_close(app); return handled; }
                if ((combo.modifiers & RUBRAVIEW_MOD_CTRL) && key_is(combo, "S")) { subs_save(app); break; }
                /* F2 or Enter: the text of the row last clicked. (Enter is not the viewer's
                   here: there it turns to the next file, and what was changed would be gone.) */
                if (key_is(combo, "F2") || key_is(combo, "Enter")) {
                    if (app->subs_anchor < count) subs_edit_begin(app, app->subs_anchor);
                    break;
                }
                /* RV-115: a line in, lines out, a row's times typed (Delete is not the viewer's here: it would delete the film) */
                if (key_is(combo, "Insert")) { subs_add(app); break; }
                if (key_is(combo, "Delete")) { subs_remove(app); break; }
                if ((combo.modifiers & RUBRAVIEW_MOD_CTRL) && key_is(combo, "T")) {
                    if (app->subs_anchor < count) subs_edit_begin_as(app, app->subs_anchor, true);
                    break;
                }
                if ((combo.modifiers & RUBRAVIEW_MOD_CTRL) && key_is(combo, "A")) {
                    if (subs_kind(app) == SUBS_TEXT) rubraview_subwin_choose_all(app->subs_chosen, count, true);
                    app->subs_dirty = true;
                    break;
                }
                if (key_is(combo, "Up")) step = -1;
                else if (key_is(combo, "Down")) step = +1;
                else if (key_is(combo, "PageUp")) step = -(rows > 1 ? rows - 1 : 1);
                else if (key_is(combo, "PageDown")) step = rows > 1 ? rows - 1 : 1;
                else if (key_is(combo, "Home")) step = -(long)count;
                else if (key_is(combo, "End")) step = (long)count;
                if (step != 0) {                 /* the list moves; the film stays */
                    rubraview_listwin_scroll(&app->subs_list, step, count);
                    app->subs_scrolled_at = rubraview_pal_time_now_seconds();
                    app->subs_dirty = true;
                    break;
                }
                /* Any other key is the viewer's: Space pauses, its own key closes this window. */
                dispatch_key(app, combo);
                app->subs_dirty = true;
                break;
            }
            default:
                break;
        }
    }
    return handled;
}
