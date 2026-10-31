/*
 * The file's information window and the help window (RV-163, D-197: moved from main.c as they were).
 */
#include "app.h"

/* ---- the help window (owner, 2026-09-23) ---- */

#define HELP_BACKGROUND 0xFF141414u
#define HELP_HEADING    0xFF6FA8DCu
#define HELP_KEYS       0xFFE8C46Fu
#define HELP_TEXT       0xFFE8E8E8u
#define HELP_LINK       0xFF5AAEFFu   /* the project page, pressed like a link */


/* ---- the file's information (owner, 2026-09-29) ---- */

#define INFO_ARENA_BYTES (4u * 1024u * 1024u)
#define INFO_MAX_ROWS 512
#define INFO_LABEL_COLS 20

void info_local_time(char *buf, size_t cap, int64_t unix_seconds) {
    buf[0] = '\0';
    if (unix_seconds <= 0) return;
    time_t t = (time_t)unix_seconds;
    struct tm local;
    if (localtime_s(&local, &t) != 0) return;
    strftime(buf, cap, "%Y-%m-%d %H:%M:%S", &local);
}

static const char *track_kind_name(rubraview_track_kind_t kind) {
    return kind == RUBRAVIEW_TRACK_VIDEO ? T("Video") : kind == RUBRAVIEW_TRACK_AUDIO ? T("Sound") : T("Subtitle");
}

static void info_folder_summary(app_state_t *app, proven_u8str_view_t dir);
static void info_add_summary(app_state_t *app, proven_arena_t *a, rubraview_info_t *info);

/* What there is to say about the page on screen, gathered into rows. */
static void info_add_details(proven_arena_t *a, rubraview_info_t *info, const rubraview_details_t *details) {
    for (size_t i = 0; details && i < details->count; ++i) {
        const rubraview_detail_t *line = &details->lines[i];
        if (line->heading) rubraview_info_heading(a, info, line->label);
        else rubraview_info_add(a, info, line->label, (proven_u8str_view_t){ (const proven_byte_t *)line->value, strlen(line->value) });
    }
}

