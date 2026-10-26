#include "rubraview/media_host.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* RV-176 (D-208): the block an FFmpeg child shares with the viewer, and the viewer's checks of what it wrote. */

void rubraview_mhost_init(rubraview_mhost_block_t *b, rubraview_mhost_mode_t mode) {
    if (!b) return;
    b->magic = RUBRAVIEW_MHOST_MAGIC;
    b->version = RUBRAVIEW_MHOST_VERSION;
    b->bytes = sizeof(*b);
    b->mode = (uint32_t)mode;
    atomic_init(&b->request_serial, 0);
    atomic_init(&b->reply_serial, 0);
    atomic_init(&b->wanted.paused, 0);
    atomic_init(&b->wanted.quit, 0);
    atomic_init(&b->wanted.seek_100ns, 0);
    atomic_init(&b->wanted.seek_generation, 0);
    atomic_init(&b->wanted.gain_millionths, 1000000);
    atomic_init(&b->wanted.audio_stream, -1);
    atomic_init(&b->wanted.audio_serial, 0);
    atomic_init(&b->observed.seq, 0);
    atomic_init(&b->heartbeat, 0);
    atomic_init(&b->log_used, 0);
    rubraview_spsc_init(&b->ring, RUBRAVIEW_MHOST_SLOTS);
}

bool rubraview_mhost_block_ok(const rubraview_mhost_block_t *b, uint64_t mapped_bytes) {
    return b && mapped_bytes >= sizeof(*b) && b->magic == RUBRAVIEW_MHOST_MAGIC && b->version == RUBRAVIEW_MHOST_VERSION &&
           (b->mode == RUBRAVIEW_MHOST_PLAYBACK || b->mode == RUBRAVIEW_MHOST_SERVICE);
}

/* ---- text ---- */

void rubraview_mhost_text_put(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0) return;
    size_t n = src ? strlen(src) : 0;
    if (n >= cap) {
        n = cap - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0u) == 0x80u) n--;   /* not inside a character */
    }
    if (n) memcpy(dst, src, n);
    memset(dst + n, 0, cap - n);
}

void rubraview_mhost_text_take(char *dst, size_t cap, const char *src, size_t src_cap) {
    if (!dst || cap == 0) return;
    dst[0] = '\0';
    if (!src || src_cap == 0) return;
    size_t len = 0;
    while (len < src_cap && src[len]) len++;   /* never past the field, ended or not */
    proven_u8str_view_t s = { .ptr = (const proven_byte_t *)src, .size = len };
    size_t out = 0;
    for (size_t pos = 0; pos < len;) {
        proven_utf8_char_t c = proven_utf8_decode_next(s, pos);
        size_t step = c.len ? c.len : 1;
        bool ok = proven_is_ok(c.err) && c.cp >= 0x20u && c.cp != 0x7Fu && !(c.cp >= 0x80u && c.cp < 0xA0u);
        if (ok) {
            if (out + c.len >= cap) break;
            memcpy(dst + out, src + pos, c.len);
            out += c.len;
        } else {
            if (out + 1 >= cap) break;
            dst[out++] = '?';
        }
        pos += step;
    }
    dst[out] = '\0';
}

void rubraview_mhost_log(rubraview_mhost_block_t *b, const char *line) {
    if (!b || !line) return;
    size_t n = strlen(line);
    if (n >= RUBRAVIEW_MHOST_LOG / 2) n = RUBRAVIEW_MHOST_LOG / 2 - 1;
    uint32_t used = atomic_load_explicit(&b->log_used, memory_order_relaxed);
    if (used > RUBRAVIEW_MHOST_LOG) used = 0;
    if (used + n + 1 > RUBRAVIEW_MHOST_LOG) {   /* the older half goes */
        uint32_t keep = used > RUBRAVIEW_MHOST_LOG / 2 ? RUBRAVIEW_MHOST_LOG / 2 : used;
        memmove(b->log, b->log + used - keep, keep);
        used = keep;
    }
    memcpy(b->log + used, line, n);
    b->log[used + n] = '\n';
    atomic_store_explicit(&b->log_used, used + (uint32_t)n + 1, memory_order_release);
}

/* ---- the open reply ---- */

