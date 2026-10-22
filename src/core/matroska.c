#include "rubraview/matroska.h"
#include "rubraview/lang.h"
#include "rubraview/number.h"
#include "rubraview/path.h"

#include <stdio.h>
#include <stdlib.h>
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
static proven_u8str_view_t read_text(const rubraview_mkv_t *mkv, const elem_t *e) {
    size_t len = e->size > 4096 ? 4096 : (size_t)e->size;
    const char *ptr = (const char*)mkv->data + e->body;
    while (len > 0 && ptr[len - 1] == '\0') len--;
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)ptr, .size = len };
}

static bool is_top_level(uint32_t id) {
    return id == ID_CLUSTER || id == ID_SEEKHEAD || id == ID_INFO || id == ID_TRACKS || id == ID_CUES ||
           id == ID_ATTACHMENTS || id == ID_CHAPTERS || id == ID_TAGS || id == ID_SEGMENT || id == ID_EBML;
}

static bool text_is(proven_u8str_view_t s, const char *lit) {
    return proven_u8str_view_eq(s, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)lit, .size = strlen(lit) });
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
    rubraview_mkv_track_t t = { .language = PROVEN_LIT(""), .name = PROVEN_LIT(""),
                                .codec = PROVEN_LIT("") };
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
    if (t.language.size == 0) t.language = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)"eng", .size = 3 };   /* the format's own default */
    t.subtitle = classify(&t);
    mkv->tracks[mkv->track_count++] = t;
}

