#include "rubraview/textview.h"
#include "rubraview/lang.h"

#include <stdlib.h>
#include <string.h>

uint32_t rubraview_text_next_cp(const char *s, size_t len, size_t *i) {
    const uint8_t *b = (const uint8_t *)s;
    uint8_t c = b[*i];
    int n = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 0;
    if (n == 0 || *i + (size_t)n > len) { (*i)++; return 0xFFFD; }
    uint32_t cp = n == 1 ? c : c & (n == 2 ? 0x1F : n == 3 ? 0x0F : 0x07);
    for (int k = 1; k < n; ++k) cp = (cp << 6) | (b[*i + (size_t)k] & 0x3F);
    *i += (size_t)n;
    return cp;
}

/* The code point that ends at `end` (the last one before it). */
static uint32_t prev_cp(const char *s, size_t end, size_t *start) {
    size_t i = end;
    while (i > 0 && ((uint8_t)s[i - 1] & 0xC0) == 0x80) --i;
    if (i > 0) --i;
    size_t j = i;
    if (start) *start = i;
    return end > 0 ? rubraview_text_next_cp(s, end, &j) : 0;
}

static bool is_space_cp(uint32_t cp) { return cp == ' ' || cp == '\t' || cp == 0x3000 || cp == 0xA0; }

/* Han, kana and the CJK marks: written without spaces, so a line break
   between two of them joins with nothing. Hangul is written with spaces. */
static bool is_unspaced(uint32_t cp) {
    return (cp >= 0x3000 && cp <= 0x30FF) || (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) ||
           (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFF00 && cp <= 0xFF60) || (cp >= 0x20000 && cp <= 0x3134F);
}

static bool is_sentence_end(uint32_t cp) {
    switch (cp) {
        case '.': case '!': case '?': case '"': case '\'':
        case 0x3002: case 0xFF01: case 0xFF1F: case 0xFF0E: case 0xFF61:   /* 。！？．｡ */
        case 0x201D: case 0x2019: case 0x300D: case 0x300F: case 0x300B:   /* ” ’ 」 』 》 */
        case 0x2026: case 0xFF02: case 0xFF07:                              /* … ＂ ＇ */
            return true;
        default:
            return false;
    }
}

/* A line's ends without its spaces. */
static void trim(const char *s, size_t *start, size_t *end) {
    while (*start < *end) {
        size_t i = *start;
        if (!is_space_cp(rubraview_text_next_cp(s, *end, &i))) break;
        *start = i;
    }
    while (*end > *start) {
        size_t before;
        if (!is_space_cp(prev_cp(s, *end, &before))) break;
        *end = before;
    }
}

bool rubraview_text_ends_sentence(const char *line, size_t len) {
    size_t start = 0, end = len;
    trim(line, &start, &end);
    return end > start && is_sentence_end(prev_cp(line, end, NULL));
}

static bool starts_indented(const char *line, size_t len) {
    if (len == 0) return false;
    size_t i = 0;
    return is_space_cp(rubraview_text_next_cp(line, len, &i));
}

typedef struct { size_t start, end; } line_t;

typedef struct builder {
    rubraview_text_view_t *v;
    size_t text_cap, para_cap;
    bool failed;
} builder_t;

static void put(builder_t *b, const char *p, size_t n) {
    if (b->failed) return;
    if (b->v->len + n + 2 > b->text_cap) {
        size_t cap = b->text_cap ? b->text_cap * 2 : 4096;
        while (cap < b->v->len + n + 2) cap *= 2;
        char *t = (char *)realloc(b->v->text, cap);
        if (!t) { b->failed = true; return; }
        b->v->text = t;
        b->text_cap = cap;
    }
    memcpy(b->v->text + b->v->len, p, n);
    b->v->len += n;
    b->v->text[b->v->len] = '\0';
}

/* A new paragraph begins here (its separator first). */
static void begin(builder_t *b, size_t source, bool gap, bool joined) {
    if (b->failed) return;
    if (b->v->count > 0 && !joined) put(b, "\n", 1);
    if (b->v->count + 1 > b->para_cap) {
        size_t cap = b->para_cap ? b->para_cap * 2 : 256;
        rubraview_text_para_t *p = (rubraview_text_para_t *)realloc(b->v->paras, cap * sizeof(*p));
        if (!p) { b->failed = true; return; }
        b->v->paras = p;
        b->para_cap = cap;
    }
    b->v->paras[b->v->count++] = (rubraview_text_para_t){
        .start = b->v->len, .len = 0, .source = source, .gap = gap, .joined = joined,
    };
}

