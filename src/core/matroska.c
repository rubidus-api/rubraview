#include "rubraview/matroska.h"
#include "rubraview/path.h"

#include <stdio.h>
#include <string.h>

/* Element IDs (RFC 9559), with their length marker as they stand in the file. */
enum {
    ID_EBML = 0x1A45DFA3u, ID_DOCTYPE = 0x4282u,
    ID_SEGMENT = 0x18538067u,
    ID_SEEKHEAD = 0x114D9B74u, ID_INFO = 0x1549A966u, ID_TRACKS = 0x1654AE6Bu, ID_CLUSTER = 0x1F43B675u,
    ID_CUES = 0x1C53BB6Bu, ID_ATTACHMENTS = 0x1941A469u, ID_CHAPTERS = 0x1043A770u, ID_TAGS = 0x1254C367u,
    ID_TIMESTAMP_SCALE = 0x2AD7B1u,
    ID_TRACK_ENTRY = 0xAEu, ID_TRACK_NUMBER = 0xD7u, ID_TRACK_TYPE = 0x83u, ID_CODEC_ID = 0x86u,
    ID_CODEC_PRIVATE = 0x63A2u, ID_LANGUAGE = 0x22B59Cu, ID_LANGUAGE_BCP47 = 0x22B59Du, ID_NAME = 0x536Eu,
    ID_CONTENT_ENCODINGS = 0x6D80u,
    ID_TIMESTAMP = 0xE7u, ID_SIMPLE_BLOCK = 0xA3u, ID_BLOCK_GROUP = 0xA0u, ID_BLOCK = 0xA1u, ID_BLOCK_DURATION = 0x9Bu,
};

typedef struct elem {
    uint32_t id;
    uint64_t body;      /* where its content begins */
    uint64_t size;      /* of the content; what is left of `limit` when unknown or cut short */
    uint64_t next;      /* where the element after it begins */
    bool     unknown;   /* the size was not written (all ones) */
} elem_t;

/* An element's header at `at`, inside [at, limit). False when there is no whole header there. */
static bool elem_at(const rubraview_mkv_t *mkv, uint64_t at, uint64_t limit, elem_t *out) {
    if (limit > mkv->size) limit = mkv->size;
    if (at >= limit) return false;
    const uint8_t *p = mkv->data;

    /* The ID: one to four bytes, the leading zeros of the first say how many; the marker stays. */
    uint8_t first = p[at];
    unsigned id_len = first & 0x80 ? 1 : first & 0x40 ? 2 : first & 0x20 ? 3 : first & 0x10 ? 4 : 0;
    if (id_len == 0 || limit - at < id_len) return false;
    uint32_t id = 0;
    for (unsigned i = 0; i < id_len; ++i) id = (id << 8) | p[at + i];
    uint64_t pos = at + id_len;
    if (pos >= limit) return false;

    /* The size: one to eight bytes, the marker taken off; all ones is "not known". */
    uint8_t lead = p[pos];
    unsigned size_len = 0;
    for (unsigned bit = 0; bit < 8; ++bit) {
        if (lead & (0x80u >> bit)) { size_len = bit + 1; break; }
    }
    if (size_len == 0 || limit - pos < size_len) return false;
    uint64_t size = lead & (0xFFu >> size_len);
    bool ones = size == (0xFFu >> size_len);
    for (unsigned i = 1; i < size_len; ++i) {
        size = (size << 8) | p[pos + i];
        if (p[pos + i] != 0xFF) ones = false;
    }
    uint64_t body = pos + size_len;

    out->id = id;
    out->body = body;
    out->unknown = ones;
    uint64_t next = 0;
    if (ones || PROVEN_CKD_ADD(&next, body, size) || next > limit) {
        /* Not known, or more than there is: it has what is left. */
        out->size = limit - body;
        out->next = limit;
    } else {
        out->size = size;
        out->next = next;
    }
    return true;
}

static uint64_t read_uint(const rubraview_mkv_t *mkv, const elem_t *e) {
    uint64_t value = 0;
    uint64_t n = e->size < 8 ? e->size : 8;
    for (uint64_t i = 0; i < n; ++i) value = (value << 8) | mkv->data[e->body + i];
    return value;
}

/* A string element: as long as it says, less the zeros it may be padded with. */
static u8str_t read_text(const rubraview_mkv_t *mkv, const elem_t *e) {
    size_t len = e->size > 4096 ? 4096 : (size_t)e->size;
    const char *ptr = (const char*)mkv->data + e->body;
    while (len > 0 && ptr[len - 1] == '\0') len--;
    return (u8str_t){ .ptr = ptr, .len = len };
}