bool rubraview_mkv_open(rubraview_mkv_t *mkv, const uint8_t *data, uint64_t size, const char **out_why) {
    const char *sink = NULL;
    if (!out_why) out_why = &sink;
    *out_why = NULL;
    if (!mkv || !data || size < 8) { *out_why = N_("not a Matroska file"); return false; }
    *mkv = (rubraview_mkv_t){ .data = data, .size = size, .timestamp_scale = 1000000u };

    elem_t head;
    if (!elem_at(mkv, 0, size, &head) || head.id != ID_EBML || head.unknown) { *out_why = N_("not a Matroska file"); return false; }
    /* The DocType, when it is given, is one of the two this is. */
    elem_t e;
    for (uint64_t at = head.body; at < head.next && elem_at(mkv, at, head.next, &e); at = e.next) {
        if (e.id == ID_DOCTYPE && !text_is(read_text(mkv, &e), "matroska") && !text_is(read_text(mkv, &e), "webm")) {
            *out_why = N_("an EBML file, but not Matroska");
            return false;
        }
        if (e.id == ID_DOCTYPE) mkv->webm = text_is(read_text(mkv, &e), "webm");
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
    if (!found) { *out_why = N_("the Matroska file has no segment"); return false; }
    mkv->segment_at = at;
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
    if (!have_tracks) { *out_why = N_("the Matroska file's tracks could not be read"); return false; }
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

size_t rubraview_mkv_subtitle_name(const rubraview_mkv_track_t *track, proven_u8str_view_t film_path, unsigned ordinal, char *out, size_t cap) {
    const char *ext = rubraview_mkv_subtitle_ext(track);
    if (!ext[0] || !out || cap == 0 || film_path.size == 0) return 0;
    size_t stem = film_path.size - rubraview_path_ext(film_path).size;
    char language[20];
    size_t n = 0;
    for (size_t i = 0; i < track->language.size && n + 1 < sizeof(language); ++i) {
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

/* ---- a subtitle put into a film (RV-115 stage 4, D-128) ---- */

enum {
    ID_VOID = 0xECu, ID_CRC32 = 0xBFu,
    ID_SEEK = 0x4DBBu, ID_SEEK_ID = 0x53ABu, ID_SEEK_POSITION = 0x53ACu,
    ID_TRACK_UID = 0x73C5u, ID_FLAG_LACING = 0x9Cu, ID_FLAG_DEFAULT = 0x88u,
    ID_CUE_POINT = 0xBBu, ID_CUE_TIME = 0xB3u, ID_CUE_TRACK_POSITIONS = 0xB7u, ID_CUE_TRACK = 0xF7u,
    ID_CUE_CLUSTER_POSITION = 0xF1u, ID_CUE_RELATIVE_POSITION = 0xF0u, ID_CUE_DURATION = 0xB2u, ID_CUE_CODEC_STATE = 0xEAu,
};

/* A buffer on the heap that EBML is written into. */
typedef struct wbuf { uint8_t *data; size_t len, cap; bool failed; } wbuf_t;

static void wput(wbuf_t *b, const void *bytes, size_t n) {
    if (b->failed || n == 0) return;
    size_t want = 0;
    if (PROVEN_CKD_ADD(&want, b->len, n)) { b->failed = true; return; }
    if (want > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < want) {
            if (PROVEN_CKD_MUL(&cap, cap, (size_t)2)) { b->failed = true; return; }
        }
        uint8_t *data = realloc(b->data, cap);
        if (!data) { b->failed = true; return; }
        b->data = data;
        b->cap = cap;
    }
    memcpy(b->data + b->len, bytes, n);
    b->len += n;
}

static unsigned id_width(uint32_t id) { return id > 0xFFFFFFu ? 4 : id > 0xFFFFu ? 3 : id > 0xFFu ? 2 : 1; }

static void wid(wbuf_t *b, uint32_t id) {
    uint8_t bytes[4] = { (uint8_t)(id >> 24), (uint8_t)(id >> 16), (uint8_t)(id >> 8), (uint8_t)id };
    unsigned w = id_width(id);
    wput(b, bytes + 4 - w, w);
}

/* The fewest bytes a size can be written in (all ones is kept for "unknown"). */
static unsigned size_width(uint64_t size) {
    for (unsigned w = 1; w < 8; ++w) {
        if (size < (((uint64_t)1 << (7 * w)) - 1)) return w;
    }
    return 8;
}

static void wsize(wbuf_t *b, uint64_t size, unsigned width) {
    if (width == 0) width = size_width(size);
    uint8_t bytes[8];
    for (unsigned i = 0; i < width; ++i) bytes[i] = (uint8_t)(size >> (8 * (width - 1 - i)));
    bytes[0] |= (uint8_t)(0x80u >> (width - 1));
    wput(b, bytes, width);
}

static void welem(wbuf_t *b, uint32_t id, const void *content, size_t n) {
    wid(b, id);
    wsize(b, n, 0);
    wput(b, content, n);
}

static void welem_uint(wbuf_t *b, uint32_t id, uint64_t value, unsigned width) {
    if (width == 0) { width = 1; while (width < 8 && (value >> (8 * width)) != 0) width++; }
    uint8_t bytes[8];
    for (unsigned i = 0; i < width; ++i) bytes[i] = (uint8_t)(value >> (8 * (width - 1 - i)));
    welem(b, id, bytes, width);
}

static void wbuf_free(wbuf_t *b) { free(b->data); *b = (wbuf_t){0}; }

/* An element's whole bytes, header and all, as they stand in the film. */
static void wraw(wbuf_t *b, const rubraview_mkv_t *mkv, uint64_t at, uint64_t next) {
    if (next > at) wput(b, mkv->data + at, (size_t)(next - at));
}

static int64_t to_ticks(const rubraview_mkv_t *mkv, int64_t ns) {
    int64_t scale = (int64_t)mkv->timestamp_scale;
    if (ns <= 0 || scale <= 0) return 0;
    return (ns + scale / 2) / scale;
}

/* ---- the subtitle to put in ---- */


/* SubStation's `H:MM:SS.cc`. */
static bool ass_clock(proven_u8str_view_t s, int64_t *out_ns) {
    s = proven_u8str_view_trim(s);
    int64_t parts[3] = { 0, 0, 0 };
    uint64_t frac = 0, frac_digits = 0;
    size_t part = 0;
    bool in_frac = false, digit = false;
    for (size_t i = 0; i < s.size; ++i) {
        char c = s.ptr[i];
        if (c >= '0' && c <= '9') {
            digit = true;
            if (in_frac) { if (frac_digits < 3) { frac = frac * 10 + (uint64_t)(c - '0'); frac_digits++; } }
            else parts[part] = rubraview_digit_append(parts[part], c);
        } else if (c == ':' && !in_frac && part < 2) { part++; }
        else if (c == '.' && !in_frac) { in_frac = true; }
        else return false;
    }
    if (!digit || part != 2) return false;
    while (frac_digits < 3) { frac *= 10; frac_digits++; }
    if (parts[0] > 99999 || parts[1] > 59 || parts[2] > 59) return false;
    *out_ns = ((int64_t)(parts[0] * 3600 + parts[1] * 60 + parts[2]) * 1000 + (int64_t)frac) * 1000000;
    return true;
}

static int put_cue_order(const void *a, const void *b) {
    const rubraview_mkv_put_cue_t *x = a, *y = b;
    return x->start_ns < y->start_ns ? -1 : x->start_ns > y->start_ns ? 1 : 0;
}

bool rubraview_mkv_put_track_from_subtitle(proven_arena_t *arena, const rubraview_subtitle_track_t *subtitle,
                                           rubraview_mkv_put_track_t *out, const char **out_why) {
    const char *sink = NULL;
    if (!out_why) out_why = &sink;
    *out_why = NULL;
    if (!arena || !subtitle || !out || subtitle->source.size == 0) { *out_why = N_("the subtitle's file was not kept"); return false; }
    *out = (rubraview_mkv_put_track_t){ .language = PROVEN_LIT(""), .name = PROVEN_LIT(""),
                                        .private_data = PROVEN_LIT("") };
    proven_u8str_view_t src = subtitle->source;
    size_t room = 0;

    if (subtitle->format == RUBRAVIEW_SUBTITLE_SRT) {
        out->codec = "S_TEXT/UTF8";
        room = subtitle->count;
        proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, room ? room : 1, sizeof(rubraview_mkv_put_cue_t));
        if (!proven_is_ok(res.err)) { *out_why = N_("no memory"); return false; }
        rubraview_mkv_put_cue_t *cues = (rubraview_mkv_put_cue_t*)(void*)res.value.ptr;
        size_t n = 0;
        for (size_t i = 0; i < subtitle->count; ++i) {
            const rubraview_subtitle_cue_t *cue = &subtitle->cues[i];
            if (cue->place.text_len == 0 || cue->place.text_at > src.size || cue->place.text_len > src.size - cue->place.text_at) continue;
            /* As written, the file's line ends made '\n'. */
            proven_result_mem_mut_t t = proven_arena_alloc(arena, cue->place.text_len + 1);
            if (!proven_is_ok(t.err)) { *out_why = N_("no memory"); return false; }
            uint8_t *text = t.value.ptr;
            size_t m = 0;
            for (size_t k = 0; k < cue->place.text_len; ++k) {
                char c = src.ptr[cue->place.text_at + k];
                if (c != '\r') text[m++] = (uint8_t)c;
            }
            double end = cue->end_seconds > cue->start_seconds ? cue->end_seconds : cue->start_seconds;
            cues[n++] = (rubraview_mkv_put_cue_t){ .start_ns = (int64_t)(cue->start_seconds * 1e9 + 0.5),
                                                   .duration_ns = (int64_t)((end - cue->start_seconds) * 1e9 + 0.5),
                                                   .payload = text, .size = m };
        }
        out->cues = cues;
        out->count = n;
    } else if (subtitle->format == RUBRAVIEW_SUBTITLE_ASS) {
        out->codec = "S_TEXT/ASS";
        /* The header: everything through the `Format:` line of `[Events]`. */
        size_t events = SIZE_MAX;
        for (size_t i = 0; i + 8 <= src.size; ++i) {
            if (memcmp(src.ptr + i, "[Events]", 8) == 0) { events = i; break; }
        }
        if (events == SIZE_MAX) { *out_why = "the SubStation file has no [Events]"; return false; }
        size_t head_end = SIZE_MAX, at = events;
        while (at < src.size) {
            size_t line_end = at;
            while (line_end < src.size && src.ptr[line_end] != '\n') line_end++;
            proven_u8str_view_t line = proven_u8str_view_trim((proven_u8str_view_t){ .ptr = src.ptr + at, .size = line_end - at });
            if (rubraview_u8_starts_with_ci(line, "format:")) { head_end = line_end < src.size ? line_end + 1 : line_end; break; }
            at = line_end + 1;
        }
        if (head_end == SIZE_MAX) { *out_why = N_("the SubStation file's events have no Format line"); return false; }
        out->private_data = (proven_u8str_view_t){ .ptr = src.ptr, .size = head_end };
        /* Count the Dialogue lines, then make each a block: ReadOrder,Layer,the rest after the times. */
        for (size_t i = head_end; i < src.size; ) {
            size_t e = i;
            while (e < src.size && src.ptr[e] != '\n') e++;
            if (rubraview_u8_starts_with_ci(proven_u8str_view_trim((proven_u8str_view_t){ .ptr = src.ptr + i, .size = e - i }), "dialogue:")) room++;
            i = e + 1;
        }
        proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, room ? room : 1, sizeof(rubraview_mkv_put_cue_t));
        if (!proven_is_ok(res.err)) { *out_why = N_("no memory"); return false; }
        rubraview_mkv_put_cue_t *cues = (rubraview_mkv_put_cue_t*)(void*)res.value.ptr;
        size_t n = 0, order = 0;
        for (size_t i = head_end; i < src.size && n < room; ) {
            size_t e = i;
            while (e < src.size && src.ptr[e] != '\n') e++;
            proven_u8str_view_t line = proven_u8str_view_trim((proven_u8str_view_t){ .ptr = src.ptr + i, .size = e - i });
            i = e + 1;
            if (!rubraview_u8_starts_with_ci(line, "dialogue:")) continue;
            proven_u8str_view_t rest = { .ptr = line.ptr + 9, .size = line.size - 9 };
            size_t commas[3], found = 0;
            for (size_t k = 0; k < rest.size && found < 3; ++k) if (rest.ptr[k] == ',') commas[found++] = k;
            if (found < 3) continue;
            int64_t start = 0, end = 0;
            if (!ass_clock((proven_u8str_view_t){ rest.ptr + commas[0] + 1, commas[1] - commas[0] - 1 }, &start) ||
                !ass_clock((proven_u8str_view_t){ rest.ptr + commas[1] + 1, commas[2] - commas[1] - 1 }, &end)) continue;
            proven_u8str_view_t layer = proven_u8str_view_trim((proven_u8str_view_t){ rest.ptr, commas[0] });
            proven_u8str_view_t tail = { rest.ptr + commas[2], rest.size - commas[2] };   /* ",Style,...,Text" */
            char number[24];
            int nn = snprintf(number, sizeof(number), "%zu,", order++);
            if (nn <= 0) continue;
            size_t size = (size_t)nn + layer.size + tail.size;
            proven_result_mem_mut_t t = proven_arena_alloc(arena, size + 1);
            if (!proven_is_ok(t.err)) { *out_why = N_("no memory"); return false; }
            uint8_t *text = t.value.ptr;
            memcpy(text, number, (size_t)nn);
            memcpy(text + nn, layer.ptr, layer.size);
            memcpy(text + nn + layer.size, tail.ptr, tail.size);
            cues[n++] = (rubraview_mkv_put_cue_t){ .start_ns = start, .duration_ns = end > start ? end - start : 0,
                                                   .payload = text, .size = size };
        }
        out->cues = cues;
        out->count = n;
    } else {
        *out_why = N_("only SubRip and SubStation subtitles are put into a film");
        return false;
    }
    if (out->count == 0) { *out_why = N_("the subtitle holds no lines to put in"); return false; }
    /* In order of start, as the film's clusters are; equal starts keep their order. */
    for (size_t i = 1; i < out->count; ++i) {
        if (out->cues[i].start_ns < out->cues[i - 1].start_ns) {
            proven_array_t view = { .data = (proven_byte_t *)(void*)out->cues, .len = out->count, .cap = out->count,
                                    .elem_size = sizeof(rubraview_mkv_put_cue_t), .align = alignof(rubraview_mkv_put_cue_t) };
            proven_array_sort(&view, put_cue_order);
            break;
        }
    }
    return true;
}

/* ---- laying the new file out ----
   The new file is a list of pieces, each a range of the film's own bytes or of
   bytes made here (the head, the cues, a cluster's new header, a subtitle's
   block). A subtitle goes inside the cluster its time falls in, before the
   first block that is not earlier than it — where a muxer would have put it —
   so a player meets it with the pictures it belongs to, also after a seek.
   That cluster gets a new header (its size has grown) and loses its CRC-32;
   every block in it is still the film's own bytes. */

void rubraview_mkv_put_free(rubraview_mkv_put_t *put) {
    if (!put) return;
    free(put->bytes);
    free(put->pieces);
    free(put->clusters);
    free(put->sub_cluster);
    free(put->sub_rel);
    put->bytes = NULL;
    put->pieces = NULL;
    put->clusters = NULL;
    put->sub_cluster = NULL;
    put->sub_rel = NULL;
}

/* An open cluster's end: where the first thing that is not its own begins. */
static uint64_t open_cluster_end(const rubraview_mkv_t *mkv, uint64_t body) {
    elem_t e;
    uint64_t at = body;
    while (at < mkv->body_end && elem_at(mkv, at, mkv->body_end, &e)) {
        if (is_top_level(e.id) || e.unknown || e.next <= at) break;
        at = e.next;
    }
    return at;
}

static uint64_t cluster_time(const rubraview_mkv_t *mkv, uint64_t body, uint64_t end) {
    elem_t e;
    for (uint64_t at = body; at < end && elem_at(mkv, at, end, &e); at = e.next) {
        if (e.id == ID_TIMESTAMP) return read_uint(mkv, &e);
        if (e.unknown || e.next <= at || e.id == ID_SIMPLE_BLOCK || e.id == ID_BLOCK_GROUP) break;
    }
    return 0;
}

/* The tick a cluster's child block starts at; -1 for what is not a block. */
static int64_t child_tick(const rubraview_mkv_t *mkv, const elem_t *e, uint64_t time) {
    elem_t in = *e;
    if (e->id == ID_BLOCK_GROUP) {
        bool found = false;
        for (uint64_t p = e->body; p < e->next && elem_at(mkv, p, e->next, &in); p = in.next) {
            if (in.id == ID_BLOCK) { found = true; break; }
            if (in.unknown || in.next <= p) break;
        }
        if (!found) return -1;
    } else if (e->id != ID_SIMPLE_BLOCK) {
        return -1;
    }
    if (in.size < 4) return -1;
    const uint8_t *b = mkv->data + in.body;
    unsigned len = 0;
    for (unsigned bit = 0; bit < 8; ++bit) if (b[0] & (0x80u >> bit)) { len = bit + 1; break; }
    if (len == 0 || in.size < (uint64_t)len + 3) return -1;
    int16_t rel = (int16_t)(uint16_t)(((uint16_t)b[len] << 8) | b[len + 1]);
    return (int64_t)time + rel;
}

/* The new Tracks: the old entries as they are (no CRC-32: the content changes), then the subtitle's. */
static void write_tracks(wbuf_t *out, const rubraview_mkv_t *mkv, const elem_t *tracks, const rubraview_mkv_put_t *put) {
    wbuf_t body = {0}, entry = {0};
    elem_t e;
    for (uint64_t at = tracks->body; at < tracks->next && elem_at(mkv, at, tracks->next, &e); at = e.next) {
        if (e.id != ID_CRC32 && e.id != ID_VOID) wraw(&body, mkv, at, e.next);
        if (e.next <= at) break;
    }
    welem_uint(&entry, ID_TRACK_NUMBER, put->number, 0);
    welem_uint(&entry, ID_TRACK_UID, put->uid, 8);
    welem_uint(&entry, ID_TRACK_TYPE, 0x11, 1);
    welem_uint(&entry, ID_FLAG_LACING, 0, 1);
    welem_uint(&entry, ID_FLAG_DEFAULT, 0, 1);
    welem(&entry, ID_CODEC_ID, put->track.codec, strlen(put->track.codec));
    proven_u8str_view_t language = put->track.language.size > 0 ? put->track.language : (proven_u8str_view_t){ .ptr = (const proven_byte_t *)"und", .size = 3 };
    welem(&entry, ID_LANGUAGE, language.ptr, language.size);
    if (put->track.name.size > 0) welem(&entry, ID_NAME, put->track.name.ptr, put->track.name.size);
    if (put->track.private_data.size > 0) welem(&entry, ID_CODEC_PRIVATE, put->track.private_data.ptr, put->track.private_data.size);
    if (!entry.failed) welem(&body, ID_TRACK_ENTRY, entry.data, entry.len);
    if (entry.failed || body.failed) out->failed = true;
    else welem(out, ID_TRACKS, body.data, body.len);
    wbuf_free(&body);
    wbuf_free(&entry);
}

/* A subtitle's BlockGroup, its time given against its cluster's. */
static void write_sub_group(wbuf_t *out, const rubraview_mkv_t *mkv, const rubraview_mkv_put_t *put, size_t i, int64_t rel) {
    wbuf_t group = {0}, block = {0};
    wsize(&block, put->number, 0);                    /* the track, written as a size is */
    uint8_t time_and_flags[3] = { (uint8_t)((uint16_t)(int16_t)rel >> 8), (uint8_t)(uint16_t)(int16_t)rel, 0 };
    wput(&block, time_and_flags, 3);
    wput(&block, put->track.cues[i].payload, put->track.cues[i].size);
    welem(&group, ID_BLOCK, block.data, block.len);
    welem_uint(&group, ID_BLOCK_DURATION, (uint64_t)to_ticks(mkv, put->track.cues[i].duration_ns), 0);
    if (block.failed || group.failed) out->failed = true;
    else welem(out, ID_BLOCK_GROUP, group.data, group.len);
    wbuf_free(&group);
    wbuf_free(&block);
}

/* The pieces: a growing list. */
static bool add_piece(rubraview_mkv_put_t *put, size_t *room, uint64_t from, uint64_t to, bool own) {
    if (to <= from) return true;
    if (put->piece_count > 0) {                       /* a range that goes on from the last one is the same piece */
        rubraview_mkv_piece_t *last = &put->pieces[put->piece_count - 1];
        if (last->own == own && last->to == from) { last->to = to; return true; }
    }
    if (put->piece_count == *room) {
        size_t grown = *room ? *room : 256;
        if (PROVEN_CKD_MUL(&grown, grown, (size_t)2)) return false;
        rubraview_mkv_piece_t *pieces = rubraview_malloc_product(grown, sizeof(*pieces), 1);
        if (!pieces) return false;
        if (put->piece_count) memcpy(pieces, put->pieces, put->piece_count * sizeof(*pieces));
        free(put->pieces);
        put->pieces = pieces;
        *room = grown;
    }
    put->pieces[put->piece_count++] = (rubraview_mkv_piece_t){ .from = from, .to = to, .own = own };
    return true;
}

static uint64_t piece_size(const rubraview_mkv_piece_t *p) { return p->to - p->from; }

/* Where a byte of the film is in the new file; UINT64_MAX when it is not carried over. */
static uint64_t new_place(const rubraview_mkv_put_t *put, size_t cluster, uint64_t old_abs) {
    const struct rubraview_mkv_put_cluster *c = &put->clusters[cluster];
    uint64_t pos = c->new_at;
    for (size_t k = c->piece_lo; k < c->piece_hi; ++k) {
        const rubraview_mkv_piece_t *p = &put->pieces[k];
        if (!p->own && old_abs >= p->from && old_abs < p->to) return pos + (old_abs - p->from);
        pos += piece_size(p);
    }
    return UINT64_MAX;
}

static size_t cluster_at_index(const rubraview_mkv_put_t *put, uint64_t old_at) {
    size_t low = 0, high = put->cluster_count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (put->clusters[mid].at < old_at) low = mid + 1; else high = mid;
    }
    return low < put->cluster_count && put->clusters[low].at == old_at ? low : SIZE_MAX;
}

