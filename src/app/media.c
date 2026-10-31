/*
 * Video and sound pages, music that outlives its page, stations, what plays after an end, a track into
 * the next (RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* ---- video pages (M5 slice 1: D-8, D-9) ---- */

void osd_say(app_state_t *app, proven_u8str_view_t text);

static void vobsub_clear(app_state_t *app);
static void pgs_clear(app_state_t *app);
int details_keep(proven_u8str_view_t source, proven_u8str_view_t written, proven_u8str_view_t named, rubraview_export_format_t format,
                        const rubraview_export_options_t *options);
static void subs_drop(app_state_t *app);
proven_arena_t *subs_arena(app_state_t *app, size_t need);
static void music_clear(app_state_t *app);
static void music_next_close(app_state_t *app);
static bool bgm_adopt(app_state_t *app);
static void music_transition_tick(app_state_t *app);
static void music_promote_next(app_state_t *app);
void go_to_spread(app_state_t *app, size_t index);
void update_precache(app_state_t *app);
static void media_after_end(app_state_t *app);
void ab_edit_close(app_state_t *app);

static void station_job_leave(app_state_t *app);

void media_close(app_state_t *app) {
    station_job_leave(app);   /* RV-157: a station still connecting belongs to the page being left */
    /* §3.14.6: a track that is still playing does not die because its
       page is leaving — it becomes the background music. This is here
       rather than in the caller so that no path can forget it. */
    if (bgm_adopt(app)) return;
    app->subbox.selected = false;
    rubraview_subbox_end_drag(&app->subbox);
    app->sub_list_open = false;
    app->sub_tap = (rubraview_tap_t){0};
    vobsub_clear(app);
    pgs_clear(app);
    music_clear(app);
    music_next_close(app);
    /* Nothing is playing now, so there is nothing for the points to be
       in: a box left open would apply them to the next file. */
    if (app->ab_edit_open) ab_edit_close(app);
    app->video_transform_ok = false;
    if (app->media) {
        rubraview_pal_media_close(app->media);
        app->media = NULL;
    }
    if (app->media_page >= 0 && (size_t)app->media_page < page_count(app)) {
        app_page_t *page = &app->pages[app->media_page];
        if (page->texture) rubraview_pal_texture_destroy(page->texture);
        *page = (app_page_t){0};
    }
    app->media_page = -1;
    app->media_paused = false;
    app->media_has_frame = false;
    app->media_sought = false;
    app->media_position = 0.0;
    app->media_title_tenth = -1;
    app->media_ended = false;
    app->media_progress_pos = -1.0;   /* 1.25.2: counted from now, not from the film before */
    app->media_progress_at = rubraview_pal_time_now_seconds();
    /* Otherwise the still page after a film would keep drawing its last
       line of dialogue. */
    vobsub_clear(app);
    pgs_clear(app);
    app->subtitle = (rubraview_subtitle_track_t){0};
    subs_drop(app);
    app->subtitle_name = PROVEN_LIT("");
    app->tracks = rubraview_tracks_create();
    app->subtitle_count = 0;
}


/* RV-084: what a file that is only sound shows — the album art Windows'
   own shell finds in it (ID3, MP4 and FLAC tags alike), or a plain dark
   square when it has none. Main thread only: the shell wants the
   apartment this thread already runs. */

void audio_chain_update(app_state_t *app);
static void music_clear(app_state_t *app) {
    if (app->music_backdrop) rubraview_pal_texture_destroy(app->music_backdrop);
    app->music_backdrop = NULL;
    app->music_tags = (rubraview_tags_t){0};
    app->music_tags_read = false;
    app->music_lyrics = (rubraview_lyrics_t){0};
    app->music_cue = (rubraview_cue_sheet_t){0};
    memset(app->viz_peaks, 0, sizeof(app->viz_peaks));
    /* RFC-0006: what a track carried, given back for the next one. A track that went on
       as the background music took its region with it (bgm_adopt). */
    if (app->mem) rubraview_region_reset(&app->mem->media);
}

/* RFC-0006: a window's block from the windows pool (the session's arena when none can be had). */
proven_arena_t *window_block_take(app_state_t *app, struct window_block *wb) {
    if (wb->block) return &wb->arena;   /* already its own */
    if (!app->mem || !rubraview_block_take(&app->mem->windows, &wb->arena, &wb->block)) return app->arena;
    return &wb->arena;
}

proven_arena_t *window_block_arena(app_state_t *app, struct window_block *wb) {
    return wb->block ? &wb->arena : app->arena;
}

/* After the window and its renderer are gone. */
void window_block_give(app_state_t *app, struct window_block *wb) {
    if (wb->block && app->mem) rubraview_block_give(&app->mem->windows, wb->block);
    wb->block = NULL;
}

/* RFC-0006: memory for a moment, from scratch — the session's arena when none can be had
   (then `give` does nothing). */
proven_arena_t *scratch_begin(app_state_t *app, rubraview_scratch_t *s, size_t bytes) {
    *s = (rubraview_scratch_t){ .cls = -1 };
    if (app->mem && rubraview_scratch_take(app->mem, bytes, s)) return &s->arena;
    return app->arena;
}

void scratch_end(app_state_t *app, rubraview_scratch_t *s) {
    if (app->mem && s->block) rubraview_scratch_give(app->mem, s);
}

/* RFC-0006: the film's subtitle — read, edited and saved — is in `subs`, emptied as
   it is put away (another chosen, the film closed). The one in use has room for `need`. */
uint32_t g_subs_generation;   /* a new one for each emptying: what was drawn from the one before is not reused */

proven_arena_t *subs_arena(app_state_t *app, size_t need) {
    proven_arena_t *a = app->mem ? rubraview_halves_live(&app->mem->subs, need) : NULL;
    return a ? a : app->arena;   /* past its limit: the session's, as before */
}

/* Room for one change to the subtitle in use: its cues copied once (an added cue, the offset folded), and some. */
size_t subs_edit_room(const app_state_t *app) {
    size_t cues = 0;
    if (!rubraview_size_product(&cues, app->subtitle.count + 1u, sizeof(rubraview_subtitle_cue_t), 1u)) cues = 64u << 20;   /* past any file read */
    return cues + (1u << 20);
}

static void subs_drop(app_state_t *app) {
    if (app->mem) rubraview_halves_reset(&app->mem->subs);
    g_subs_generation++;
}

/* The media region with room for `need` more (RFC-0006): a track's head, tags, words and sheet. */
static proven_arena_t *media_arena(app_state_t *app, size_t need) {
    if (!app->mem) return app->arena;
    if (!rubraview_region_ensure(&app->mem->media, need)) return app->arena;   /* past its limit: the session's, as before */
    return &app->mem->media.arena;
}

/* The equaliser window's bands (owner, 2026-10-01): `audio.eq_band1..10`
   in the settings, the Custom preset's gains. */
proven_u8str_view_t eq_band_key(size_t band) {
    static const char *const KEYS[RUBRAVIEW_EQ_BANDS] = {
        "eq_band1", "eq_band2", "eq_band3", "eq_band4", "eq_band5",
        "eq_band6", "eq_band7", "eq_band8", "eq_band9", "eq_band10",
    };
    return cstr(KEYS[band < RUBRAVIEW_EQ_BANDS ? band : 0]);
}

double eq_band_db(const app_state_t *app, size_t band) {
    return rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), eq_band_key(band));
}

int eq_preset_now(const app_state_t *app) {
    int preset = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("eq_preset")));
    return preset >= 0 && preset < RUBRAVIEW_EQWIN_PRESETS ? preset : 0;
}

proven_u8str_view_t eq_preset_label(int preset) {
    return preset == RUBRAVIEW_EQWIN_CUSTOM ? TU8("Custom") : rubraview_eq_preset_name((rubraview_eq_preset_t)preset);
}

/* What the sliders show: the Custom gains, or the named preset's. */
void eq_shown_gains(const app_state_t *app, float *out) {
    int preset = eq_preset_now(app);
    if (preset == RUBRAVIEW_EQWIN_CUSTOM) {
        for (size_t b = 0; b < RUBRAVIEW_EQ_BANDS; ++b) out[b] = (float)eq_band_db(app, b);
    } else {
        rubraview_eq_preset_gains((rubraview_eq_preset_t)preset, out);
    }
}

/* The equaliser, ReplayGain's gain for the song playing and night mode,
   handed to the sound output (owner, 2026-09-30). */
void audio_chain_update(app_state_t *app) {
    rubraview_audio_chain_config_t config = { .eq_preset = RUBRAVIEW_EQ_FLAT, .night_mode = false, .gain = 1.0 };
    int preset = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("eq_preset")));
    config.eq_preset = preset >= 0 && preset < RUBRAVIEW_EQ_PRESET_COUNT ? (rubraview_eq_preset_t)preset : RUBRAVIEW_EQ_FLAT;
    if (preset == RUBRAVIEW_EQWIN_CUSTOM) {   /* the window's own ten gains (owner, 2026-10-01) */
        config.eq_custom = true;
        for (size_t b = 0; b < RUBRAVIEW_EQ_BANDS; ++b) config.eq_gains_db[b] = (float)eq_band_db(app, b);
    }
    config.night_mode = rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("night_mode")) > 0.5;
    int rg = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("replaygain")));
    const rubraview_tags_t *tags = app->bgm_media && !app->media ? &app->bgm_tags : &app->music_tags;
    if (rg > 0) config.gain = rubraview_replaygain_factor(&tags->replaygain, (rubraview_replaygain_mode_t)rg, 0.0);
    rubraview_pal_audio_set_chain(&config);
}

/* The cover, blurred: a small copy of it, softened, and left for the
   renderer to stretch — which is the blur a backdrop actually wants and
   costs a few thousand pixels rather than a few million. */
void music_make_backdrop(app_state_t *app, const rubraview_tags_t *tags) {
    if (!tags->art || tags->art_size == 0) return;
    rubraview_pixbuf_t art = rubraview_pal_image_read_pixels(app->arena, PROVEN_LIT(""),
                                                             tags->art, tags->art_size, false);
    if (!rubraview_pixbuf_is_valid(&art)) return;

    rubraview_pixbuf_t small = rubraview_pixbuf_resample(app->arena, &art, 64, 64, RUBRAVIEW_FILTER_BILINEAR);
    if (!rubraview_pixbuf_is_valid(&small)) return;
    rubraview_pixbuf_t soft = rubraview_filter_box_blur(app->arena, &small, 6);
    if (!rubraview_pixbuf_is_valid(&soft)) soft = small;
    rubraview_pixbuf_t bgra = soft.format == RUBRAVIEW_PIXFMT_BGRA8
        ? soft : rubraview_pixbuf_convert(app->arena, &soft, RUBRAVIEW_PIXFMT_BGRA8);
    if (!rubraview_pixbuf_is_valid(&bgra)) return;

    app->music_backdrop = rubraview_pal_texture_create_bgra(app->renderer, bgra.width, bgra.height);
    if (!app->music_backdrop) return;
    if (!rubraview_pal_texture_upload_bgra(app->music_backdrop, bgra.pixels, bgra.stride)) {
        rubraview_pal_texture_destroy(app->music_backdrop);
        app->music_backdrop = NULL;
    }
}