static void info_gather(app_state_t *app) {
    if (!app->info_mem) {
        app->info_mem = malloc(INFO_ARENA_BYTES);
        if (!app->info_mem) return;
        app->info_arena = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)app->info_mem, .size = INFO_ARENA_BYTES });
    }
    proven_arena_reset(&app->info_arena);
    proven_arena_t *a = &app->info_arena;
    rubraview_info_t info = {0};
    char b[256];
    app->info_sel.active = false;

    int32_t index = current_page_index(app);
    if (index < 0 || (size_t)index >= page_count(app)) {
        rubraview_info_heading(a, &info, T("Nothing is open"));
    } else {
        const rubraview_page_ref_t *ref = &app->source.pages[index];
        bool in_archive = app->source.kind != RUBRAVIEW_PAGE_SOURCE_FOLDER;
        proven_u8str_view_t path = ref->path;

        /* The file */
        rubraview_info_heading(a, &info, in_archive ? T("Page") : T("File"));
        rubraview_info_add(a, &info, T("Name"), in_archive ? ref->name : rubraview_path_basename(path));
        if (!in_archive) rubraview_info_add(a, &info, T("Folder"), rubraview_path_dirname(path));
        rubraview_info_addf(a, &info, T("Position"), "%d of %zu", index + 1, page_count(app));
        uint64_t file_bytes = 0;
        rubraview_fs_entry_t entry;
        if (!in_archive && rubraview_pal_fs_stat(a, path, &entry)) {
            file_bytes = entry.size_bytes;
            rubraview_info_add(a, &info, T("Size"), (proven_u8str_view_t){ (const proven_byte_t *)b, rubraview_info_bytes(b, sizeof(b), entry.size_bytes) });
            info_local_time(b, sizeof(b), entry.mtime);
            rubraview_info_add(a, &info, T("Modified"), (proven_u8str_view_t){ (const proven_byte_t *)b, strlen(b) });
            info_local_time(b, sizeof(b), entry.ctime);
            rubraview_info_add(a, &info, T("Created"), (proven_u8str_view_t){ (const proven_byte_t *)b, strlen(b) });
        }
        if (in_archive) {
            file_bytes = rubraview_page_source_entry_size(&app->source, (size_t)index);
            rubraview_info_add(a, &info, T("Size"), (proven_u8str_view_t){ (const proven_byte_t *)b, rubraview_info_bytes(b, sizeof(b), file_bytes) });
            const fulta_arc_entry_t *en = rubraview_page_source_entry(&app->source, (size_t)index);
            if (en) {
                /* The method as the archive names it (FultaArc's word for it); a
                   solid block has no packed size of its page's own. */
                char packed[96];
                rubraview_info_bytes(packed, sizeof(packed), en->packed_size);
                if (en->packed_size > 0) rubraview_info_addf(a, &info, T("In the archive"), "%s, %s", en->method ? en->method : "?", packed);
                else rubraview_info_addf(a, &info, T("In the archive"), "%s", en->method ? en->method : "?");
            }
            rubraview_info_heading(a, &info, T("Archive"));
            rubraview_info_add(a, &info, T("Name"), rubraview_path_basename(app->source.archive_path));
            rubraview_info_add(a, &info, T("Folder"), rubraview_path_dirname(app->source.archive_path));
            rubraview_info_add(a, &info, T("Kind"), app->source.kind == RUBRAVIEW_PAGE_SOURCE_ARCHIVE_7Z ? TU8("7z (CB7)")
                                               : app->source.kind == RUBRAVIEW_PAGE_SOURCE_ARCHIVE_RAR ? TU8("RAR (CBR)")
                                               : app->source.kind == RUBRAVIEW_PAGE_SOURCE_ARCHIVE_ALZ ? TU8("ALZ")
                                               : app->source.kind == RUBRAVIEW_PAGE_SOURCE_ARCHIVE_EGG ? TU8("EGG")
                                                     : TU8("ZIP (CBZ)"));
            if (rubraview_page_source_is_solid(&app->source))
                rubraview_info_add(a, &info, T("Solid"), TU8("yes: a far page decodes the ones before it"));
            uint64_t archive_bytes = app->archive_map.size, set_bytes = 0;
            size_t volumes = rubraview_page_source_volumes(&app->source, &set_bytes);
            if (volumes > 1) {
                rubraview_info_addf(a, &info, T("Volumes"), T("%zu, read as one"), volumes);
                archive_bytes = set_bytes;   /* a set: the volumes read so far */
            }
            if (rubraview_page_source_is_encrypted(&app->source)) rubraview_info_add(a, &info, T("Encrypted"), TU8("yes"));
            rubraview_info_add(a, &info, T("Size"), (proven_u8str_view_t){ (const proven_byte_t *)b, rubraview_info_bytes(b, sizeof(b), archive_bytes) });
            rubraview_info_addf(a, &info, T("Pages"), "%zu", page_count(app));
            /* the book's own summary: its folders and what it holds unpacked */
            uint64_t unpacked = 0;
            size_t folders = 0;
            proven_u8str_view_t last = PROVEN_LIT("");
            for (size_t p = 0; p < page_count(app); ++p) {
                unpacked += rubraview_page_source_entry_size(&app->source, p);
                proven_u8str_view_t dir = rubraview_path_dirname(app->source.pages[p].name);
                if (dir.size > 0 && !(dir.size == last.size && memcmp(dir.ptr, last.ptr, dir.size) == 0)) { folders++; last = dir; }
            }
            if (folders) rubraview_info_addf(a, &info, T("Folders inside"), "%zu", folders);
            rubraview_info_add(a, &info, T("Pages unpacked"), (proven_u8str_view_t){ (const proven_byte_t *)b, rubraview_info_bytes(b, sizeof(b), unpacked) });
        }

        bool film = app->media && app->media_page == index;
        if (film) {
            /* A film or a song */
            const rubraview_media_info_t *m = &app->media_info;
            rubraview_info_heading(a, &info, m->has_video ? T("Film") : T("Sound"));
            rubraview_info_add(a, &info, T("Length"), (proven_u8str_view_t){ (const proven_byte_t *)b, rubraview_info_duration(b, sizeof(b), m->duration_seconds) });
            if (m->has_video) {
                char code[32];
                rubraview_info_fourcc(code, sizeof(code), m->video_fourcc);
                rubraview_info_addf(a, &info, T("Picture"), T("%d x %d, %.3g frames a second"), m->width, m->height, m->frame_rate);
                rubraview_info_add(a, &info, T("Picture codec"), (proven_u8str_view_t){ (const proven_byte_t *)code, strlen(code) });
                rubraview_info_add(a, &info, T("Decoded"), m->hardware_decode ? TU8("on the graphics card") : TU8("in software"));
            }
            if (m->has_audio && m->audio_sample_rate) {
                rubraview_info_addf(a, &info, T("Sound"), T("%u Hz, %u channel%s"), (unsigned)m->audio_sample_rate,
                                    (unsigned)m->audio_channels, m->audio_channels == 1 ? "" : TC("plural", "s"));
            }
            {   /* 1.25.1: the device moved to another format since (the decoder said one thing, the samples another) */
                uint32_t pr = 0, pc = 0;
                if (m->has_audio && rubraview_pal_media_audio_format(app->media, &pr, &pc) && pr > 0 &&
                    (pr != m->audio_sample_rate || pc != m->audio_channels))
                    rubraview_info_addf(a, &info, T("Sound played at"), T("%u Hz, %u channel%s"), (unsigned)pr, (unsigned)pc,
                                        pc == 1 ? "" : TC("plural", "s"));
            }
            uint32_t kbps = m->bitrate_kbps;
            if (!kbps && file_bytes && m->duration_seconds > 0.0) kbps = (uint32_t)((double)file_bytes * 8.0 / m->duration_seconds / 1000.0);
            if (kbps) rubraview_info_addf(a, &info, T("Bitrate"), "%u kbit/s%s", (unsigned)kbps, m->bitrate_kbps ? "" : T(" (size over length)"));
            rubraview_info_add(a, &info, T("Opened by"), m->backend == RUBRAVIEW_BACKEND_FFMPEG ? PROVEN_LIT("FFmpeg") : PROVEN_LIT("Media Foundation"));
            if (m->has_video) {
                /* 1.25.1 (owner, 2026-10-20: "어떤 백엔드나 다이렉트x 같은거 어떻게 하여 화면에 출력해 주는지도 자세한 정보"):
                   the way a picture goes from the file to the screen */
                rubraview_info_heading(a, &info, T("Display"));
                if (m->path_decoder[0]) {
                    char dec[256];
                    int dn = snprintf(dec, sizeof(dec), "%s", rubraview_tr(m->path_decoder));
                    if (dn > 0 && m->decode_threads > 0) snprintf(dec + dn, sizeof(dec) - (size_t)dn, T(", %d threads"), (int)m->decode_threads);
                    else if (dn > 0 && m->decode_threads < 0) snprintf(dec + dn, sizeof(dec) - (size_t)dn, "%s", T(", a thread a core"));
                    rubraview_info_add(a, &info, T("Decoder"), cstr(dec));
                }
                if (m->path_colours[0]) rubraview_info_add(a, &info, T("Colours"), cstr(rubraview_tr(m->path_colours)));
                if (m->path_handover[0]) rubraview_info_add(a, &info, T("Into the window"), cstr(rubraview_tr(m->path_handover)));
                rubraview_render_facts_t rf;
                if (rubraview_pal_render_facts(app->renderer, &rf)) {
                    unsigned fl = rf.feature_level;
                    rubraview_info_addf(a, &info, T("Drawn by"), T("Direct2D 1.1 on Direct3D 11 (feature level %u_%u), %s"),
                                        (fl >> 12) & 0xFu, (fl >> 8) & 0xFu, rf.warp ? T("WARP, Windows' software rasteriser") : rf.adapter);
                    if (rf.video_memory > 0)
                        rubraview_info_addf(a, &info, T("Video memory"), "%llu MB", (unsigned long long)(rf.video_memory >> 20));
                    if (rf.hdr_output)   /* RV-153 */
                        rubraview_info_addf(a, &info, T("Presented"), T("DXGI swap chain, %d buffers, scRGB FP16 for an HDR screen (SDR white at %.0f nits), %s"),
                                            rf.buffers, rf.hdr_sdr_white, rf.vsync ? T("with the screen's refresh (vsync)") : T("as soon as drawn"));
                    else
                    rubraview_info_addf(a, &info, T("Presented"), T("DXGI swap chain, %d buffers, BGRA, %s, %s"), rf.buffers,
                                        rf.present_model == RUBRAVIEW_PRESENT_FLIP_DISCARD ? T("flip model (flip discard)")
                                        : rf.present_model == RUBRAVIEW_PRESENT_FLIP_SEQUENTIAL ? T("flip model (flip sequential)")
                                        : T("copy model (blt)"),
                                        rf.vsync ? T("with the screen's refresh (vsync)") : T("as soon as drawn"));
                    if (rf.decode_device) rubraview_info_addf(a, &info, T("Card's decoders"), T("%u kinds (Direct3D 11 video)"), (unsigned)rf.decoder_profiles);
                }
                int gm = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("video"), PROVEN_LIT("hardware_decode")));
                const char *mode = gm <= 0 ? TC("choice", "off") : gm == 1 ? TC("choice", "on") : TC("choice", "always");
                rubraview_info_addf(a, &info, T("GPU decoding"), "%s%s%s", mode,
                                    m->path_why_software[0] ? " - " : "", m->path_why_software[0] ? rubraview_tr(m->path_why_software) : "");
                if (m->hdr_transfer) {   /* RV-150 */
                    const char *kind = m->hdr_transfer == RUBRAVIEW_HDR_PQ ? "PQ (HDR10)" : "HLG";
                    double peak = 0.0;
                    if (m->hdr_tone_mapped && m->hdr_output && rubraview_pal_media_hdr_peak(app->media, &peak))   /* RV-153 */
                        rubraview_info_addf(a, &info, T("HDR"), T("%s, peak %.0f nits, sent to the screen as HDR: tone mapped on the graphics card (BT.2390) to its peak of %.0f nits"),
                                            kind, peak, app->media_hdr_screen.hdr && app->media_hdr_screen.peak_nits > 0.0 ? app->media_hdr_screen.peak_nits
                                            : app->media_hdr_screen.hdr ? RUBRAVIEW_HDR_DEFAULT_PEAK : app->media_hdr_screen.sdr_white_nits);
                    else if (m->hdr_tone_mapped && rubraview_pal_media_hdr_peak(app->media, &peak))
                        rubraview_info_addf(a, &info, T("HDR"), T("%s, peak %.0f nits, tone mapped on the graphics card (BT.2390) to a %.0f-nit white"),
                                            kind, peak, RUBRAVIEW_SDR_WHITE_NITS);
                    else
                        rubraview_info_addf(a, &info, T("HDR"), T("%s, not tone mapped - %s"), kind,
                                            m->path_hdr_why[0] ? rubraview_tr(m->path_hdr_why) : T("Windows' decoder does not tone map"));
                    if (m->hdr_tone_mapped && !m->hdr_output) {   /* RV-153: why not as HDR */
                        int ho = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("video"), PROVEN_LIT("hdr_output")));
                        const char *why = ho <= 0 ? T("HDR output is off (Settings > Video)")
                                        : app->hdr_output_refused && app->hdr_output_why ? rubraview_tr(app->hdr_output_why)
                                        : !app->media_hdr_screen.hdr ? T("this screen is not in HDR (Windows' display settings)")
                                        : T("not for this film");
                        rubraview_info_addf(a, &info, T("HDR output"), T("off - %s"), why);
                    }
                    if (app->media_hdr_handed == 1)
                        rubraview_info_add(a, &info, T("Handed over"), TU8("Windows' decoder found it HDR; FFmpeg took it to tone map it"));
                    else if (app->media_hdr_handed == 2)   /* RV-152 */
                        rubraview_info_add(a, &info, T("Handed over"), TU8("Windows' decoder names no transfer for this codec; FFmpeg found it HDR and took it to tone map it"));
                }
            }
            /* What the container and the codecs say of themselves, all of it (owner, 2026-10-11). */
            info_add_details(a, &info, rubraview_pal_media_details(app->media));
            if (app->tracks.count > 0) {
                rubraview_info_heading(a, &info, T("Tracks"));
                for (size_t t = 0; t < app->tracks.count && t < 32; ++t) {
                    const rubraview_track_t *tr = &app->tracks.tracks[t];
                    char label[24];
                    snprintf(label, sizeof(label), "%s %zu", track_kind_name(tr->kind), t + 1);
                    int n = snprintf(b, sizeof(b), "%.*s", (int)tr->codec.size, tr->codec.ptr);
                    if (tr->channels > 0 && n > 0) n += snprintf(b + n, sizeof(b) - (size_t)n, T(", %d ch"), tr->channels);
                    if (tr->language.size && n > 0) n += snprintf(b + n, sizeof(b) - (size_t)n, ", %.*s", (int)tr->language.size, tr->language.ptr);
                    if (tr->title.size && n > 0) n += snprintf(b + n, sizeof(b) - (size_t)n, ", \"%.*s\"", (int)tr->title.size, tr->title.ptr);
                    if (n > 0) rubraview_info_add(a, &info, label, (proven_u8str_view_t){ (const proven_byte_t *)b, (size_t)n < sizeof(b) ? (size_t)n : sizeof(b) - 1 });
                }
            }
            if (app->music_tags_read) {
                const rubraview_tags_t *g = &app->music_tags;
                rubraview_info_heading(a, &info, T("Tags"));
                rubraview_info_add(a, &info, T("Title"), g->title);
                rubraview_info_add(a, &info, T("Artist"), g->artist);
                rubraview_info_add(a, &info, T("Album"), g->album);
                rubraview_info_add(a, &info, T("Year"), g->year);
                rubraview_info_add(a, &info, T("Track"), g->track_number);
                rubraview_info_add(a, &info, T("Genre"), g->genre);
                rubraview_info_add(a, &info, T("Codec"), g->codec);
                if (g->bits_per_sample) rubraview_info_addf(a, &info, T("Bits"), "%u", (unsigned)g->bits_per_sample);
                if (g->art_size) rubraview_info_add(a, &info, T("Cover"), (proven_u8str_view_t){ (const proven_byte_t *)b, rubraview_info_bytes(b, sizeof(b), g->art_size) });
            }
        } else if (!is_media_path(path) && !is_text_path(path)) {
            /* A picture: what it is, then what its EXIF says. */
            rubraview_fs_mapping_t map = {0};
            void *mem = NULL;
            const uint8_t *bytes = NULL;
            size_t size = 0;
            if (!in_archive) {
                if (rubraview_pal_fs_map(path, &map) && map.size <= (uint64_t)SIZE_MAX) { bytes = map.data; size = (size_t)map.size; }
            } else if (page_read_cost(app, (size_t)index) <= PAGE_JOB_MIN_COST) {
                rubraview_page_bytes_t pb = page_read_owned(&app->source, &app->source_lock, (size_t)index, &mem);
                if (pb.ok) { bytes = (const uint8_t*)pb.data.ptr; size = pb.data.size; }
            }
            rubraview_image_desc_t d;
            bool described = bytes && rubraview_pal_image_describe(in_archive ? PROVEN_LIT("") : path, bytes, size, &d);
            if (!described && !in_archive) described = rubraview_pal_image_describe(path, NULL, 0, &d);
            /* The file's own account of how it was made (read below too, for the ratio). */
            static rubraview_details_t details;
            uint32_t raw_bits = 0;
            memset(&details, 0, sizeof(details));
            bool known = bytes && rubraview_image_details(bytes, size, &details, &raw_bits);
            /* D-120: an AVIF or a JPEG XL decoded by our own readers reaches
               Windows as a TIFF made in memory; that carrier's name and
               pixel format are not the file's. */
            const char *own = known && details.count > 0 && details.lines[0].heading ? details.lines[0].label : "";
            bool carried = described && strcmp(d.format, "TIFF") == 0 && own[0] && strcmp(own, "TIFF") != 0;
            rubraview_info_heading(a, &info, T("Picture"));
            if (described) {
                rubraview_info_add(a, &info, T("Format"), carried ? cstr(own) : (proven_u8str_view_t){ (const proven_byte_t *)d.format, strlen(d.format) });
                rubraview_info_addf(a, &info, T("Pixels"), T("%d x %d (%.1f megapixels)"), d.width, d.height,
                                    (double)d.width * (double)d.height / 1e6);
                rubraview_info_add(a, &info, T("Aspect ratio"), (proven_u8str_view_t){ (const proven_byte_t *)b, rubraview_info_aspect(b, sizeof(b), d.width, d.height) });
                if (!carried) rubraview_info_add(a, &info, T("Pixel format"), (proven_u8str_view_t){ (const proven_byte_t *)d.pixels, strlen(d.pixels) });
                if (d.dpi_x > 0.0) {
                    rubraview_info_addf(a, &info, T("Resolution"), T("%.0f x %.0f dpi"), d.dpi_x, d.dpi_y);
                    rubraview_info_add(a, &info, T("Print size"),
                                       (proven_u8str_view_t){ (const proven_byte_t *)b, rubraview_info_print_size(b, sizeof(b), d.width, d.height, d.dpi_x, d.dpi_y) });
                }
                if (d.frames > 1) rubraview_info_addf(a, &info, T("Frames"), "%u", (unsigned)d.frames);
                rubraview_info_add(a, &info, T("Colour profile"), d.color_profile ? TU8("embedded") : TU8("none"));
            } else if (in_archive && !bytes) {
                rubraview_info_add(a, &info, T("Details"), TU8("not read: far inside a solid 7z block"));
            }
            const app_page_t *page = &app->pages[index];
            if (page->loaded && page->reduced) {
                rubraview_info_addf(a, &info, T("On screen"), T("reduced to %d x %d (the graphics card's limit)"), page->width, page->height);
            }
            if (page->loaded && page->width > 0 && page->height > 0) {
                rubraview_info_add(a, &info, T("In memory"),
                                   (proven_u8str_view_t){ (const proven_byte_t *)b, rubraview_info_bytes(b, sizeof(b), (uint64_t)page->width * (uint64_t)page->height * 4u) });
            }
            /* RV-130: read once more, timed, and its colours counted: a
               picture up to 24 megapixels, so turning pages with the window
               open stays quick. */
            if (bytes && described && d.frames <= 1 && d.width > 0 && d.height > 0 &&
                (double)d.width * (double)d.height <= 24e6) {
                size_t need = picture_bytes((size_t)d.width, (size_t)d.height, 4u, 4u << 20);
                void *work = malloc(need);
                if (work) {
                    proven_arena_t wa = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)work, .size = need });
                    double t0 = rubraview_pal_time_now_seconds();
                    rubraview_pixbuf_t px = rubraview_pal_image_read_pixels(&wa, in_archive ? PROVEN_LIT("") : path, bytes, size, false);
                    double ms = (rubraview_pal_time_now_seconds() - t0) * 1000.0;
                    if (px.pixels) {
                        rubraview_info_addf(a, &info, T("Decoded in"), T("%.0f ms"), ms);
                        uint32_t colours = rubraview_info_unique_colours(&wa, &px);
                        if (colours) rubraview_info_addf(a, &info, T("Unique colours"), "%u", (unsigned)colours);
                    }
                    free(work);
                }
            }
            if (bytes) {
                /* How much the file's way of storing saved. */
                if (described && d.width > 0 && d.height > 0) {
                    double pixels = (double)d.width * (double)d.height * (d.frames > 1 ? (double)d.frames : 1.0);
                    char ratio[96];
                    rubraview_info_addf(a, &info, T("Stored at"), T("%.2f bits a pixel"), (double)size * 8.0 / pixels);
                    if (rubraview_detail_ratio(ratio, sizeof(ratio), (double)size, pixels * (raw_bits ? raw_bits : 24) / 8.0)) {
                        rubraview_info_addf(a, &info, T("Compression"), T("%s, at %u bits a pixel"), ratio, (unsigned)(raw_bits ? raw_bits : 24));
                    }
                }
            }
            {
                /* RV-166 (D-199): how a settled page reaches the screen without a graphics card */
                rubraview_render_facts_t rf;
                if (rubraview_pal_render_facts(app->renderer, &rf) && rf.screen_copies)
                    rubraview_info_add(a, &info, T("Settled pages"), cstr(T("drawn from a copy at screen size (software adapter)")));
            }
            if (bytes && known) info_add_details(a, &info, &details);
            if (bytes) {
                rubraview_exif_info_t exif;
                if (rubraview_exif_read(bytes, size, &exif)) rubraview_info_add_exif(a, &info, &exif);
                else rubraview_info_add(a, &info, T("EXIF"), TU8("none in this file"));
                static rubraview_iptc_info_t iptc;
                if (rubraview_iptc_read(bytes, size, &iptc)) rubraview_info_add_iptc(a, &info, &iptc);
            }
            /* 1.18.0: what Rubraview kept beside it, shown as the file's own */
            rubraview_details_src_t beside;
            proven_u8str_view_t beside_path = PROVEN_LIT("");
            if (!in_archive && details_beside(a, path, &beside, &beside_path)) {
                proven_u8str_view_t beside_name = rubraview_path_basename(beside_path);
                char title[400];
                snprintf(title, sizeof(title), T("Kept beside it in %.*s"), (int)(beside_name.size < 300 ? beside_name.size : 300), beside_name.ptr);
                rubraview_info_heading(a, &info, title);
                for (size_t i = 0; i < beside.info.count; ++i) {
                    const rubraview_info_line_t *l = &beside.info.lines[i];
                    if (l->heading) rubraview_info_heading(a, &info, (const char *)l->label.ptr);
                    else rubraview_info_add(a, &info, (const char *)l->label.ptr, l->value);
                }
                if (beside.xmp.size > 0) rubraview_info_addf(a, &info, T("XMP"), T("%zu bytes"), beside.xmp.size);
            }
            free(mem);
            rubraview_pal_fs_unmap(&map);
        }
    }

    /* What the folder holds (the book's folder when a book is open). */
    if (index >= 0 && (size_t)index < page_count(app)) {
        proven_u8str_view_t dir = app->source.kind != RUBRAVIEW_PAGE_SOURCE_FOLDER ? rubraview_path_dirname(app->source.archive_path)
                                                                     : rubraview_path_dirname(app->source.pages[index].path);
        info_folder_summary(app, dir);
        info_add_summary(app, a, &info);
    }

    proven_result_mem_mut_t rr = rubraview_arena_alloc_array(a, INFO_MAX_ROWS, sizeof(proven_u8str_view_t));
    proven_result_mem_mut_t hr = rubraview_arena_alloc_array(a, INFO_MAX_ROWS, sizeof(bool));
    if (!proven_is_ok(rr.err) || !proven_is_ok(hr.err)) { app->info_count = 0; return; }
    app->info_rows = (proven_u8str_view_t*)(void*)rr.value.ptr;
    app->info_heading = (bool*)(void*)hr.value.ptr;
    app->info_count = rubraview_info_rows(a, &info, INFO_LABEL_COLS, app->info_rows, INFO_MAX_ROWS);
    for (size_t i = 0, r = 0; i < info.count && r < app->info_count; ++i, ++r) app->info_heading[r] = info.lines[i].heading;
    if (app->info_scroll > (int32_t)app->info_count) app->info_scroll = 0;
    app->info_dirty = true;
}

