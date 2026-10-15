#include "rubraview/subtitle.h"
#include "rubraview/number.h"
#include "rubraview/path.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

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

/* ---- cue list ---- */

typedef struct cue_buf {
    rubraview_subtitle_cue_t *data;
    size_t count, capacity;
} cue_buf_t;

static bool cue_push(proven_arena_t *arena, cue_buf_t *b, rubraview_subtitle_cue_t cue) {
    if (cue.end_seconds <= cue.start_seconds) return false;   /* a cue with no duration */

    if (b->count >= b->capacity) {
        size_t new_cap = b->capacity == 0 ? 64 : b->capacity * 2;
        proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, new_cap, sizeof(*b->data));
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
static u8str_t strip_markup_as(proven_arena_t *arena, u8str_t text, bool braces_too, bool html);

static u8str_t strip_markup(proven_arena_t *arena, u8str_t text, bool braces_too) {
    return strip_markup_as(arena, text, braces_too, false);
}

/* Is this `<p ...>` or `</p>` — and not `<pre>` or another tag that begins with p? */
static bool is_paragraph_tag(u8str_t rest) {
    size_t at = (rest.len > 1 && rest.ptr[1] == '/') ? 2 : 1;
    if (rest.len <= at + 1 || (rest.ptr[at] != 'p' && rest.ptr[at] != 'P')) return false;
    char after = rest.ptr[at + 1];
    return after == '>' || after == ' ' || after == '\t' || after == '\r' || after == '\n' || after == '/';
}

/* `html`: SAMI is HTML, where the file's own line ends, tabs and runs of
   spaces are one space and only `<br>` — or a new `<p>` — starts a line
   (owner, 2026-10-11: captions stood a blank line apart, because the line
   end after each `<br>` was kept as a second one). */
static u8str_t strip_markup_as(proven_arena_t *arena, u8str_t text, bool braces_too, bool html) {
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
            if (html) {
                if (rubraview_u8_starts_with_ci(rest, "<br") || is_paragraph_tag(rest)) {
                    while (written > 0 && out[written - 1] == ' ') written--;
                    /* No line before the first words, and one break however many tags ask. */
                    if (written > 0 && out[written - 1] != '\n') out[written++] = '\n';
                }
            } else if (rubraview_u8_starts_with_ci(rest, "<br")) {
                if (written == 0 || out[written - 1] != '\n') out[written++] = '\n';
            }
            while (i < text.len && text.ptr[i] != '>') i++;
            continue;
        }
        if (html && (text.ptr[i] == '\n' || text.ptr[i] == '\r' || text.ptr[i] == '\t' || text.ptr[i] == ' ')) {
            if (written > 0 && out[written - 1] != '\n' && out[written - 1] != ' ') out[written++] = ' ';
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
        size_t line_pos = s.pos;   /* RV-115: where the cue's block begins, its identifier or its timing */
        u8str_t line = trim(next_line(&s));
        if (line.len == 0) continue;

        /* WebVTT's own header lines are not cues. */
        if (rubraview_u8_starts_with_ci(line, "WEBVTT") || rubraview_u8_starts_with_ci(line, "NOTE") ||
            rubraview_u8_starts_with_ci(line, "STYLE") || rubraview_u8_starts_with_ci(line, "REGION")) {
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
        left = trim(left);
        rubraview_subtitle_place_t place = {
            .start_at = (size_t)(left.ptr - text.ptr), .start_len = left.len,
            .end_at = (size_t)(right.ptr - text.ptr), .end_len = right.len,
        };
        const char *body_first = NULL, *body_end = NULL;
        bool body_whole = true;

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
                if (!body_first) body_first = body.ptr;
                body_end = body.ptr + body.len;
            } else {
                body_whole = false;   /* longer than is read: shown cut, and not to be written back */
            }
        }
        if (body_first && body_whole) {
            place.text_at = (size_t)(body_first - text.ptr);
            place.text_len = (size_t)(body_end - body_first);
        }
        place.block_at = line_pos;
        place.block_len = s.pos - line_pos;   /* through the blank line after it */

        if (!timed || used == 0) continue;

        u8str_t raw = { .ptr = buffer, .len = used };
        rubraview_subtitle_cue_t cue = {
            .start_seconds = start,
            .end_seconds = end,
            .text = strip_markup(arena, raw, false),
            .language = { .ptr = "", .len = 0 },
            .place = place,
        };
        cue_push(arena, cues, cue);
    }
}

/* ---- SAMI ---- */

/* SAMI is HTML: `<SYNC Start=1234>` opens a caption and the next SYNC
   closes it. The text between them is usually `<P Class=KRCC>...`, and
   the class is the only place the language is written. */
static void sort_cues(rubraview_subtitle_cue_t *cues, size_t count);

/* Where a caption's words stand between its SYNC and the next: after the
   `<P ...>` that opens it, to the last thing that is not white space. */
static rubraview_subtitle_place_t smi_place(u8str_t text, size_t from, size_t to, size_t digits, size_t digits_len) {
    rubraview_subtitle_place_t place = { .start_at = digits, .start_len = digits_len };
    size_t at = from;
    while (at < to && (text.ptr[at] == ' ' || text.ptr[at] == '\t' || text.ptr[at] == '\r' || text.ptr[at] == '\n')) at++;
    if (at < to && text.ptr[at] == '<' && is_paragraph_tag((u8str_t){ .ptr = text.ptr + at, .len = to - at })) {
        while (at < to && text.ptr[at] != '>') at++;
        if (at < to) at++;
    }
    size_t end = to;
    while (end > at && (text.ptr[end - 1] == ' ' || text.ptr[end - 1] == '\t' || text.ptr[end - 1] == '\r' || text.ptr[end - 1] == '\n')) end--;
    place.text_at = at;
    place.text_len = end - at;
    return place;
}