void rubraview_mhost_info_put(rubraview_mhost_info_t *w, const rubraview_media_info_t *info) {
    if (!w || !info) return;
    memset(w, 0, sizeof(*w));
    w->backend = (int32_t)info->backend;
    w->width = info->width;
    w->height = info->height;
    w->decode_threads = info->decode_threads;
    w->hdr_transfer = info->hdr_transfer;
    w->video_fourcc = info->video_fourcc;
    w->audio_sample_rate = info->audio_sample_rate;
    w->audio_channels = info->audio_channels;
    w->bitrate_kbps = info->bitrate_kbps;
    w->has_video = info->has_video;
    w->has_audio = info->has_audio;
    w->audio_output = info->audio_output;
    w->hardware_decode = info->hardware_decode;
    w->hdr_tone_mapped = info->hdr_tone_mapped;
    w->hdr_output = info->hdr_output;
    w->live = info->live;
    w->duration_seconds = info->duration_seconds;
    w->frame_rate = info->frame_rate;
    rubraview_mhost_text_put(w->path_decoder, sizeof w->path_decoder, info->path_decoder);
    rubraview_mhost_text_put(w->path_colours, sizeof w->path_colours, info->path_colours);
    rubraview_mhost_text_put(w->path_handover, sizeof w->path_handover, info->path_handover);
    rubraview_mhost_text_put(w->path_why_software, sizeof w->path_why_software, info->path_why_software);
    rubraview_mhost_text_put(w->path_hdr_why, sizeof w->path_hdr_why, info->path_hdr_why);
}

static bool finite_at_least(double v, double lo) { return isfinite(v) && v >= lo; }

bool rubraview_mhost_info_take(const rubraview_mhost_info_t *w, rubraview_media_info_t *out) {
    if (!w || !out) return false;
    memset(out, 0, sizeof(*out));
    if (w->has_video > 1 || w->has_audio > 1 || w->audio_output > 1 || w->hardware_decode > 1 || w->hdr_tone_mapped > 1 ||
        w->hdr_output > 1 || w->live > 1) return false;
    if (w->has_video && (w->width <= 0 || w->height <= 0 || w->width > RUBRAVIEW_MHOST_MAX_SIDE || w->height > RUBRAVIEW_MHOST_MAX_SIDE))
        return false;
    if (!w->has_video && (w->width != 0 || w->height != 0)) return false;
    if (!finite_at_least(w->duration_seconds, 0.0) || !finite_at_least(w->frame_rate, 0.0) || w->frame_rate > 1000.0) return false;
    if (w->hdr_transfer < 0 || w->hdr_transfer > 2 || w->decode_threads < -1 || w->decode_threads > 1024) return false;
    if (w->audio_channels > 64 || w->audio_sample_rate > 768000) return false;
    out->backend = RUBRAVIEW_BACKEND_FFMPEG;   /* whatever it said: the child is FFmpeg's */
    out->duration_seconds = w->duration_seconds;
    out->frame_rate = w->frame_rate;
    out->width = w->width;
    out->height = w->height;
    out->has_video = w->has_video;
    out->has_audio = w->has_audio;
    out->audio_output = w->audio_output;
    out->video_fourcc = w->video_fourcc;
    out->hardware_decode = w->hardware_decode;
    out->audio_sample_rate = w->audio_sample_rate;
    out->audio_channels = w->audio_channels;
    out->bitrate_kbps = w->bitrate_kbps;
    out->decode_threads = w->decode_threads;
    out->hdr_transfer = w->hdr_transfer;
    out->hdr_tone_mapped = w->hdr_tone_mapped;
    out->hdr_output = w->hdr_output;
    out->live = w->live;
    rubraview_mhost_text_take(out->path_decoder, sizeof out->path_decoder, w->path_decoder, sizeof w->path_decoder);
    rubraview_mhost_text_take(out->path_colours, sizeof out->path_colours, w->path_colours, sizeof w->path_colours);
    rubraview_mhost_text_take(out->path_handover, sizeof out->path_handover, w->path_handover, sizeof w->path_handover);
    rubraview_mhost_text_take(out->path_why_software, sizeof out->path_why_software, w->path_why_software, sizeof w->path_why_software);
    rubraview_mhost_text_take(out->path_hdr_why, sizeof out->path_hdr_why, w->path_hdr_why, sizeof w->path_hdr_why);
    return true;
}