/* Read the file's own words once per track. */
static void music_load(app_state_t *app, proven_u8str_view_t path) {
    music_clear(app);
    if (path.size == 0) return;
    proven_u8str_view_t head = rubraview_pal_fs_read_file(media_arena(app, MUSIC_MAX_HEAD_BYTES + 4096u), path, MUSIC_MAX_HEAD_BYTES);
    if (head.size == 0) return;      /* a record too large for the head budget: no tags, and it still plays */
    app->music_tags = rubraview_tags_read(media_arena(app, head.size + (1u << 20)), (rubraview_tags_source_t){
        .head = (const uint8_t*)head.ptr, .head_size = head.size, .file_size = head.size });
    app->music_tags_read = true;
    music_make_backdrop(app, &app->music_tags);
    /* A `.lrc` of the same name beside it: the words, in time. Else the
       words the file carries (owner, 2026-10-01): SYLT's timed lines,
       then USLT / LYRICS / ©lyr text, which is read as LRC when it is. */
    proven_arena_t *ma = media_arena(app, 3u << 20);   /* the words: a megabyte at most, and their parse */
    proven_u8str_view_t lrc = rubraview_pal_fs_read_file(ma, rubraview_path_with_ext(ma, path, ".lrc"), 1024u * 1024u);
    if (lrc.size > 0) app->music_lyrics = rubraview_lyrics_parse(ma, lrc);
    if (app->music_lyrics.count == 0 && app->music_tags.synced_lyrics_count > 0) {
        app->music_lyrics = rubraview_lyrics_from_lines(ma, app->music_tags.synced_lyrics,
                                                        app->music_tags.synced_lyrics_count);
    }
    if (app->music_lyrics.count == 0 && app->music_tags.lyrics_text.size > 0) {
        app->music_lyrics = rubraview_lyrics_from_text(ma, app->music_tags.lyrics_text);
    }
    ma = media_arena(app, 3u << 20);   /* the sheet: the same */
    /* A `.cue` of the same name: one file cut into the record's tracks.
       Else a sheet of any name beside it whose FILE names this file
       (owner, 2026-10-01: `CDImage.cue` for `Album.flac`). */
    proven_u8str_view_t cue = rubraview_pal_fs_read_file(ma, rubraview_path_with_ext(ma, path, ".cue"), 1024u * 1024u);
    if (cue.size > 0) app->music_cue = rubraview_cue_parse(ma, cue, app->music_tags.duration_seconds);
    if (app->music_cue.count < 2) {
        app->music_cue = (rubraview_cue_sheet_t){0};
        proven_u8str_view_t dir = rubraview_path_dirname(path);
        if (dir.size == 0) dir = PROVEN_LIT(".");
        if (dir.size >= sizeof(app->cue_scan_dir) || dir.size != app->cue_scan_dir_len ||
            memcmp(dir.ptr, app->cue_scan_dir, dir.size) != 0) {
            /* A folder not listed yet: its sheets, kept for the tracks after this one. */
            app->cue_scan_count = 0;
            app->cue_scan_paths = NULL;
            app->cue_scan_dir_len = 0;
            rubraview_fs_listing_t beside = rubraview_pal_fs_list_dir(app->arena, dir);
            size_t sheets = 0;
            for (size_t i = 0; i < beside.count; ++i) {
                if (!beside.entries[i].is_directory && rubraview_glob_match_list(beside.entries[i].name, PROVEN_LIT("*.cue"))) sheets++;
            }
            if (sheets > 64) sheets = 64;
            proven_result_mem_mut_t res = sheets ? rubraview_arena_alloc_array(app->arena, sheets, sizeof(proven_u8str_view_t))
                                                 : (proven_result_mem_mut_t){ .err = PROVEN_OK };
            if (sheets && proven_is_ok(res.err)) {
                app->cue_scan_paths = (proven_u8str_view_t*)(void*)res.value.ptr;
                for (size_t i = 0; i < beside.count && app->cue_scan_count < sheets; ++i) {
                    const rubraview_fs_entry_t *e = &beside.entries[i];
                    if (e->is_directory || !rubraview_glob_match_list(e->name, PROVEN_LIT("*.cue"))) continue;
                    app->cue_scan_paths[app->cue_scan_count++] = app_keep(app, e->path);
                }
            }
            if (dir.size < sizeof(app->cue_scan_dir)) {
                memcpy(app->cue_scan_dir, dir.ptr, dir.size);
                app->cue_scan_dir_len = dir.size;
            }
        }
        for (size_t i = 0; i < app->cue_scan_count; ++i) {
            proven_arena_t *sa = media_arena(app, 3u << 20);   /* each sheet read for this track only */
            proven_u8str_view_t text = rubraview_pal_fs_read_file(sa, app->cue_scan_paths[i], 1024u * 1024u);
            rubraview_cue_sheet_t sheet = rubraview_cue_parse(sa, text, app->music_tags.duration_seconds);
            if (sheet.count >= 2 && rubraview_cue_names_audio(&sheet, rubraview_path_basename(path))) {
                app->music_cue = sheet;
                break;
            }
        }
    }
    audio_chain_update(app);
}

/* A `.cue` opened on its own (owner, 2026-10-01) plays the audio it
   names — beside it, or with another extension of that name when the
   record was converted after the sheet was written. "" when there is none. */
proven_u8str_view_t cue_audio_path(app_state_t *app, proven_u8str_view_t cue_path) {
    proven_u8str_view_t text = rubraview_pal_fs_read_file(app->arena, cue_path, 1024u * 1024u);
    rubraview_cue_sheet_t sheet = rubraview_cue_parse(app->arena, text, 0.0);
    if (sheet.audio_file.size == 0) return PROVEN_LIT("");
    proven_u8str_view_t named = sheet.audio_file;
    for (size_t i = named.size; i > 0; --i) {
        if (named.ptr[i - 1] == '/' || named.ptr[i - 1] == '\\') { named = (proven_u8str_view_t){ named.ptr + i, named.size - i }; break; }
    }
    proven_u8str_view_t dir = rubraview_path_dirname(cue_path);
    proven_u8str_view_t audio = rubraview_path_join(app->arena, dir, named);
    rubraview_fs_entry_t found;
    if (rubraview_pal_fs_stat(app->arena, audio, &found) && !found.is_directory) return audio;
    static const char *const EXTS[] = { ".flac", ".wav", ".ape", ".wv", ".mp3", ".m4a", ".ogg", ".opus", ".tta" };
    for (size_t i = 0; i < sizeof(EXTS) / sizeof(EXTS[0]); ++i) {
        proven_u8str_view_t other = rubraview_path_with_ext(app->arena, audio, EXTS[i]);
        if (rubraview_pal_fs_stat(app->arena, other, &found) && !found.is_directory) return other;
    }
    return PROVEN_LIT("");
}

static rubraview_texture_t *audio_page_picture(app_state_t *app, proven_u8str_view_t path, int32_t *out_w, int32_t *out_h) {
    rubraview_texture_t *texture = NULL;
    int32_t w = 0, h = 0;
    uint8_t *pixels = rubraview_pal_shell_thumbnail_bgra(path, 512, &w, &h);
    if (pixels) {
        texture = rubraview_pal_texture_create_bgra(app->renderer, w, h);
        if (texture && !rubraview_pal_texture_upload_bgra(texture, pixels, w * 4)) {
            rubraview_pal_texture_destroy(texture);
            texture = NULL;
        }
        if (texture) { *out_w = w; *out_h = h; }
        free(pixels);
    }
    /* The shell knows the common formats; when it does not (FLAC and
       Opus often), the cover this file carries is read here instead. */
    if (!texture && app->music_tags_read && app->music_tags.art_size > 0) {
        rubraview_image_load_result_t art = rubraview_pal_image_load_texture_from_memory(
            app->renderer, app->music_tags.art, app->music_tags.art_size, false);
        if (art.texture) {
            texture = art.texture;
            *out_w = art.width;
            *out_h = art.height;
        }
    }
    if (!texture) {
        enum { SIDE = 512 };
        uint32_t *pixels = (uint32_t*)rubraview_malloc_product((size_t)SIDE, SIDE, 4u);
        if (pixels) {
            for (size_t k = 0; k < (size_t)SIDE * SIDE; ++k) pixels[k] = 0xFF262626u;
            texture = rubraview_pal_texture_create_bgra(app->renderer, SIDE, SIDE);
            if (texture && !rubraview_pal_texture_upload_bgra(texture, (const uint8_t*)(const void*)pixels, SIDE * 4)) {
                rubraview_pal_texture_destroy(texture);
                texture = NULL;
            }
            if (texture) { *out_w = SIDE; *out_h = SIDE; }
            free(pixels);
        }
    }
    return texture;
}

/* The media page with its texture, made again after a lost device: a
   frame-sized one for video, the album picture for sound only. */
static app_page_t *media_page_ready(app_state_t *app) {
    if (!app->media || app->media_page < 0 || (size_t)app->media_page >= page_count(app)) return NULL;
    app_page_t *page = &app->pages[app->media_page];
    if (!page->texture) {
        int32_t w = app->media_info.width, h = app->media_info.height;
        /* RV-062: a film decoded on the card gets a texture on the card,
           and its frames are copied into it there; so does one tone mapped there (RV-150). */
        page->texture = !app->media_info.has_video
            ? audio_page_picture(app, app->source.pages[app->media_page].path, &w, &h)
            : app->media_info.hdr_output ? rubraview_pal_texture_create_video_hdr(app->renderer, w, h)   /* RV-153 */
            : app->media_info.hardware_decode || app->media_info.hdr_tone_mapped
                ? rubraview_pal_texture_create_video(app->renderer, w, h)
                : rubraview_pal_texture_create_bgra(app->renderer, w, h);
        if (!page->texture) return NULL;
        page->width = w;
        page->height = h;
        page->loaded = true;
        page->failed = false;
        page->failure_said = false;
        /* A sound-only page's picture is already on it; a video's first
           frame is still to come. */
        app->media_has_frame = !app->media_info.has_video;
        app->needs_relayout = true;
    }
    return page;
}

/* What reading one takes at most: the text, its code page turned to UTF-8, the words and the cues (RFC-0006). */
#define SUBTITLE_PARSE_BYTES (8u * SUBTITLE_MAX_BYTES)
/* A DVD's index is text, its pictures are not: a feature film's worth of
   subpictures runs to a few tens of megabytes. */