static bool is_top_level(uint32_t id) {
    return id == ID_CLUSTER || id == ID_SEEKHEAD || id == ID_INFO || id == ID_TRACKS || id == ID_CUES ||
           id == ID_ATTACHMENTS || id == ID_CHAPTERS || id == ID_TAGS || id == ID_SEGMENT || id == ID_EBML;
}

static bool text_is(u8str_t s, const char *lit) {
    return rubraview_u8_eq(s, (u8str_t){ .ptr = lit, .len = strlen(lit) });
}

static rubraview_mkv_sub_t classify(const rubraview_mkv_track_t *t) {
    if (t->type != 0x11) return RUBRAVIEW_MKV_SUB_NONE;
    if (text_is(t->codec, "S_TEXT/UTF8")) return RUBRAVIEW_MKV_SUB_SRT;
    if (text_is(t->codec, "S_TEXT/ASS") || text_is(t->codec, "S_TEXT/SSA") ||
        text_is(t->codec, "S_ASS") || text_is(t->codec, "S_SSA")) return RUBRAVIEW_MKV_SUB_ASS;
    if (text_is(t->codec, "S_VOBSUB") || text_is(t->codec, "S_HDMV/PGS") || text_is(t->codec, "S_DVBSUB") ||
        text_is(t->codec, "S_IMAGE/BMP")) return RUBRAVIEW_MKV_SUB_PICTURE;
    return RUBRAVIEW_MKV_SUB_OTHER;
}

static void read_track(rubraview_mkv_t *mkv, const elem_t *entry) {
    if (mkv->track_count >= RUBRAVIEW_MKV_MAX_TRACKS) return;
    rubraview_mkv_track_t t = { .language = { .ptr = "", .len = 0 }, .name = { .ptr = "", .len = 0 },
                                .codec = { .ptr = "", .len = 0 } };
    bool bcp47 = false;
    uint64_t at = entry->body;
    elem_t e;
    while (at < entry->next && elem_at(mkv, at, entry->next, &e)) {
        switch (e.id) {
            case ID_TRACK_NUMBER: t.number = read_uint(mkv, &e); break;
            case ID_TRACK_TYPE: t.type = (uint8_t)read_uint(mkv, &e); break;
            case ID_CODEC_ID: t.codec = read_text(mkv, &e); break;
            case ID_NAME: t.name = read_text(mkv, &e); break;
            case ID_LANGUAGE: if (!bcp47) t.language = read_text(mkv, &e); break;
            case ID_LANGUAGE_BCP47: t.language = read_text(mkv, &e); bcp47 = true; break;
            case ID_CODEC_PRIVATE:
                t.private_data = mkv->data + e.body;
                t.private_size = e.size > SIZE_MAX ? SIZE_MAX : (size_t)e.size;
                break;
            case ID_CONTENT_ENCODINGS: t.encoded = true; break;
            default: break;
        }
        if (e.next <= at) break;
        at = e.next;
    }
    if (t.number == 0) return;                       /* a track no block can name */
    if (t.language.len == 0) t.language = (u8str_t){ .ptr = "eng", .len = 3 };   /* the format's own default */
    t.subtitle = classify(&t);
    mkv->tracks[mkv->track_count++] = t;
}