static void end_para(builder_t *b) {
    if (b->failed || b->v->count == 0) return;
    rubraview_text_para_t *p = &b->v->paras[b->v->count - 1];
    p->len = b->v->len - p->start;
}

/* One line of a joined run: a space before it unless either side is
   written without spaces. */
static void join_line(builder_t *b, const char *s, size_t start, size_t end) {
    rubraview_text_para_t *p = &b->v->paras[b->v->count - 1];
    if (b->failed || start >= end) return;
    if (b->v->len > p->start) {
        size_t i = start;
        uint32_t next = rubraview_text_next_cp(s, end, &i);
        uint32_t last = prev_cp(b->v->text, b->v->len, NULL);
        if (!is_unspaced(last) && !is_unspaced(next)) put(b, " ", 1);
    }
    put(b, s + start, end - start);
}

/* RV-123: the line before k is blank. */
static bool blank_before(const bool *blank, size_t k) { return k > 0 && blank[k - 1]; }

bool rubraview_text_view_build(rubraview_text_view_t *out, const char *s, size_t len,
                               rubraview_text_breaks_t breaks, bool trim_lead) {
    return rubraview_text_view_build_ex(out, s, len, breaks, trim_lead, false);
}

bool rubraview_text_view_build_ex(rubraview_text_view_t *out, const char *s, size_t len,
                               rubraview_text_breaks_t breaks, bool trim_lead, bool headings) {
    *out = (rubraview_text_view_t){0};
    builder_t b = { .v = out };

    /* The lines, and the blank ones. */
    size_t count = 1;
    for (size_t i = 0; i < len; ++i) count += s[i] == '\n';
    line_t *lines = (line_t *)malloc(count * sizeof(line_t));
    bool *blank = (bool *)malloc(count * sizeof(bool));
    if (!lines || !blank) { free(lines); free(blank); return false; }
    size_t n = 0, at = 0;
    for (size_t i = 0; i <= len; ++i) {
        if (i == len || s[i] == '\n') {
            lines[n] = (line_t){ at, i };
            size_t a = at, e = i;
            trim(s, &a, &e);
            blank[n] = a == e;
            ++n;
            at = i + 1;
        }
    }
    if (n > 0 && lines[n - 1].start == len && n > 1) --n;   /* the file's last "\n" ends a line; it opens none */

    if (breaks == RUBRAVIEW_TEXT_BREAKS_KEEP || breaks == RUBRAVIEW_TEXT_BREAKS_NO_BLANK) {
        for (size_t k = 0; k < n; ++k) {
            if (breaks == RUBRAVIEW_TEXT_BREAKS_NO_BLANK && blank[k]) continue;
            size_t a = lines[k].start, e = lines[k].end;
            if (trim_lead) { size_t z = e; trim(s, &a, &z); }
            begin(&b, lines[k].start, false, false);
            put(&b, s + a, e - a);
            end_para(&b);
        }
    } else if (breaks == RUBRAVIEW_TEXT_BREAKS_ONE_LINE) {
        /* All of it in one run; laid out in pieces cut after a sentence. */
        bool open = false;
        for (size_t k = 0; k < n; ++k) {
            if (blank[k]) continue;
            size_t a = lines[k].start, e = lines[k].end;
            trim(s, &a, &e);
            if (!open) { begin(&b, lines[k].start, false, false); open = true; }
            rubraview_text_para_t *p = &b.v->paras[b.v->count - 1];
            if (!b.failed && b.v->len - p->start >= RUBRAVIEW_TEXT_PIECE &&
                rubraview_text_ends_sentence(b.v->text + p->start, b.v->len - p->start)) {
                end_para(&b);
                put(&b, " ", 1);   /* the space the run had here, at the end of the piece before */
                b.v->paras[b.v->count - 1].len++;
                begin(&b, lines[k].start, false, true);
            }
            join_line(&b, s, a, e);
        }
        if (open) end_para(&b);
    } else {
        /* PARAGRAPHS (the owner, 2026-10-17): a paragraph ends where a line
           ends a sentence and more line breaks follow it than usually do.
           "Usually" is the commonest run of breaks after a line. A file that
           never has more (a break and nothing else between paragraphs) ends
           one where a sentence ends and the next line is indented, or, with
           no indenting at all, wherever a line ends a sentence. */
        size_t *run = (size_t *)calloc(n ? n : 1, sizeof(size_t));
        size_t hist[8] = {0}, lines_in = 0, lines_indented = 0;
        bool any_indent = false;
        if (!run) { free(lines); free(blank); return false; }
        for (size_t k = 0; k < n; ++k) {
            if (blank[k]) continue;
            size_t r = 1, j = k + 1;
            while (j < n && blank[j]) { ++r; ++j; }
            run[k] = j < n ? r : 0;   /* the last line: nothing follows */
            if (run[k]) hist[r < 7 ? r : 7]++;
            bool in = starts_indented(s + lines[k].start, lines[k].end - lines[k].start);
            any_indent = any_indent || in;
            lines_in++;
            lines_indented += in;
        }
        /* 1.25.0 (VM, 2026-10-20: indented paragraphs a break apart, a blank line now and then, ran
           together): an indent marks a paragraph when only some lines have one */
        bool indent_marks = lines_indented > 0 && lines_indented < lines_in;
        size_t usual = 1;
        for (size_t r = 1; r < 8; ++r) if (hist[r] > hist[usual]) usual = r;
        bool more_ever = false;
        for (size_t r = usual + 1; r < 8; ++r) more_ever = more_ever || hist[r] > 0;

        size_t first_line = 0;
        while (first_line < n && blank[first_line]) ++first_line;
        bool open = false;
        for (size_t k = 0; k < n; ++k) {
            if (blank[k]) continue;
            size_t a = lines[k].start, e = lines[k].end;
            trim(s, &a, &e);
            /* RV-123: a heading stands alone, the paragraph before it ended first */
            /* (a blank line before it, or the file's start, and one after it) */
            bool heading = headings && e > a && run[k] >= 2 && (k == first_line || blank_before(blank, k)) &&
                           rubraview_text_columns((proven_u8str_view_t){ .ptr = (const proven_byte_t *)s + a, .size = e - a }) <= 30 &&
                           !rubraview_text_ends_sentence(s + lines[k].start, lines[k].end - lines[k].start);
            if (heading && open) { end_para(&b); open = false; }
            if (!open) {
                /* 1.25.0 (owner, 2026-10-20: "문단의 맨 처음은 띄우게 ... 문단의 끝에서 개행. 빈 줄이 들어가는건 아님"):
                   as in a printed novel — the first line in by a space, the next paragraph on the next line */
                begin(&b, lines[k].start, false, false);
                open = true;
                if (!trim_lead && starts_indented(s + lines[k].start, lines[k].end - lines[k].start)) {
                    size_t keep_a = lines[k].start;
                    put(&b, s + keep_a, a - keep_a);   /* the indent the paragraph had */
                } else if (!heading) {
                    put(&b, " ", 1);
                }
                put(&b, s + a, e - a);
            } else {
                join_line(&b, s, a, e);
            }
            if (heading) { end_para(&b); open = false; continue; }
            if (run[k] == 0) continue;
            size_t next = k + run[k];
            bool sentence = rubraview_text_ends_sentence(s + lines[k].start, lines[k].end - lines[k].start);
            bool ends;
            if (more_ever) ends = sentence && (run[k] > usual ||
                                               (indent_marks && starts_indented(s + lines[next].start, lines[next].end - lines[next].start)));
            else if (any_indent) ends = sentence && starts_indented(s + lines[next].start, lines[next].end - lines[next].start);
            else ends = sentence;
            if (ends) { end_para(&b); open = false; }
        }
        if (open) end_para(&b);
        free(run);
    }

    free(lines);
    free(blank);
    if (out->count == 0 && !b.failed) { begin(&b, 0, false, false); end_para(&b); }
    if (b.failed) { rubraview_text_view_free(out); return false; }
    if (!out->text) { put(&b, "", 0); if (b.failed) { rubraview_text_view_free(out); return false; } }
    return true;
}