static void parse_smi(proven_arena_t *arena, u8str_t text, cue_buf_t *cues) {
    size_t i = 0;
    double open_at = -1.0;
    size_t body_start = 0;
    size_t open_digits = 0, open_digits_len = 0;   /* the number of the SYNC that is open */
    size_t open_sync = 0;                          /* RV-115: where that SYNC's tag begins */
    u8str_t language = { .ptr = "", .len = 0 };

    while (i < text.len) {
        if (text.ptr[i] != '<') { i++; continue; }

        u8str_t rest = { .ptr = text.ptr + i, .len = text.len - i };
        if (!rubraview_u8_starts_with_ci(rest, "<sync")) { i++; continue; }

        /* Close the caption that was open. */
        if (open_at >= 0.0 && i > body_start) {
            u8str_t raw = { .ptr = text.ptr + body_start, .len = i - body_start };
            u8str_t body = strip_markup_as(arena, raw, false, true);
            if (body.len > 0) {
                /* SAMI marks an empty caption with `&nbsp;`, which means
                   "nothing is showing now" rather than a caption. It is
                   kept for now, with no text: it is where the caption
                   before it ends. The blanks are dropped below. */
                bool blank = body.len <= 6 && body.ptr[0] == '&';
                rubraview_subtitle_cue_t cue = {
                    .start_seconds = open_at,
                    /* SAMI has no end time: a caption lasts until the
                       next one of its language. Filled in below. */
                    .end_seconds = open_at + 0.001,
                    .text = blank ? (u8str_t){ .ptr = body.ptr, .len = 0 } : body,
                    .language = language,
                    .place = smi_place(text, body_start, i, open_digits, open_digits_len),
                };
                cue.place.block_at = open_sync;
                cue.place.block_len = i - open_sync;
                cue_push(arena, cues, cue);
            }
        }

        /* Read `Start=NNNN`, in milliseconds. */
        size_t tag_end = i;
        while (tag_end < text.len && text.ptr[tag_end] != '>') tag_end++;

        double start_ms = -1.0;
        for (size_t k = i; k + 6 < tag_end; ++k) {
            u8str_t at = { .ptr = text.ptr + k, .len = tag_end - k };
            if (!rubraview_u8_starts_with_ci(at, "start")) continue;
            size_t v = k + 5;
            while (v < tag_end && (text.ptr[v] == ' ' || text.ptr[v] == '=' || text.ptr[v] == '"')) v++;
            size_t digits = v;
            while (v < tag_end && text.ptr[v] >= '0' && text.ptr[v] <= '9') v++;
            double ms = 0.0;
            if (v > digits && rubraview_parse_double((u8str_t){ .ptr = text.ptr + digits, .len = v - digits }, &ms)) {
                start_ms = ms;
                open_digits = digits;
                open_digits_len = v - digits;
            }
            break;
        }

        /* The class on the <P> that follows names the language. The
           search starts past this tag's own '>' and stops at the next
           SYNC, so a caption with no class does not borrow the next
           one's. */
        for (size_t look = tag_end + 1; look < text.len && look < tag_end + 200; ++look) {
            u8str_t at = { .ptr = text.ptr + look, .len = text.len - look };
            if (rubraview_u8_starts_with_ci(at, "<sync")) break;
            if (!rubraview_u8_starts_with_ci(at, "class")) continue;

            size_t v = look + 5;
            while (v < text.len && (text.ptr[v] == ' ' || text.ptr[v] == '=' || text.ptr[v] == '"')) v++;
            size_t start = v;
            while (v < text.len && text.ptr[v] != '>' && text.ptr[v] != ' ' && text.ptr[v] != '"') v++;
            language = (u8str_t){ .ptr = text.ptr + start, .len = v - start };
            break;
        }

        open_at = start_ms >= 0.0 ? start_ms / 1000.0 : -1.0;
        open_sync = i;
        body_start = tag_end < text.len ? tag_end + 1 : text.len;
        i = body_start;
    }

    /* The last caption has no SYNC after it to close it (2026-09-23):
       without this the file's final caption was dropped, and a final
       `&nbsp;` never said where the one before it stops. */
    if (open_at >= 0.0 && text.len > body_start) {
        /* the last caption stops where the body does: its words and its block are not the file's closing tags */
        size_t stop = text.len;
        for (size_t k = body_start; k + 6 <= text.len; ++k) {
            if (text.ptr[k] == '<' && rubraview_u8_starts_with_ci((u8str_t){ .ptr = text.ptr + k, .len = text.len - k }, "</body")) { stop = k; break; }
        }
        u8str_t raw = { .ptr = text.ptr + body_start, .len = text.len - body_start };
        u8str_t body = strip_markup_as(arena, raw, false, true);
        if (body.len > 0) {
            bool blank = body.len <= 6 && body.ptr[0] == '&';
            rubraview_subtitle_cue_t cue = {
                .start_seconds = open_at,
                .end_seconds = open_at + 0.001,
                .text = blank ? (u8str_t){ .ptr = body.ptr, .len = 0 } : body,
                .language = language,
                .place = smi_place(text, body_start, stop, open_digits, open_digits_len),
            };
            cue.place.block_at = open_sync;
            cue.place.block_len = stop > open_sync ? stop - open_sync : 0;
            cue_push(arena, cues, cue);
        }
    }

    /* In time order first: a file is not obliged to be written in it
       (owner, 2026-09-23), and what follows asks "which caption comes
       next in this language", which only means something in order. */
    sort_cues(cues->data, cues->count);

    /* A SAMI caption ends where the next one of *its own language*
       starts (owner, 2026-09-23). One file often carries several
       languages, a class each, interleaved at the same moments; ending
       each caption at the next of any class gave every one of them a
       length of zero, and nothing was ever shown. */
    for (size_t k = 0; k < cues->count; ++k) {
        rubraview_subtitle_cue_t *cue = &cues->data[k];
        double end = -1.0;
        for (size_t n = k + 1; n < cues->count; ++n) {
            const rubraview_subtitle_cue_t *later = &cues->data[n];
            if (later->language.len != cue->language.len) continue;
            if (later->language.len > 0 && memcmp(later->language.ptr, cue->language.ptr, cue->language.len) != 0) continue;
            end = later->start_seconds;
            /* An empty caption does nothing but close this one: its number is this one's end. */
            if (later->text.len == 0) {
                cue->place.end_at = later->place.start_at;
                cue->place.end_len = later->place.start_len;
                cue->place.close_at = later->place.block_at;   /* RV-115: it goes with this one */
                cue->place.close_len = later->place.block_len;
            }
            break;
        }
        cue->end_seconds = end > cue->start_seconds ? end : cue->start_seconds + 3.0;
    }

    /* The blanks have done their work: they said where a caption stops. */
    size_t kept = 0;
    for (size_t k = 0; k < cues->count; ++k) {
        if (cues->data[k].text.len == 0) continue;
        cues->data[kept++] = cues->data[k];
    }
    cues->count = kept;
}

/* ---- SubStation Alpha ---- */

/* `Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,the text`, and the
   text is everything after the ninth comma — which is why the field
   count matters rather than just splitting. */