#define VOBSUB_MAX_IDX_BYTES (4u * 1024u * 1024u)
/* Mapped, not read into the 64 MB working arena, where anything over about
   60 MB could never load (found 2026-09-29): the cap is a sanity bound now. */
#define VOBSUB_MAX_SUB_BYTES (1024ull * 1024u * 1024u)
#define PGS_MAX_SUP_BYTES    (1024ull * 1024u * 1024u)

/* A subtitle picture file, mapped; empty when it cannot be, or is too large. */
static proven_u8str_view_t map_subtitle_file(proven_u8str_view_t path, rubraview_fs_mapping_t *map, uint64_t cap) {
    rubraview_pal_fs_unmap(map);
    if (!rubraview_pal_fs_map(path, map)) return PROVEN_LIT("");
    if (map->size > cap) { rubraview_pal_fs_unmap(map); return PROVEN_LIT(""); }
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(const char*)map->data, .size = (size_t)map->size };
}
#define SUBTITLE_MAX_CANDIDATES 8

/* §3.16.1 / R135: the subtitle files that share the film's name. The
   folder is listed rather than reused from the page list, because a
   .srt is not a page — the page source never saw it. */
static size_t subtitle_candidates(proven_arena_t *arena, proven_u8str_view_t video_path,
                                  rubraview_subtitle_candidate_t *out, size_t capacity,
                                  const char **out_why) {
    if (out_why) *out_why = T("no subtitle file beside the video");
    rubraview_fs_listing_t listing =
        rubraview_pal_fs_list_dir(arena, rubraview_path_dirname(video_path));
    if (listing.count == 0) { if (out_why) *out_why = T("the folder could not be listed"); return 0; }

    proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, listing.count, sizeof(proven_u8str_view_t));
    if (!proven_is_ok(res.err)) { if (out_why) *out_why = T("out of memory"); return 0; }
    proven_u8str_view_t *paths = (proven_u8str_view_t*)(void*)res.value.ptr;
    size_t sibling_count = 0;
    for (size_t i = 0; i < listing.count; ++i) {
        if (listing.entries[i].is_directory) continue;
        paths[sibling_count++] = listing.entries[i].path;
    }
    return rubraview_subtitle_discover(video_path, paths, sibling_count, out, capacity);
}

/* Reads and parses one of them. */
static rubraview_subtitle_track_t subtitle_read(proven_arena_t *arena,
                                                rubraview_subtitle_candidate_t candidate,
                                                const char **out_why) {
    rubraview_subtitle_track_t empty = {0};
    proven_u8str_view_t text = rubraview_pal_fs_read_file(arena, candidate.path, SUBTITLE_MAX_BYTES);
    if (text.size == 0) { if (out_why) *out_why = T("the subtitle file could not be read"); return empty; }
    /* A Korean .smi is nearly always CP949, and the parser reads UTF-8.
       The archive-name check is a UTF-8 validator, so it answers this
       question too; bytes that are not UTF-8 are read in the machine's
       own code page (0). */
    bool transcoded = false;
    if (rubraview_archive_filename_detect(text, false) == RUBRAVIEW_ENCODING_NEEDS_FALLBACK) {
        proven_u8str_view_t converted = rubraview_pal_transcode_codepage(arena, text, 0);
        if (converted.size > 0) { text = converted; transcoded = true; }
    }
    rubraview_subtitle_track_t track = rubraview_subtitle_parse(arena, text, candidate.format);
    /* RV-115: where it came from and how it was read, for saving it again. */
    track.source_path = candidate.path;
    track.source_transcoded = transcoded;
    if (track.count == 0) { if (out_why) *out_why = T("the subtitle file held no usable lines"); return empty; }
    if (out_why) *out_why = NULL;
    return track;
}

/* What `--probe-media` reports: the first subtitle file, read. */
rubraview_subtitle_track_t subtitle_find(proven_arena_t *arena, proven_u8str_view_t video_path,
                                                proven_u8str_view_t *out_name, const char **out_why) {
    rubraview_subtitle_track_t empty = {0};
    if (out_name) *out_name = PROVEN_LIT("");
    rubraview_subtitle_candidate_t found[SUBTITLE_MAX_CANDIDATES];
    size_t count = subtitle_candidates(arena, video_path, found, SUBTITLE_MAX_CANDIDATES, out_why);
    if (count == 0) return empty;
    rubraview_subtitle_track_t track = subtitle_read(arena, found[0], out_why);
    if (track.count > 0 && out_name) *out_name = rubraview_path_basename(found[0].path);
    return track;
}

/* Puts the film's tracks — its own sound, and every subtitle file beside
   it — into one list, and turns on the subtitle the reader would want
   (§3.16.2). Subtitles are "off" as well as every file: that is a
   choice, so it is in the cycle. */
void tracks_prepare(app_state_t *app, proven_u8str_view_t video_path) {
    app->tracks = rubraview_tracks_create();
    app->subtitle_last = -1;
    if (app->media) rubraview_pal_media_tracks(app->media, &app->tracks);

    /* RFC-0006: the folder listed and each file looked into, for the moment; what is
       kept of them (their paths, their languages' names) goes to the session's memory. */
    rubraview_scratch_t sc;
    proven_arena_t *sa = scratch_begin(app, &sc, 4u << 20);
    rubraview_subtitle_candidate_t found[SUBTITLE_MAX_CANDIDATES];
    size_t found_count = subtitle_candidates(sa, video_path, found, SUBTITLE_MAX_CANDIDATES, NULL);
    /* kept before the block goes back: a block given back holds the pool's own links */
    for (size_t i = 0; i < found_count; ++i) found[i].path = app_keep(app, found[i].path);
    scratch_end(app, &sc);

    /* One entry per track: a text file is one, a DVD index is one for
       each language it lists, so `C` walks them like any other track. */
    app->subtitle_count = 0;
    for (size_t i = 0; i < found_count && app->subtitle_count < SUBTITLE_MAX_CANDIDATES; ++i) {
        size_t languages = 1;
        proven_u8str_view_t tags[SUBTITLE_MAX_CANDIDATES];
        if (found[i].format == RUBRAVIEW_SUBTITLE_VOBSUB) {
            proven_arena_t *pa = scratch_begin(app, &sc, VOBSUB_MAX_IDX_BYTES + 1024u);
            proven_u8str_view_t idx_text = rubraview_pal_fs_read_file(pa, found[i].path, VOBSUB_MAX_IDX_BYTES);
            size_t n = rubraview_vobsub_languages(idx_text, tags, SUBTITLE_MAX_CANDIDATES);
            if (n > 0) languages = n;
            scratch_end(app, &sc);
        } else if (found[i].format == RUBRAVIEW_SUBTITLE_PGS) {
            languages = 1;     /* one `.sup` is one language, and it is not text */
        } else {
            /* A SAMI file usually holds every language at once, a class
               each (owner, 2026-09-23); the others hold one. */
            proven_arena_t *pa = scratch_begin(app, &sc, SUBTITLE_PARSE_BYTES);
            rubraview_subtitle_track_t peek = subtitle_read(pa, found[i], NULL);
            size_t n = rubraview_subtitle_languages(&peek, tags, SUBTITLE_MAX_CANDIDATES);
            if (n > 1) languages = n;
            scratch_end(app, &sc);
        }
        for (size_t k = 0; k < languages && app->subtitle_count < SUBTITLE_MAX_CANDIDATES; ++k) {
            app->subtitle_files[app->subtitle_count] = found[i];
            app->subtitle_vobsub_stream[app->subtitle_count] = (int32_t)k;
            app->subtitle_count++;
        }
    }

    static const char *const FORMAT_NAME[] = { "?", "srt", "smi", "vtt", "ass", "idx", "sup" };
    for (size_t i = 0; i < app->subtitle_count; ++i) {
        proven_u8str_view_t language = rubraview_subtitle_language_tag(video_path, app->subtitle_files[i].path);
        proven_u8str_view_t tags[SUBTITLE_MAX_CANDIDATES];
        size_t which = (size_t)app->subtitle_vobsub_stream[i];
        if (app->subtitle_files[i].format == RUBRAVIEW_SUBTITLE_VOBSUB) {
            /* The index names its own languages; the file's name does not. */
            proven_arena_t *pa = scratch_begin(app, &sc, VOBSUB_MAX_IDX_BYTES + 1024u);
            proven_u8str_view_t idx_text = rubraview_pal_fs_read_file(pa, app->subtitle_files[i].path, VOBSUB_MAX_IDX_BYTES);
            size_t n = rubraview_vobsub_languages(idx_text, tags, SUBTITLE_MAX_CANDIDATES);
            if (which < n) language = app_keep(app, tags[which]);
            scratch_end(app, &sc);
        } else if (app->subtitle_files[i].format == RUBRAVIEW_SUBTITLE_PGS) {
            /* Its name is all there is to go on, which `language` already has. */
        } else if (which > 0 || app->subtitle_files[i].format == RUBRAVIEW_SUBTITLE_SMI) {
            proven_arena_t *pa = scratch_begin(app, &sc, SUBTITLE_PARSE_BYTES);
            rubraview_subtitle_track_t peek = subtitle_read(pa, app->subtitle_files[i], NULL);
            size_t n = rubraview_subtitle_languages(&peek, tags, SUBTITLE_MAX_CANDIDATES);
            if (which < n && tags[which].size > 0) language = app_keep(app, tags[which]);
            scratch_end(app, &sc);
        }
        rubraview_track_t track = {
            .kind = RUBRAVIEW_TRACK_SUBTITLE,
            .is_external = true,          /* a file beside the video, not a stream in it */
            .stream_index = (int32_t)i,   /* into app->subtitle_files, not the container */
            .language = language,
            .title = rubraview_path_basename(app->subtitle_files[i].path),
            .codec = cstr(FORMAT_NAME[(size_t)app->subtitle_files[i].format < 7
                                      ? (size_t)app->subtitle_files[i].format : 0]),
        };
        rubraview_tracks_add(&app->tracks, track);
    }
}

/* Shows one subtitle track, or none when `index` is -1. */
/* The picture subtitles go with the film they belong to. */
static void vobsub_clear(app_state_t *app) {
    if (app->vobsub_texture) rubraview_pal_texture_destroy(app->vobsub_texture);
    app->vobsub_texture = NULL;
    app->vobsub_texture_cue = -1;
    app->vobsub_texture_w = app->vobsub_texture_h = 0;
    app->vobsub = (rubraview_vobsub_track_t){0};
    app->vobsub_sub = PROVEN_LIT("");
    rubraview_pal_fs_unmap(&app->vobsub_map);
}