void rubraview_text_view_free(rubraview_text_view_t *view) {
    if (!view) return;
    free(view->text);
    free(view->paras);
    *view = (rubraview_text_view_t){0};
}

size_t rubraview_text_view_para_at(const rubraview_text_view_t *view, size_t offset) {
    if (!view || view->count == 0) return 0;
    size_t lo = 0, hi = view->count - 1;
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        if (view->paras[mid].start <= offset) lo = mid; else hi = mid - 1;
    }
    return lo;
}

size_t rubraview_text_view_para_of_source(const rubraview_text_view_t *view, size_t source) {
    if (!view || view->count == 0) return 0;
    size_t lo = 0, hi = view->count - 1;
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        if (view->paras[mid].source <= source) lo = mid; else hi = mid - 1;
    }
    return lo;
}

/* ---- finding ---- */

static uint32_t fold(uint32_t cp) {
    if (cp >= 0xFF01 && cp <= 0xFF5E) cp -= 0xFEE0;           /* full-width ASCII */
    if (cp == 0x3000) cp = ' ';
    if (cp >= 'A' && cp <= 'Z') cp += 32;
    if ((cp >= 0xC0 && cp <= 0xDE && cp != 0xD7)) cp += 32;   /* Latin-1 capitals */
    if (cp >= 0x391 && cp <= 0x3A9) cp += 32;                 /* Greek */
    if (cp >= 0x410 && cp <= 0x42F) cp += 32;                 /* Cyrillic */
    return cp;
}