/* What the folder holds, a kind at a time, listed once per folder (owner,
   2026-09-29: "폴더 전체 요약"). Its own memory: a folder of a hundred
   thousand files is a big listing. */
enum { INFO_PICTURES, INFO_FILMS, INFO_SONGS, INFO_ARCHIVES, INFO_FOLDERS, INFO_OTHER, INFO_ALL };

static void info_folder_summary(app_state_t *app, proven_u8str_view_t dir) {
    if (dir.size == 0 || dir.size >= sizeof(app->info_folder)) return;
    if (strlen(app->info_folder) == dir.size && memcmp(app->info_folder, dir.ptr, dir.size) == 0) return;   /* the same one */
    memset(app->info_counts, 0, sizeof(app->info_counts));
    memset(app->info_bytes, 0, sizeof(app->info_bytes));
    size_t budget = 64u * 1024u * 1024u;
    void *mem = malloc(budget);
    if (!mem) return;
    proven_arena_t scratch = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)mem, .size = budget });
    rubraview_fs_listing_t listing = rubraview_pal_fs_list_dir(&scratch, dir);
    for (size_t i = 0; i < listing.count; ++i) {
        const rubraview_fs_entry_t *e = &listing.entries[i];
        int kind = e->is_directory ? INFO_FOLDERS
                 : rubraview_glob_match_list(e->name, PROVEN_LIT(IMAGE_FILTER)) ? INFO_PICTURES
                 : rubraview_glob_match_list(e->name, PROVEN_LIT(ARCHIVE_FILTER)) ? INFO_ARCHIVES
                 : rubraview_glob_match_list(e->name, PROVEN_LIT(MEDIA_FILTER))
                     ? (rubraview_tags_is_music_name(e->name) ? INFO_SONGS : INFO_FILMS)
                 : INFO_OTHER;
        app->info_counts[kind]++;
        if (!e->is_directory) {
            app->info_bytes[kind] += e->size_bytes;
            app->info_bytes[INFO_ALL] += e->size_bytes;
            app->info_counts[INFO_ALL]++;
        }
    }
    free(mem);
    memcpy(app->info_folder, dir.ptr, dir.size);
    app->info_folder[dir.size] = '\0';
}