bool rubraview_mkv_open(rubraview_mkv_t *mkv, const uint8_t *data, uint64_t size, const char **out_why) {
    const char *sink = NULL;
    if (!out_why) out_why = &sink;
    *out_why = NULL;
    if (!mkv || !data || size < 8) { *out_why = "not a Matroska file"; return false; }
    *mkv = (rubraview_mkv_t){ .data = data, .size = size, .timestamp_scale = 1000000u };

    elem_t head;
    if (!elem_at(mkv, 0, size, &head) || head.id != ID_EBML || head.unknown) { *out_why = "not a Matroska file"; return false; }
    /* The DocType, when it is given, is one of the two this is. */
    elem_t e;
    for (uint64_t at = head.body; at < head.next && elem_at(mkv, at, head.next, &e); at = e.next) {
        if (e.id == ID_DOCTYPE && !text_is(read_text(mkv, &e), "matroska") && !text_is(read_text(mkv, &e), "webm")) {
            *out_why = "an EBML file, but not Matroska";
            return false;
        }
        if (e.next <= at) break;
    }

    /* The segment: the first one after the head. */
    uint64_t at = head.next;
    elem_t segment = {0};
    bool found = false;
    while (at < size && elem_at(mkv, at, size, &e)) {
        if (e.id == ID_SEGMENT) { segment = e; found = true; break; }
        if (e.unknown || e.next <= at) break;
        at = e.next;
    }
    if (!found) { *out_why = "the Matroska file has no segment"; return false; }
    mkv->body_at = segment.body;
    mkv->body_end = segment.next;

    /* Its children, a header at a time, until the tracks are known: they
       stand before the first cluster in nearly every file, and after the
       clusters in a few. */
    bool have_tracks = false, have_info = false;
    at = mkv->body_at;
    while (at < mkv->body_end && elem_at(mkv, at, mkv->body_end, &e)) {
        if (e.id == ID_INFO && !e.unknown) {
            elem_t in;
            for (uint64_t p = e.body; p < e.next && elem_at(mkv, p, e.next, &in); p = in.next) {
                if (in.id == ID_TIMESTAMP_SCALE) {
                    uint64_t scale = read_uint(mkv, &in);
                    if (scale > 0) mkv->timestamp_scale = scale;
                }
                if (in.next <= p) break;
            }
            have_info = true;
        } else if (e.id == ID_TRACKS && !e.unknown) {
            elem_t entry;
            for (uint64_t p = e.body; p < e.next && elem_at(mkv, p, e.next, &entry); p = entry.next) {
                if (entry.id == ID_TRACK_ENTRY && !entry.unknown) read_track(mkv, &entry);
                if (entry.next <= p) break;
            }
            have_tracks = true;
        } else if (e.id == ID_CLUSTER && have_tracks) {
            break;                                   /* the head is behind us */
        }
        if (have_tracks && have_info) break;
        if (e.unknown || e.next <= at) break;        /* an open cluster before any tracks: the walk would find its end, the head does not look */
        at = e.next;
    }
    if (!have_tracks) { *out_why = "the Matroska file's tracks could not be read"; return false; }
    return true;
}

int32_t rubraview_mkv_track_index(const rubraview_mkv_t *mkv, uint64_t number) {
    if (!mkv) return -1;
    for (size_t i = 0; i < mkv->track_count; ++i) {
        if (mkv->tracks[i].number == number) return (int32_t)i;
    }
    return -1;
}

void rubraview_mkv_walk_begin(const rubraview_mkv_t *mkv, rubraview_mkv_walk_t *walk) {
    if (!walk) return;
    *walk = (rubraview_mkv_walk_t){ .at = mkv ? mkv->body_at : 0 };
}

/* A block's own header: the track (a size-like number), sixteen bits of
   time beside the cluster's, a byte of flags; the rest is the frame. */
static bool read_block(const rubraview_mkv_t *mkv, uint64_t body, uint64_t size, uint64_t cluster_time, int64_t duration_ticks,
                       rubraview_mkv_block_t *out) {
    if (size < 4) return false;
    const uint8_t *p = mkv->data + body;
    unsigned len = 0;
    for (unsigned bit = 0; bit < 8; ++bit) {
        if (p[0] & (0x80u >> bit)) { len = bit + 1; break; }
    }
    if (len == 0 || size < (uint64_t)len + 3) return false;
    uint64_t track = p[0] & (0xFFu >> len);
    for (unsigned i = 1; i < len; ++i) track = (track << 8) | p[i];
    int16_t rel = (int16_t)(uint16_t)(((uint16_t)p[len] << 8) | p[len + 1]);
    uint8_t flags = p[len + 2];

    /* Ticks to nanoseconds; a clock no film has is refused, not wrapped. */
    int64_t ticks = 0, scale = 0, start = 0, duration = -1;
    if (cluster_time > (uint64_t)INT64_MAX || mkv->timestamp_scale > (uint64_t)INT64_MAX) return false;
    scale = (int64_t)mkv->timestamp_scale;
    if (PROVEN_CKD_ADD(&ticks, (int64_t)cluster_time, (int64_t)rel)) return false;
    if (PROVEN_CKD_MUL(&start, ticks, scale)) return false;
    if (duration_ticks >= 0 && PROVEN_CKD_MUL(&duration, duration_ticks, scale)) duration = -1;

    uint64_t payload = size - len - 3;
    out->track = track;
    out->start_ns = start;
    out->duration_ns = duration_ticks >= 0 ? duration : -1;
    out->payload = p + len + 3;
    out->size = payload > SIZE_MAX ? SIZE_MAX : (size_t)payload;
    out->laced = (flags & 0x06) != 0;
    return true;
}