/* Whether the needle (folded code points) is at `i`; its length in bytes. */
static bool match_at(const char *s, size_t len, size_t i, const uint32_t *needle, size_t n, size_t *bytes) {
    size_t j = i;
    for (size_t k = 0; k < n; ++k) {
        if (j >= len) return false;
        if (fold(rubraview_text_next_cp(s, len, &j)) != needle[k]) return false;
    }
    *bytes = j - i;
    return true;
}

static size_t folded(proven_u8str_view_t needle, uint32_t *out, size_t cap) {
    size_t n = 0, i = 0;
    while (i < needle.size && n < cap) out[n++] = fold(rubraview_text_next_cp((const char *)needle.ptr, needle.size, &i));
    return n;
}

bool rubraview_text_find(const rubraview_text_view_t *view, proven_u8str_view_t needle, size_t from, bool backward,
                         size_t *at, size_t *len) {
    if (!view || !view->text || needle.size == 0) return false;
    uint32_t want[256];
    size_t n = folded(needle, want, 256);
    if (n == 0) return false;
    const char *s = view->text;
    size_t total = view->len;
    if (from > total) from = total;
    size_t bytes = 0;
    if (!backward) {
        for (int pass = 0; pass < 2; ++pass) {
            size_t i = pass == 0 ? from : 0, stop = pass == 0 ? total : from;
            while (i < stop) {
                if (match_at(s, total, i, want, n, &bytes)) { *at = i; *len = bytes; return true; }
                size_t j = i;
                (void)rubraview_text_next_cp(s, total, &j);
                i = j;
            }
        }
    } else {
        for (int pass = 0; pass < 2; ++pass) {
            size_t i = pass == 0 ? from : total, stop = pass == 0 ? 0 : from;
            while (i > stop) {
                size_t start;
                (void)prev_cp(s, i, &start);
                i = start;
                if (match_at(s, total, i, want, n, &bytes)) { *at = i; *len = bytes; return true; }
            }
        }
    }
    return false;
}

size_t rubraview_text_count(const rubraview_text_view_t *view, proven_u8str_view_t needle, size_t at, size_t *which) {
    if (which) *which = 0;
    if (!view || !view->text || needle.size == 0) return 0;
    uint32_t want[256];
    size_t n = folded(needle, want, 256);
    if (n == 0) return 0;
    size_t count = 0, bytes = 0;
    for (size_t i = 0; i < view->len;) {
        if (match_at(view->text, view->len, i, want, n, &bytes)) {
            ++count;
            if (which && i == at) *which = count;
            i += bytes;
            continue;
        }
        size_t j = i;
        (void)rubraview_text_next_cp(view->text, view->len, &j);
        i = j;
    }
    return count;
}