/* `movie.idx` names the index; its pictures are in `movie.sub` beside it. */
static void vobsub_load(app_state_t *app, proven_u8str_view_t idx_path, size_t language) {
    vobsub_clear(app);
    proven_arena_t *sa = subs_arena(app, 4u * VOBSUB_MAX_IDX_BYTES);
    proven_u8str_view_t idx_text = rubraview_pal_fs_read_file(sa, idx_path, VOBSUB_MAX_IDX_BYTES);
    if (idx_text.size == 0) return;

    /* The whole path, not just the name: the viewer is not always
       started in the film's folder. */
    proven_u8str_view_t sub_path = rubraview_path_with_ext(sa, idx_path, ".sub");
    if (sub_path.size == 0) return;
    app->vobsub_sub = map_subtitle_file(sub_path, &app->vobsub_map, VOBSUB_MAX_SUB_BYTES);
    if (app->vobsub_sub.size == 0) {
        osd_say(app, TU8("the .sub file beside the index is missing"));
        return;
    }
    if (!app->vobsub_pixels) {
        proven_result_mem_mut_t mem = proven_arena_alloc(app->arena, RUBRAVIEW_VOBSUB_MAX_PIXELS);
        if (!proven_is_ok(mem.err)) return;
        app->vobsub_pixels = (uint8_t*)mem.value.ptr;
    }
    app->vobsub = rubraview_vobsub_index(sa, idx_text, language);
}

/* `movie.sup` is the whole of it: no index, no second file. */
static void pgs_clear(app_state_t *app) {
    if (app->pgs_texture) rubraview_pal_texture_destroy(app->pgs_texture);
    app->pgs_texture = NULL;
    app->pgs_texture_cue = -1;
    app->pgs_texture_w = app->pgs_texture_h = 0;
    app->pgs = (rubraview_pgs_track_t){0};
    app->pgs_bytes = PROVEN_LIT("");
    rubraview_pal_fs_unmap(&app->pgs_map);
}

static void pgs_load(app_state_t *app, proven_u8str_view_t sup_path) {
    pgs_clear(app);
    app->pgs_bytes = map_subtitle_file(sup_path, &app->pgs_map, PGS_MAX_SUP_BYTES);
    if (app->pgs_bytes.size == 0) {
        osd_say(app, TU8("the subtitle file could not be read"));
        return;
    }
    if (!app->pgs_pixels) {
        proven_result_mem_mut_t mem = proven_arena_alloc(app->arena, RUBRAVIEW_PGS_MAX_PIXELS);
        if (!proven_is_ok(mem.err)) return;
        app->pgs_pixels = (uint8_t*)mem.value.ptr;
    }
    app->pgs = rubraview_pgs_index(subs_arena(app, 16u << 20), (const uint8_t*)app->pgs_bytes.ptr, app->pgs_bytes.size);
}

/* The sync the reader set by hand for this film comes back — for a text
   track and for a picture one alike, since one offset moves them all. */
static void subtitle_restore_offset(app_state_t *app) {
    if (app->media_page >= 0 && (size_t)app->media_page < page_count(app) &&
        proven_u8str_view_eq(app->subtitle_offset_for, app->source.pages[app->media_page].path)) {
        app->subtitle.offset_seconds = app->subtitle_offset_seconds;
    }
}

void subtitle_select(app_state_t *app, int32_t index) {
    /* RV-115: moved or edited and not saved — said, since it cannot be asked here. */
    if (rubraview_subtitle_changed(&app->subtitle)) osd_say(app, TU8("the subtitle's changes were not saved"));
    vobsub_clear(app);
    pgs_clear(app);
    app->subtitle = (rubraview_subtitle_track_t){0};
    subs_drop(app);
    app->subtitle_name = PROVEN_LIT("");
    app->tracks.current_subtitle = -1;
    if (index < 0 || (size_t)index >= app->tracks.count) return;
    const rubraview_track_t *track = &app->tracks.tracks[index];
    if (track->kind != RUBRAVIEW_TRACK_SUBTITLE) return;

    if (track->is_external) {
        if (track->stream_index < 0 || (size_t)track->stream_index >= app->subtitle_count) return;
        if (app->subtitle_files[track->stream_index].format == RUBRAVIEW_SUBTITLE_VOBSUB) {
            vobsub_load(app, app->subtitle_files[track->stream_index].path,
                        (size_t)app->subtitle_vobsub_stream[track->stream_index]);
            if (app->vobsub.count == 0) return;
            proven_u8str_view_t picture_label = rubraview_track_label(app->subtitle_label, sizeof(app->subtitle_label),
                                                          &app->tracks, index);
            app->subtitle_name = track->title.size > 0 ? track->title : picture_label;
            app->tracks.current_subtitle = index;
            subtitle_restore_offset(app);
            return;
        }
        if (app->subtitle_files[track->stream_index].format == RUBRAVIEW_SUBTITLE_PGS) {
            pgs_load(app, app->subtitle_files[track->stream_index].path);
            if (app->pgs.count == 0) return;
            proven_u8str_view_t picture_label = rubraview_track_label(app->subtitle_label, sizeof(app->subtitle_label),
                                                          &app->tracks, index);
            app->subtitle_name = track->title.size > 0 ? track->title : picture_label;
            app->tracks.current_subtitle = index;
            subtitle_restore_offset(app);
            return;
        }
        app->subtitle = subtitle_read(subs_arena(app, SUBTITLE_PARSE_BYTES), app->subtitle_files[track->stream_index], NULL);
        /* One file, several languages: show the one this track stands for. */
        proven_u8str_view_t tags[SUBTITLE_MAX_CANDIDATES];
        size_t n = rubraview_subtitle_languages(&app->subtitle, tags, SUBTITLE_MAX_CANDIDATES);
        size_t which = (size_t)app->subtitle_vobsub_stream[track->stream_index];
        if (n > 1 && which < n) app->subtitle.shown_language = tags[which];
    } else {
        /* §3.16.1 / D-12: a stream inside the file. Reading it walks the
           whole container once, so say what is happening first. */
        osd_say(app, TU8("reading the subtitles out of the file"));
        proven_arena_t *sa = subs_arena(app, 2u * SUBTITLE_PARSE_BYTES);
        proven_u8str_view_t text = rubraview_pal_media_read_subtitle_stream(app->media, sa, track->stream_index);
        if (text.size == 0) {
            osd_say(app, TU8("this backend cannot read that subtitle stream"));
            return;
        }
        app->subtitle = rubraview_subtitle_parse(sa, text, RUBRAVIEW_SUBTITLE_SRT);
    }
    if (app->subtitle.count == 0) return;
    /* A stream inside a file often has no name of its own, so the OSD
       uses the same label the track menu shows. */
    proven_u8str_view_t label = rubraview_track_label(app->subtitle_label, sizeof(app->subtitle_label),
                                          &app->tracks, index);
    app->subtitle_name = track->title.size > 0 ? track->title : label;
    app->tracks.current_subtitle = index;
    subtitle_restore_offset(app);
}

static void subtitle_load(app_state_t *app, proven_u8str_view_t video_path) {
    tracks_prepare(app, video_path);
    /* §3.16.2: the preferred language decides; with none set, the first
       file wins. Parsing happens now, and only for the one chosen. */
    subtitle_select(app, rubraview_tracks_choose(&app->tracks, RUBRAVIEW_TRACK_SUBTITLE,
                                                 rubraview_settings_get_text(&app->settings, PROVEN_LIT("video"), PROVEN_LIT("subtitle_language"))));
    /* RV-107: and the sound in the language asked for, when the film has one. */
    proven_u8str_view_t sound = rubraview_settings_get_text(&app->settings, PROVEN_LIT("video"), PROVEN_LIT("audio_language"));
    if (sound.size > 0 && app->media) {
        int32_t want = rubraview_tracks_choose(&app->tracks, RUBRAVIEW_TRACK_AUDIO, sound);
        if (want >= 0 && want != app->tracks.current_audio && (size_t)want < app->tracks.count &&
            rubraview_pal_media_select_audio_track(app->media, app->tracks.tracks[want].stream_index)) {
            app->tracks.current_audio = want;
        }
    }
}

/* ---- RV-081: music that outlives its page ---- */

bool bgm_pause_for_sound(const app_state_t *app) {
    return rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("bgm_pause_on_video")) != 0.0;
}

void bgm_do(app_state_t *app, rubraview_bgm_action_t action) {
    if (!app->bgm_media) return;
    if (action == RUBRAVIEW_BGM_DO_PAUSE && !app->bgm_paused) {
        rubraview_pal_media_set_paused(app->bgm_media, true);
        app->bgm_paused = true;
    } else if (action == RUBRAVIEW_BGM_DO_RESUME && app->bgm_paused) {
        rubraview_pal_media_set_paused(app->bgm_media, false);
        app->bgm_paused = false;
    }
}

void bgm_close(app_state_t *app) {
    if (app->bgm_media) rubraview_pal_media_close(app->bgm_media);
    app->bgm_media = NULL;
    app->bgm_info = (rubraview_media_info_t){0};
    app->bgm_page = -1;
    app->bgm_position = 0.0;
    app->bgm_paused = false;
    app->bgm_tags = (rubraview_tags_t){0};   /* RFC-0006: its words given back with it */
    if (app->mem) rubraview_region_reset(&app->mem->bgm);
    (void)rubraview_bgm_event(&app->bgm, RUBRAVIEW_BGM_MUSIC_CLOSED, bgm_pause_for_sound(app));
}

/* The page is leaving but the track is not: the player is moved out of
   the page's hands and goes on playing behind whatever is opened next.
   Nothing is opened or decoded again — it is the same player. */
static bool bgm_adopt(app_state_t *app) {
    if (!app->media || app->media_info.has_video || app->media_ended) return false;
    if (!app->media_info.has_audio) return false;
    if (app->media_page < 0 || (size_t)app->media_page >= page_count(app)) return false;
    if (!rubraview_tags_is_music_name(rubraview_path_basename(app->source.pages[app->media_page].path))) return false;

    bgm_close(app);                 /* only one piece of background music at a time */
    music_next_close(app);          /* and no track waiting behind it */

    app->bgm_media = app->media;
    app->bgm_info = app->media_info;
    app->bgm_page = app->media_page;
    app->bgm_position = app->media_position;
    app->bgm_tags = app->music_tags;   /* its words outlive the page too */
    if (app->mem) {
        /* RFC-0006: and the region they are in goes with them — the media and the background
           music's regions change places whole, nothing copied (VM, 2026-10-18: turning through
           thirty songs kept every one, as each became the background music for a moment). */
        rubraview_region_t held = app->mem->bgm;
        app->mem->bgm = app->mem->media;
        app->mem->media = held;
    }
    app->bgm_paused = app->media_paused;
    app->media = NULL;
    app->media_page = -1;
    app->media_ended = false;
    app->media_progress_pos = -1.0;   /* 1.25.2: counted from now, not from the film before */
    app->media_progress_at = rubraview_pal_time_now_seconds();
    app->media_has_frame = false;
    app->media_sought = false;

    /* The page it came from is gone, so nothing is sounding on screen —
       otherwise the arbiter would see the track's own page as a rival
       and stand the music aside from itself. */
    (void)rubraview_bgm_event(&app->bgm, RUBRAVIEW_BGM_PAGE_QUIET, bgm_pause_for_sound(app));
    bgm_do(app, rubraview_bgm_event(&app->bgm, RUBRAVIEW_BGM_MUSIC_OPENED, bgm_pause_for_sound(app)));
    return true;
}