/* A cue point of the subtitle: when it comes, where its block is, how long it lasts —
   a player that seeks to a moment inside a subtitle finds it by this. */
static void write_sub_point(wbuf_t *out, const rubraview_mkv_t *mkv, const rubraview_mkv_put_t *put, size_t i, uint64_t new_body) {
    wbuf_t point = {0}, pos = {0};
    welem_uint(&pos, ID_CUE_TRACK, put->number, 0);
    welem_uint(&pos, ID_CUE_CLUSTER_POSITION, put->sub_cluster[i] - new_body, 8);
    welem_uint(&pos, ID_CUE_RELATIVE_POSITION, put->sub_rel[i], 8);
    welem_uint(&pos, ID_CUE_DURATION, (uint64_t)to_ticks(mkv, put->track.cues[i].duration_ns), 0);
    welem_uint(&point, ID_CUE_TIME, (uint64_t)to_ticks(mkv, put->track.cues[i].start_ns), 0);
    welem(&point, ID_CUE_TRACK_POSITIONS, pos.data, pos.len);
    if (pos.failed || point.failed) out->failed = true;
    else welem(out, ID_CUE_POINT, point.data, point.len);
    wbuf_free(&point);
    wbuf_free(&pos);
}

/* The new Cues: each position moved to where its cluster and its block now are, the
   rest as it was; and the subtitle's own points among them, in order of time. */