static void info_add_summary(app_state_t *app, proven_arena_t *a, rubraview_info_t *info) {
    const char *const NAME[] = { T("Pictures"), T("Films"), T("Songs"), T("Archives"), T("Folders"), T("Other files"), T("All files") };
    rubraview_info_heading(a, info, T("This folder"));
    char size[96];
    for (int k = 0; k <= INFO_ALL; ++k) {
        if (app->info_counts[k] == 0) continue;
        if (k == INFO_FOLDERS) { rubraview_info_addf(a, info, NAME[k], "%llu", (unsigned long long)app->info_counts[k]); continue; }
        rubraview_info_bytes(size, sizeof(size), app->info_bytes[k]);
        rubraview_info_addf(a, info, NAME[k], "%llu, %s", (unsigned long long)app->info_counts[k], size);
    }
}

/* Which page and which film the rows are of: gathered again when it changes. */
static int64_t info_key_now(const app_state_t *app) {
    int64_t page = current_page_index(app);
    int64_t loaded = page >= 0 && (size_t)page < page_count(app) && app->pages[page].loaded ? 1 : 0;
    return (page + 1) * 4 + (app->media ? 2 : 0) + loaded + (int64_t)app->source.page_count * 1000003;
}

void info_close(app_state_t *app) {
    if (!app->info_open) return;
    rubraview_pal_window_get_frame(app->info_window, &app->info_frame[0], &app->info_frame[1],
                                   &app->info_frame[2], &app->info_frame[3]);
    if (app->info_renderer) rubraview_pal_render_destroy(app->info_renderer);
    if (app->info_window) rubraview_pal_window_destroy(app->info_window);
    window_block_give(app, &app->info_block);
    app->info_renderer = NULL;
    app->info_window = NULL;
    app->info_open = false;
}