void rubraview_mhost_tracks_put(rubraview_mhost_block_t *b, const rubraview_track_set_t *tracks) {
    if (!b) return;
    memset(b->tracks, 0, sizeof b->tracks);
    b->track_count = 0;
    b->current_video = b->current_audio = b->current_subtitle = -1;
    if (!tracks) return;
    size_t n = tracks->count < RUBRAVIEW_MAX_TRACKS ? tracks->count : RUBRAVIEW_MAX_TRACKS;
    for (size_t i = 0; i < n; ++i) {
        const rubraview_track_t *t = &tracks->tracks[i];
        rubraview_mhost_track_t *w = &b->tracks[i];
        w->kind = (int32_t)t->kind;
        w->stream_index = t->stream_index;
        w->channels = t->channels;
        w->is_default = t->is_default;
        w->is_forced = t->is_forced;
        char tmp[RUBRAVIEW_MHOST_TEXT];
        const proven_u8str_view_t parts[3] = { t->language, t->title, t->codec };
        char *dst[3] = { w->language, w->title, w->codec };
        for (int k = 0; k < 3; ++k) {
            size_t len = parts[k].size < sizeof tmp - 1 ? parts[k].size : sizeof tmp - 1;
            if (len) memcpy(tmp, parts[k].ptr, len);
            tmp[len] = '\0';
            rubraview_mhost_text_put(dst[k], RUBRAVIEW_MHOST_TEXT, tmp);
        }
    }
    b->track_count = (int32_t)n;
    b->current_video = tracks->current_video;
    b->current_audio = tracks->current_audio;
    b->current_subtitle = tracks->current_subtitle;
}

bool rubraview_mhost_tracks_take(const rubraview_mhost_track_t *w, int32_t count, int32_t current_video, int32_t current_audio,
                                 int32_t current_subtitle, rubraview_track_set_t *out,
                                 char text[RUBRAVIEW_MAX_TRACKS][3][RUBRAVIEW_MHOST_TEXT]) {
    if (!w || !out || !text) return false;
    *out = rubraview_tracks_create();
    if (count < 0 || count > RUBRAVIEW_MAX_TRACKS) return false;
    if (current_video < -1 || current_video >= count || current_audio < -1 || current_audio >= count ||
        current_subtitle < -1 || current_subtitle >= count) return false;
    for (int32_t i = 0; i < count; ++i) {
        const rubraview_mhost_track_t *t = &w[i];
        if (t->kind < RUBRAVIEW_TRACK_VIDEO || t->kind > RUBRAVIEW_TRACK_SUBTITLE) return false;
        if (t->stream_index < 0 || t->stream_index > 65535 || t->channels < 0 || t->channels > 64) return false;
        if (t->is_default > 1 || t->is_forced > 1) return false;
        rubraview_mhost_text_take(text[i][0], RUBRAVIEW_MHOST_TEXT, t->language, sizeof t->language);
        rubraview_mhost_text_take(text[i][1], RUBRAVIEW_MHOST_TEXT, t->title, sizeof t->title);
        rubraview_mhost_text_take(text[i][2], RUBRAVIEW_MHOST_TEXT, t->codec, sizeof t->codec);
        out->tracks[i] = (rubraview_track_t){
            .kind = (rubraview_track_kind_t)t->kind, .stream_index = t->stream_index, .channels = t->channels,
            .is_default = t->is_default, .is_forced = t->is_forced, .is_external = false,
            .language = { (const proven_byte_t *)text[i][0], strlen(text[i][0]) },
            .title    = { (const proven_byte_t *)text[i][1], strlen(text[i][1]) },
            .codec    = { (const proven_byte_t *)text[i][2], strlen(text[i][2]) },
        };
    }
    out->count = (size_t)count;
    out->current_video = current_video;
    out->current_audio = current_audio;
    out->current_subtitle = current_subtitle;
    return true;
}

void rubraview_mhost_details_put(rubraview_mhost_block_t *b, const rubraview_details_t *details) {
    if (!b) return;
    memset(b->details, 0, sizeof b->details);
    b->detail_count = 0;
    if (!details) return;
    size_t n = details->count < RUBRAVIEW_DETAIL_MAX ? details->count : RUBRAVIEW_DETAIL_MAX;
    for (size_t i = 0; i < n; ++i) {
        b->details[i].heading = details->lines[i].heading;
        rubraview_mhost_text_put(b->details[i].label, sizeof b->details[i].label, details->lines[i].label);
        rubraview_mhost_text_put(b->details[i].value, sizeof b->details[i].value, details->lines[i].value);
    }
    b->detail_count = (uint32_t)n;
}