static void write_cues(wbuf_t *out, const rubraview_mkv_t *mkv, const elem_t *cues, const rubraview_mkv_put_t *put, uint64_t new_body) {
    wbuf_t body = {0};
    size_t next_sub = 0;
    elem_t point;
    for (uint64_t at = cues ? cues->body : 0; cues && at < cues->next && elem_at(mkv, at, cues->next, &point); at = point.next) {
        if (point.next <= at) break;
        if (point.id != ID_CUE_POINT) continue;
        wbuf_t pbody = {0};
        size_t kept = 0;
        elem_t in;
        for (uint64_t p = point.body; p < point.next && elem_at(mkv, p, point.next, &in); p = in.next) {
            if (in.next <= p) break;
            if (in.id == ID_CUE_TIME) {
                uint64_t time = read_uint(mkv, &in);
                while (next_sub < put->track.count && (uint64_t)to_ticks(mkv, put->track.cues[next_sub].start_ns) < time) {
                    write_sub_point(&body, mkv, put, next_sub++, new_body);
                }
            }
            if (in.id != ID_CUE_TRACK_POSITIONS) { if (in.id != ID_CRC32 && in.id != ID_VOID) wraw(&pbody, mkv, p, in.next); continue; }
            /* First where its cluster is, then the rest with the block's place moved. */
            uint64_t old_cluster = UINT64_MAX, old_rel = UINT64_MAX;
            elem_t t;
            for (uint64_t q = in.body; q < in.next && elem_at(mkv, q, in.next, &t); q = t.next) {
                if (t.next <= q) break;
                if (t.id == ID_CUE_CLUSTER_POSITION) old_cluster = read_uint(mkv, &t);
                if (t.id == ID_CUE_RELATIVE_POSITION) old_rel = read_uint(mkv, &t);
            }
            uint64_t old_abs = 0;
            size_t k = SIZE_MAX;
            if (old_cluster != UINT64_MAX && !PROVEN_CKD_ADD(&old_abs, mkv->body_at, old_cluster)) k = cluster_at_index(put, old_abs);
            if (k == SIZE_MAX) continue;                         /* it named no cluster: it is left out */
            const struct rubraview_mkv_put_cluster *c = &put->clusters[k];
            uint64_t new_rel = UINT64_MAX;
            if (old_rel != UINT64_MAX && !c->rebuilt) {
                new_rel = old_rel;                           /* the cluster is as it was, header and all */
            } else if (old_rel != UINT64_MAX) {
                uint64_t abs = 0, moved = UINT64_MAX;
                if (!PROVEN_CKD_ADD(&abs, c->data_at, old_rel)) moved = new_place(put, k, abs);
                if (moved != UINT64_MAX && moved >= c->new_data_at) new_rel = moved - c->new_data_at;
            }
            wbuf_t tbody = {0};
            for (uint64_t q = in.body; q < in.next && elem_at(mkv, q, in.next, &t); q = t.next) {
                if (t.next <= q) break;
                if (t.id == ID_CUE_CLUSTER_POSITION) welem_uint(&tbody, ID_CUE_CLUSTER_POSITION, c->new_at - new_body, 8);
                else if (t.id == ID_CUE_RELATIVE_POSITION) { if (new_rel != UINT64_MAX) welem_uint(&tbody, ID_CUE_RELATIVE_POSITION, new_rel, 8); }
                else if (t.id != ID_CUE_CODEC_STATE && t.id != ID_CRC32 && t.id != ID_VOID) wraw(&tbody, mkv, q, t.next);
            }
            if (!tbody.failed) { welem(&pbody, ID_CUE_TRACK_POSITIONS, tbody.data, tbody.len); kept++; }
            else pbody.failed = true;
            wbuf_free(&tbody);
        }
        if (kept > 0 && !pbody.failed) welem(&body, ID_CUE_POINT, pbody.data, pbody.len);
        if (pbody.failed) body.failed = true;
        wbuf_free(&pbody);
    }
    while (next_sub < put->track.count) write_sub_point(&body, mkv, put, next_sub++, new_body);
    if (body.failed) out->failed = true;
    else if (body.len > 0) welem(out, ID_CUES, body.data, body.len);
    wbuf_free(&body);
}