static void parse_ass(proven_arena_t *arena, u8str_t text, cue_buf_t *cues) {
    scanner_t s = { .ptr = text.ptr, .len = text.len, .pos = 0 };

    while (!at_end(&s)) {
        size_t line_pos = s.pos;
        u8str_t line = next_line(&s);
        if (!rubraview_u8_starts_with_ci(trim(line), "dialogue:")) continue;

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
            .place = {
                .start_at = (size_t)(trim(fields[1]).ptr - text.ptr), .start_len = trim(fields[1]).len,
                .end_at = (size_t)(trim(fields[2]).ptr - text.ptr), .end_len = trim(fields[2]).len,
                .text_at = (size_t)(body.ptr - text.ptr), .text_len = body.len,
                .block_at = line_pos, .block_len = s.pos - line_pos,   /* the line and its line end */
            },
        };
        if (cue.text.len > 0) cue_push(arena, cues, cue);
    }
}

/* ---- entry points ---- */

rubraview_subtitle_format_t rubraview_subtitle_format_for_name(u8str_t filename) {
    u8str_t ext = rubraview_path_ext(filename);
    if (ext.len > 0 && ext.ptr[0] == '.') { ext.ptr++; ext.len--; }

    if (rubraview_u8_starts_with_ci(ext, "srt") && ext.len == 3) return RUBRAVIEW_SUBTITLE_SRT;
    if (rubraview_u8_starts_with_ci(ext, "smi") && ext.len == 3) return RUBRAVIEW_SUBTITLE_SMI;
    if (rubraview_u8_starts_with_ci(ext, "sami") && ext.len == 4) return RUBRAVIEW_SUBTITLE_SMI;
    if (rubraview_u8_starts_with_ci(ext, "vtt") && ext.len == 3) return RUBRAVIEW_SUBTITLE_VTT;
    if (rubraview_u8_starts_with_ci(ext, "ass") && ext.len == 3) return RUBRAVIEW_SUBTITLE_ASS;
    if (rubraview_u8_starts_with_ci(ext, "ssa") && ext.len == 3) return RUBRAVIEW_SUBTITLE_ASS;
    if (rubraview_u8_starts_with_ci(ext, "idx") && ext.len == 3) return RUBRAVIEW_SUBTITLE_VOBSUB;
    if (rubraview_u8_starts_with_ci(ext, "sup") && ext.len == 3) return RUBRAVIEW_SUBTITLE_PGS;
    return RUBRAVIEW_SUBTITLE_UNKNOWN;
}

/* When the name does not say, the content does: each format has an
   opening that no other has. */