void info_show(app_state_t *app) {
    if (app->info_open) { info_close(app); return; }   /* I or Ctrl+I again puts it away */
    rubraview_window_config_t config = {
        .title = T("Rubraview - information"), .width = 760, .height = 640, .owner = app->window,
    };
    app->info_window = rubraview_pal_window_create(window_block_take(app, &app->info_block), &config);
    if (!app->info_window) { osd_say(app, TU8("the information window could not be opened")); return; }
    if (app->info_frame[2] > 0 && app->info_frame[3] > 0) {
        rubraview_pal_window_set_frame(app->info_window, app->info_frame[0], app->info_frame[1],
                                       app->info_frame[2], app->info_frame[3]);
    }
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->info_window, &w, &h);
    app->info_renderer = rubraview_pal_render_create(window_block_arena(app, &app->info_block), rubraview_pal_window_native_handle(app->info_window), w, h);
    ui_font_apply_all(app);
    if (!app->info_renderer) {
        rubraview_pal_window_destroy(app->info_window); window_block_give(app, &app->info_block);
        app->info_window = NULL;
        osd_say(app, TU8("the information window could not be drawn"));
        return;
    }
    double dpi = rubraview_pal_window_dpi_scale(app->info_window);
    app->info_font = 15.0 * dpi;
    if (!rubraview_pal_render_mono_cell(app->info_renderer, app->info_font, &app->info_cell_w, &app->info_cell_h) ||
        app->info_cell_w <= 0.0 || app->info_cell_h <= 0.0) {
        app->info_cell_w = 8.3 * dpi;
        app->info_cell_h = 17.6 * dpi;
    }
    app->info_open = true;
    app->info_scroll = 0;
    app->info_message[0] = '\0';
    app->info_key = info_key_now(app);
    info_gather(app);
}

static int32_t info_visible_rows(const app_state_t *app) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->info_window, &w, &h);
    int32_t rows = (int32_t)((double)h / app->info_cell_h) - 2;
    return rows > 1 ? rows : 1;
}

static void info_scroll_by(app_state_t *app, int32_t lines) {
    int32_t last = (int32_t)app->info_count - info_visible_rows(app);
    if (last < 0) last = 0;
    app->info_scroll += lines;
    if (app->info_scroll > last) app->info_scroll = last;
    if (app->info_scroll < 0) app->info_scroll = 0;
    app->info_dirty = true;
}

/* RV-170: the rows' scroll bar, at the window's right edge. */
bool info_sb(app_state_t *app, rubraview_scrollbar_t *sb, double bw, double dpi) {
    if (!app->info_open) return false;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->info_window, &w, &h);
    int32_t rows = info_visible_rows(app);
    double ch = app->info_cell_h;
    *sb = (rubraview_scrollbar_t){ .area = { (double)w - bw - 2.0 * dpi, ch * 0.5, bw, ch * (double)rows },
                                   .button = bw, .total = (double)app->info_count, .shown = (double)rows,
                                   .first = (double)app->info_scroll, .step = 1.0, .min_thumb = ch };
    return true;
}

void info_sb_to(app_state_t *app, size_t first) {
    info_scroll_by(app, (int32_t)first - app->info_scroll);
}

#define INFO_BACKGROUND 0xFF16181Cu
#define INFO_HEADING    0xFF7FB2E5u
#define INFO_TEXT       0xFFE8E8E8u
#define INFO_DIM        0xFF8A9099u

/* Text on the grid, each narrow run and each wide character at its own cell. */
static void info_text(app_state_t *app, proven_u8str_view_t text, double x, double y, uint32_t color) {
    rubraview_cell_run_t runs[256];
    size_t n = rubraview_cell_runs(text, runs, sizeof(runs) / sizeof(runs[0]));
    for (size_t i = 0; i < n; ++i) {
        proven_u8str_view_t piece = { .ptr = text.ptr + runs[i].offset, .size = runs[i].length };
        if (piece.size == 1 && piece.ptr[0] == ' ') continue;
        rubraview_pal_render_draw_text_mono(app->info_renderer, piece, x + app->info_cell_w * runs[i].col,
                                            y, app->info_font, color);
    }
}