/* Back to the page the music came from: the same player takes its place
   again, at the moment it has reached. */
static bool bgm_release_to_page(app_state_t *app, int32_t index) {
    if (!app->bgm_media || index < 0 || index != app->bgm_page) return false;
    app->media = app->bgm_media;
    app->media_info = app->bgm_info;
    app->media_page = index;
    app->media_paused = app->bgm_paused;
    app->media_position = app->bgm_position;
    app->media_ended = false;
    app->media_progress_pos = -1.0;   /* 1.25.2: counted from now, not from the film before */
    app->media_progress_at = rubraview_pal_time_now_seconds();
    app->media_has_frame = false;
    app->media_sought = false;
    app->media_title_tenth = -1;
    app->media_master = rubraview_media_master_for(app->media_info.has_audio, app->media_info.audio_output);
    app->media_clock = rubraview_media_clock_create(app->media_master, app->bgm_position,
                                                    rubraview_pal_time_now_seconds());
    rubraview_media_clock_set_rate(&app->media_clock, app->media_speed, rubraview_pal_time_now_seconds());
    if (!app->media_paused) rubraview_media_clock_resume(&app->media_clock, rubraview_pal_time_now_seconds());

    app->bgm_media = NULL;
    app->bgm_page = -1;
    app->bgm_paused = false;
    (void)rubraview_bgm_event(&app->bgm, RUBRAVIEW_BGM_MUSIC_CLOSED, bgm_pause_for_sound(app));

    music_load(app, app->source.pages[index].path);
    subtitle_load(app, app->source.pages[index].path);
    (void)media_page_ready(app);
    return true;
}

/* Once a pass: keep the background music's own position, and notice when
   it has played itself out. */
void bgm_tick(app_state_t *app) {
    if (!app->bgm_media) return;
    if (!app->bgm_paused) {
        double heard = 0.0, at = 0.0;
        if (rubraview_pal_media_audio_position(app->bgm_media, &heard, &at)) app->bgm_position = heard;
        /* The backend says "finished" before the device has been heard
           out (the same early word the crossfade ran into, T084), so the
           track is only let go once it has actually reached its end. */
        if (rubraview_pal_media_stopped(app->bgm_media) ||   /* RV-176: its FFmpeg stopped: on to the next */
            (rubraview_pal_media_finished(app->bgm_media) &&
             (app->bgm_info.duration_seconds <= 0.0 ||
              app->bgm_position >= app->bgm_info.duration_seconds - 0.25))) {
            bgm_close(app);
        }
    }
}

static void media_adopt(app_state_t *app, int32_t index, proven_u8str_view_t path, rubraview_media_open_result_t opened,
                        rubraview_media_failure_t why);

/* ---- RV-157: a station connected to off the main thread ----
   A station that does not answer holds FFmpeg for up to its time limit; the window does not wait with it. The
   job is the page's: turning away lets it go, and a player it opens after that is closed by its own thread. */
typedef struct station_job {
    HANDLE thread;
    volatile LONG state;              /* 0 connecting, 1 done (the main thread's to take), 2 let go (the thread's to close) */
    int32_t page;
    rubraview_window_t *wake;
    rubraview_media_open_result_t result;
    /* RV-158: the addresses tried, in order: the station's, its list's fall-backs, those of a list one of them names */
    char tries[RUBRAVIEW_STREAM_MAX_TRIES][RUBRAVIEW_STREAM_URL_MAX];
    bool fetched[RUBRAVIEW_STREAM_MAX_TRIES];   /* came out of a fetched list: a list it names is not followed */
    size_t count;
    bool sniffed, nested;
} station_job_t;

/* RV-158: the fall-backs an .asx gave a station's address, kept while the program runs (the page has only one). */
#define STATION_ALTS_MAX 128
typedef struct station_alts {
    char *primary;
    char *alts[RUBRAVIEW_PLAYLIST_MAX_ALTERNATES];
    size_t count;
} station_alts_t;
static station_alts_t g_station_alts[STATION_ALTS_MAX];
static size_t g_station_alts_next;

static char *dup_view(proven_u8str_view_t v) {
    char *s = (char *)malloc(v.size + 1);
    if (s) { memcpy(s, v.ptr, v.size); s[v.size] = '\0'; }
    return s;
}

void station_alts_note(const rubraview_playlist_entry_t *e) {
    if (e->alternate_count == 0) return;
    station_alts_t *slot = &g_station_alts[g_station_alts_next++ % STATION_ALTS_MAX];
    free(slot->primary);
    for (size_t i = 0; i < slot->count; ++i) free(slot->alts[i]);
    *slot = (station_alts_t){ .primary = dup_view(e->path) };
    for (size_t i = 0; i < e->alternate_count && i < RUBRAVIEW_PLAYLIST_MAX_ALTERNATES; ++i)
        if ((slot->alts[slot->count] = dup_view(e->alternates[i])) != NULL) slot->count++;
}

static const station_alts_t *station_alts_of(proven_u8str_view_t primary) {
    for (size_t i = 0; i < STATION_ALTS_MAX; ++i) {
        const station_alts_t *s = &g_station_alts[i];
        if (s->primary && proven_u8str_view_eq(proven_u8str_view_from_cstr(s->primary), primary)) return s;
    }
    return NULL;
}

static bool station_try_add(station_job_t *job, proven_u8str_view_t address, bool fetched) {
    if (job->count >= RUBRAVIEW_STREAM_MAX_TRIES || address.size >= RUBRAVIEW_STREAM_URL_MAX || !rubraview_stream_is_address(address)) return false;
    for (size_t i = 0; i < job->count; ++i) if (proven_u8str_view_eq(proven_u8str_view_from_cstr(job->tries[i]), address)) return false;
    memcpy(job->tries[job->count], address.ptr, address.size);
    job->tries[job->count][address.size] = '\0';
    job->fetched[job->count++] = fetched;
    return true;
}

char *list_text_decode(proven_u8str_view_t raw, size_t *len);

/* RV-158: the list an address names (or, `sniffing`, what an address that would not play answers with), fetched
   and read as a list file is; its stations join the tries. A list it names in turn is not followed. */
static void station_list_take(station_job_t *job, proven_u8str_view_t address, bool sniffing) {
    uint8_t *raw = (uint8_t *)malloc(RUBRAVIEW_STREAM_LIST_MAX);
    int64_t got = raw ? rubraview_pal_ffmpeg_fetch(address, raw, RUBRAVIEW_STREAM_LIST_MAX) : -1;
    size_t room = 1u << 20;
    void *mem = got > 0 ? malloc(room) : NULL;
    if (mem) {
        size_t len = 0;
        proven_u8str_view_t bytes = proven_u8str_view_from_bytes(raw, (size_t)got);
        char *text = list_text_decode(bytes, &len);
        proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = mem, .size = room });
        /* by its text: the address's name may carry a query, and one that answered a list has none to go by */
        rubraview_playlist_t pl = rubraview_playlist_parse_named(&arena, sniffing ? PROVEN_LIT("") : rubraview_path_basename(address),
                                                                 text ? proven_u8str_view_from_bytes(text, len) : bytes);
        for (size_t i = 0; i < pl.count; ++i) {
            const rubraview_playlist_entry_t *e = &pl.entries[i];
            for (size_t k = 0; k <= e->alternate_count; ++k) {
                proven_u8str_view_t a = k == 0 ? e->path : e->alternates[k - 1];
                if (rubraview_stream_is_list_address(a)) { job->nested = true; continue; }
                (void)station_try_add(job, a, true);
            }
        }
        free(text);
    }
    free(mem);
    free(raw);
}

static DWORD WINAPI station_job_run(LPVOID arg) {
    station_job_t *job = (station_job_t*)arg;
    job->result = (rubraview_media_open_result_t){ .media = NULL, .failure = RUBRAVIEW_MEDIA_FAIL_STATION };
    for (size_t i = 0; i < job->count && !job->result.media; ++i) {
        if (InterlockedCompareExchange(&job->state, 2, 2) == 2) break;   /* let go: no more tries */
        proven_u8str_view_t address = proven_u8str_view_from_cstr(job->tries[i]);
        if (rubraview_stream_is_list_address(address)) {
            if (job->fetched[i]) job->nested = true;
            else station_list_take(job, address, false);
            continue;
        }
        rubraview_media_open_result_t r = rubraview_pal_media_open(address, RUBRAVIEW_BACKEND_FFMPEG, NULL);
        if (r.media) { job->result = r; break; }
        if (r.failure != RUBRAVIEW_MEDIA_OPENED) job->result.failure = r.failure;
        /* the station's own address answered with something that is not a stream: a list, perhaps */
        if (!job->fetched[i] && !job->sniffed) { job->sniffed = true; station_list_take(job, address, true); }
    }
    if (InterlockedCompareExchange(&job->state, 1, 0) == 2) {
        if (job->result.media) rubraview_pal_media_close(job->result.media);
        free(job);
        return 0;
    }
    rubraview_pal_window_wake(job->wake);
    return 0;
}

static void station_job_leave(app_state_t *app) {
    station_job_t *job = app->station_job;
    if (!job) return;
    app->station_job = NULL;
    HANDLE thread = job->thread;
    if (InterlockedCompareExchange(&job->state, 2, 0) == 1) {   /* it had finished: closed here */
        WaitForSingleObject(thread, INFINITE);
        if (job->result.media) rubraview_pal_media_close(job->result.media);
        free(job);
    }
    CloseHandle(thread);   /* otherwise the thread closes what it opens, and frees the job */
}

static void station_job_start(app_state_t *app, int32_t index, proven_u8str_view_t path) {
    station_job_leave(app);
    station_job_t *job = (station_job_t*)calloc(1, sizeof(*job));
    if (job && station_try_add(job, path, false)) {
        const station_alts_t *alts = station_alts_of(path);   /* RV-158 */
        for (size_t i = 0; alts && i < alts->count; ++i) (void)station_try_add(job, proven_u8str_view_from_cstr(alts->alts[i]), false);
        job->page = index;
        job->wake = app->window;
        job->thread = CreateThread(NULL, 0, station_job_run, job, 0, NULL);
    }
    if (!job || !job->thread) {
        free(job);
        media_adopt(app, index, path, (rubraview_media_open_result_t){ .media = NULL, .failure = RUBRAVIEW_MEDIA_FAIL_STATION },
                    RUBRAVIEW_MEDIA_FAIL_STATION);
        return;
    }
    app->station_job = job;
    osd_say(app, TU8("connecting to the station..."));
}