static rubraview_subtitle_format_t sniff(u8str_t text) {
    if (text.len == 0) return RUBRAVIEW_SUBTITLE_UNKNOWN;

    for (size_t i = 0; i + 6 < text.len && i < 4096; ++i) {
        u8str_t at = { .ptr = text.ptr + i, .len = text.len - i };
        if (rubraview_u8_starts_with_ci(at, "WEBVTT")) return RUBRAVIEW_SUBTITLE_VTT;
        if (rubraview_u8_starts_with_ci(at, "<SAMI")) return RUBRAVIEW_SUBTITLE_SMI;
        if (rubraview_u8_starts_with_ci(at, "<SYNC")) return RUBRAVIEW_SUBTITLE_SMI;
        if (rubraview_u8_starts_with_ci(at, "[Script Info]")) return RUBRAVIEW_SUBTITLE_ASS;
        if (rubraview_u8_starts_with_ci(at, "Dialogue:")) return RUBRAVIEW_SUBTITLE_ASS;
        if (rubraview_u8_starts_with_ci(at, "-->")) return RUBRAVIEW_SUBTITLE_SRT;
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
    track.source = text;
    if (cues.count > 0) track.language = cues.data[0].language;
    return track;
}

size_t rubraview_subtitle_languages(const rubraview_subtitle_track_t *track, u8str_t *out, size_t max) {
    if (!track || !out || max == 0) return 0;
    size_t count = 0;
    for (size_t i = 0; i < track->count && count < max; ++i) {
        u8str_t language = track->cues[i].language;
        bool seen = false;
        for (size_t k = 0; k < count && !seen; ++k) {
            seen = out[k].len == language.len &&
                   (language.len == 0 || memcmp(out[k].ptr, language.ptr, language.len) == 0);
        }
        if (!seen) out[count++] = language;
    }
    return count;
}

static bool cue_is_shown(const rubraview_subtitle_track_t *track, const rubraview_subtitle_cue_t *cue) {
    if (cue->removed) return false;   /* RV-115: taken out, kept only for the save */
    if (track->shown_language.len == 0) return true;
    return cue->language.len == track->shown_language.len &&
           memcmp(cue->language.ptr, track->shown_language.ptr, cue->language.len) == 0;
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

    /* Back over the cues that belong to another language: in a SAMI file
       every language has a cue at the same moment, and only one of them
       is the reader's (owner, 2026-09-23). */
    for (size_t i = low; i > 0; --i) {
        const rubraview_subtitle_cue_t *cue = &track->cues[i - 1];
        if (!cue_is_shown(track, cue)) continue;
        return t < cue->end_seconds ? cue : NULL;
    }
    return NULL;
}

void rubraview_subtitle_nudge(rubraview_subtitle_track_t *track, bool later) {
    rubraview_subtitle_shift(track, later ? 0.5 : -0.5);
}

void rubraview_subtitle_shift(rubraview_subtitle_track_t *track, double seconds) {
    if (!track) return;
    /* To the millisecond: ten steps of 0.1 are one second, not 0.9999999. */
    track->offset_seconds = (double)llround((track->offset_seconds + seconds) * 1000.0) / 1000.0;
}

void rubraview_subtitle_sync_reset(rubraview_subtitle_track_t *track) {
    if (track) track->offset_seconds = 0.0;
}

u8str_t rubraview_subtitle_offset_text(char *buffer, size_t buffer_size, double offset_seconds) {
    if (!buffer || buffer_size == 0) return (u8str_t){ .ptr = "", .len = 0 };
    buffer[0] = '\0';
    if (fabs(offset_seconds) < 0.0005) return (u8str_t){ .ptr = buffer, .len = 0 };
    int n = snprintf(buffer, buffer_size, "sub %+.1f s", offset_seconds);
    if (n < 0) { buffer[0] = '\0'; n = 0; }
    size_t len = (size_t)n < buffer_size ? (size_t)n : buffer_size - 1;
    return (u8str_t){ .ptr = buffer, .len = len };
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

/* `film.old3.smi`: the file a save kept (RV-115). It is the subtitle as
   it was, not one to offer beside the one that took its name. */
static bool is_kept_copy(u8str_t name) {
    u8str_t ext = rubraview_path_ext(name);
    size_t end = name.len - ext.len, at = end;
    while (at > 0 && name.ptr[at - 1] >= '0' && name.ptr[at - 1] <= '9') at--;
    return at < end && at >= 4 && memcmp(name.ptr + at - 4, ".old", 4) == 0;
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
        if (is_kept_copy(name)) continue;

        if (out && found < capacity) {
            out[found] = (rubraview_subtitle_candidate_t){ .path = sibling_paths[i], .format = format };
        }
        found++;
    }
    return found;
}

/* ---- writing back (RV-115, D-126) ---- */

static u8str_t arena_copy(proven_arena_t *arena, const char *ptr, size_t len) {
    u8str_t empty = { .ptr = "", .len = 0 };
    proven_result_mem_mut_t res = proven_arena_alloc(arena, len + 1);
    if (!proven_is_ok(res.err)) return empty;
    if (len > 0) memcpy(res.value.ptr, ptr, len);
    res.value.ptr[len] = '\0';
    return (u8str_t){ .ptr = (const char*)res.value.ptr, .len = len };
}

static bool place_in(const rubraview_subtitle_track_t *track, size_t at, size_t len) {
    return len > 0 && at <= track->source.len && len <= track->source.len - at;
}

u8str_t rubraview_subtitle_cue_typed(proven_arena_t *arena, const rubraview_subtitle_track_t *track, size_t index) {
    u8str_t empty = { .ptr = "", .len = 0 };
    if (!arena || !track || index >= track->count) return empty;
    const rubraview_subtitle_cue_t *cue = &track->cues[index];
    u8str_t raw;
    if (cue->text_changed || cue->added) raw = cue->written;
    else if (place_in(track, cue->place.text_at, cue->place.text_len)) raw = (u8str_t){ track->source.ptr + cue->place.text_at, cue->place.text_len };
    else return empty;

    /* Twice the room: a line end becomes the two characters of `\N`. */
    proven_result_mem_mut_t res = proven_arena_alloc(arena, raw.len * 2 + 1);
    if (!proven_is_ok(res.err)) return empty;
    char *out = (char*)(void*)res.value.ptr;
    size_t n = 0;
    bool lines = track->format == RUBRAVIEW_SUBTITLE_SRT || track->format == RUBRAVIEW_SUBTITLE_VTT;
    for (size_t i = 0; i < raw.len; ++i) {
        char c = raw.ptr[i];
        if (c == '\r') continue;
        if (c == '\n' || c == '\t') {
            if (lines && c == '\n') { out[n++] = '\\'; out[n++] = 'N'; }
            else if (n > 0 && out[n - 1] != ' ') out[n++] = ' ';
            continue;
        }
        out[n++] = c;
    }
    out[n] = '\0';
    return (u8str_t){ .ptr = out, .len = n };
}

static bool retype_cue(proven_arena_t *arena, const rubraview_subtitle_track_t *track, rubraview_subtitle_cue_t *cue, u8str_t typed);

bool rubraview_subtitle_cue_retype(proven_arena_t *arena, rubraview_subtitle_track_t *track, size_t index, u8str_t typed) {
    if (!arena || !track || index >= track->count) return false;
    rubraview_subtitle_cue_t *cue = &track->cues[index];
    if (!cue->added && !place_in(track, cue->place.text_at, cue->place.text_len)) return false;
    return retype_cue(arena, track, cue, typed);
}

/* The typed line written as the file writes a cue's words, and shown as they read. */
static bool retype_cue(proven_arena_t *arena, const rubraview_subtitle_track_t *track, rubraview_subtitle_cue_t *cue, u8str_t typed) {
    typed = trim(typed);
    if (typed.len == 0) return false;

    /* The file's own line end, for the formats whose text has lines. */
    const char *eol = "\n";
    for (size_t i = 0; i + 1 < track->source.len && i < 65536; ++i) {
        if (track->source.ptr[i] == '\n') break;
        if (track->source.ptr[i] == '\r' && track->source.ptr[i + 1] == '\n') { eol = "\r\n"; break; }
    }
    const char *brk = track->format == RUBRAVIEW_SUBTITLE_SMI ? "<br>"
                    : track->format == RUBRAVIEW_SUBTITLE_ASS ? "\\N" : eol;
    size_t brk_len = strlen(brk);

    proven_result_mem_mut_t res = proven_arena_alloc(arena, typed.len * 2 + 1);   /* a break is four bytes at most for two typed */
    if (!proven_is_ok(res.err)) return false;
    char *out = (char*)(void*)res.value.ptr;
    size_t n = 0;
    for (size_t i = 0; i < typed.len; ++i) {
        char c = typed.ptr[i];
        if (c == '\r' || c == '\n') continue;                 /* one line is typed */
        if (c == '\\' && i + 1 < typed.len && typed.ptr[i + 1] == 'N') {
            /* no blank line inside a SubRip cue: it would end it */
            if (n > 0) { memcpy(out + n, brk, brk_len); n += brk_len; }
            i++;
            while (i + 1 < typed.len && typed.ptr[i + 1] == ' ') i++;
            continue;
        }
        out[n++] = c;
    }
    out[n] = '\0';
    u8str_t written = { .ptr = out, .len = n };

    /* What is shown is read as the reader reads it: a SubRip cue's lines joined by a plain line end. */
    u8str_t plain = written;
    if (brk[0] == '\r') {
        proven_result_mem_mut_t plain_res = proven_arena_alloc(arena, n + 1);
        if (!proven_is_ok(plain_res.err)) return false;
        char *to = (char*)(void*)plain_res.value.ptr;
        size_t m = 0;
        for (size_t i = 0; i < n; ++i) if (out[i] != '\r') to[m++] = out[i];
        to[m] = '\0';
        plain = (u8str_t){ .ptr = to, .len = m };
    }
    u8str_t shown = track->format == RUBRAVIEW_SUBTITLE_SMI ? strip_markup_as(arena, plain, false, true)
                  : strip_markup(arena, plain, track->format == RUBRAVIEW_SUBTITLE_ASS);
    if (shown.len == 0) return false;
    cue->written = written;
    cue->text = shown;
    cue->text_changed = true;
    return true;
}

rubraview_subtitle_track_t rubraview_subtitle_offset_folded(proven_arena_t *arena, const rubraview_subtitle_track_t *track) {
    rubraview_subtitle_track_t out = track ? *track : (rubraview_subtitle_track_t){0};
    if (!track || track->offset_seconds == 0.0 || !(track->offset_seconds == track->offset_seconds) || track->count == 0) return out;
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, track->count, sizeof(rubraview_subtitle_cue_t));
    if (!proven_is_ok(res.err)) return (rubraview_subtitle_track_t){0};
    rubraview_subtitle_cue_t *cues = (rubraview_subtitle_cue_t *)res.value.ptr;
    double by = track->offset_seconds;
    for (size_t i = 0; i < track->count; ++i) {
        cues[i] = track->cues[i];
        double start = cues[i].start_seconds + by, end = cues[i].end_seconds + by;
        if (start < 0.0) start = 0.0;   /* not before the beginning; one that ends before it ends there */
        if (end < start) end = start;
        cues[i].start_seconds = start;
        cues[i].end_seconds = end;
        cues[i].time_changed = true;
    }
    out.cues = cues;
    out.offset_seconds = 0.0;
    return out;
}