void draw_info_window(app_state_t *app) {
    if (!app->info_open) return;
    int64_t key = info_key_now(app);
    if (key != app->info_key) { app->info_key = key; info_gather(app); }
    if (!app->info_dirty) return;
    app->info_dirty = false;
    rubraview_renderer_t *r = app->info_renderer;
    const double cw = app->info_cell_w, ch = app->info_cell_h;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->info_window, &w, &h);
    rubraview_pal_render_begin(r, INFO_BACKGROUND);
    double x0 = cw;
    int32_t rows = info_visible_rows(app);
    for (int32_t row = 0; row < rows; ++row) {
        size_t index = (size_t)(app->info_scroll + row);
        if (index >= app->info_count) break;
        double y = ch * (0.5 + (double)row);
        int32_t c0 = 0, c1 = 0;
        if (rubraview_grid_selection_row(&app->info_sel, (int32_t)index, &c0, &c1)) {
            int32_t last = (int32_t)((double)w / cw);
            if (c1 > last) c1 = last;
            if (c1 >= c0) rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ x0 + cw * c0, y, cw * (c1 - c0 + 1), ch }, 0x704A90D9u, 0.0);
        }
        info_text(app, app->info_rows[index], x0, y, app->info_heading[index] ? INFO_HEADING : INFO_TEXT);
    }
    char foot[160];
    int n = snprintf(foot, sizeof(foot), T(" %s%sdrag to mark   |   Ctrl+C copies (all when nothing is marked)   |   Ctrl+A   |   Esc"),
                     app->info_message, app->info_message[0] ? "   |   " : "");
    if (n > 0) rubraview_pal_render_draw_text_mono(r, (proven_u8str_view_t){ (const proven_byte_t *)foot, (size_t)n < sizeof(foot) ? (size_t)n : sizeof(foot) - 1 },
                                                   0.0, (double)h - ch * 1.2, app->info_font, INFO_DIM);
    draw_scrollbar(app, r, RVSB_INFO, app->info_px, app->info_py);   /* RV-170 */
    if (!rubraview_pal_render_end(r)) app->info_dirty = true;
}

/* Ctrl+C: what is marked, or everything; the rows as they are drawn. */
static void info_copy(app_state_t *app) {
    char *text = (char*)malloc(65536);
    if (!text) return;
    rubraview_grid_selection_t all = { true, 0, 0, (int32_t)app->info_count, INT32_MAX / 2 };
    const rubraview_grid_selection_t *sel = app->info_sel.active ? &app->info_sel : &all;
    size_t n = rubraview_grid_selection_copy(sel, app->info_rows, (int32_t)app->info_count, text, 65536);
    bool ok = n > 0 && rubraview_pal_clipboard_set_text(app->info_window, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)text, .size = n });
    snprintf(app->info_message, sizeof(app->info_message), "%s", ok ? (app->info_sel.active ? T("copied") : T("copied everything")) : T("nothing to copy"));
    free(text);
    app->info_dirty = true;
}

size_t info_pump(app_state_t *app) {
    size_t handled = 0;
    rubraview_window_event_t event;
    while (app->info_open && rubraview_pal_window_poll_event(app->info_window, &event)) {
        handled++;
        int32_t col = 0, row = 0;
        if (event.kind == RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN || event.kind == RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE ||
            event.kind == RUBRAVIEW_WINDOW_EVENT_MOUSE_UP) {
            col = (int32_t)((event.mouse.x - app->info_cell_w) / app->info_cell_w);
            row = app->info_scroll + (int32_t)(event.mouse.y / app->info_cell_h - 0.5);
            if (col < 0) col = 0;
            if (row < 0) row = 0;
            if (row >= (int32_t)app->info_count) row = app->info_count > 0 ? (int32_t)app->info_count - 1 : 0;
        }
        switch (event.kind) {
            case RUBRAVIEW_WINDOW_EVENT_CLOSE:
                info_close(app);
                return handled;
            case RUBRAVIEW_WINDOW_EVENT_RESIZE:
                rubraview_pal_render_resize(app->info_renderer, event.resize.width, event.resize.height);
                app->info_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_PAINT:
                app->info_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_WHEEL:
                info_scroll_by(app, event.mouse.wheel_delta > 0 ? -3 : 3);
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN:
                if (event.mouse.button != RUBRAVIEW_MOUSE_LEFT) break;
                if (scrollbar_press(app, RVSB_INFO, event.mouse.x, event.mouse.y)) { app->info_dirty = true; break; }   /* RV-170 */
                app->info_sel = (rubraview_grid_selection_t){ true, row, col, row, col };
                app->info_selecting = true;
                app->info_message[0] = '\0';
                app->info_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE:
                app->info_px = event.mouse.x;
                app->info_py = event.mouse.y;
                if (scrollbar_motion(app, app->info_window, event.mouse.x, event.mouse.y)) { app->info_dirty = true; break; }   /* RV-170 */
                if (scrollbar_hover(app, RVSB_INFO, event.mouse.x, event.mouse.y)) app->info_dirty = true;
                if (!app->info_selecting) break;
                app->info_sel.end_row = row;
                app->info_sel.end_col = col;
                app->info_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_UP:
                if (scrollbar_release(app, app->info_window)) { app->info_dirty = true; break; }   /* RV-170 */
                if (!app->info_selecting) break;
                app->info_selecting = false;
                if (app->info_sel.anchor_row == app->info_sel.end_row && app->info_sel.anchor_col == app->info_sel.end_col) {
                    app->info_sel.active = false;   /* a click marks nothing */
                }
                app->info_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN: {
                rubraview_key_combo_t k = event.key.combo;
                bool ctrl = (k.modifiers & RUBRAVIEW_MOD_CTRL) != 0;
                if (ctrl && key_is(k, "C")) { info_copy(app); break; }
                if (ctrl && key_is(k, "A")) {
                    app->info_sel = (rubraview_grid_selection_t){ true, 0, 0, (int32_t)app->info_count, INT32_MAX / 2 };
                    app->info_dirty = true;
                    break;
                }
                if (key_is(k, "Escape") || key_is(k, "I")) { info_close(app); return handled; }
                if (key_is(k, "Down")) info_scroll_by(app, 1);
                else if (key_is(k, "Up")) info_scroll_by(app, -1);
                else if (key_is(k, "PageDown")) info_scroll_by(app, info_visible_rows(app) - 1);
                else if (key_is(k, "PageUp")) info_scroll_by(app, -(info_visible_rows(app) - 1));
                else if (key_is(k, "Home")) info_scroll_by(app, -1000000);
                else if (key_is(k, "End")) info_scroll_by(app, 1000000);
                break;
            }
            default:
                break;
        }
    }
    return handled;
}