bool rubraview_mhost_details_take(const rubraview_mhost_detail_t *w, uint32_t count, rubraview_details_t *out) {
    if (!w || !out) return false;
    out->count = 0;
    if (count > RUBRAVIEW_DETAIL_MAX) return false;
    for (uint32_t i = 0; i < count; ++i) {
        if (w[i].heading > 1) return false;
        out->lines[i].heading = w[i].heading;
        rubraview_mhost_text_take(out->lines[i].label, sizeof out->lines[i].label, w[i].label, sizeof w[i].label);
        rubraview_mhost_text_take(out->lines[i].value, sizeof out->lines[i].value, w[i].value, sizeof w[i].value);
    }
    out->count = count;
    return true;
}

bool rubraview_mhost_opened_take(const rubraview_mhost_block_t *b, rubraview_mhost_opened_t *out) {
    if (!b || !out) return false;
    memset(out, 0, sizeof(*out));
    /* copied out of the block first: what is checked is what is used */
    int32_t failure = b->open_failure;
    int32_t count = b->track_count, video = b->current_video, audio = b->current_audio, subtitle = b->current_subtitle;
    uint32_t detail_count = b->detail_count;
    uint64_t frame_bytes = b->frame_bytes;
    static _Thread_local rubraview_mhost_info_t info;
    static _Thread_local rubraview_mhost_track_t tracks[RUBRAVIEW_MAX_TRACKS];
    static _Thread_local rubraview_mhost_detail_t details[RUBRAVIEW_DETAIL_MAX];
    char map[RUBRAVIEW_MHOST_NAME];
    memcpy(&info, &b->info, sizeof info);
    memcpy(tracks, b->tracks, sizeof tracks);
    memcpy(details, b->details, sizeof details);
    memcpy(map, b->frame_map, sizeof map);

    if (failure < RUBRAVIEW_MEDIA_OPENED || failure > RUBRAVIEW_MEDIA_FAIL_STATION_NEEDS_FFMPEG) { out->why = "its failure"; return false; }
    out->failure = (rubraview_media_failure_t)failure;
    if (failure != RUBRAVIEW_MEDIA_OPENED) { out->why = "not opened"; return false; }
    if (!rubraview_mhost_info_take(&info, &out->info)) { out->why = "what it said of the file"; return false; }
    if (!rubraview_mhost_tracks_take(tracks, count, video, audio, subtitle, &out->tracks, out->text)) { out->why = "its tracks"; return false; }
    if (!rubraview_mhost_details_take(details, detail_count, &out->details)) { out->why = "its details"; return false; }
    if (out->info.has_video) {
        if (frame_bytes != (uint64_t)out->info.width * (uint64_t)out->info.height * 4u) { out->why = "its pictures' size"; return false; }
        if (!rubraview_mhost_map_name_ok(map, sizeof map)) { out->why = "its pictures' name"; return false; }
        memcpy(out->frame_map, map, sizeof map);
        out->frame_bytes = frame_bytes;
    }
    return true;
}

/* ---- frames ---- */

bool rubraview_mhost_slot_take(const rubraview_mhost_slot_t *w, rubraview_mhost_slot_t *out) {
    if (!w || !out) return false;
    rubraview_mhost_slot_t s;
    memcpy(&s, w, sizeof s);
    if (s.on_gpu > 1 || !isfinite(s.pts) || !isfinite(s.duration) || s.duration < 0.0 || s.duration > 3600.0) return false;
    if (s.pts < -86400.0 || s.pts > 1e9) return false;
    if (s.on_gpu && s.shared_handle == 0) return false;
    *out = s;
    return true;
}

int32_t rubraview_mhost_ring_waiting(size_t head, size_t tail) {
    size_t n = head - tail;   /* the totals only grow; unsigned difference */
    return n <= RUBRAVIEW_MHOST_SLOTS ? (int32_t)n : -1;
}

/* ---- observed ---- */