#define PUT_FAIL(why_text) do { wbuf_free(&own); wbuf_free(&kept); rubraview_mkv_put_free(put); *out_why = (why_text); return false; } while (0)

bool rubraview_mkv_put_plan(rubraview_mkv_put_t *put, const rubraview_mkv_t *mkv, const rubraview_mkv_put_track_t *track,
                            uint64_t uid, const char **out_why) {
    const char *sink = NULL;
    if (!out_why) out_why = &sink;
    *out_why = NULL;
    if (!put || !mkv || !track || track->count == 0 || !track->codec) { *out_why = N_("nothing to put in"); return false; }
    *put = (rubraview_mkv_put_t){ .mkv = mkv, .track = *track, .uid = uid ? uid : 1 };
    if (mkv->webm) { *out_why = N_("a WebM film holds no SubRip or SubStation subtitles"); return false; }
    for (size_t i = 0; i < mkv->track_count; ++i) {
        if (mkv->tracks[i].number >= put->number) put->number = mkv->tracks[i].number + 1;
    }
    if (put->number == 0 || put->number > 126) { *out_why = N_("the film has too many tracks to add one"); return false; }
    wbuf_t own = {0}, kept = {0};

    /* The segment's children: how many clusters, where the tracks and the cues are. */
    elem_t e, tracks = {0}, cues = {0};
    bool have_tracks = false, have_cues = false;
    size_t clusters = 0;
    for (uint64_t at = mkv->body_at; at < mkv->body_end && elem_at(mkv, at, mkv->body_end, &e); ) {
        uint64_t next = e.next;
        if (e.id == ID_CLUSTER) {
            if (e.unknown) next = open_cluster_end(mkv, e.body);
            clusters++;
        } else if (e.unknown) {
            PUT_FAIL(N_("the film has a part of unknown length that is not a cluster"));
        }
        if (e.id == ID_TRACKS && !have_tracks) { tracks = e; have_tracks = true; }
        if (e.id == ID_CUES && !have_cues) { cues = e; have_cues = true; }
        if (next <= at) break;
        at = next;
    }
    if (!have_tracks || clusters == 0) PUT_FAIL(N_("the film's tracks or clusters could not be found"));
    put->clusters = rubraview_malloc_product(clusters, sizeof(put->clusters[0]), 1);
    put->sub_cluster = rubraview_malloc_product(track->count, sizeof(uint64_t), 1);
    put->sub_rel = rubraview_malloc_product(track->count, sizeof(uint64_t), 1);
    if (!put->clusters || !put->sub_cluster || !put->sub_rel) PUT_FAIL(N_("no memory"));

    /* The parts kept before the clusters, in their order: Info, Tracks (new), the rest; and the clusters. */
    struct { uint32_t id; uint64_t at; } seek[8];
    size_t seek_count = 0, found = 0;
    for (uint64_t at = mkv->body_at; at < mkv->body_end && elem_at(mkv, at, mkv->body_end, &e); ) {
        uint64_t next = e.next;
        if (e.id == ID_CLUSTER) {
            if (e.unknown) next = open_cluster_end(mkv, e.body);
            if (found < clusters) {
                put->clusters[found] = (struct rubraview_mkv_put_cluster){ .at = at, .data_at = e.body, .end = next,
                                                                           .time = cluster_time(mkv, e.body, next) };
                found++;
            }
        } else if (e.id != ID_SEEKHEAD && e.id != ID_VOID && e.id != ID_CUES && e.id != ID_CRC32 &&
                   !(e.id == ID_TRACKS && e.body != tracks.body)) {   /* a second Tracks, if a file had one: a player reads the first */
            bool first_of_kind = true;
            for (size_t k = 0; k < seek_count; ++k) if (seek[k].id == e.id) first_of_kind = false;
            if (first_of_kind && seek_count < 7) seek[seek_count++] = (typeof(seek[0])){ .id = e.id, .at = kept.len };
            if (e.id == ID_TRACKS) write_tracks(&kept, mkv, &e, put);
            else wraw(&kept, mkv, at, next);
        }
        if (next <= at) break;
        at = next;
    }
    put->cluster_count = found;
    if (kept.failed || found == 0) PUT_FAIL(N_("the film's head could not be copied"));

    /* The SeekHead's size does not hang on the places it names (eight bytes each). */
    wbuf_t probe = {0};
    for (size_t k = 0; k < seek_count + 1; ++k) {         /* and the Cues, always written: the subtitle has points */
        wbuf_t s = {0};
        welem_uint(&s, ID_SEEK_ID, 0x1F43B675u, 4);
        welem_uint(&s, ID_SEEK_POSITION, 0, 8);
        welem(&probe, ID_SEEK, s.data, s.len);
        wbuf_free(&s);
    }
    size_t seekhead_size = id_width(ID_SEEKHEAD) + size_width(probe.len) + probe.len;
    wbuf_free(&probe);
    uint64_t prefix = mkv->segment_at;                /* the EBML head and whatever stood before the segment */
    uint64_t new_body = prefix + 4 + 8;               /* the segment's ID and its size in eight bytes */
    uint64_t head_size = new_body + seekhead_size + kept.len;

    /* The clusters, a subtitle put in where its time falls. */
    size_t room = 0, next_sub = 0;
    uint64_t pos = head_size;
    /* The head goes first; its bytes are made last, when every place is known. */
    if (!add_piece(put, &room, 1, 2, true)) PUT_FAIL(N_("no memory"));
    for (size_t k = 0; k < put->cluster_count; ++k) {
        struct rubraview_mkv_put_cluster *c = &put->clusters[k];
        bool last = k + 1 == put->cluster_count;
        int64_t limit = last ? INT64_MAX : (int64_t)put->clusters[k + 1].time;
        /* Subtitles before the first cluster's time: a cluster of their own, before it. */
        while (k == 0 && next_sub < track->count && to_ticks(mkv, track->cues[next_sub].start_ns) < (int64_t)c->time) {
            wbuf_t one = {0};
            welem_uint(&one, ID_TIMESTAMP, (uint64_t)to_ticks(mkv, track->cues[next_sub].start_ns), 0);
            put->sub_cluster[next_sub] = pos;
            put->sub_rel[next_sub] = one.len;
            write_sub_group(&one, mkv, put, next_sub, 0);
            uint64_t from = own.len;
            welem(&own, ID_CLUSTER, one.data, one.len);
            bool failed = one.failed;
            wbuf_free(&one);
            if (failed || own.failed || !add_piece(put, &room, from, own.len, true)) PUT_FAIL(N_("no memory"));
            pos += own.len - from;
            next_sub++;
        }
        /* Those of this cluster, whose time it can carry (sixteen bits beside its own). */
        size_t lo = next_sub, hi = next_sub;
        while (hi < track->count && to_ticks(mkv, track->cues[hi].start_ns) < limit &&
               to_ticks(mkv, track->cues[hi].start_ns) - (int64_t)c->time <= 32767) hi++;
        c->new_at = pos;
        c->piece_lo = put->piece_count;
        if (lo == hi) {
            if (!add_piece(put, &room, c->at, c->end, false)) PUT_FAIL(N_("no memory"));
            c->new_data_at = pos + (c->data_at - c->at);
            pos += c->end - c->at;
        } else {
            /* Its children again, the subtitles among them; then its new header in front. */
            uint64_t size = 0;
            size_t header = put->piece_count;
            if (!add_piece(put, &room, own.len, own.len + 12, true)) PUT_FAIL(N_("no memory"));
            uint64_t header_at = own.len;
            uint8_t zeros[12] = {0};
            wput(&own, zeros, 12);
            put->pieces[header].to = own.len;
            size_t sub = lo;
            uint64_t data_pos = pos + 12;
            elem_t ch;
            for (uint64_t at = c->data_at; at < c->end && elem_at(mkv, at, c->end, &ch); at = ch.next) {
                if (ch.next <= at || ch.unknown) break;
                if (ch.id == ID_CRC32) continue;              /* its content changes */
                int64_t tick = child_tick(mkv, &ch, c->time);
                while (sub < hi && tick >= 0 && to_ticks(mkv, track->cues[sub].start_ns) <= tick) {
                    uint64_t from = own.len;
                    put->sub_cluster[sub] = pos;
                    put->sub_rel[sub] = data_pos - (pos + 12);
                    write_sub_group(&own, mkv, put, sub, to_ticks(mkv, track->cues[sub].start_ns) - (int64_t)c->time);
                    if (own.failed || !add_piece(put, &room, from, own.len, true)) PUT_FAIL(N_("no memory"));
                    data_pos += own.len - from;
                    sub++;
                }
                if (!add_piece(put, &room, at, ch.next, false)) PUT_FAIL(N_("no memory"));
                data_pos += ch.next - at;
            }
            while (sub < hi) {                            /* after its last block */
                uint64_t from = own.len;
                put->sub_cluster[sub] = pos;
                put->sub_rel[sub] = data_pos - (pos + 12);
                write_sub_group(&own, mkv, put, sub, to_ticks(mkv, track->cues[sub].start_ns) - (int64_t)c->time);
                if (own.failed || !add_piece(put, &room, from, own.len, true)) PUT_FAIL(N_("no memory"));
                data_pos += own.len - from;
                sub++;
            }
            size = data_pos - (pos + 12);
            /* The header: the cluster's ID, its size in eight bytes. */
            wbuf_t h = {0};
            wid(&h, ID_CLUSTER);
            wsize(&h, size, 8);
            if (h.failed || h.len != 12) { wbuf_free(&h); PUT_FAIL(N_("no memory")); }
            memcpy(own.data + header_at, h.data, 12);
            wbuf_free(&h);
            c->new_data_at = pos + 12;
            c->rebuilt = true;
            pos = data_pos;
        }
        c->piece_hi = put->piece_count;
        next_sub = hi;
        /* Those its clock cannot carry, or after the last cluster: clusters of their own, after it. */
        while (next_sub < track->count && (last || to_ticks(mkv, track->cues[next_sub].start_ns) < limit)) {
            wbuf_t one = {0};
            welem_uint(&one, ID_TIMESTAMP, (uint64_t)to_ticks(mkv, track->cues[next_sub].start_ns), 0);
            put->sub_cluster[next_sub] = pos;
            put->sub_rel[next_sub] = one.len;
            write_sub_group(&one, mkv, put, next_sub, 0);
            uint64_t from = own.len;
            welem(&own, ID_CLUSTER, one.data, one.len);
            bool failed = one.failed;
            wbuf_free(&one);
            if (failed || own.failed || !add_piece(put, &room, from, own.len, true)) PUT_FAIL(N_("no memory"));
            pos += own.len - from;
            next_sub++;
        }
    }

    /* The cues, at the end. */
    uint64_t cues_at = pos;
    uint64_t cues_from = own.len;
    write_cues(&own, mkv, have_cues ? &cues : NULL, put, new_body);
    if (own.failed || !add_piece(put, &room, cues_from, own.len, true)) PUT_FAIL(N_("no memory"));
    pos += own.len - cues_from;
    put->total = pos;

    /* The head: prefix, segment, SeekHead, the parts kept. Made last, it is the first piece. */
    uint64_t head_from = own.len;
    wraw(&own, mkv, 0, prefix);
    wid(&own, ID_SEGMENT);
    wsize(&own, put->total - new_body, 8);
    wbuf_t sh = {0};
    for (size_t k = 0; k < seek_count; ++k) {
        wbuf_t s = {0};
        welem_uint(&s, ID_SEEK_ID, seek[k].id, id_width(seek[k].id));
        welem_uint(&s, ID_SEEK_POSITION, seekhead_size + seek[k].at, 8);
        welem(&sh, ID_SEEK, s.data, s.len);
        wbuf_free(&s);
    }
    wbuf_t s = {0};
    welem_uint(&s, ID_SEEK_ID, ID_CUES, 4);
    welem_uint(&s, ID_SEEK_POSITION, cues_at - new_body, 8);
    welem(&sh, ID_SEEK, s.data, s.len);
    wbuf_free(&s);
    welem(&own, ID_SEEKHEAD, sh.data, sh.len);
    wbuf_free(&sh);
    wput(&own, kept.data, kept.len);
    /* The head must be exactly as long as the layout counted, or every place after it is wrong. */
    if (own.failed || own.len - head_from != head_size) PUT_FAIL(N_("the new film could not be laid out"));
    put->pieces[0] = (rubraview_mkv_piece_t){ .from = head_from, .to = own.len, .own = true };
    wbuf_free(&kept);
    put->bytes = own.data;
    put->bytes_size = own.len;
    return true;
}