static void help_measure(app_state_t *app) {
    double dpi = rubraview_pal_window_dpi_scale(app->help_window);
    app->help_font = 15.0 * dpi;
    if (!rubraview_pal_render_mono_cell(app->help_renderer, app->help_font,
                                        &app->help_cell_w, &app->help_cell_h) ||
        app->help_cell_w <= 0.0 || app->help_cell_h <= 0.0) {
        app->help_cell_w = 8.3 * dpi;
        app->help_cell_h = 17.6 * dpi;
    }
}

/* The lines are built from the keymap in force, so a rebound key shows
   its new binding the next time the window is opened or the keys change. */
static void help_rebuild(app_state_t *app) {
    app->help_count = rubraview_help_build(app->arena, &app->keymap, rubraview_boxes_document(),
                                           app->help_lines, sizeof(app->help_lines) / sizeof(app->help_lines[0]));
    /* Then the notices of what is linked in: the executable is offered on
       its own too, and they go where it goes (owner, 2026-09-29). */
    const char *const *notices = NULL;
    size_t notice_count = rubraview_notice_lines(&notices);
    size_t room = sizeof(app->help_lines) / sizeof(app->help_lines[0]);
    if (app->help_count + 3 < room) {
        app->help_lines[app->help_count++] = (rubraview_help_line_t){ .kind = RUBRAVIEW_HELP_BLANK };
        app->help_lines[app->help_count++] = (rubraview_help_line_t){ .kind = RUBRAVIEW_HELP_HEADING,
                                                                      .text = TU8("Licences of what is inside") };
    }
    for (size_t i = 0; i < notice_count && app->help_count < room; ++i) {
        app->help_lines[app->help_count++] = (rubraview_help_line_t){
            .kind = RUBRAVIEW_HELP_NOTE, .text = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)notices[i], .size = strlen(notices[i]) } };
    }
    /* The second column starts after the longest key list there is, so a
       binding with three keys does not run into what it does. */
    size_t widest = 0;
    for (size_t i = 0; i < app->help_count; ++i) {
        if (app->help_lines[i].keys.size > widest) widest = app->help_lines[i].keys.size;
    }
    app->help_key_cols = (int32_t)widest + 4;
    if (app->help_key_cols < 20) app->help_key_cols = 20;
    if (app->help_key_cols > 48) app->help_key_cols = 48;
    /* The rows as drawn: two cells in, the keys, the text from its column. */
    if (!app->help_row_text) app->help_row_text = (char (*)[256])calloc(2048, 256);
    for (size_t i = 0; i < app->help_count && app->help_row_text; ++i) {
        const rubraview_help_line_t *l = &app->help_lines[i];
        char *row = app->help_row_text[i];
        int n = 0;
        if (l->kind == RUBRAVIEW_HELP_ENTRY) {
            int pad = app->help_key_cols - 2 - (int)l->keys.size;
            n = snprintf(row, 256, "  %.*s%*s%.*s", (int)l->keys.size, l->keys.ptr, pad > 1 ? pad : 1, "",
                         (int)l->text.size, l->text.ptr);
        } else if (l->kind != RUBRAVIEW_HELP_BLANK) {
            n = snprintf(row, 256, "  %.*s", (int)l->text.size, l->text.ptr);
        }
        if (n < 0) n = 0;
        if (n > 255) n = 255;
        app->help_rows[i] = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)row, .size = (size_t)n };
    }
    app->help_sel.active = false;
    app->help_dirty = true;
}

void help_close(app_state_t *app) {
    if (!app->help_open) return;
    rubraview_pal_window_get_frame(app->help_window, &app->help_frame[0], &app->help_frame[1],
                                   &app->help_frame[2], &app->help_frame[3]);
    if (app->help_renderer) rubraview_pal_render_destroy(app->help_renderer);
    if (app->help_window) rubraview_pal_window_destroy(app->help_window);
    window_block_give(app, &app->help_block);
    app->help_renderer = NULL;
    app->help_window = NULL;
    app->help_open = false;
}

void help_show(app_state_t *app) {
    if (app->help_open) { help_close(app); return; }   /* ? again puts it away */

    rubraview_window_config_t config = {
        .title = T("Rubraview help - keys"), .width = 860, .height = 720, .owner = app->window,
    };
    app->help_window = rubraview_pal_window_create(window_block_take(app, &app->help_block), &config);
    if (!app->help_window) {
        osd_say(app, TU8("the help window could not be opened"));
        return;
    }
    if (app->help_frame[2] > 0 && app->help_frame[3] > 0) {
        rubraview_pal_window_set_frame(app->help_window, app->help_frame[0], app->help_frame[1],
                                       app->help_frame[2], app->help_frame[3]);
    }
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->help_window, &w, &h);
    app->help_renderer = rubraview_pal_render_create(window_block_arena(app, &app->help_block),
                                                     rubraview_pal_window_native_handle(app->help_window), w, h);
    ui_font_apply_all(app);
    if (!app->help_renderer) {
        rubraview_pal_window_destroy(app->help_window); window_block_give(app, &app->help_block);
        app->help_window = NULL;
        osd_say(app, TU8("the help window could not be drawn"));
        return;
    }
    help_measure(app);
    app->help_open = true;
    app->help_scroll = 0;
    help_rebuild(app);
}

/* How many lines fit, and how far the reader may scroll. */
/* Above the keys (owner, 2026-10-18): the version and the day it was
   released, the project's page as a link, a blank row. */
#define HELP_TOP_ROWS 3

static int32_t help_rows(const app_state_t *app) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->help_window, &w, &h);
    int32_t rows = (int32_t)((double)h / app->help_cell_h) - 1 - HELP_TOP_ROWS;
    return rows > 1 ? rows : 1;
}

static void help_scroll_by(app_state_t *app, int32_t lines) {
    int32_t last = (int32_t)app->help_count - help_rows(app);
    if (last < 0) last = 0;
    app->help_scroll += lines;
    if (app->help_scroll > last) app->help_scroll = last;
    if (app->help_scroll < 0) app->help_scroll = 0;
    app->help_dirty = true;
}