void rubraview_mhost_observed_put(rubraview_mhost_observed_t *o, const rubraview_mhost_seen_t *in) {
    if (!o || !in) return;
    uint32_t seq = atomic_load_explicit(&o->seq, memory_order_relaxed);
    atomic_store_explicit(&o->seq, seq + 1, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    o->position_known = in->position_known;
    o->position = in->position;
    o->wall = in->wall;
    o->end_generation = in->end_generation;
    o->drained = in->drained;
    o->played_rate = in->played_rate;
    o->played_channels = in->played_channels;
    o->tone_peak = in->tone_peak;
    o->icy_serial = in->icy_serial;
    rubraview_mhost_text_put(o->icy_title, sizeof o->icy_title, in->icy_title);
    atomic_store_explicit(&o->seq, seq + 2, memory_order_release);
}

bool rubraview_mhost_observed_take(const rubraview_mhost_observed_t *o, rubraview_mhost_seen_t *out) {
    if (!o || !out) return false;
    rubraview_mhost_observed_t copy;
    bool steady = false;
    for (int tries = 0; tries < 8 && !steady; ++tries) {
        uint32_t before = atomic_load_explicit(&((rubraview_mhost_observed_t *)o)->seq, memory_order_acquire);
        if (before & 1u) continue;
        memcpy((char *)&copy + sizeof(copy.seq), (const char *)o + sizeof(copy.seq), sizeof copy - sizeof copy.seq);
        atomic_thread_fence(memory_order_acquire);
        uint32_t after = atomic_load_explicit(&((rubraview_mhost_observed_t *)o)->seq, memory_order_relaxed);
        steady = before == after;
    }
    if (!steady) return false;
    if (copy.position_known < 0 || copy.position_known > 1 || copy.drained < 0 || copy.drained > 1) return false;
    if (copy.position_known && (!isfinite(copy.position) || !isfinite(copy.wall) || copy.position < -1.0 || copy.position > 1e9))
        return false;
    if (!isfinite(copy.tone_peak) || copy.tone_peak < 0.0 || copy.tone_peak > 100000.0) return false;
    if (copy.played_rate > 768000 || copy.played_channels > 64) return false;
    *out = (rubraview_mhost_seen_t){
        .position_known = copy.position_known != 0, .position = copy.position, .wall = copy.wall,
        .end_generation = copy.end_generation, .drained = copy.drained != 0,
        .played_rate = copy.played_rate, .played_channels = copy.played_channels,
        .tone_peak = copy.tone_peak, .icy_serial = copy.icy_serial,
    };
    rubraview_mhost_text_take(out->icy_title, sizeof out->icy_title, copy.icy_title, sizeof copy.icy_title);
    return true;
}

/* ---- sound ---- */

void rubraview_mhost_audio_take(const rubraview_mhost_audio_t *w, rubraview_audio_chain_config_t *chain, double *speed,
                                double *volume, bool *muted, uint32_t *latency_ms) {
    rubraview_mhost_audio_t a;
    memcpy(&a, w, sizeof a);
    *speed = a.speed_permille >= 250 && a.speed_permille <= 4000 ? a.speed_permille / 1000.0 : 1.0;
    *volume = a.volume_permille <= 1000 ? a.volume_permille / 1000.0 : 1.0;
    *muted = a.muted != 0;
    *latency_ms = a.latency_ms >= 20 && a.latency_ms <= 100 ? a.latency_ms : 40;
    *chain = (rubraview_audio_chain_config_t){ .eq_preset = RUBRAVIEW_EQ_FLAT, .gain = 1.0 };
    if (a.eq_preset >= 0 && a.eq_preset < RUBRAVIEW_EQ_PRESET_COUNT) chain->eq_preset = (rubraview_eq_preset_t)a.eq_preset;
    chain->eq_custom = a.eq_custom != 0;
    for (int i = 0; i < RUBRAVIEW_EQ_BANDS; ++i)
        chain->eq_gains_db[i] = isfinite(a.eq_gains_db[i]) && fabsf(a.eq_gains_db[i]) <= 24.0f ? a.eq_gains_db[i] : 0.0f;
    chain->night_mode = a.night_mode != 0;
    chain->gain = isfinite(a.gain) && a.gain >= 0.0 && a.gain <= 16.0 ? a.gain : 1.0;
}

void rubraview_mhost_tap_put(rubraview_mhost_tap_t *t, const float *samples, size_t count) {
    if (!t) return;
    if (count > RUBRAVIEW_MHOST_TAP) count = RUBRAVIEW_MHOST_TAP;
    uint32_t seq = atomic_load_explicit(&t->seq, memory_order_relaxed);
    atomic_store_explicit(&t->seq, seq + 1, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    if (count && samples) memcpy(t->samples, samples, count * sizeof(float));
    t->count = (uint32_t)count;
    atomic_store_explicit(&t->seq, seq + 2, memory_order_release);
}

size_t rubraview_mhost_tap_take(const rubraview_mhost_tap_t *t, float *out, size_t count) {
    if (!t || !out || count == 0) return 0;
    for (int tries = 0; tries < 4; ++tries) {
        uint32_t before = atomic_load_explicit(&((rubraview_mhost_tap_t *)t)->seq, memory_order_acquire);
        if (before & 1u) continue;
        uint32_t n = t->count;
        if (n > RUBRAVIEW_MHOST_TAP) return 0;
        size_t take = n < count ? n : count;
        memcpy(out, t->samples + (n - take), take * sizeof(float));   /* the latest */
        atomic_thread_fence(memory_order_acquire);
        if (atomic_load_explicit(&((rubraview_mhost_tap_t *)t)->seq, memory_order_relaxed) != before) continue;
        for (size_t i = 0; i < take; ++i) out[i] = isfinite(out[i]) ? (out[i] > 4.0f ? 4.0f : out[i] < -4.0f ? -4.0f : out[i]) : 0.0f;
        return take;
    }
    return 0;
}

/* ---- names and sizes ---- */

#define MAP_PREFIX "Local\\rubraview-mh-"

void rubraview_mhost_map_name(char out[RUBRAVIEW_MHOST_NAME], uint32_t pid, const uint8_t random16[16]) {
    int n = snprintf(out, RUBRAVIEW_MHOST_NAME, MAP_PREFIX "%u-", (unsigned)pid);
    for (int i = 0; i < 16 && n > 0 && n + 2 < RUBRAVIEW_MHOST_NAME; ++i) {
        static const char HEX[] = "0123456789abcdef";
        out[n++] = HEX[random16[i] >> 4];
        out[n++] = HEX[random16[i] & 15u];
    }
    if (n > 0 && n < RUBRAVIEW_MHOST_NAME) out[n] = '\0';
}

bool rubraview_mhost_map_name_ok(const char *name, size_t cap) {
    if (!name || cap == 0) return false;
    size_t len = 0;
    while (len < cap && name[len]) len++;
    if (len == cap) return false;   /* not ended inside its field */
    size_t p = sizeof(MAP_PREFIX) - 1;
    if (len <= p || memcmp(name, MAP_PREFIX, p) != 0) return false;
    size_t digits = 0;
    while (p < len && name[p] >= '0' && name[p] <= '9') { p++; digits++; }
    if (digits == 0 || digits > 10 || p >= len || name[p] != '-') return false;
    p++;
    size_t hex = 0;
    while (p < len && ((name[p] >= '0' && name[p] <= '9') || (name[p] >= 'a' && name[p] <= 'f'))) { p++; hex++; }
    return p == len && hex == 32;
}

bool rubraview_mhost_size_ok(uint64_t bytes, uint64_t limit) {
    return bytes > 0 && bytes <= limit && bytes <= RUBRAVIEW_MHOST_MAP_MAX;
}

/* ---- JPEG XL's pictures ---- */

size_t rubraview_mhost_frames_head(uint32_t count) {
    return 4u + (size_t)count * 24u;
}

static uint32_t get_u32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static int32_t get_i32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static uint64_t get_u64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }
static double get_f64(const uint8_t *p) { double v; memcpy(&v, p, 8); return v; }

uint32_t rubraview_mhost_frame_count(const uint8_t *payload, size_t size) {
    if (!payload || size < 4) return 0;
    uint32_t count = get_u32(payload);
    if (count > RUBRAVIEW_MHOST_FRAMES_MAX || rubraview_mhost_frames_head(count) > size) return 0;
    return count;
}

bool rubraview_mhost_frame_at(const uint8_t *payload, size_t size, uint32_t index, int32_t max_side,
                              rubraview_mhost_frame_ref_t *out) {
    uint32_t count = rubraview_mhost_frame_count(payload, size);
    if (!out || index >= count) return false;
    const uint8_t *e = payload + 4 + (size_t)index * 24u;
    rubraview_mhost_frame_ref_t f = { get_i32(e), get_i32(e + 4), get_f64(e + 8), get_u64(e + 16) };
    if (max_side <= 0 || max_side > RUBRAVIEW_MHOST_MAX_SIDE) max_side = RUBRAVIEW_MHOST_MAX_SIDE;
    if (f.width <= 0 || f.height <= 0 || f.width > max_side || f.height > max_side) return false;
    if (!isfinite(f.seconds) || f.seconds < 0.0 || f.seconds > 3600.0) return false;
    uint64_t bytes = (uint64_t)f.width * (uint64_t)f.height * 4u;
    if (f.offset < rubraview_mhost_frames_head(count) || f.offset > size || bytes > size - f.offset) return false;
    *out = f;
    return true;
}

/* ---- the filter catalog ---- */

#define CAT_MAGIC 0x43465652u   /* "RVFC" */
#define CAT_FILTERS_MAX 8192
#define CAT_STRING_MAX 8192

typedef struct packer { uint8_t *out; size_t cap, used; bool full; } packer_t;

static void pk_bytes(packer_t *p, const void *b, size_t n) {
    if (p->full || n > p->cap - p->used) { p->full = true; return; }
    if (p->out && n) memcpy(p->out + p->used, b, n);
    p->used += n;
}
static void pk_u32(packer_t *p, uint32_t v) { pk_bytes(p, &v, 4); }
static void pk_f64(packer_t *p, double v) { pk_bytes(p, &v, 8); }
static void pk_str(packer_t *p, proven_u8str_view_t s) {
    size_t n = s.size < CAT_STRING_MAX ? s.size : CAT_STRING_MAX;
    pk_u32(p, (uint32_t)n);
    pk_bytes(p, s.ptr, n);
}

size_t rubraview_mhost_catalog_pack(const rubraview_ffilter_catalog_t *catalog, uint8_t *out, size_t cap) {
    if (!catalog) return 0;
    packer_t p = { out, out ? cap : SIZE_MAX, 0, false };
    int32_t count = catalog->filter_count < CAT_FILTERS_MAX ? catalog->filter_count : CAT_FILTERS_MAX;
    if (count < 0) count = 0;
    pk_u32(&p, CAT_MAGIC);
    pk_u32(&p, (uint32_t)count);
    for (int32_t i = 0; i < count; ++i) {
        const rubraview_ffilter_t *f = &catalog->filters[i];
        pk_str(&p, f->name);
        pk_str(&p, f->description);
        int32_t options = f->option_count < RUBRAVIEW_FFILTER_MAX_SHOWN ? f->option_count : RUBRAVIEW_FFILTER_MAX_SHOWN;
        if (options < 0) options = 0;
        pk_u32(&p, (uint32_t)options);
        for (int32_t k = 0; k < options; ++k) {
            const rubraview_ffilter_option_t *o = &f->options[k];
            pk_u32(&p, (uint32_t)o->kind);
            pk_str(&p, o->name);
            pk_str(&p, o->help);
            pk_str(&p, o->text);
            pk_f64(&p, o->value);
            pk_f64(&p, o->min);
            pk_f64(&p, o->max);
            int32_t choices = o->choices && o->choice_count > 0 ? o->choice_count : 0;
            if (choices > RUBRAVIEW_FFILTER_MAX_CHOICES) choices = RUBRAVIEW_FFILTER_MAX_CHOICES;
            pk_u32(&p, (uint32_t)choices);
            for (int32_t c = 0; c < choices; ++c) pk_str(&p, o->choices[c]);
        }
    }
    return p.full ? 0 : p.used;
}

typedef struct reader { const uint8_t *data; size_t size, at; bool bad; } reader_t;

static uint32_t rd_u32(reader_t *r) {
    if (r->bad || r->size - r->at < 4) { r->bad = true; return 0; }
    uint32_t v = get_u32(r->data + r->at);
    r->at += 4;
    return v;
}
static double rd_f64(reader_t *r) {
    if (r->bad || r->size - r->at < 8) { r->bad = true; return 0.0; }
    double v = get_f64(r->data + r->at);
    r->at += 8;
    if (!isfinite(v)) { r->bad = true; return 0.0; }
    return v;
}
static proven_u8str_view_t rd_str(reader_t *r, proven_arena_t *arena) {
    uint32_t n = rd_u32(r);
    if (r->bad || n > CAT_STRING_MAX || n > r->size - r->at) { r->bad = true; return (proven_u8str_view_t){ 0 }; }
    proven_result_mem_mut_t res = proven_arena_alloc(arena, (size_t)n + 1);
    if (!proven_is_ok(res.err)) { r->bad = true; return (proven_u8str_view_t){ 0 }; }
    char *s = (char *)res.value.ptr;
    /* the same safety as a fixed field: valid UTF-8, no control bytes */
    char raw[CAT_STRING_MAX + 1];
    memcpy(raw, r->data + r->at, n);
    raw[n] = '\0';
    r->at += n;
    rubraview_mhost_text_take(s, (size_t)n + 1, raw, n);
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)s, .size = strlen(s) };
}