int rubraview_mkv_put_step(rubraview_mkv_put_t *put, rubraview_mkv_emit_fn emit, void *ctx, size_t budget) {
    if (!put || !emit || !put->pieces) return -1;
    if (budget == 0) budget = 1;
    while (put->index < put->piece_count) {
        const rubraview_mkv_piece_t *p = &put->pieces[put->index];
        const uint8_t *base = p->own ? put->bytes : put->mkv->data;
        uint64_t left = piece_size(p) - put->offset;
        size_t n = left < budget ? (size_t)left : budget;
        if (n > 0 && !emit(ctx, base + p->from + put->offset, n)) return -1;
        put->offset += n;
        budget -= n;
        if (put->offset == piece_size(p)) { put->index++; put->offset = 0; }
        if (budget == 0) return put->index < put->piece_count ? 1 : 0;
    }
    return 0;
}

/* ---- the new file checked against the old ---- */

static bool same_block(const rubraview_mkv_block_t *a, const rubraview_mkv_block_t *b) {
    return a->track == b->track && a->start_ns == b->start_ns && a->duration_ns == b->duration_ns && a->size == b->size &&
           a->laced == b->laced && (a->size == 0 || memcmp(a->payload, b->payload, a->size) == 0);
}

bool rubraview_mkv_check_begin(rubraview_mkv_check_t *check, const rubraview_mkv_put_t *put, const uint8_t *made, uint64_t made_size) {
    if (!check || !put) return false;
    *check = (rubraview_mkv_check_t){0};
    const rubraview_mkv_t *old = put->mkv;
    if (made_size != put->total) { check->why = N_("the new film is not the size it was laid out to be"); return false; }
    if (!rubraview_mkv_open(&check->made, made, made_size, &check->why)) return false;
    if (check->made.track_count != old->track_count + 1) { check->why = N_("the new film's tracks are not the old ones and one more"); return false; }
    for (size_t i = 0; i < old->track_count; ++i) {
        const rubraview_mkv_track_t *a = &old->tracks[i], *b = &check->made.tracks[i];
        if (a->number != b->number || a->type != b->type || !proven_u8str_view_eq(a->codec, b->codec) ||
            a->private_size != b->private_size || (a->private_size && memcmp(a->private_data, b->private_data, a->private_size) != 0)) {
            check->why = N_("a track of the new film is not as it was");
            return false;
        }
    }
    const rubraview_mkv_track_t *added = &check->made.tracks[old->track_count];
    if (added->number != put->number || added->type != 0x11 || !text_is(added->codec, put->track.codec)) {
        check->why = N_("the subtitle track was not found in the new film");
        return false;
    }
    if (check->made.timestamp_scale != old->timestamp_scale) { check->why = N_("the new film's clock is not the old one's"); return false; }
    rubraview_mkv_walk_begin(old, &check->old_walk);
    rubraview_mkv_walk_begin(&check->made, &check->new_walk);
    return true;
}