bool rubraview_subtitle_changed(const rubraview_subtitle_track_t *track) {
    if (!track) return false;
    for (size_t i = 0; i < track->count; ++i) {
        const rubraview_subtitle_cue_t *c = &track->cues[i];
        if (c->added && c->removed) continue;   /* made and taken out again: nothing */
        if (c->time_changed || c->text_changed || c->removed || c->added) return true;
    }
    return false;
}

bool rubraview_subtitle_parse_time(u8str_t text, double *out_seconds) {
    double t = 0.0;
    if (!out_seconds || !parse_clock(text, &t) || !(t >= 0.0) || t > 359999.0) return false;
    *out_seconds = t;
    return true;
}

/* In order of start again after one cue moved; returns where `index` went. */
static size_t resort_one(rubraview_subtitle_track_t *track, size_t index) {
    rubraview_subtitle_cue_t cue = track->cues[index];
    size_t at = index;
    while (at > 0 && track->cues[at - 1].start_seconds > cue.start_seconds) { track->cues[at] = track->cues[at - 1]; at--; }
    if (at == index) {
        while (at + 1 < track->count && track->cues[at + 1].start_seconds < cue.start_seconds) { track->cues[at] = track->cues[at + 1]; at++; }
    }
    track->cues[at] = cue;
    return at;
}

int32_t rubraview_subtitle_cue_set_time(rubraview_subtitle_track_t *track, size_t index, double start, double end) {
    if (!track || index >= track->count || track->cues[index].removed) return -1;
    if (!(start >= 0.0) || !(end > start) || end > 359999.0) return -1;
    rubraview_subtitle_cue_t *cue = &track->cues[index];
    if (cue->start_seconds == start && cue->end_seconds == end) return (int32_t)index;
    cue->start_seconds = start;
    cue->end_seconds = end;
    if (!cue->added) cue->time_changed = true;
    return (int32_t)resort_one(track, index);
}

bool rubraview_subtitle_cue_remove(rubraview_subtitle_track_t *track, size_t index) {
    if (!track || index >= track->count || track->cues[index].removed) return false;
    rubraview_subtitle_cue_t *cue = &track->cues[index];
    if (!cue->added && !place_in(track, cue->place.block_at, cue->place.block_len)) return false;   /* no block to take out */
    cue->removed = true;
    return true;
}

int32_t rubraview_subtitle_cue_add(proven_arena_t *arena, rubraview_subtitle_track_t *track, double start, double end, u8str_t typed) {
    if (!arena || !track || !(start >= 0.0) || !(end > start)) return -1;
    bool some = false;   /* a cue of the file's own to make it from */
    for (size_t i = 0; i < track->count && !some; ++i) {
        some = !track->cues[i].added && place_in(track, track->cues[i].place.block_at, track->cues[i].place.block_len);
    }
    if (!some) return -1;
    rubraview_subtitle_cue_t made = { .start_seconds = start, .end_seconds = end, .added = true,
                                      .language = track->shown_language.len ? track->shown_language
                                                : track->count ? track->cues[0].language : (u8str_t){ .ptr = "", .len = 0 } };
    if (!retype_cue(arena, track, &made, typed)) return -1;
    made.text_changed = false;   /* its words go in with its block */
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, track->count + 1, sizeof(rubraview_subtitle_cue_t));
    if (!proven_is_ok(res.err)) return -1;
    rubraview_subtitle_cue_t *cues = (rubraview_subtitle_cue_t*)(void*)res.value.ptr;
    size_t at = 0;
    while (at < track->count && track->cues[at].start_seconds <= start) at++;
    memcpy(cues, track->cues, at * sizeof(cues[0]));
    cues[at] = made;
    memcpy(cues + at + 1, track->cues + at, (track->count - at) * sizeof(cues[0]));
    track->cues = cues;
    track->count++;
    return (int32_t)at;
}

static u8str_t write_time(proven_arena_t *arena, rubraview_subtitle_format_t format, double seconds);

/* The file's own line end. */
static const char *source_eol(const rubraview_subtitle_track_t *track) {
    for (size_t i = 0; i + 1 < track->source.len && i < 65536; ++i) {
        if (track->source.ptr[i] == '\n') break;
        if (track->source.ptr[i] == '\r' && track->source.ptr[i + 1] == '\n') return "\r\n";
    }
    return "\n";
}

typedef struct swap { size_t at, len; u8str_t with; } swap_t;

/* A piece of the file with some of its places written over. */
static bool copy_swapped(char *out, size_t *n, size_t cap, u8str_t source, size_t from, size_t len, swap_t *swaps, size_t count) {
    for (size_t i = 1; i < count; ++i) {   /* in order of place */
        swap_t key = swaps[i];
        size_t k = i;
        while (k > 0 && swaps[k - 1].at > key.at) { swaps[k] = swaps[k - 1]; k--; }
        swaps[k] = key;
    }
    size_t at = from;
    for (size_t i = 0; i < count; ++i) {
        if (swaps[i].len == 0 || swaps[i].at < at || swaps[i].at + swaps[i].len > from + len) continue;   /* not in this piece */
        size_t plain = swaps[i].at - at;
        if (*n + plain + swaps[i].with.len > cap) return false;
        memcpy(out + *n, source.ptr + at, plain); *n += plain;
        memcpy(out + *n, swaps[i].with.ptr, swaps[i].with.len); *n += swaps[i].with.len;
        at = swaps[i].at + swaps[i].len;
    }
    if (*n + (from + len - at) > cap) return false;
    memcpy(out + *n, source.ptr + at, from + len - at);
    *n += from + len - at;
    return true;
}