bool rubraview_mkv_walk_next(const rubraview_mkv_t *mkv, rubraview_mkv_walk_t *walk, rubraview_mkv_block_t *out) {
    if (!mkv || !walk || !out) return false;
    elem_t e;
    for (;;) {
        if (!walk->in_cluster) {
            if (walk->at >= mkv->body_end || !elem_at(mkv, walk->at, mkv->body_end, &e)) return false;
            if (e.id == ID_CLUSTER) {
                walk->in_cluster = true;
                walk->cluster_open = e.unknown;
                walk->cluster_end = e.next;
                walk->cluster_time = 0;
                walk->at = e.body;
                continue;
            }
            if (e.unknown || e.next <= walk->at) return false;   /* nothing says where it ends */
            walk->at = e.next;
            continue;
        }
        if (walk->at >= walk->cluster_end) { walk->in_cluster = false; continue; }
        if (!elem_at(mkv, walk->at, walk->cluster_end, &e)) return false;
        if (walk->cluster_open && is_top_level(e.id)) { walk->in_cluster = false; continue; }
        if (e.unknown || e.next <= walk->at) return false;
        walk->at = e.next;
        if (e.id == ID_TIMESTAMP) {
            walk->cluster_time = read_uint(mkv, &e);
        } else if (e.id == ID_SIMPLE_BLOCK) {
            if (read_block(mkv, e.body, e.size, walk->cluster_time, -1, out)) return true;
        } else if (e.id == ID_BLOCK_GROUP) {
            elem_t in, block = {0};
            bool have_block = false;
            int64_t duration = -1;
            for (uint64_t p = e.body; p < e.next && elem_at(mkv, p, e.next, &in); p = in.next) {
                if (in.id == ID_BLOCK && !in.unknown) { block = in; have_block = true; }
                else if (in.id == ID_BLOCK_DURATION) {
                    uint64_t ticks = read_uint(mkv, &in);
                    duration = ticks > (uint64_t)INT64_MAX ? -1 : (int64_t)ticks;
                }
                if (in.unknown || in.next <= p) break;
            }
            if (have_block && read_block(mkv, block.body, block.size, walk->cluster_time, duration, out)) return true;
        }
    }
}

double rubraview_mkv_walk_share(const rubraview_mkv_t *mkv, const rubraview_mkv_walk_t *walk) {
    if (!mkv || !walk || mkv->body_end <= mkv->body_at || walk->at <= mkv->body_at) return 0.0;
    if (walk->at >= mkv->body_end) return 1.0;
    return (double)(walk->at - mkv->body_at) / (double)(mkv->body_end - mkv->body_at);
}

/* ---- a subtitle track written out ---- */

/* Room that counts what it is asked to hold, and holds what fits. */
typedef struct sink { char *out; size_t cap, need; } sink_t;

static void put(sink_t *s, const char *bytes, size_t len) {
    if (s->out && s->need <= s->cap && len <= s->cap - s->need) memcpy(s->out + s->need, bytes, len);
    if (PROVEN_CKD_ADD(&s->need, s->need, len)) s->need = SIZE_MAX;
}

static void put_text(sink_t *s, const char *text) { put(s, text, strlen(text)); }

static bool has_events(const uint8_t *data, size_t size) {
    static const char mark[] = "[Events]";
    for (size_t i = 0; i + sizeof(mark) - 1 <= size; ++i) {
        if (memcmp(data + i, mark, sizeof(mark) - 1) == 0) return true;
    }
    return false;
}

size_t rubraview_mkv_subtitle_head(const rubraview_mkv_track_t *track, char *out, size_t cap) {
    if (!track || track->subtitle != RUBRAVIEW_MKV_SUB_ASS) return 0;
    sink_t s = { .out = out, .cap = cap };
    size_t size = track->private_size;
    while (size > 0 && track->private_data[size - 1] == '\0') size--;
    if (size > 0) {
        put(&s, (const char*)track->private_data, size);
        if (track->private_data[size - 1] != '\n') put_text(&s, "\n");
    } else {
        put_text(&s, "[Script Info]\nScriptType: v4.00+\n\n");
    }
    if (size == 0 || !has_events(track->private_data, size)) {
        put_text(&s, "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n");
    }
    return s.need;
}