int rubraview_mkv_check_step(rubraview_mkv_check_t *check, const rubraview_mkv_put_t *put, size_t budget_blocks) {
    if (!check || !put || check->why) return -1;
    const rubraview_mkv_t *old = put->mkv;
    int64_t scale = (int64_t)old->timestamp_scale;
    rubraview_mkv_block_t a, b;
    if (check->phase == 0) {
        for (size_t n = 0; n < budget_blocks; ++n) {
            if (!rubraview_mkv_walk_next(&check->made, &check->new_walk, &b)) {
                if (rubraview_mkv_walk_next(old, &check->old_walk, &a)) { check->why = N_("a block of the film is missing from the new one"); return -1; }
                if (check->sub_blocks != put->track.count) { check->why = N_("not every subtitle is in the new film"); return -1; }
                if (check->new_walk.at != check->made.body_end) { check->why = N_("the new film could not be read to its end"); return -1; }
                check->phase = 1;
                break;
            }
            if (b.track == put->number) {
                if (check->sub_blocks >= put->track.count) { check->why = N_("the new film holds more subtitles than were put in"); return -1; }
                const rubraview_mkv_put_cue_t *cue = &put->track.cues[check->sub_blocks];
                if (b.start_ns != to_ticks(old, cue->start_ns) * scale || b.duration_ns != to_ticks(old, cue->duration_ns) * scale ||
                    b.size != cue->size || b.laced || memcmp(b.payload, cue->payload, cue->size) != 0) {
                    check->why = N_("a subtitle in the new film is not the one put in");
                    return -1;
                }
                check->sub_blocks++;
                continue;
            }
            if (!rubraview_mkv_walk_next(old, &check->old_walk, &a)) { check->why = N_("the new film holds a block the film did not"); return -1; }
            if (!same_block(&a, &b)) { check->why = N_("a block of the new film differs from the film's"); return -1; }
            check->blocks++;
        }
        if (check->phase == 0) return 1;
    }
    /* Every cue point names a cluster. */
    elem_t e;
    const rubraview_mkv_t *made = &check->made;
    for (uint64_t at = made->body_at; at < made->body_end && elem_at(made, at, made->body_end, &e); at = e.next) {
        if (e.next <= at) break;
        if (e.id != ID_CUES) continue;
        elem_t point, in, t;
        for (uint64_t p = e.body; p < e.next && elem_at(made, p, e.next, &point); p = point.next) {
            if (point.next <= p) break;
            for (uint64_t q = point.body; q < point.next && elem_at(made, q, point.next, &in); q = in.next) {
                if (in.next <= q) break;
                if (in.id != ID_CUE_TRACK_POSITIONS) continue;
                for (uint64_t r = in.body; r < in.next && elem_at(made, r, in.next, &t); r = t.next) {
                    if (t.next <= r) break;
                    if (t.id != ID_CUE_CLUSTER_POSITION) continue;
                    uint64_t target = 0;
                    elem_t c;
                    if (PROVEN_CKD_ADD(&target, made->body_at, read_uint(made, &t)) || !elem_at(made, target, made->body_end, &c) ||
                        c.id != ID_CLUSTER) {
                        check->why = N_("a cue point of the new film does not lead to a cluster");
                        return -1;
                    }
                }
            }
        }
    }
    return 0;
}