void draw_help_window(app_state_t *app) {
    if (!app->help_open || !app->help_dirty) return;
    app->help_dirty = false;
    rubraview_renderer_t *r = app->help_renderer;
    const double cw = app->help_cell_w, ch = app->help_cell_h, fs = app->help_font;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->help_window, &w, &h);

    rubraview_pal_render_begin(r, HELP_BACKGROUND);

    int32_t rows = help_rows(app);
    double keys_col = cw * 2.0;
    double text_col = cw * (double)app->help_key_cols;
    {
        char top[160];
        int n = snprintf(top, sizeof(top), T("Rubraview %s, released %s"), RUBRAVIEW_VERSION_STRING, RUBRAVIEW_RELEASE_DATE);
        if (n > 0) rubraview_pal_render_draw_text_mono(r, (proven_u8str_view_t){ (const proven_byte_t *)top, (size_t)n }, keys_col, 0.0, fs, HELP_HEADING);
        proven_u8str_view_t url = PROVEN_LIT(RUBRAVIEW_PROJECT_URL);
        rubraview_pal_render_draw_text_mono(r, url, keys_col, ch, fs, HELP_LINK);
        rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ keys_col, ch * 2.0 - (ch > 16.0 ? ch / 16.0 : 1.0) * 1.5,
                                                                  cw * (double)url.size, ch > 16.0 ? ch / 16.0 : 1.0 }, HELP_LINK, 0.0);
    }
    for (int32_t row = 0; row < rows; ++row) {
        size_t index = (size_t)(app->help_scroll + row);
        if (index >= app->help_count) break;
        const rubraview_help_line_t *line = &app->help_lines[index];
        double y = ch * (double)(row + HELP_TOP_ROWS);
        int32_t c0 = 0, c1 = 0;
        if (rubraview_grid_selection_row(&app->help_sel, (int32_t)index, &c0, &c1)) {
            int32_t last = (int32_t)((double)w / cw);
            if (c1 > last) c1 = last;
            if (c1 >= c0) rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ cw * c0, y, cw * (c1 - c0 + 1), ch }, 0x704A90D9u, 0.0);
        }
        switch (line->kind) {
            case RUBRAVIEW_HELP_HEADING:
                rubraview_pal_render_draw_text_mono(r, line->text, keys_col, y, fs, HELP_HEADING);
                rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ keys_col, y + ch * 0.92,
                                                                          (double)w - keys_col * 2.0, 1.0 },
                                               HELP_HEADING, 0.0);
                break;
            case RUBRAVIEW_HELP_ENTRY:
                rubraview_pal_render_draw_text_mono(r, line->keys, keys_col, y, fs, HELP_KEYS);
                rubraview_pal_render_draw_text_mono(r, line->text, text_col, y, fs, HELP_TEXT);
                break;
            case RUBRAVIEW_HELP_NOTE:
                rubraview_pal_render_draw_text_mono(r, line->text, keys_col, y, fs, HELP_TEXT);
                break;
            case RUBRAVIEW_HELP_BLANK:
            default:
                break;
        }
    }

    /* The bottom line says how to move and how to leave. */
    char foot[128];
    int written = snprintf(foot, sizeof(foot), T(" %s%skeys, then the licences at the end   |   drag to mark, Ctrl+C copies   |   ? or Esc closes"),
                           app->help_message, app->help_message[0] ? "   |   " : "");
    if (written > 0) {
        rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ 0.0, (double)h - ch, (double)w, ch },
                                       0xFF1E1E1Eu, 0.0);
        rubraview_pal_render_draw_text_mono(r, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)foot, .size = (size_t)written },
                                            0.0, (double)h - ch, fs, HELP_HEADING);
    }
    rubraview_pal_render_end(r);
}

size_t help_pump(app_state_t *app) {
    size_t handled = 0;
    rubraview_window_event_t event;
    while (app->help_open && rubraview_pal_window_poll_event(app->help_window, &event)) {
        handled++;
        switch (event.kind) {
            case RUBRAVIEW_WINDOW_EVENT_CLOSE:
                help_close(app);
                return handled;
            case RUBRAVIEW_WINDOW_EVENT_RESIZE:
                rubraview_pal_render_resize(app->help_renderer, event.resize.width, event.resize.height);
                help_scroll_by(app, 0);   /* the last line may have moved */
                break;
            case RUBRAVIEW_WINDOW_EVENT_DPI_CHANGED:
                help_measure(app);
                app->help_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_PAINT:
                app->help_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_WHEEL:
                help_scroll_by(app, (int32_t)(-event.mouse.wheel_delta * 3.0));
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN:
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE:
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_UP: {
                /* A drag marks the text, as in the settings and information windows. */
                int32_t col = (int32_t)(event.mouse.x / app->help_cell_w);
                int32_t screen_row = (int32_t)(event.mouse.y / app->help_cell_h);
                if (screen_row == 1 && event.kind == RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN && event.mouse.button == RUBRAVIEW_MOUSE_LEFT &&
                    col >= 2 && col < 2 + (int32_t)strlen(RUBRAVIEW_PROJECT_URL)) {   /* the project's page */
                    if (!rubraview_pal_shell_open_web(RUBRAVIEW_PROJECT_URL)) snprintf(app->help_message, sizeof(app->help_message), "%s", T("the page could not be opened"));
                    app->help_dirty = true;
                    break;
                }
                int32_t row = app->help_scroll + screen_row - HELP_TOP_ROWS;
                if (row >= (int32_t)app->help_count) row = app->help_count ? (int32_t)app->help_count - 1 : 0;
                if (row < 0) row = 0;
                if (col < 0) col = 0;
                if (event.kind == RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN && event.mouse.button == RUBRAVIEW_MOUSE_LEFT) {
                    app->help_sel = (rubraview_grid_selection_t){ true, row, col, row, col };
                    app->help_selecting = true;
                    app->help_message[0] = '\0';
                } else if (event.kind == RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE && app->help_selecting) {
                    app->help_sel.end_row = row;
                    app->help_sel.end_col = col;
                } else if (event.kind == RUBRAVIEW_WINDOW_EVENT_MOUSE_UP && app->help_selecting) {
                    app->help_selecting = false;
                    if (app->help_sel.anchor_row == app->help_sel.end_row && app->help_sel.anchor_col == app->help_sel.end_col) {
                        app->help_sel.active = false;
                    }
                } else {
                    break;
                }
                app->help_dirty = true;
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN: {
                rubraview_key_combo_t combo = event.key.combo;
                bool ctrl = (combo.modifiers & RUBRAVIEW_MOD_CTRL) != 0;
                if (ctrl && key_is(combo, "A")) {
                    app->help_sel = (rubraview_grid_selection_t){ true, 0, 0, (int32_t)app->help_count, INT32_MAX / 2 };
                    app->help_dirty = true;
                    break;
                }
                if (ctrl && key_is(combo, "C")) {
                    char *text = (char*)malloc(131072);
                    if (!text) break;
                    rubraview_grid_selection_t all = { true, 0, 0, (int32_t)app->help_count, INT32_MAX / 2 };
                    size_t n = rubraview_grid_selection_copy(app->help_sel.active ? &app->help_sel : &all,
                                                             app->help_rows, (int32_t)app->help_count, text, 131072);
                    bool ok = n > 0 && rubraview_pal_clipboard_set_text(app->help_window, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)text, .size = n });
                    snprintf(app->help_message, sizeof(app->help_message), "%s",
                             ok ? (app->help_sel.active ? T("copied") : T("copied everything")) : T("nothing to copy"));
                    free(text);
                    app->help_dirty = true;
                    break;
                }
                if (key_is(combo, "Escape") || ((combo.modifiers & RUBRAVIEW_MOD_SHIFT) && key_is(combo, "Slash"))) { help_close(app); return handled; }   /* ? again (RV-211) */
                else if (key_is(combo, "Down")) help_scroll_by(app, 1);
                else if (key_is(combo, "Up")) help_scroll_by(app, -1);
                else if (key_is(combo, "PageDown") || key_is(combo, "Space")) help_scroll_by(app, help_rows(app) - 1);
                else if (key_is(combo, "PageUp")) help_scroll_by(app, -(help_rows(app) - 1));
                else if (key_is(combo, "Home")) help_scroll_by(app, -(int32_t)app->help_count);
                else if (key_is(combo, "End")) help_scroll_by(app, (int32_t)app->help_count);
                break;
            }
            default:
                break;
        }
    }
    return handled;
}