static bool ends_blank(const char *p, size_t n) {   /* a blank line at the end: "\n\n" or "\n\r\n" */
    return (n >= 2 && p[n - 1] == '\n' && p[n - 2] == '\n') || (n >= 3 && p[n - 1] == '\n' && p[n - 2] == '\r' && p[n - 3] == '\n');
}

/* RV-115: an added cue's block, made from the neighbour `tpl` (the cue before it when `after`, else the one after):
   its times and its words in their places, set apart from the blocks around it as the format wants. */
static u8str_t added_block(proven_arena_t *arena, const rubraview_subtitle_track_t *track, const rubraview_subtitle_cue_t *tpl,
                           const rubraview_subtitle_cue_t *cue, bool after, u8str_t words) {
    u8str_t empty = { .ptr = "", .len = 0 };
    const char *eol = source_eol(track);
    size_t eol_len = strlen(eol);
    u8str_t start = write_time(arena, track->format, cue->start_seconds);
    u8str_t end = write_time(arena, track->format, cue->end_seconds);
    const rubraview_subtitle_place_t *pl = &tpl->place;
    size_t cap = pl->block_len + pl->close_len + start.len + end.len + words.len + eol_len * 4 + 16;
    proven_result_mem_mut_t res = proven_arena_alloc(arena, cap);
    if (!proven_is_ok(res.err) || start.len == 0 || end.len == 0) return empty;
    char *out = (char*)(void*)res.value.ptr;
    size_t n = 0;
    bool lines = track->format == RUBRAVIEW_SUBTITLE_SRT || track->format == RUBRAVIEW_SUBTITLE_VTT;
    if (after && (lines || track->format == RUBRAVIEW_SUBTITLE_ASS)) {
        /* the block before may be the file's last, with no line end or no blank line after it */
        const char *b = track->source.ptr + pl->block_at;
        size_t bl = pl->block_len;
        bool line_ended = bl > 0 && b[bl - 1] == '\n';
        if (!line_ended) { memcpy(out + n, eol, eol_len); n += eol_len; }
        if (lines && !ends_blank(b, bl)) { memcpy(out + n, eol, eol_len); n += eol_len; }
    }
    swap_t swaps[3] = { { pl->start_at, pl->start_len, start }, { pl->end_at, pl->end_len, end },
                        { pl->text_at, words.len ? pl->text_len : 0, words } };   /* no words given: its own stay */
    if (!copy_swapped(out, &n, cap, track->source, pl->block_at, pl->block_len, swaps, 3)) return empty;
    /* (a SAMI caption's closing SYNC is `added_close`: it goes where its own time does) */
    if ((lines || track->format == RUBRAVIEW_SUBTITLE_ASS) && (n == 0 || out[n - 1] != '\n')) {
        if (n + eol_len > cap) return empty;
        memcpy(out + n, eol, eol_len); n += eol_len;
    }
    if (lines && !ends_blank(out, n)) {
        if (n + eol_len > cap) return empty;
        memcpy(out + n, eol, eol_len); n += eol_len;
    }
    return (u8str_t){ .ptr = out, .len = n };
}

static u8str_t write_time(proven_arena_t *arena, rubraview_subtitle_format_t format, double seconds) {
    char buf[40];
    if (!(seconds > 0.0)) seconds = 0.0;
    if (seconds > 359999.0) seconds = 359999.0;
    long long ms = (long long)(seconds * 1000.0 + 0.5);
    int n;
    if (format == RUBRAVIEW_SUBTITLE_SMI) {
        n = snprintf(buf, sizeof(buf), "%lld", ms);
    } else if (format == RUBRAVIEW_SUBTITLE_ASS) {
        long long cs = (long long)(seconds * 100.0 + 0.5);
        n = snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld.%02lld", cs / 360000, cs / 6000 % 60, cs / 100 % 60, cs % 100);
    } else {
        n = snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld%c%03lld", ms / 3600000, ms / 60000 % 60, ms / 1000 % 60,
                     format == RUBRAVIEW_SUBTITLE_VTT ? '.' : ',', ms % 1000);
    }
    return n > 0 ? arena_copy(arena, buf, (size_t)n) : (u8str_t){ .ptr = "", .len = 0 };
}

/* RV-115: the empty SYNC that closes a SAMI caption made from `tpl`, at `cue`'s end; empty when `tpl` has none. */
static u8str_t added_close(proven_arena_t *arena, const rubraview_subtitle_track_t *track, const rubraview_subtitle_cue_t *tpl,
                           const rubraview_subtitle_cue_t *cue) {
    u8str_t empty = { .ptr = "", .len = 0 };
    const rubraview_subtitle_place_t *pl = &tpl->place;
    if (!place_in(track, pl->close_at, pl->close_len)) return empty;
    u8str_t end = write_time(arena, track->format, cue->end_seconds);
    size_t cap = pl->close_len + end.len + 8;
    proven_result_mem_mut_t res = proven_arena_alloc(arena, cap);
    if (!proven_is_ok(res.err) || end.len == 0) return empty;
    char *out = (char*)(void*)res.value.ptr;
    size_t n = 0;
    swap_t close[1] = { { pl->end_at, pl->end_len, end } };
    if (!copy_swapped(out, &n, cap, track->source, pl->close_at, pl->close_len, close, 1)) return empty;
    return (u8str_t){ .ptr = out, .len = n };
}

typedef struct patch {
    size_t at, len; u8str_t with; size_t seq; bool cut;
    double when; int smi;   /* RV-115: a SAMI insertion — 1 a caption, 2 its closing SYNC — placed by its time */
} patch_t;

/* RV-115: where a SAMI piece of time `when` goes so the SYNCs stay in order of time: before the first SYNC
   that is not being taken out and starts later (a caption) or not earlier (a closing SYNC); else before `</BODY>`. */
