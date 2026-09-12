#include "rubraview/subtitle.h"
#include "rubraview/path.h"
#include <string.h>
#include <stdlib.h>

/* ---- shared scanning ---- */

typedef struct scanner {
    const char *ptr;
    size_t len;
    size_t pos;
} scanner_t;

static bool at_end(const scanner_t *s) { return s->pos >= s->len; }

/* One line, without its terminator. Handles CRLF and LF alike, because
   subtitle files are shared between systems more than most files are. */
static u8str_t next_line(scanner_t *s) {
    size_t start = s->pos;
    while (s->pos < s->len && s->ptr[s->pos] != '\n') s->pos++;

    size_t end = s->pos;
    if (end > start && s->ptr[end - 1] == '\r') end--;
    if (s->pos < s->len) s->pos++;   /* step over the '\n' */

    return (u8str_t){ .ptr = s->ptr + start, .len = end - start };
}

static u8str_t trim(u8str_t s) {
    while (s.len > 0 && (s.ptr[0] == ' ' || s.ptr[0] == '\t')) { s.ptr++; s.len--; }
    while (s.len > 0 && (s.ptr[s.len - 1] == ' ' || s.ptr[s.len - 1] == '\t')) s.len--;
    return s;
}

static bool starts_with_ci(u8str_t s, const char *prefix) {
    size_t n = strlen(prefix);
    if (s.len < n) return false;
    for (size_t i = 0; i < n; ++i) {
        char a = s.ptr[i];
        char b = prefix[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

/* ---- cue list ---- */

typedef struct cue_buf {
    rubraview_subtitle_cue_t *data;
    size_t count, capacity;
} cue_buf_t;

static bool cue_push(proven_arena_t *arena, cue_buf_t *b, rubraview_subtitle_cue_t cue) {
    if (cue.end_seconds <= cue.start_seconds) return false;   /* a cue with no duration */

    if (b->count >= b->capacity) {
        size_t new_cap = b->capacity == 0 ? 64 : b->capacity * 2;
        proven_result_mem_mut_t res = proven_arena_alloc(arena, new_cap * sizeof(*b->data));
        if (!proven_is_ok(res.err)) return false;
        rubraview_subtitle_cue_t *data = (rubraview_subtitle_cue_t*)(void*)res.value.ptr;
        if (b->data && b->count > 0) memcpy(data, b->data, b->count * sizeof(*b->data));
        b->data = data;
        b->capacity = new_cap;
    }
    b->data[b->count++] = cue;
    return true;
}

/* ---- timestamps ---- */

/* `hh:mm:ss` followed by a fraction after either ',' (SubRip) or '.'
   (WebVTT). Hours may be missing, which WebVTT allows. Returns false on
   anything it does not recognise, and the caller skips that cue. */
static bool parse_clock(u8str_t s, double *out) {
    s = trim(s);
    if (s.len == 0) return false;

    double parts[3] = {0.0, 0.0, 0.0};
    size_t part = 0;
    double value = 0.0;
    double fraction = 0.0, fraction_scale = 1.0;
    bool in_fraction = false;
    bool any_digit = false;

    for (size_t i = 0; i <= s.len; ++i) {
        char c = i < s.len ? s.ptr[i] : ':';   /* a virtual separator ends the last field */

        if (c >= '0' && c <= '9') {
            any_digit = true;
            if (in_fraction) {
                fraction_scale *= 10.0;
                fraction = fraction * 10.0 + (c - '0');
            } else {
                value = value * 10.0 + (c - '0');
            }
            continue;
        }

        if (c == ',' || c == '.') {
            if (in_fraction) return false;
            in_fraction = true;
            continue;
        }

        if (c == ':') {
            if (!any_digit) return false;
            if (part >= 3) return false;
            parts[part++] = value;
            value = 0.0;
            any_digit = false;
            if (i < s.len && in_fraction) return false;   /* a fraction before the last field */
            continue;
        }

        return false;   /* anything else is not a timestamp */
    }

    if (part == 0) return false;

    double seconds = 0.0;
    if (part == 3) seconds = parts[0] * 3600.0 + parts[1] * 60.0 + parts[2];
    else if (part == 2) seconds = parts[0] * 60.0 + parts[1];
    else seconds = parts[0];

    if (in_fraction && fraction_scale > 1.0) seconds += fraction / fraction_scale;

    *out = seconds;
    return true;
}

/* ---- markup ---- */

/* Strips `<i>`, `<font ...>`, SubStation's `{\pos(...)}` and SAMI's
   tags, and turns `<br>` into a line break. What is left is the words,
   which is all the renderer draws. */
static u8str_t strip_markup(proven_arena_t *arena, u8str_t text, bool braces_too) {
    u8str_t empty = { .ptr = "", .len = 0 };
    if (text.len == 0) return empty;

    proven_result_mem_mut_t res = proven_arena_alloc(arena, text.len + 1);
    if (!proven_is_ok(res.err)) return empty;
    char *out = (char*)(void*)res.value.ptr;

    size_t written = 0;
    for (size_t i = 0; i < text.len; ++i) {
        if (text.ptr[i] == '<') {
            /* `<br>` and `<br/>` are the one tag that means something. */
            u8str_t rest = { .ptr = text.ptr + i, .len = text.len - i };
            if (starts_with_ci(rest, "<br")) {
                if (written == 0 || out[written - 1] != '\n') out[written++] = '\n';
            }
            while (i < text.len && text.ptr[i] != '>') i++;
            continue;
        }
        if (braces_too && text.ptr[i] == '{') {
            while (i < text.len && text.ptr[i] != '}') i++;
            continue;
        }
        /* SubStation writes its line breaks as a literal backslash-N. */
        if (braces_too && text.ptr[i] == '\\' && i + 1 < text.len &&
            (text.ptr[i + 1] == 'N' || text.ptr[i + 1] == 'n')) {
            out[written++] = '\n';
            i++;
            continue;
        }
        out[written++] = text.ptr[i];
    }

    /* Trailing blank space is common and never wanted. */
    while (written > 0 && (out[written - 1] == '\n' || out[written - 1] == ' ' ||
                           out[written - 1] == '\r' || out[written - 1] == '\t')) {
        written--;
    }
    out[written] = '\0';
    return (u8str_t){ .ptr = out, .len = written };
}

/* ---- SubRip and WebVTT ---- */

/* The two are the same shape: an optional identifier, a line with an
   arrow in it, then the text. They differ in the fraction separator,
   which parse_clock already accepts either way, and in WebVTT's header
   and cue settings. */
static void parse_srt_like(proven_arena_t *arena, u8str_t text, cue_buf_t *cues) {
    scanner_t s = { .ptr = text.ptr, .len = text.len, .pos = 0 };

    while (!at_end(&s)) {
        u8str_t line = trim(next_line(&s));
        if (line.len == 0) continue;

        /* WebVTT's own header lines are not cues. */
        if (starts_with_ci(line, "WEBVTT") || starts_with_ci(line, "NOTE") ||
            starts_with_ci(line, "STYLE") || starts_with_ci(line, "REGION")) {
            continue;
        }

        /* Find the arrow. If this line has none it is an identifier, and
           the next one should have it. */
        size_t arrow = SIZE_MAX;
        for (size_t i = 0; i + 2 < line.len; ++i) {
            if (line.ptr[i] == '-' && line.ptr[i + 1] == '-' && line.ptr[i + 2] == '>') { arrow = i; break; }
        }
        if (arrow == SIZE_MAX) {
            if (at_end(&s)) break;
            line = trim(next_line(&s));
            for (size_t i = 0; i + 2 < line.len; ++i) {
                if (line.ptr[i] == '-' && line.ptr[i + 1] == '-' && line.ptr[i + 2] == '>') { arrow = i; break; }
            }
            if (arrow == SIZE_MAX) continue;
        }

        double start = 0.0, end = 0.0;
        u8str_t left = { .ptr = line.ptr, .len = arrow };
        u8str_t right = { .ptr = line.ptr + arrow + 3, .len = line.len - arrow - 3 };

        /* WebVTT allows cue settings after the end time. */
        right = trim(right);
        for (size_t i = 0; i < right.len; ++i) {
            if (right.ptr[i] == ' ' || right.ptr[i] == '\t') { right.len = i; break; }
        }

        bool timed = parse_clock(left, &start) && parse_clock(right, &end);

        /* The text runs to the next blank line whether the timing
           parsed or not — otherwise a bad cue would swallow the one
           after it. */
        char buffer[4096];
        size_t used = 0;
        while (!at_end(&s)) {
            size_t mark = s.pos;
            u8str_t body = next_line(&s);
            if (trim(body).len == 0) break;
            /* A new timing line means the previous cue had no text. */
            bool has_arrow = false;
            for (size_t i = 0; i + 2 < body.len; ++i) {
                if (body.ptr[i] == '-' && body.ptr[i + 1] == '-' && body.ptr[i + 2] == '>') { has_arrow = true; break; }
            }
            if (has_arrow) { s.pos = mark; break; }

            if (used + body.len + 1 < sizeof(buffer)) {
                if (used > 0) buffer[used++] = '\n';
                memcpy(buffer + used, body.ptr, body.len);
                used += body.len;
            }
        }

        if (!timed || used == 0) continue;

        u8str_t raw = { .ptr = buffer, .len = used };
        rubraview_subtitle_cue_t cue = {
            .start_seconds = start,
            .end_seconds = end,
            .text = strip_markup(arena, raw, false),
            .language = { .ptr = "", .len = 0 },
        };
        cue_push(arena, cues, cue);
    }
}

/* ---- SAMI ---- */

/* SAMI is HTML: `<SYNC Start=1234>` opens a caption and the next SYNC
   closes it. The text between them is usually `<P Class=KRCC>...`, and
   the class is the only place the language is written. */
static void parse_smi(proven_arena_t *arena, u8str_t text, cue_buf_t *cues) {
    size_t i = 0;
    double open_at = -1.0;
    size_t body_start = 0;
    u8str_t language = { .ptr = "", .len = 0 };

    while (i < text.len) {
        if (text.ptr[i] != '<') { i++; continue; }

        u8str_t rest = { .ptr = text.ptr + i, .len = text.len - i };
        if (!starts_with_ci(rest, "<sync")) { i++; continue; }

        /* Close the caption that was open. */
        if (open_at >= 0.0 && i > body_start) {
            u8str_t raw = { .ptr = text.ptr + body_start, .len = i - body_start };
            u8str_t body = strip_markup(arena, raw, false);
            if (body.len > 0) {
                /* SAMI marks an empty caption with `&nbsp;`, which means
                   "nothing is showing now" rather than a caption. */
                bool blank = body.len <= 6 && (body.len == 0 || body.ptr[0] == '&');
                if (!blank) {
                    rubraview_subtitle_cue_t cue = {
                        .start_seconds = open_at,
                        /* SAMI has no end time: a caption lasts until the
                           next SYNC. The end is filled in below. */
                        .end_seconds = open_at + 0.001,
                        .text = body,
                        .language = language,
                    };
                    cue_push(arena, cues, cue);
                }
            }
        }

        /* Read `Start=NNNN`, in milliseconds. */
        size_t tag_end = i;
        while (tag_end < text.len && text.ptr[tag_end] != '>') tag_end++;

        double start_ms = -1.0;
        for (size_t k = i; k + 6 < tag_end; ++k) {
            u8str_t at = { .ptr = text.ptr + k, .len = tag_end - k };
            if (!starts_with_ci(at, "start")) continue;
            size_t v = k + 5;
            while (v < tag_end && (text.ptr[v] == ' ' || text.ptr[v] == '=' || text.ptr[v] == '"')) v++;
            char number[24];
            size_t n = 0;
            while (v < tag_end && text.ptr[v] >= '0' && text.ptr[v] <= '9' && n + 1 < sizeof(number)) {
                number[n++] = text.ptr[v++];
            }
            if (n > 0) { number[n] = '\0'; start_ms = strtod(number, NULL); }
            break;
        }

        /* The class on the <P> that follows names the language. The
           search starts past this tag's own '>' and stops at the next
           SYNC, so a caption with no class does not borrow the next
           one's. */
        for (size_t look = tag_end + 1; look < text.len && look < tag_end + 200; ++look) {
            u8str_t at = { .ptr = text.ptr + look, .len = text.len - look };
            if (starts_with_ci(at, "<sync")) break;
            if (!starts_with_ci(at, "class")) continue;

            size_t v = look + 5;
            while (v < text.len && (text.ptr[v] == ' ' || text.ptr[v] == '=' || text.ptr[v] == '"')) v++;
            size_t start = v;
            while (v < text.len && text.ptr[v] != '>' && text.ptr[v] != ' ' && text.ptr[v] != '"') v++;
            language = (u8str_t){ .ptr = text.ptr + start, .len = v - start };
            break;
        }

        open_at = start_ms >= 0.0 ? start_ms / 1000.0 : -1.0;
        body_start = tag_end < text.len ? tag_end + 1 : text.len;
        i = body_start;
    }

    /* A SAMI caption ends where the next one starts. */
    for (size_t k = 0; k + 1 < cues->count; ++k) {
        cues->data[k].end_seconds = cues->data[k + 1].start_seconds;
    }
    if (cues->count > 0) {
        rubraview_subtitle_cue_t *last = &cues->data[cues->count - 1];
        if (last->end_seconds <= last->start_seconds) last->end_seconds = last->start_seconds + 3.0;
    }
}

/* ---- SubStation Alpha ---- */

/* `Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,the text`, and the
   text is everything after the ninth comma — which is why the field
   count matters rather than just splitting. */
static void parse_ass(proven_arena_t *arena, u8str_t text, cue_buf_t *cues) {
    scanner_t s = { .ptr = text.ptr, .len = text.len, .pos = 0 };

    while (!at_end(&s)) {
        u8str_t line = next_line(&s);
        if (!starts_with_ci(trim(line), "dialogue:")) continue;

        u8str_t rest = trim(line);
        rest.ptr += 9;   /* past "Dialogue:" */
        rest.len -= 9;

        u8str_t fields[10];
        size_t field_count = 0;
        size_t start = 0;
        for (size_t i = 0; i < rest.len && field_count < 9; ++i) {
            if (rest.ptr[i] != ',') continue;
            fields[field_count++] = (u8str_t){ .ptr = rest.ptr + start, .len = i - start };
            start = i + 1;
        }
        if (field_count < 9) continue;

        /* Everything after the ninth comma is the text, commas and all. */
        u8str_t body = { .ptr = rest.ptr + start, .len = rest.len - start };

        double begin = 0.0, finish = 0.0;
        if (!parse_clock(fields[1], &begin) || !parse_clock(fields[2], &finish)) continue;

        rubraview_subtitle_cue_t cue = {
            .start_seconds = begin,
            .end_seconds = finish,
            .text = strip_markup(arena, body, true),
            .language = { .ptr = "", .len = 0 },
        };
        if (cue.text.len > 0) cue_push(arena, cues, cue);
    }
}

/* ---- entry points ---- */

rubraview_subtitle_format_t rubraview_subtitle_format_for_name(u8str_t filename) {
    u8str_t ext = rubraview_path_ext(filename);
    if (ext.len > 0 && ext.ptr[0] == '.') { ext.ptr++; ext.len--; }

    if (starts_with_ci(ext, "srt") && ext.len == 3) return RUBRAVIEW_SUBTITLE_SRT;
    if (starts_with_ci(ext, "smi") && ext.len == 3) return RUBRAVIEW_SUBTITLE_SMI;
    if (starts_with_ci(ext, "sami") && ext.len == 4) return RUBRAVIEW_SUBTITLE_SMI;
    if (starts_with_ci(ext, "vtt") && ext.len == 3) return RUBRAVIEW_SUBTITLE_VTT;
    if (starts_with_ci(ext, "ass") && ext.len == 3) return RUBRAVIEW_SUBTITLE_ASS;
    if (starts_with_ci(ext, "ssa") && ext.len == 3) return RUBRAVIEW_SUBTITLE_ASS;
    return RUBRAVIEW_SUBTITLE_UNKNOWN;
}

/* When the name does not say, the content does: each format has an
   opening that no other has. */
static rubraview_subtitle_format_t sniff(u8str_t text) {
    if (text.len == 0) return RUBRAVIEW_SUBTITLE_UNKNOWN;

    for (size_t i = 0; i + 6 < text.len && i < 4096; ++i) {
        u8str_t at = { .ptr = text.ptr + i, .len = text.len - i };
        if (starts_with_ci(at, "WEBVTT")) return RUBRAVIEW_SUBTITLE_VTT;
        if (starts_with_ci(at, "<SAMI")) return RUBRAVIEW_SUBTITLE_SMI;
        if (starts_with_ci(at, "<SYNC")) return RUBRAVIEW_SUBTITLE_SMI;
        if (starts_with_ci(at, "[Script Info]")) return RUBRAVIEW_SUBTITLE_ASS;
        if (starts_with_ci(at, "Dialogue:")) return RUBRAVIEW_SUBTITLE_ASS;
        if (starts_with_ci(at, "-->")) return RUBRAVIEW_SUBTITLE_SRT;
    }
    return RUBRAVIEW_SUBTITLE_UNKNOWN;
}

/* Insertion sort by start time: subtitle files are almost always
   already in order, which is the case this is fastest on. */
static void sort_cues(rubraview_subtitle_cue_t *cues, size_t count) {
    for (size_t i = 1; i < count; ++i) {
        rubraview_subtitle_cue_t key = cues[i];
        size_t j = i;
        while (j > 0 && cues[j - 1].start_seconds > key.start_seconds) {
            cues[j] = cues[j - 1];
            j--;
        }
        cues[j] = key;
    }
}

rubraview_subtitle_track_t rubraview_subtitle_parse(proven_arena_t *arena, u8str_t text,
                                                    rubraview_subtitle_format_t format) {
    rubraview_subtitle_track_t track = {0};
    if (!arena || text.len == 0) return track;

    if (format == RUBRAVIEW_SUBTITLE_UNKNOWN) format = sniff(text);
    track.format = format;

    cue_buf_t cues = {0};
    switch (format) {
        case RUBRAVIEW_SUBTITLE_SMI: parse_smi(arena, text, &cues); break;
        case RUBRAVIEW_SUBTITLE_ASS: parse_ass(arena, text, &cues); break;
        case RUBRAVIEW_SUBTITLE_SRT:
        case RUBRAVIEW_SUBTITLE_VTT: parse_srt_like(arena, text, &cues); break;
        default: return track;
    }

    sort_cues(cues.data, cues.count);
    track.cues = cues.data;
    track.count = cues.count;
    if (cues.count > 0) track.language = cues.data[0].language;
    return track;
}

const rubraview_subtitle_cue_t *rubraview_subtitle_at(const rubraview_subtitle_track_t *track,
                                                      double time_seconds) {
    if (!track || track->count == 0) return NULL;

    double t = time_seconds - track->offset_seconds;

    /* Cues are sorted and rarely overlap, so a binary search finds the
       last one that has started, and then it is one comparison to know
       whether it is still showing. */
    size_t low = 0, high = track->count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (track->cues[mid].start_seconds <= t) low = mid + 1;
        else high = mid;
    }
    if (low == 0) return NULL;

    const rubraview_subtitle_cue_t *cue = &track->cues[low - 1];
    return t < cue->end_seconds ? cue : NULL;
}

void rubraview_subtitle_nudge(rubraview_subtitle_track_t *track, bool later) {
    if (!track) return;
    track->offset_seconds += later ? 0.5 : -0.5;
}

u8str_t rubraview_subtitle_language_tag(u8str_t video_path, u8str_t subtitle_path) {
    u8str_t none = { .ptr = "", .len = 0 };
    u8str_t stem = rubraview_path_stem(rubraview_path_basename(video_path));
    u8str_t name = rubraview_path_basename(subtitle_path);
    if (stem.len == 0 || name.len <= stem.len + 1) return none;
    if (memcmp(name.ptr, stem.ptr, stem.len) != 0 || name.ptr[stem.len] != '.') return none;

    /* What is left is either `srt` — the extension, and no language —
       or `kor.srt`, whose first part is the tag. */
    u8str_t rest = { .ptr = name.ptr + stem.len + 1, .len = name.len - stem.len - 1 };
    for (size_t i = 0; i < rest.len; ++i) {
        if (rest.ptr[i] == '.') return (u8str_t){ .ptr = rest.ptr, .len = i };
    }
    return none;
}

size_t rubraview_subtitle_discover(u8str_t video_path,
                                   const u8str_t *sibling_paths, size_t sibling_count,
                                   rubraview_subtitle_candidate_t *out, size_t capacity) {
    if (!sibling_paths || sibling_count == 0) return 0;

    u8str_t stem = rubraview_path_stem(rubraview_path_basename(video_path));
    if (stem.len == 0) return 0;

    size_t found = 0;
    for (size_t i = 0; i < sibling_count; ++i) {
        u8str_t name = rubraview_path_basename(sibling_paths[i]);
        rubraview_subtitle_format_t format = rubraview_subtitle_format_for_name(name);
        if (format == RUBRAVIEW_SUBTITLE_UNKNOWN) continue;

        /* `movie.srt` and `movie.kor.srt` both belong to `movie.mkv`:
           the name has to *begin* with the stem, not equal it, because
           the language tag lives between the stem and the extension. */
        if (name.len < stem.len) continue;
        if (memcmp(name.ptr, stem.ptr, stem.len) != 0) continue;
        if (name.len > stem.len && name.ptr[stem.len] != '.') continue;

        if (out && found < capacity) {
            out[found] = (rubraview_subtitle_candidate_t){ .path = sibling_paths[i], .format = format };
        }
        found++;
    }
    return found;
}