int32_t station_connecting_page(const app_state_t *app) {
    return app->station_job ? app->station_job->page : -1;
}

/* Once a pass: a station that has answered (or not) becomes its page's player, when its page is still on screen. */
void station_job_poll(app_state_t *app) {
    station_job_t *job = app->station_job;
    if (!job || InterlockedCompareExchange(&job->state, 1, 1) != 1) return;
    app->station_job = NULL;
    WaitForSingleObject(job->thread, INFINITE);
    CloseHandle(job->thread);
    rubraview_media_open_result_t opened = job->result;
    int32_t page = job->page;
    bool nested = job->nested && !opened.media;
    free(job);
    if (nested) osd_say(app, TU8("the station's list names another list; that one is not followed"));   /* RV-158 */
    if (page != current_page_index(app) || app->media || (size_t)page >= page_count(app)) {
        if (opened.media) rubraview_pal_media_close(opened.media);
        return;
    }
    media_adopt(app, page, app->source.pages[page].path, opened, RUBRAVIEW_MEDIA_FAIL_STATION);
    update_window_title(app);
}

/* RV-158: what the station playing says is on, into the title when it changes. */
void station_title_poll(app_state_t *app) {
    char now[sizeof(app->station_title)] = "";
    if (app->media && app->media_info.live) (void)rubraview_pal_media_stream_title(app->media, now, sizeof(now));
    if (strcmp(now, app->station_title) == 0) return;
    memcpy(app->station_title, now, sizeof(now));
    update_window_title(app);
}

/* Opens the video when the page on screen is one, and closes the old one. */
void media_prepare(app_state_t *app) {
    text_prepare(app);   /* RFC-0005: a text page is read here too */
    int32_t index = current_page_index(app);
    if (app->media && index == app->media_page) return;
    if (!app->media && app->station_job && app->station_job->page == index) return;   /* RV-157: still connecting */

    media_close(app);   /* which hands a playing track to the background */

    /* Back on the music's own page: the same player, where it had got to. */
    if (bgm_release_to_page(app, index)) return;

    if (index < 0 || (size_t)index >= page_count(app)) {
        bgm_do(app, rubraview_bgm_event(&app->bgm, RUBRAVIEW_BGM_PAGE_QUIET, bgm_pause_for_sound(app)));
        return;
    }
    proven_u8str_view_t path = app->source.pages[index].path;
    if (!is_media_path(path) || app->pages[index].skipped) {
        /* A picture or a comic makes no sound: the music has the floor. (D-98: nor does a film the layout leaves out.) */
        bgm_do(app, rubraview_bgm_event(&app->bgm, RUBRAVIEW_BGM_PAGE_QUIET, bgm_pause_for_sound(app)));
        return;
    }

    /* RV-157: a station is FFmpeg's alone, connected to in the background */
    if (is_station_path(path)) {
        if (!rubraview_pal_media_backend_available(RUBRAVIEW_BACKEND_FFMPEG)) {   /* (this also loads FFmpeg here, not on the job's thread) */
            media_adopt(app, index, path, (rubraview_media_open_result_t){ .media = NULL, .failure = RUBRAVIEW_MEDIA_FAIL_STATION_NEEDS_FFMPEG },
                        RUBRAVIEW_MEDIA_FAIL_STATION_NEEDS_FFMPEG);
            return;
        }
        station_job_start(app, index, path);
        return;
    }

    /* D-9: the preferred backend first, the other one when it cannot. */
    rubraview_media_backend_t order[2];
    bool no_ffmpeg = app->media_no_ffmpeg_page == index + 1;   /* RV-176: it stopped on this one */
    size_t count = rubraview_media_backend_order(no_ffmpeg ? RUBRAVIEW_BACKEND_MEDIA_FOUNDATION : app->media_preferred,
                                                 !no_ffmpeg && rubraview_pal_media_backend_available(RUBRAVIEW_BACKEND_FFMPEG),
                                                 order);
    rubraview_media_open_result_t opened = { .media = NULL, .failure = RUBRAVIEW_MEDIA_FAIL_FILE };
    rubraview_media_failure_t why = RUBRAVIEW_MEDIA_FAIL_FILE;
    /* RV-062: decoding on the graphics card, as [video] hardware_decode
       says — off (the default until it has been measured on a real card),
       on where the card offers decoders, or always (diagnostic). */
    rubraview_media_gpu_t gpu = {
        .mode = (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("video"), PROVEN_LIT("hardware_decode"))),
    };
    if (gpu.mode > 0) gpu.device = rubraview_pal_render_video_device(app->renderer, &gpu.decoder_profiles);
    /* RV-150: an HDR film is tone mapped by our shader on the renderer's decode device */
    gpu.tone_mapping = (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("video"), PROVEN_LIT("hdr_tone_mapping")));
    if (gpu.tone_mapping > 0) gpu.shader_device = gpu.device ? gpu.device : rubraview_pal_render_video_device(app->renderer, NULL);
    /* RV-153: [video] hdr_output — an HDR film tone mapped for the HDR screen the window is on (always: any screen) */
    int32_t hdr_out = (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("video"), PROVEN_LIT("hdr_output")));
    rubraview_pal_render_hdr_display(app->renderer, &app->media_hdr_screen);
    if (gpu.tone_mapping > 0 && !app->hdr_output_refused && (hdr_out == 2 || (hdr_out == 1 && app->media_hdr_screen.hdr))) {
        gpu.hdr_screen_peak = !app->media_hdr_screen.hdr ? app->media_hdr_screen.sdr_white_nits
                            : app->media_hdr_screen.peak_nits > 0.0 ? app->media_hdr_screen.peak_nits : RUBRAVIEW_HDR_DEFAULT_PEAK;
        gpu.hdr_sdr_white = app->media_hdr_screen.sdr_white_nits;
    }
    for (size_t i = 0; i < count; ++i) {
        opened = rubraview_pal_media_open(path, order[i], &gpu);
        if (opened.media) break;
        why = i == 0 ? opened.failure : rubraview_media_failure_pick(why, opened.failure);
    }
    /* RV-150 (owner, 2026-10-21): Windows' decoder does not tone map an HDR film; FFmpeg does, when it is here */
    app->media_hdr_handed = 0;
    if (opened.media && opened.info.backend == RUBRAVIEW_BACKEND_MEDIA_FOUNDATION &&
        rubraview_media_hdr_ask_ffmpeg(opened.info.hdr_transfer, opened.info.video_fourcc) &&
        gpu.tone_mapping > 0 && !no_ffmpeg && rubraview_pal_media_backend_available(RUBRAVIEW_BACKEND_FFMPEG)) {
        rubraview_media_open_result_t other = rubraview_pal_media_open(path, RUBRAVIEW_BACKEND_FFMPEG, &gpu);
        /* not when FFmpeg would lose the sound Windows plays */
        if (other.media && other.info.hdr_tone_mapped && (other.info.audio_output || !opened.info.audio_output)) {
            rubraview_pal_media_close(opened.media);
            app->media_hdr_handed = opened.info.hdr_transfer ? 1 : 2;
            opened = other;
        } else if (other.media) {
            rubraview_pal_media_close(other.media);
        }
    }
    media_adopt(app, index, path, opened, why);
}

/* What was opened for page `index` becomes the page's player; nothing opened, and it is said why and passed. */
static void media_adopt(app_state_t *app, int32_t index, proven_u8str_view_t path, rubraview_media_open_result_t opened,
                        rubraview_media_failure_t why) {
    if (!opened.media) {
        osd_say(app, why >= RUBRAVIEW_MEDIA_FAIL_STATION ? rubraview_media_failure_text(why)
                                                          : rubraview_media_failure_text_with(why, rubraview_pal_ffmpeg_refused()));
        app->notice_seconds = MEDIA_NOTICE_SECONDS;   /* long enough to read the HEVC line */
        app->pages[index].failed = true;
        app->media_skip_pending = true;   /* D-9: say so, then move on */
        return;
    }

    app->media = opened.media;
    app->media_info = opened.info;
    app->media_page = index;
    app->media_paused = false;
    app->media_has_frame = false;
    app->media_sought = false;
    app->media_position = 0.0;
    app->media_title_tenth = -1;
    app->media_ended = false;
    app->media_progress_pos = -1.0;   /* 1.25.2: counted from now, not from the film before */
    app->media_progress_at = rubraview_pal_time_now_seconds();
    /* §5.3: the sound is the master clock when it reaches a device;
       otherwise — a silent file, or no device, as on the test VM — the
       wall clock is. */
    app->media_master = rubraview_media_master_for(opened.info.has_audio, opened.info.audio_output);
    app->media_clock = rubraview_media_clock_create(app->media_master, 0.0, rubraview_pal_time_now_seconds());
    if (app->media_speed <= 0.0) app->media_speed = 1.0;
    rubraview_media_clock_set_rate(&app->media_clock, app->media_speed, rubraview_pal_time_now_seconds());
    rubraview_pal_audio_set_speed(app->media_speed);
    app->ab_a = app->ab_b = -1.0;   /* a repeat belongs to the file it was set in */
    /* §3.2.6 / RV-061: a slide holding a film or a track waits for the
       whole of it, not for the still-image interval. */
    if (app->slides && (size_t)app->spread_index < app->layout.count) {
        app->slides[app->spread_index].kind = RUBRAVIEW_MEDIA_VIDEO;
        app->slides[app->spread_index].duration_seconds = opened.info.duration_seconds;
    }
    /* A file whose sound cannot be played looks exactly like a file with
       no sound, which is how three measurements were misread on
       2026-09-24. Say which call failed instead. */
    if (opened.info.has_audio && !opened.info.audio_output) {
        char why[128], line[192];
        int n = rubraview_pal_audio_last_failure(why, sizeof(why))
            ? snprintf(line, sizeof(line), T("no sound: the device would not open (%s)"), why)
            : snprintf(line, sizeof(line), T("no sound: the device would not open"));
        if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
    }
    /* §3.14.6: something with sound of its own is on screen now. */
    if (opened.info.has_audio) {
        bgm_do(app, rubraview_bgm_event(&app->bgm, RUBRAVIEW_BGM_PAGE_SOUNDS, bgm_pause_for_sound(app)));
    }
    if (!opened.info.has_video && !opened.info.live) music_load(app, path);   /* RV-157: a station has no tags or words to read */
    if (!opened.info.live) subtitle_load(app, path);
    if (opened.info.live) osd_say(app, TU8("live: an internet radio station"));
    if (app->subtitle.count > 0) {
        char line[256];
        int n = snprintf(line, sizeof(line), T("subtitles: %.*s"),
                         (int)app->subtitle_name.size, app->subtitle_name.ptr);
        if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
    }
    if (!media_page_ready(app)) {
        media_close(app);
        osd_say(app, TU8("could not make room to show that video"));
        app->pages[index].failed = true;
        app->media_skip_pending = true;
        return;
    }
    app->needs_relayout = true;   /* the page has a size now */
}