static void put_clock(sink_t *s, int64_t ns, bool substation) {
    char buf[48];
    if (ns < 0) ns = 0;
    int n;
    if (substation) {
        int64_t cs = (ns + 5000000) / 10000000;
        n = snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld.%02lld", (long long)(cs / 360000), (long long)(cs / 6000 % 60),
                     (long long)(cs / 100 % 60), (long long)(cs % 100));
    } else {
        int64_t ms = (ns + 500000) / 1000000;
        n = snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld,%03lld", (long long)(ms / 3600000), (long long)(ms / 60000 % 60),
                     (long long)(ms / 1000 % 60), (long long)(ms % 1000));
    }
    if (n > 0) put(s, buf, (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1);
}

size_t rubraview_mkv_subtitle_cue(const rubraview_mkv_track_t *track, unsigned counter, const rubraview_mkv_block_t *block,
                                  char *out, size_t cap) {
    if (!track || !block || block->laced || !block->payload) return 0;
    if (track->subtitle != RUBRAVIEW_MKV_SUB_SRT && track->subtitle != RUBRAVIEW_MKV_SUB_ASS) return 0;
    const char *text = (const char*)block->payload;
    size_t len = block->size;
    while (len > 0 && (text[len - 1] == '\0' || text[len - 1] == '\n' || text[len - 1] == '\r' || text[len - 1] == ' ')) len--;
    if (len == 0) return 0;

    int64_t end = 0;
    if (PROVEN_CKD_ADD(&end, block->start_ns, block->duration_ns >= 0 ? block->duration_ns : (int64_t)3000000000)) end = block->start_ns;
    sink_t s = { .out = out, .cap = cap };

    if (track->subtitle == RUBRAVIEW_MKV_SUB_ASS) {
        /* ReadOrder,Layer,rest: the first is dropped, the times go in after the second. */
        size_t first = 0;
        while (first < len && text[first] != ',') first++;
        size_t second = first < len ? first + 1 : len;
        while (second < len && text[second] != ',') second++;
        if (second >= len) return 0;                 /* not an event as the format carries one */
        put_text(&s, "Dialogue: ");
        put(&s, text + first + 1, second - first - 1);
        put_text(&s, ",");
        put_clock(&s, block->start_ns, true);
        put_text(&s, ",");
        put_clock(&s, end, true);
        put(&s, text + second, len - second);
        put_text(&s, "\n");
        return s.need;
    }

    char number[24];
    int n = snprintf(number, sizeof(number), "%u\n", counter);
    if (n > 0) put(&s, number, (size_t)n);
    put_clock(&s, block->start_ns, false);
    put_text(&s, " --> ");
    put_clock(&s, end, false);
    put_text(&s, "\n");
    /* The words, a line end as one '\n' and no empty line among them: one would end the cue. */
    bool line_start = true;
    for (size_t i = 0; i < len; ++i) {
        char c = text[i];
        if (c == '\r') continue;
        if (c == '\n') {
            if (!line_start) { put(&s, "\n", 1); line_start = true; }
            continue;
        }
        put(&s, &c, 1);
        line_start = false;
    }
    put_text(&s, line_start ? "\n" : "\n\n");
    return s.need;
}

const char *rubraview_mkv_subtitle_ext(const rubraview_mkv_track_t *track) {
    if (!track) return "";
    if (track->subtitle == RUBRAVIEW_MKV_SUB_SRT) return ".srt";
    if (track->subtitle == RUBRAVIEW_MKV_SUB_ASS) return ".ass";
    return "";
}

size_t rubraview_mkv_subtitle_name(const rubraview_mkv_track_t *track, u8str_t film_path, unsigned ordinal, char *out, size_t cap) {
    const char *ext = rubraview_mkv_subtitle_ext(track);
    if (!ext[0] || !out || cap == 0 || film_path.len == 0) return 0;
    size_t stem = film_path.len - rubraview_path_ext(film_path).len;
    char language[20];
    size_t n = 0;
    for (size_t i = 0; i < track->language.len && n + 1 < sizeof(language); ++i) {
        char c = track->language.ptr[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-') language[n++] = c;
    }
    language[n] = '\0';
    if (strcmp(language, "und") == 0) language[0] = '\0';
    char tail[64];
    int t = ordinal > 0 ? snprintf(tail, sizeof(tail), "%s%s.%u%s", language[0] ? "." : "", language, ordinal + 1, ext)
                        : snprintf(tail, sizeof(tail), "%s%s%s", language[0] ? "." : "", language, ext);
    if (t <= 0 || (size_t)t >= sizeof(tail)) return 0;
    size_t total = 0;
    if (PROVEN_CKD_ADD(&total, stem, (size_t)t) || total >= cap) return 0;
    memcpy(out, film_path.ptr, stem);
    memcpy(out + stem, tail, (size_t)t);
    out[total] = '\0';
    return total;
}