static size_t smi_insert_at(const rubraview_subtitle_track_t *track, const patch_t *patches, size_t count, double when, int smi) {
    u8str_t t = track->source;
    for (size_t i = 0; i + 5 < t.len; ++i) {
        if (t.ptr[i] != '<') continue;
        u8str_t rest = { .ptr = t.ptr + i, .len = t.len - i };
        if (rubraview_u8_starts_with_ci(rest, "</body")) return i;
        if (!rubraview_u8_starts_with_ci(rest, "<sync")) continue;
        bool cut = false;
        for (size_t k = 0; k < count && !cut; ++k) cut = patches[k].cut && i >= patches[k].at && i < patches[k].at + patches[k].len;
        if (cut) continue;
        size_t v = i + 5, stop = i;
        while (stop < t.len && t.ptr[stop] != '>') stop++;
        double ms = -1.0;
        for (; v + 5 < stop; ++v) {
            if (!rubraview_u8_starts_with_ci((u8str_t){ .ptr = t.ptr + v, .len = stop - v }, "start")) continue;
            size_t d = v + 5;
            while (d < stop && (t.ptr[d] == ' ' || t.ptr[d] == '=' || t.ptr[d] == '"')) d++;
            ms = 0.0;
            while (d < stop && t.ptr[d] >= '0' && t.ptr[d] <= '9') { ms = ms * 10.0 + (t.ptr[d] - '0'); d++; }
            break;
        }
        double at = ms / 1000.0;
        long long a = (long long)(at * 1000.0 + 0.5), w = (long long)(when * 1000.0 + 0.5);
        if (ms >= 0.0 && (smi == 2 ? a >= w : a > w)) return i;
    }
    return t.len;
}

static int patch_order(const void *a, const void *b) {
    const patch_t *x = a, *y = b;
    if (x->at != y->at) return x->at < y->at ? -1 : 1;
    if ((x->len == 0) != (y->len == 0)) return x->len == 0 ? -1 : 1;   /* an insertion before what is written over there */
    if (x->smi && y->smi) {                                             /* SAMI pieces at one place: by time, a closing SYNC first */
        if (x->when != y->when) return x->when < y->when ? -1 : 1;
        if (x->smi != y->smi) return x->smi == 2 ? -1 : 1;
    }
    return x->seq < y->seq ? -1 : x->seq > y->seq ? 1 : 0;              /* insertions at one place in the order of their cues */
}