static void media_show(app_state_t *app, app_page_t *page, const rubraview_video_frame_t *frame) {
    if (frame->gpu_texture) {
        rubraview_pal_texture_copy_video_frame(page->texture, frame->gpu_texture, frame->gpu_subresource);
        if (app->frame_wanted && frame->width > 0 && frame->height > 0) {
            /* RV-108: a frame on the card is read back from the page's texture. */
            size_t stride = (size_t)page->width * 4u;
            uint8_t *back = (uint8_t*)rubraview_malloc_product(stride, (size_t)page->height, 1);
            if (back && rubraview_pal_texture_read_video(page->texture, back, stride)) {
                frame_take(app, back, page->width, page->height, stride);
            } else {
                app->frame_wanted = FRAME_NOT;
                osd_say(app, TU8("the frame could not be read back from the graphics card"));
            }
            free(back);
        }
    } else {
        rubraview_pal_texture_upload_bgra(page->texture, frame->pixels, frame->stride);
        if (app->frame_wanted) frame_take(app, frame->pixels, frame->width, frame->height, (size_t)frame->stride);
    }
    app->media_has_frame = true;
    app->media_new_picture = true;
    app->media_position = frame->pts;
    app->media_shown_pts = frame->pts;
    app->media_shown_duration = frame->duration;
    app->media_sought = false;
    int64_t tenth = (int64_t)(frame->pts * 10.0);
    if (app->media_paused || tenth != app->media_title_tenth) {
        app->media_title_tenth = tenth;
        update_window_title(app);
    }
}

/* Once per loop pass: follow the sound, put the due frame on the page,
   and notice the end. */
void media_tick(app_state_t *app) {
    app_page_t *page = media_page_ready(app);
    if (!page) return;
    media_ab_check(app);
    rubraview_media_t *m = app->media;
    double now = rubraview_pal_time_now_seconds();

    /* D-120: the sound may be played at another rate than the file was
       opened with (another track, a format that changed while decoding);
       the information window and the analyser go by what is played. */
    uint32_t played_rate = 0, played_channels = 0;
    if (app->media_info.audio_output && rubraview_pal_media_audio_format(m, &played_rate, &played_channels)) {
        app->media_info.audio_sample_rate = played_rate;
        app->media_info.audio_channels = played_channels;
    }
    /* And the pictures' rate is what their times say: Windows' Matroska
       source names half of it, and a frame step went two pictures. */
    double measured_rate = 0.0;
    if (rubraview_pal_media_picture_rate(m, &measured_rate) &&
        rubraview_media_rate_differs(app->media_info.frame_rate, measured_rate)) {
        app->media_info.frame_rate = measured_rate;
    }

    /* §5.3: with the sound as master, the clock follows what is heard. */
    /* When the sound has run out before the picture (a film whose picture
       runs longer), the clock goes on by the wall: held at the last sound
       heard, the pictures after it never came due and the film never
       ended, so To Next did not go on (owner, 2026-10-16). */
    bool dry = !app->media_paused && rubraview_pal_media_sound_over(m);
    if (!dry) app->sound_dry_since = 0.0;
    else if (app->sound_dry_since == 0.0) app->sound_dry_since = now;
    bool sound_runs = !dry || now - app->sound_dry_since < 0.25;   /* a moment's gap is not the end */
    if (app->media_master == RUBRAVIEW_CLOCK_AUDIO && sound_runs) {
        double heard = 0.0, at = 0.0;
        if (rubraview_pal_media_audio_position(m, &heard, &at)) {
            rubraview_media_clock_sync_audio(&app->media_clock,
                rubraview_audio_position_now_at_rate(heard, at, now, !app->media_paused, MEDIA_AUDIO_EXTRAPOLATION,
                                                     app->media_speed > 0.0 ? app->media_speed : 1.0),
                now);
        }
    }
    double clock = rubraview_media_clock_now(&app->media_clock, now);

    if (!app->media_info.has_video) {
        /* Only sound: the position is the clock. */
        app->media_position = clock;
        int64_t tenth = (int64_t)(clock * 10.0);
        if (tenth != app->media_title_tenth) {
            app->media_title_tenth = tenth;
            update_window_title(app);
        }
    } else if (!(app->media_paused && app->media_has_frame)) {
        rubraview_video_frame_t frame;
        while (rubraview_pal_media_peek_frame(m, &frame)) {
            /* The first picture after opening or seeking shows at once. */
            rubraview_frame_decision_t decision = app->media_has_frame
                ? rubraview_media_schedule(frame.pts, frame.duration, clock)
                : RUBRAVIEW_FRAME_SHOW;
            if (decision == RUBRAVIEW_FRAME_WAIT) break;
            if (decision == RUBRAVIEW_FRAME_DROP && rubraview_pal_media_frames_ready(m) > 1) {
                rubraview_pal_media_pop_frame(m);
                app->stats_dropped++;   /* RV-215 */
                continue;
            }
            bool first = !app->media_has_frame;
            if (app->info_open && !first) { app->stats_shown++; app->stats_late += clock - frame.pts; }   /* RV-215 (D-248) */
            media_show(app, page, &frame);
            rubraview_pal_media_pop_frame(m);
            /* Only a wall clock may be moved to the picture. The sound is
               the truth when it is the master; a late picture is dropped. */
            if (app->media_master == RUBRAVIEW_CLOCK_WALL &&
                (first || clock - frame.pts > MEDIA_RESYNC_SECONDS)) {
                rubraview_media_clock_seek(&app->media_clock, frame.pts, now);
            }
            break;
        }
        /* RV-111: sound before the first picture or after the last — the
           film is where its clock is, not where its picture stands. With
           no picture yet since opening, that is where the sound is heard
           to be (after a seek the place asked for stands until it lands). */
        bool heard_first = !app->media_has_frame && !app->media_sought && app->media_master == RUBRAVIEW_CLOCK_AUDIO;
        if (!app->media_paused && (app->media_has_frame || heard_first)) {
            double at = rubraview_media_position_running(app->media_has_frame ? app->media_shown_pts : -1e9,
                                                         app->media_shown_duration,
                                                         rubraview_media_clock_now(&app->media_clock, now),
                                                         app->media_info.duration_seconds);
            if (at != app->media_position) {
                app->media_position = at;
                int64_t tenth = (int64_t)(at * 10.0);
                if (tenth != app->media_title_tenth) {
                    app->media_title_tenth = tenth;
                    update_window_title(app);
                }
            }
        }
    }

    /* RV-075: the next track is opened while this one still plays, and
       takes over when it is due. */
    music_transition_tick(app);
    if (app->media != m) return;      /* it took over; the rest is the new track's */
    if (rubraview_pal_media_stopped(m)) { media_stopped_tick(app); return; }   /* RV-176: before its end is judged */

    if (app->media_paused) app->media_progress_at = now;   /* 1.25.2: a pause is no stall */
    if (!app->media_paused) {
        /* A sound-only file with no device decodes nothing; its end is its duration. */
        bool at_end = (app->media_info.has_video || app->media_info.audio_output)
            ? rubraview_pal_media_finished(m)
            : (app->media_info.duration_seconds > 0.0 && clock >= app->media_info.duration_seconds);
        /* 1.25.2: the end nobody announced — a last picture waiting on a stopped clock, a device that never
           empties — found at the length with nothing new: no picture shown, no sound heard, for 3 s */
        double heard = 0.0, heard_at = 0.0;
        double progress = (app->media_has_frame ? app->media_shown_pts : 0.0) +
                          (rubraview_pal_media_audio_position(m, &heard, &heard_at) ? heard : 0.0);
        if (fabs(progress - app->media_progress_pos) > 0.001) {
            app->media_progress_pos = progress;
            app->media_progress_at = now;
        }
        if (!at_end && rubraview_media_end_overdue(app->media_position, app->media_info.duration_seconds,
                                                   now - app->media_progress_at)) at_end = true;
        if (at_end && app->next_media && app->next_playing) {
            /* One was already playing under it: hand over rather than
               stop — but not in the middle of a crossfade. The backend
               says "finished" when its decoder has run out, which is
               about a second before the sound the device still holds has
               been heard; handing over there closes the output and cuts
               both the tail and the fade (measured, T084). So the
               overlap is allowed to finish, with a wall-clock guard in
               case the position stops moving. */
            if (app->music_end_seen == 0.0) app->music_end_seen = now;
            double waited = now - app->music_end_seen;
            double crossfade = rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("crossfade_seconds"));
            if (!app->music_overlapping || waited > crossfade + 1.0) {
                music_promote_next(app);
                return;
            }
        } else if (!at_end) {
            app->music_end_seen = 0.0;
        }
        if (at_end && app->music_overlapping) return;   /* the overlap has the floor */
        if (at_end) {
            /* The end: hold the last picture. Slice 4 hands this to the slide show. */
            rubraview_media_clock_pause(&app->media_clock, now);
            rubraview_pal_media_set_paused(m, true);
            app->media_paused = true;
            app->media_ended = true;
            /* §3.14.6: the film is over; the music that stood aside for
               it may come back. */
            bgm_do(app, rubraview_bgm_event(&app->bgm, RUBRAVIEW_BGM_PAGE_QUIET, bgm_pause_for_sound(app)));
            if (!app->media_info.has_video && app->media_info.duration_seconds > 0.0 &&
                app->media_position > app->media_info.duration_seconds) {
                app->media_position = app->media_info.duration_seconds;   /* the clock overshoots by a pass */
            }
            update_window_title(app);
            media_after_end(app);
        }
    }
}

/* ---- What plays after a film or a song ends (owner, 2026-09-29) ---- */

static bool page_plays(void *ctx, size_t index) {
    const app_state_t *app = (const app_state_t *)ctx;
    return index < page_count(app) && is_media_path(app->source.pages[index].path);
}

rubraview_repeat_mode_t repeat_mode(const app_state_t *app) {
    int mode = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("at_end")));
    return mode >= 0 && mode < RUBRAVIEW_REPEAT_MODE_COUNT ? (rubraview_repeat_mode_t)mode : RUBRAVIEW_REPEAT_NEXT;
}

/* The page to play when this one ends, decided once for it (a shuffle's
   pick must not change between preparing the next track and reaching it). */