bool rubraview_mhost_catalog_unpack(proven_arena_t *arena, const uint8_t *data, size_t size,
                                    rubraview_ffilter_catalog_t *out) {
    if (!arena || !data || !out) return false;
    *out = (rubraview_ffilter_catalog_t){ 0 };
    reader_t r = { data, size, 0, false };
    if (rd_u32(&r) != CAT_MAGIC) return false;
    uint32_t count = rd_u32(&r);
    if (r.bad || count > CAT_FILTERS_MAX || count > (size - r.at) / 16u) return false;
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, count ? count : 1, sizeof(rubraview_ffilter_t));
    if (!proven_is_ok(res.err)) return false;
    rubraview_ffilter_t *filters = (rubraview_ffilter_t *)(void *)res.value.ptr;
    for (uint32_t i = 0; i < count && !r.bad; ++i) {
        rubraview_ffilter_t *f = &filters[i];
        *f = (rubraview_ffilter_t){ 0 };
        f->name = rd_str(&r, arena);
        f->description = rd_str(&r, arena);
        uint32_t options = rd_u32(&r);
        if (r.bad || options > RUBRAVIEW_FFILTER_MAX_SHOWN) { r.bad = true; break; }
        if (options) {
            res = rubraview_arena_alloc_array(arena, options, sizeof(rubraview_ffilter_option_t));
            if (!proven_is_ok(res.err)) { r.bad = true; break; }
            f->options = (rubraview_ffilter_option_t *)(void *)res.value.ptr;
        }
        for (uint32_t k = 0; k < options && !r.bad; ++k) {
            rubraview_ffilter_option_t *o = &f->options[k];
            *o = (rubraview_ffilter_option_t){ 0 };
            uint32_t kind = rd_u32(&r);
            if (kind > RUBRAVIEW_FFILTER_TEXT) { r.bad = true; break; }
            o->kind = (rubraview_ffilter_kind_t)kind;
            o->name = rd_str(&r, arena);
            o->help = rd_str(&r, arena);
            o->text = rd_str(&r, arena);
            o->value = rd_f64(&r);
            o->min = rd_f64(&r);
            o->max = rd_f64(&r);
            uint32_t choices = rd_u32(&r);
            if (r.bad || choices > RUBRAVIEW_FFILTER_MAX_CHOICES) { r.bad = true; break; }
            if (o->kind == RUBRAVIEW_FFILTER_CHOICE && choices == 0) { r.bad = true; break; }
            if (choices) {
                res = rubraview_arena_alloc_array(arena, choices, sizeof(proven_u8str_view_t));
                if (!proven_is_ok(res.err)) { r.bad = true; break; }
                o->choices = (proven_u8str_view_t *)(void *)res.value.ptr;
                for (uint32_t c = 0; c < choices; ++c) o->choices[c] = rd_str(&r, arena);
                o->choice_count = (int32_t)choices;
                if (o->kind == RUBRAVIEW_FFILTER_CHOICE && (o->value < 0.0 || o->value >= (double)choices)) r.bad = true;
            }
            if (o->min > o->max) r.bad = true;
            if (o->kind == RUBRAVIEW_FFILTER_INT || o->kind == RUBRAVIEW_FFILTER_FLOAT || o->kind == RUBRAVIEW_FFILTER_BOOL)
                o->value = o->value < o->min ? o->min : o->value > o->max ? o->max : o->value;   /* a slider's place */
        }
        f->option_count = (int32_t)options;
    }
    if (r.bad || r.at != size) return false;
    out->filters = filters;
    out->filter_count = (int32_t)count;
    return true;
}