u8str_t rubraview_subtitle_patched(proven_arena_t *arena, const rubraview_subtitle_track_t *track, const char **out_why) {
    u8str_t empty = { .ptr = "", .len = 0 };
    const char *why_sink = NULL;
    if (!out_why) out_why = &why_sink;
    *out_why = NULL;
    if (!arena || !track || track->source.len == 0) { *out_why = "the subtitle's file was not kept"; return empty; }
    if (!rubraview_subtitle_changed(track)) return track->source;

    /* Three places a cue at most. */
    if (track->count > SIZE_MAX / 4 / sizeof(patch_t)) { *out_why = "too many subtitles"; return empty; }
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, track->count * 4 + 1, sizeof(patch_t));
    if (!proven_is_ok(res.err)) { *out_why = "no memory"; return empty; }
    patch_t *patches = (patch_t*)(void*)res.value.ptr;
    size_t count = 0;

    /* RV-115: a SAMI caption moved past others in time is moved in the file too — taken out where it was and
       written again after the unchanged caption before it — since a SAMI file is read in the order of its SYNCs. */
    bool *moved = NULL;
    if (track->format == RUBRAVIEW_SUBTITLE_SMI) {
        proven_result_mem_mut_t mres = rubraview_arena_alloc_array(arena, track->count ? track->count : 1, sizeof(bool));
        if (!proven_is_ok(mres.err)) { *out_why = "no memory"; return empty; }
        moved = (bool*)(void*)mres.value.ptr;
        memset(moved, 0, track->count * sizeof(bool));
        size_t gap_from = 0;
        size_t before = 0;            /* the block of the unchanged caption before the gap */
        bool have_before = false;
        for (size_t i = 0; i <= track->count; ++i) {
            const rubraview_subtitle_cue_t *c = i < track->count ? &track->cues[i] : NULL;
            bool fixed = c && !c->added && !c->removed && !c->time_changed;
            if (c && !fixed) continue;
            /* the changed captions between two unchanged ones: in place only if they stand between them, in order */
            size_t after = c ? c->place.block_at : SIZE_MAX;
            size_t last = before;
            bool in_order = true;
            for (size_t k = gap_from; k < i; ++k) {
                const rubraview_subtitle_cue_t *g = &track->cues[k];
                if (g->added || g->removed || !g->time_changed) continue;
                size_t at = g->place.block_at;
                if ((have_before && at < last) || at > after || (!have_before && k > gap_from && at < last)) in_order = false;
                last = at;
            }
            for (size_t k = gap_from; k < i && !in_order; ++k) {
                const rubraview_subtitle_cue_t *g = &track->cues[k];
                if (!g->added && !g->removed && g->time_changed && place_in(track, g->place.block_at, g->place.block_len)) moved[k] = true;
            }
            if (c) { before = c->place.block_at; have_before = true; gap_from = i + 1; }
        }
    }

    for (size_t i = 0; i < track->count; ++i) {
        const rubraview_subtitle_cue_t *cue = &track->cues[i];
        if (cue->added && cue->removed) continue;
        if (moved && moved[i]) {
            patches[count++] = (patch_t){ cue->place.block_at, cue->place.block_len, empty, i, true, 0.0, 0 };
            if (place_in(track, cue->place.close_at, cue->place.close_len)) {
                patches[count++] = (patch_t){ cue->place.close_at, cue->place.close_len, empty, i, true, 0.0, 0 };
            }
            /* written again after the nearest caption before it that stays, or before the first that does */
            const rubraview_subtitle_cue_t *tpl = NULL;
            bool after = true;
            for (size_t k = i; k > 0 && !tpl; --k) {
                const rubraview_subtitle_cue_t *c = &track->cues[k - 1];
                if (!c->added && !c->removed && !moved[k - 1] && place_in(track, c->place.block_at, c->place.block_len)) tpl = c;
            }
            for (size_t k = i + 1; k < track->count && !tpl; ++k) {
                const rubraview_subtitle_cue_t *c = &track->cues[k];
                if (!c->added && !c->removed && !moved[k] && place_in(track, c->place.block_at, c->place.block_len)) { tpl = c; after = false; }
            }
            size_t at = tpl ? tpl->place.block_at : track->source.len;
            if (tpl && after) {
                at = tpl->place.block_at + tpl->place.block_len;
                if (place_in(track, tpl->place.close_at, tpl->place.close_len) && tpl->place.close_at + tpl->place.close_len > at) {
                    at = tpl->place.close_at + tpl->place.close_len;
                }
            }
            u8str_t block = added_block(arena, track, cue, cue, false, cue->text_changed ? cue->written : empty);
            if (block.len == 0) { *out_why = "a moved caption could not be written"; return empty; }
            patches[count++] = (patch_t){ at, 0, block, i, false, cue->start_seconds, 1 };
            u8str_t close = added_close(arena, track, cue, cue);
            if (close.len > 0) patches[count++] = (patch_t){ at, 0, close, i, false, cue->end_seconds, 2 };
            continue;
        }
        if (cue->removed) {   /* RV-115: its block out, and a SAMI caption's closing SYNC with it */
            patches[count++] = (patch_t){ cue->place.block_at, cue->place.block_len, empty, i, true, 0.0, 0 };
            if (place_in(track, cue->place.close_at, cue->place.close_len)) {
                patches[count++] = (patch_t){ cue->place.close_at, cue->place.close_len, empty, i, true, 0.0, 0 };
            }
            continue;
        }
        if (cue->added) {     /* RV-115: written in as the nearest cue of the file's own, after it or before it */
            const rubraview_subtitle_cue_t *tpl = NULL;
            bool after = true;
            for (size_t k = i; k > 0 && !tpl; --k) {
                const rubraview_subtitle_cue_t *c = &track->cues[k - 1];
                if (!c->added && !c->removed && place_in(track, c->place.block_at, c->place.block_len)) tpl = c;
            }
            for (size_t k = i + 1; k < track->count && !tpl; ++k) {
                const rubraview_subtitle_cue_t *c = &track->cues[k];
                if (!c->added && !c->removed && place_in(track, c->place.block_at, c->place.block_len)) { tpl = c; after = false; }
            }
            if (!tpl) { *out_why = "a new subtitle has no other in the file to be written as"; return empty; }
            size_t at = tpl->place.block_at;
            if (after) {
                at = tpl->place.block_at + tpl->place.block_len;
                if (place_in(track, tpl->place.close_at, tpl->place.close_len) && tpl->place.close_at + tpl->place.close_len > at) {
                    at = tpl->place.close_at + tpl->place.close_len;
                }
            }
            u8str_t block = added_block(arena, track, tpl, cue, after, cue->written);
            if (block.len == 0) { *out_why = "a new subtitle could not be written"; return empty; }
            bool smi = track->format == RUBRAVIEW_SUBTITLE_SMI;
            patches[count++] = (patch_t){ at, 0, block, i, false, cue->start_seconds, smi ? 1 : 0 };
            u8str_t close = smi ? added_close(arena, track, tpl, cue) : empty;
            if (close.len > 0) patches[count++] = (patch_t){ at, 0, close, i, false, cue->end_seconds, 2 };
            continue;
        }
        if (cue->time_changed) {
            if (!place_in(track, cue->place.start_at, cue->place.start_len)) { *out_why = "a moved subtitle has no place in the file"; return empty; }
            patches[count++] = (patch_t){ cue->place.start_at, cue->place.start_len, write_time(arena, track->format, cue->start_seconds), i, false, 0.0, 0 };
            if (place_in(track, cue->place.end_at, cue->place.end_len)) {
                patches[count++] = (patch_t){ cue->place.end_at, cue->place.end_len, write_time(arena, track->format, cue->end_seconds), i, false, 0.0, 0 };
            }
        }
        if (cue->text_changed) {
            if (!place_in(track, cue->place.text_at, cue->place.text_len)) { *out_why = "an edited subtitle has no place in the file"; return empty; }
            patches[count++] = (patch_t){ cue->place.text_at, cue->place.text_len, cue->written, i, false, 0.0, 0 };
        }
    }
    for (size_t i = 0; i < count; ++i) {   /* SAMI insertions by their time */
        if (patches[i].smi) patches[i].at = smi_insert_at(track, patches, count, patches[i].when, patches[i].smi);
    }
    qsort(patches, count, sizeof(patches[0]), patch_order);

    /* Two captions of a SAMI may share one SYNC (a class each): the same
       place written twice is written once; places that only overlap are
       a file this cannot save. */
    size_t kept = 0, grow = 0;
    for (size_t i = 0; i < count; ++i) {
        if (patches[i].with.len == 0 && !patches[i].cut) { *out_why = "a time or a text could not be written"; return empty; }
        if (kept > 0 && patches[i].len > 0 && patches[i].at == patches[kept - 1].at && patches[i].len == patches[kept - 1].len) continue;
        if (kept > 0 && patches[i].at < patches[kept - 1].at + patches[kept - 1].len) { *out_why = "two changes touch the same place in the file"; return empty; }
        patches[kept++] = patches[i];
        if (patches[i].with.len > SIZE_MAX / 2 - grow) { *out_why = "too long"; return empty; }
        grow += patches[i].with.len;
    }
    if (track->source.len > SIZE_MAX / 2 - grow) { *out_why = "too long"; return empty; }
    proven_result_mem_mut_t out_res = proven_arena_alloc(arena, track->source.len + grow + 1);
    if (!proven_is_ok(out_res.err)) { *out_why = "no memory"; return empty; }
    char *out = (char*)(void*)out_res.value.ptr;
    size_t n = 0, from = 0;
    for (size_t i = 0; i < kept; ++i) {
        memcpy(out + n, track->source.ptr + from, patches[i].at - from);
        n += patches[i].at - from;
        memcpy(out + n, patches[i].with.ptr, patches[i].with.len);
        n += patches[i].with.len;
        from = patches[i].at + patches[i].len;
    }
    memcpy(out + n, track->source.ptr + from, track->source.len - from);
    n += track->source.len - from;
    out[n] = '\0';
    return (u8str_t){ .ptr = out, .len = n };
}

u8str_t rubraview_subtitle_backup_path(proven_arena_t *arena, u8str_t path, unsigned number) {
    u8str_t empty = { .ptr = "", .len = 0 };
    if (!arena || path.len == 0 || path.len > 32000) return empty;
    u8str_t ext = rubraview_path_ext(path);
    size_t stem = path.len - ext.len;
    char mark[32];
    int m = snprintf(mark, sizeof(mark), ".old%u", number);
    if (m <= 0) return empty;
    proven_result_mem_mut_t res = proven_arena_alloc(arena, path.len + (size_t)m + 1);
    if (!proven_is_ok(res.err)) return empty;
    char *out = (char*)(void*)res.value.ptr;
    memcpy(out, path.ptr, stem);
    memcpy(out + stem, mark, (size_t)m);
    memcpy(out + stem + (size_t)m, path.ptr + stem, ext.len);
    out[path.len + (size_t)m] = '\0';
    return (u8str_t){ .ptr = out, .len = path.len + (size_t)m };
}