static int32_t media_follow_page(app_state_t *app) {
    if (!app->media || app->media_page < 0) return -1;
    rubraview_repeat_mode_t mode = repeat_mode(app);
    size_t count = page_count(app);
    if (app->follow_for == app->media_page && app->follow_mode == (int32_t)mode && app->follow_count == count) {
        return app->follow_page;
    }
    if (mode == RUBRAVIEW_REPEAT_SHUFFLE) {
        if (app->shuffle_flags_count != count || !app->shuffle_flags) {   /* a new list: a new shuffle */
            free(app->shuffle_flags);
            app->shuffle_flags = (uint8_t *)malloc(count ? count : 1);
            app->shuffle_flags_count = app->shuffle_flags ? count : 0;
            double t = rubraview_pal_time_now_seconds();
            uint64_t seed = 0;
            memcpy(&seed, &t, sizeof(seed) < sizeof(t) ? sizeof(seed) : sizeof(t));
            rubraview_shuffle_reset(&app->shuffle, app->shuffle_flags, app->shuffle_flags_count, seed ^ 0xA5A5A5A5u);
        }
        rubraview_shuffle_mark(&app->shuffle, (size_t)app->media_page);
    }
    app->follow_page = rubraview_repeat_follow(mode, app->media_page, count, page_plays, app, &app->shuffle);
    app->follow_for = app->media_page;
    app->follow_mode = (int32_t)mode;
    app->follow_count = count;
    return app->follow_page;
}

/* The film or the song has ended and is holding its last moment: go on as
   the mode says. A slide show decides for itself; A-B repeat never ends. */
void media_toggle_pause(app_state_t *app);
static void media_after_end(app_state_t *app) {
    if (app->slideshow_running || repeat_mode(app) == RUBRAVIEW_REPEAT_STOP) return;
    int32_t next = media_follow_page(app);
    if (next < 0) return;
    if (next == app->media_page) {
        media_toggle_pause(app);   /* ended: it plays again from the start */
        return;
    }
    app->follow_for = -1;
    app->subs_leave_unasked = true;
    go_to_spread(app, spread_for_chosen_page(app, next));
    app->subs_leave_unasked = false;
    update_precache(app);
}

/* ---- RV-075: one track running into the next ---- */

/* The page after this one, when it is a music file. A folder holds
   pictures and films too, and neither is something to slide into: the
   next track is the next *page*, and only when it is a track. */
static int32_t media_follow_page(app_state_t *app);
static int32_t music_next_page(app_state_t *app) {
    /* What plays next is the end-of-file mode's choice (owner, 2026-09-29);
       it is prepared under this one only when it is a track, not the same one. */
    int32_t next = media_follow_page(app);
    if (next < 0 || next == app->media_page) return -1;
    proven_u8str_view_t path = app->source.pages[next].path;
    if (!is_media_path(path) || !rubraview_tags_is_music_name(rubraview_path_basename(path))) return -1;
    return next;
}

static void music_next_close(app_state_t *app) {
    if (app->next_media) rubraview_pal_media_close(app->next_media);
    app->next_media = NULL;
    app->next_info = (rubraview_media_info_t){0};
    app->next_page = -1;
    app->next_playing = false;
    app->music_overlapping = false;
    app->music_end_seen = 0.0;
}

/* The one that was waiting becomes the one that is playing. Nothing is
   opened here — that is the whole point — so the page moves to it with
   the player it already has. */
static void music_promote_next(app_state_t *app) {
    if (!app->next_media) return;
    int32_t page = app->next_page;

    if (app->media) rubraview_pal_media_close(app->media);
    vobsub_clear(app);
    pgs_clear(app);
    music_clear(app);

    app->media = app->next_media;
    app->media_info = app->next_info;
    app->media_page = page;
    app->next_media = NULL;
    app->next_page = -1;
    app->next_playing = false;
    app->music_overlapping = false;
    app->music_end_seen = 0.0;

    rubraview_pal_media_set_gain(app->media, 1.0);
    app->media_paused = false;
    app->media_ended = false;
    app->media_progress_pos = -1.0;   /* 1.25.2: counted from now, not from the film before */
    app->media_progress_at = rubraview_pal_time_now_seconds();
    app->media_has_frame = false;
    app->media_sought = false;
    app->media_position = 0.0;
    app->media_title_tenth = -1;
    app->media_master = rubraview_media_master_for(app->media_info.has_audio, app->media_info.audio_output);
    app->media_clock = rubraview_media_clock_create(app->media_master, 0.0, rubraview_pal_time_now_seconds());
    rubraview_media_clock_set_rate(&app->media_clock, app->media_speed, rubraview_pal_time_now_seconds());
    app->ab_a = app->ab_b = -1.0;      /* a repeat belongs to the track it was set in */

    music_load(app, app->source.pages[page].path);
    subtitle_load(app, app->source.pages[page].path);
    (void)media_page_ready(app);

    /* And the page on screen follows. `media_prepare` sees that the
       player is already this page's and leaves it alone. */
    go_to_spread(app, spread_index_for_page(app, page));
    update_precache(app);
}

/* Called once a tick while a track plays: open the next one in time,
   start it when it is due, and hold both at the levels the plan says. */
static void music_transition_tick(app_state_t *app) {
    if (!app->media || app->media_info.has_video || app->media_paused) return;
    if (app->media_info.live) return;   /* RV-157: a station never ends on its own, so nothing follows it in time */

    rubraview_track_change_t how = {
        .gapless = rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("gapless")) != 0.0,
        .crossfade_seconds = rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("crossfade_seconds")),
    };
    int32_t next = music_next_page(app);
    /* The track that was waiting is no longer the one that follows —
       the reader moved on by hand — so it is let go. */
    if (app->next_media && app->next_page != next) music_next_close(app);

    rubraview_track_plan_t plan = rubraview_track_plan(how, app->media_position,
                                                       app->media_info.duration_seconds, next >= 0);

    if (plan.open_next && !app->next_media && next >= 0 && is_station_path(app->source.pages[next].path)) {
        app->next_page = next;   /* RV-157: a station is not opened ahead; it is joined when it is reached */
    } else if (plan.open_next && !app->next_media && next >= 0) {
        proven_u8str_view_t path = app->source.pages[next].path;
        rubraview_media_backend_t order[2];
        size_t count = rubraview_media_backend_order(app->media_preferred,
                                                     rubraview_pal_media_backend_available(RUBRAVIEW_BACKEND_FFMPEG),
                                                     order);
        for (size_t i = 0; i < count; ++i) {
            /* No graphics card for a track: there are no pictures in it. */
            rubraview_media_open_result_t opened = rubraview_pal_media_open(path, order[i], NULL);
            if (opened.media) {
                app->next_media = opened.media;
                app->next_info = opened.info;
                app->next_page = next;
                app->next_playing = false;
                rubraview_pal_media_set_gain(app->next_media, 0.0);
                rubraview_pal_media_set_paused(app->next_media, true);
                break;
            }
        }
        /* One that cannot be opened is not tried again every tick: the
           end of this track will move to it in the ordinary way. */
        if (!app->next_media) app->next_page = next;
    }

    if (plan.start_next && app->next_media && !app->next_playing) {
        rubraview_pal_media_set_paused(app->next_media, false);
        app->next_playing = true;
    }

    rubraview_pal_media_set_gain(app->media, plan.gain_current);
    if (app->next_media) rubraview_pal_media_set_gain(app->next_media, plan.gain_next);
    app->music_overlapping = app->next_playing && !plan.close_current;

    if (plan.close_current && app->next_playing) music_promote_next(app);
}

/* D-15 A-B repeat: past B, back to A. */
void media_seek_to(app_state_t *app, double seconds);
void media_ab_check(app_state_t *app) {
    if (!app->media || app->media_paused || app->ab_a < 0.0 || app->ab_b <= app->ab_a) return;
    if (app->media_position >= app->ab_b) {
        media_seek_to(app, app->ab_a);
        /* The picture still shown is from past B until the next frame
           comes; without this every tick would seek again and it would
           never play on. */
        app->media_position = app->ab_a;
    }
}

void media_seek_to(app_state_t *app, double seconds) {
    if (!app->media) return;
    if (app->media_info.live) { osd_say(app, TU8("a live station cannot be wound")); return; }   /* RV-157 */
    double duration = app->media_info.duration_seconds;
    if (duration > 0.0 && seconds > duration) seconds = duration;
    if (seconds < 0.0) seconds = 0.0;
    rubraview_pal_media_seek(app->media, seconds);
    rubraview_media_clock_seek(&app->media_clock, seconds, rubraview_pal_time_now_seconds());
    /* The landing frame shows at once, even while paused; a sound-only
       page keeps its picture. */
    app->media_has_frame = !app->media_info.has_video;
    app->media_sought = true;
    app->media_ended = false;
    app->media_progress_pos = -1.0;   /* 1.25.2: counted from now, not from the film before */
    app->media_progress_at = rubraview_pal_time_now_seconds();
}

void media_toggle_pause(app_state_t *app) {
    if (!app->media) return;
    double now = rubraview_pal_time_now_seconds();
    if (app->media_paused) {
        /* At the end, playing again starts from the beginning. */
        if (app->media_ended) media_seek_to(app, 0.0);
        rubraview_media_clock_resume(&app->media_clock, now);
        rubraview_pal_media_set_paused(app->media, false);
        app->media_paused = false;
    } else {
        rubraview_media_clock_pause(&app->media_clock, now);
        rubraview_pal_media_set_paused(app->media, true);
        app->media_paused = true;
    }
    update_window_title(app);
}

static double media_frame_seconds(const app_state_t *app) {
    return app->media_info.frame_rate > 0.0 ? 1.0 / app->media_info.frame_rate : 1.0 / 30.0;
}

/* §5.3 frame stepping. Forward takes the next decoded frame; back seeks
   into the middle of the previous frame's interval, which the exact seek
   turns into that frame. Either way playback is paused first. */
void media_step(app_state_t *app, bool forward) {
    if (!app->media || !app->media_info.has_video) return;   /* sound has no frames to step */
    if (!app->media_paused) {
        rubraview_media_clock_pause(&app->media_clock, rubraview_pal_time_now_seconds());
        rubraview_pal_media_set_paused(app->media, true);
        app->media_paused = true;
    }
    if (!forward) {
        media_seek_to(app, app->media_position - media_frame_seconds(app) * 0.5);
        update_window_title(app);
        return;
    }
    app_page_t *page = media_page_ready(app);
    rubraview_video_frame_t frame;
    for (int i = 0; i < 100 && !rubraview_pal_media_peek_frame(app->media, &frame); ++i) {
        rubraview_pal_time_sleep_ms(5);   /* the decoder is at most one frame behind */
    }
    if (page && rubraview_pal_media_peek_frame(app->media, &frame)) {
        media_show(app, page, &frame);
        rubraview_pal_media_pop_frame(app->media);
        rubraview_media_clock_seek(&app->media_clock, frame.pts, rubraview_pal_time_now_seconds());
    }
}
