#include "rubraview/lyricsearch.h"
#include "rubraview/path.h"
#include "proven/sha2.h"

/* RV-323 (R369, D-411): see the header. Every text is UTF-8; only ASCII letters are folded or tested, so Hangul,
   kana and the rest pass through whole. */

static size_t clen(const char *s) { return proven_cstr_len(s); }

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }

/* `word` at `at` in `s`, ASCII case aside, standing alone (no ASCII letter just before or after it). */
static bool word_at(const char *s, size_t at, const char *word) {
    size_t n = clen(word);
    for (size_t i = 0; i < n; ++i) if (!s[at + i] || lower(s[at + i]) != word[i]) return false;
    char before = at ? lower(s[at - 1]) : ' ', after = lower(s[at + n]);
    bool letter_before = before >= 'a' && before <= 'z', letter_after = after >= 'a' && after <= 'z';
    return !letter_before && !letter_after;
}

static bool holds_word(const char *s, size_t from, size_t to, const char *word) {
    size_t n = clen(word);
    for (size_t i = from; i + n <= to; ++i) if (word_at(s, i, word)) return true;
    return false;
}

static void cut(char *s, size_t from, size_t to) {
    size_t n = clen(s);
    if (to > n) to = n;
    proven_sys_mem_move(s + from, s + to, n - to + 1);
}

/* The marks a release adds to a title that no lyrics database keeps. */
static const char *const NOISE[] = {
    "feat", "ft", "featuring", "official", "video", "audio", "lyric", "lyrics", "mv", "m/v", "hd", "hq", "4k",
    "remaster", "remastered", "explicit", "clean", "visualizer", "visualiser", "color coded", "eng sub", "han",
};

void rubraview_lyrics_clean(char *s) {
    if (!s) return;
    /* bracketed marks: (..), [..], {..}, and full-width （..） 【..】 */
    for (size_t i = 0; s[i];) {
        const char *open = NULL, *close = NULL;
        size_t olen = 1;
        if (s[i] == '(') { open = "("; close = ")"; }
        else if (s[i] == '[') { open = "["; close = "]"; }
        else if (s[i] == '{') { open = "{"; close = "}"; }
        else if ((unsigned char)s[i] == 0xEF && (unsigned char)s[i + 1] == 0xBC && (unsigned char)s[i + 2] == 0x88) { open = "\xEF\xBC\x88"; close = "\xEF\xBC\x89"; olen = 3; }
        else if ((unsigned char)s[i] == 0xE3 && (unsigned char)s[i + 1] == 0x80 && (unsigned char)s[i + 2] == 0x90) { open = "\xE3\x80\x90"; close = "\xE3\x80\x91"; olen = 3; }
        if (!open) { i++; continue; }
        size_t clen_ = clen(close), end = i + olen;
        while (s[end] && proven_sys_mem_cmp(s + end, close, clen_) != 0) end++;
        if (!s[end]) { i++; continue; }
        bool noise = false;
        for (size_t k = 0; k < sizeof NOISE / sizeof NOISE[0] && !noise; ++k) noise = holds_word(s, i + olen, end, NOISE[k]);
        if (noise) cut(s, i, end + clen_);
        else i = end + clen_;
    }
    /* "feat. X", "ft. X", "featuring X" outside brackets: to the end */
    for (size_t i = 0; s[i]; ++i) {
        if (!(word_at(s, i, "feat") || word_at(s, i, "ft") || word_at(s, i, "featuring")) || i == 0) continue;
        if (s[i - 1] == ' ') { s[i] = 0; break; }
        if (s[i - 1] == '(' || s[i - 1] == '[') { s[i - 1] = 0; break; }   /* a bracket left open */
    }
    /* "Title - Lyrics", "Title | Official" and the like: a dash or bar followed only by noise */
    for (size_t i = 0; s[i]; ++i) {
        if ((s[i] != '-' && s[i] != '|') || i == 0 || s[i - 1] != ' ') continue;
        size_t n = clen(s);
        bool noise = false;
        for (size_t k = 0; k < sizeof NOISE / sizeof NOISE[0] && !noise; ++k) noise = holds_word(s, i + 1, n, NOISE[k]);
        if (noise) { s[i] = 0; break; }
    }
    /* underscores to blanks, blanks single, ends trimmed */
    size_t w = 0;
    bool space = true;
    for (size_t r = 0; s[r]; ++r) {
        char c = s[r] == '_' ? ' ' : s[r];
        if (c == ' ' || c == '\t') { if (!space) s[w++] = ' '; space = true; }
        else { s[w++] = c; space = false; }
    }
    while (w > 0 && (s[w - 1] == ' ' || s[w - 1] == '-' || s[w - 1] == '|' || s[w - 1] == ',')) w--;
    s[w] = 0;
}

static void put_field(char *dst, proven_u8str_view_t src) {
    size_t n = src.size < RUBRAVIEW_LYRICS_FIELD - 1 ? src.size : RUBRAVIEW_LYRICS_FIELD - 1;
    while (n > 0 && n < src.size && (src.ptr[n] & 0xC0) == 0x80) n--;   /* never half a character */
    if (n) proven_sys_mem_copy(dst, src.ptr, n);
    dst[n] = 0;
}

static bool same_text(const char *a, const char *b) {
    size_t i = 0;
    for (; a[i] && b[i]; ++i) if (lower(a[i]) != lower(b[i])) return false;
    return a[i] == b[i];
}

static bool add_query(rubraview_lyrics_query_t *out, size_t *n, size_t cap, const rubraview_lyrics_query_t *q) {
    if (*n >= cap || !q->title[0]) return false;
    for (size_t i = 0; i < *n; ++i)
        if (same_text(out[i].title, q->title) && same_text(out[i].artist, q->artist)) return false;
    out[(*n)++] = *q;
    return true;
}

/* The file's name without its ending and a leading track number ("01 ", "01. ", "1-03 - ", "A1 "), split at " - ". */
static void from_name(proven_u8str_view_t path, rubraview_lyrics_query_t *q) {
    proven_u8str_view_t base = rubraview_path_basename(path), ext = rubraview_path_ext(base);
    if (ext.size < base.size) base.size -= ext.size;
    char name[RUBRAVIEW_LYRICS_FIELD];
    put_field(name, base);
    size_t i = 0;
    while (name[i] >= '0' && name[i] <= '9') i++;
    bool disc = false;   /* "1-03": a disc and its track */
    if (i > 0 && i <= 3 && name[i] == '-' && name[i + 1] >= '0' && name[i + 1] <= '9') { i++; while (name[i] >= '0' && name[i] <= '9') i++; disc = true; }
    if (i > 0 && i <= 6 && name[i]) {
        /* a track number: followed by ".", ")", "-" or "_", or with a leading zero by a blank ("01 Title"); "4 Minutes",
           "99 Problems" and a title of digits alone stay */
        bool mark = name[i] == '.' || name[i] == ')' || name[i] == '-' || name[i] == '_' || ((name[0] == '0' || disc) && name[i] == ' ');
        if (mark) {
            size_t j = i;
            while (name[j] == '.' || name[j] == ' ' || name[j] == '-' || name[j] == '_' || name[j] == ')') j++;
            if (name[j]) cut(name, 0, j);
        }
    }
    char *dash = NULL;
    for (size_t k = 0; name[k] && name[k + 1] && name[k + 2]; ++k)
        if (name[k] == ' ' && name[k + 1] == '-' && name[k + 2] == ' ') { dash = name + k; break; }
    if (dash) {
        *dash = 0;
        proven_sys_mem_copy(q->artist, name, clen(name) + 1);
        size_t t = clen(dash + 3);
        proven_sys_mem_move(q->title, dash + 3, t + 1);
    } else {
        proven_sys_mem_copy(q->title, name, clen(name) + 1);
    }
    rubraview_lyrics_clean(q->artist);
    rubraview_lyrics_clean(q->title);
}

/* The first of "A & B", "A, B", "A x B", "A feat. B", "A / B", "A;B" */
static bool first_artist(char *a) {
    static const char *const SEP[] = { " & ", ", ", " x ", " X ", " / ", ";", " and ", " with " };
    for (size_t k = 0; k < sizeof SEP / sizeof SEP[0]; ++k) {
        size_t n = clen(SEP[k]);
        for (size_t i = 1; a[i]; ++i) {
            if (proven_sys_mem_cmp(a + i, SEP[k], n) != 0) continue;
            a[i] = 0;
            rubraview_lyrics_clean(a);
            return a[0] != 0;
        }
    }
    return false;
}

size_t rubraview_lyrics_queries(const rubraview_tags_t *tags, proven_u8str_view_t path,
                                rubraview_lyrics_query_t *out, size_t cap) {
    if (!out || cap == 0) return 0;
    size_t n = 0;
    rubraview_lyrics_query_t q;
    proven_sys_mem_set(&q, 0, sizeof q);
    if (tags) {
        put_field(q.artist, tags->artist);
        put_field(q.title, tags->title);
        put_field(q.album, tags->album);
        q.seconds = tags->duration_seconds;
        rubraview_lyrics_clean(q.artist);
        rubraview_lyrics_clean(q.title);
        rubraview_lyrics_clean(q.album);
    }
    rubraview_lyrics_query_t named;
    proven_sys_mem_set(&named, 0, sizeof named);
    if (path.size) from_name(path, &named);
    named.seconds = q.seconds;
    if (!q.title[0]) {   /* no title tag: the name's, with the tag's artist when the name has none */
        proven_sys_mem_copy(q.title, named.title, sizeof q.title);
        if (!q.artist[0]) proven_sys_mem_copy(q.artist, named.artist, sizeof q.artist);
    }
    add_query(out, &n, cap, &q);
    if (named.title[0] && named.artist[0]) { proven_sys_mem_copy(named.album, q.album, sizeof named.album); add_query(out, &n, cap, &named); }
    rubraview_lyrics_query_t one = q;
    if (first_artist(one.artist)) add_query(out, &n, cap, &one);
    rubraview_lyrics_query_t alone = q;
    alone.artist[0] = 0;
    alone.album[0] = 0;
    add_query(out, &n, cap, &alone);
    return n;
}

/* ---- addresses ---- */

typedef struct url { char *out; size_t cap, at; bool ok; } url_t;

static void url_raw(url_t *u, const char *s) {
    for (size_t i = 0; s[i]; ++i) {
        if (u->at + 1 >= u->cap) { u->ok = false; return; }
        u->out[u->at++] = s[i];
    }
    u->out[u->at] = 0;
}

static void url_text(url_t *u, const char *s, size_t len) {
    static const char HEX[] = "0123456789ABCDEF";
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)s[i];
        bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        if (u->at + 4 >= u->cap) { u->ok = false; return; }
        if (plain) u->out[u->at++] = (char)c;
        else { u->out[u->at++] = '%'; u->out[u->at++] = HEX[c >> 4]; u->out[u->at++] = HEX[c & 15]; }
    }
    u->out[u->at] = 0;
}

static void url_param(url_t *u, const char *name, const char *value, bool *first) {
    url_raw(u, *first ? "?" : "&");
    *first = false;
    url_raw(u, name);
    url_raw(u, "=");
    url_text(u, value, clen(value));
}

size_t rubraview_lrclib_get_url(const rubraview_lyrics_query_t *q, char *out, size_t cap) {
    if (!q || !out || cap == 0 || !q->title[0]) return 0;
    url_t u = { out, cap, 0, true };
    bool first = true;
    url_raw(&u, "https://lrclib.net/api/get");
    url_param(&u, "artist_name", q->artist, &first);
    url_param(&u, "track_name", q->title, &first);
    url_param(&u, "album_name", q->album, &first);
    if (q->seconds > 0.0) {
        char s[24];
        int n = rubraview_fmt(s, sizeof s, "{}", PROVEN_ARG((int64_t)(q->seconds + 0.5)));
        if (n > 0) url_param(&u, "duration", s, &first);
    }
    return u.ok ? u.at : 0;
}

size_t rubraview_lrclib_search_url(const rubraview_lyrics_query_t *q, proven_u8str_view_t words, char *out, size_t cap) {
    if (!out || cap == 0) return 0;
    url_t u = { out, cap, 0, true };
    bool first = true;
    url_raw(&u, "https://lrclib.net/api/search");
    if (words.size > 0) {
        url_raw(&u, "?q=");
        first = false;
        url_text(&u, (const char *)words.ptr, words.size);
    } else {
        if (!q || !q->title[0]) return 0;
        url_param(&u, "track_name", q->title, &first);
        if (q->artist[0]) url_param(&u, "artist_name", q->artist, &first);
    }
    return u.ok ? u.at : 0;
}

/* ---- the answer: JSON, only as much as LRCLIB writes ---- */

typedef struct js { const uint8_t *p; size_t n, at; proven_arena_t *arena; int depth; } js_t;

static void js_space(js_t *j) { while (j->at < j->n && (j->p[j->at] == ' ' || j->p[j->at] == '\n' || j->p[j->at] == '\r' || j->p[j->at] == '\t')) j->at++; }

static bool js_take(js_t *j, char c) {
    js_space(j);
    if (j->at < j->n && j->p[j->at] == c) { j->at++; return true; }
    return false;
}

static int hexv(uint8_t c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

static bool js_hex4(js_t *j, uint32_t *v) {
    if (j->at + 4 > j->n) return false;
    *v = 0;
    for (int i = 0; i < 4; ++i) { int h = hexv(j->p[j->at + (size_t)i]); if (h < 0) return false; *v = *v << 4 | (uint32_t)h; }
    j->at += 4;
    return true;
}

static size_t put_utf8(uint8_t *o, uint32_t cp) {
    if (cp < 0x80) { o[0] = (uint8_t)cp; return 1; }
    if (cp < 0x800) { o[0] = (uint8_t)(0xC0 | cp >> 6); o[1] = (uint8_t)(0x80 | (cp & 63)); return 2; }
    if (cp < 0x10000) { o[0] = (uint8_t)(0xE0 | cp >> 12); o[1] = (uint8_t)(0x80 | ((cp >> 6) & 63)); o[2] = (uint8_t)(0x80 | (cp & 63)); return 3; }
    o[0] = (uint8_t)(0xF0 | cp >> 18); o[1] = (uint8_t)(0x80 | ((cp >> 12) & 63)); o[2] = (uint8_t)(0x80 | ((cp >> 6) & 63)); o[3] = (uint8_t)(0x80 | (cp & 63));
    return 4;
}

/* A string; its text into the arena when `keep` (no longer than it was written, so its length is room enough). */
static bool js_string(js_t *j, bool keep, proven_u8str_view_t *out) {
    js_space(j);
    if (j->at >= j->n || j->p[j->at] != '"') return false;
    size_t start = ++j->at, end = start;
    while (end < j->n && j->p[end] != '"') end += j->p[end] == '\\' ? 2 : 1;
    if (end >= j->n) return false;
    uint8_t *o = NULL;
    if (keep) {
        proven_result_mem_mut_t r = rubraview_arena_alloc_array(j->arena, end - start + 1, 1);
        if (!proven_is_ok(r.err)) return false;
        o = (uint8_t *)r.value.ptr;
    }
    size_t w = 0;
    while (j->at < end) {
        uint8_t c = j->p[j->at++];
        if (c != '\\') { if (o) o[w++] = c; continue; }
        uint8_t e = j->p[j->at++];
        uint32_t cp = 0;
        switch (e) {
            case 'n': cp = '\n'; break;
            case 'r': cp = '\r'; break;
            case 't': cp = '\t'; break;
            case 'b': cp = 8; break;
            case 'f': cp = 12; break;
            case 'u':
                if (!js_hex4(j, &cp)) return false;
                if (cp >= 0xD800 && cp < 0xDC00 && j->at + 6 <= end && j->p[j->at] == '\\' && j->p[j->at + 1] == 'u') {
                    uint32_t lo = 0;
                    j->at += 2;
                    if (!js_hex4(j, &lo) || lo < 0xDC00 || lo > 0xDFFF) return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                } else if (cp >= 0xD800 && cp < 0xE000) {
                    cp = 0xFFFD;
                }
                break;
            default: cp = e; break;   /* \" \\ \/ */
        }
        if (o) {
            uint8_t tmp[4];
            size_t k = put_utf8(tmp, cp);
            if (w + k > end - start) return false;   /* "A" is longer written than made: never short */
            proven_sys_mem_copy(o + w, tmp, k);
            w += k;
        }
    }
    j->at = end + 1;
    if (o) { o[w] = 0; *out = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)o, .size = w }; }
    return true;
}

static bool js_number(js_t *j, double *v) {
    js_space(j);
    size_t s = j->at;
    bool neg = false;
    if (j->at < j->n && j->p[j->at] == '-') { neg = true; j->at++; }
    double x = 0.0, scale = 0.0;
    bool any = false;
    while (j->at < j->n) {
        uint8_t c = j->p[j->at];
        if (c >= '0' && c <= '9') { any = true; if (scale > 0.0) { x += (c - '0') * scale; scale /= 10.0; } else x = x * 10.0 + (c - '0'); }
        else if (c == '.' && scale == 0.0) scale = 0.1;
        else if (c == 'e' || c == 'E' || c == '+' || (c == '-' && j->at > s)) { /* an exponent: LRCLIB writes none; read past it */ }
        else break;
        j->at++;
    }
    if (v) *v = neg ? -x : x;
    return any;
}

static bool js_skip(js_t *j);

static bool js_skip_container(js_t *j, char close) {
    if (++j->depth > 32) return false;
    if (js_take(j, close)) { j->depth--; return true; }
    for (;;) {
        if (close == '}') { if (!js_string(j, false, NULL) || !js_take(j, ':')) return false; }
        if (!js_skip(j)) return false;
        if (js_take(j, ',')) continue;
        if (js_take(j, close)) { j->depth--; return true; }
        return false;
    }
}

static bool js_word(js_t *j, const char *w) {
    size_t n = clen(w);
    if (j->at + n > j->n || proven_sys_mem_cmp(j->p + j->at, w, n) != 0) return false;
    j->at += n;
    return true;
}

static bool js_skip(js_t *j) {
    js_space(j);
    if (j->at >= j->n) return false;
    uint8_t c = j->p[j->at];
    if (c == '"') return js_string(j, false, NULL);
    if (c == '{') { j->at++; return js_skip_container(j, '}'); }
    if (c == '[') { j->at++; return js_skip_container(j, ']'); }
    if (c == 't') return js_word(j, "true");
    if (c == 'f') return js_word(j, "false");
    if (c == 'n') return js_word(j, "null");
    return js_number(j, NULL);
}

static bool key_is(proven_u8str_view_t k, const char *name) {
    size_t n = clen(name);
    return k.size == n && proven_sys_mem_cmp(k.ptr, name, n) == 0;
}

static bool js_record(js_t *j, rubraview_lrclib_hit_t *h) {
    proven_sys_mem_set(h, 0, sizeof *h);
    if (!js_take(j, '{')) return false;
    if (js_take(j, '}')) return true;
    for (;;) {
        proven_u8str_view_t key = { 0 };
        if (!js_string(j, true, &key) || !js_take(j, ':')) return false;
        js_space(j);
        bool is_null = j->at < j->n && j->p[j->at] == 'n';
        proven_u8str_view_t *text = key_is(key, "trackName") ? &h->track : key_is(key, "artistName") ? &h->artist
                                  : key_is(key, "albumName") ? &h->album : key_is(key, "plainLyrics") ? &h->plain
                                  : key_is(key, "syncedLyrics") ? &h->synced : NULL;
        if (text && !is_null) {
            if (!js_string(j, true, text)) return false;
        } else if (key_is(key, "id") && !is_null) {
            double v = 0;
            if (!js_number(j, &v)) return false;
            h->id = (int64_t)v;
        } else if (key_is(key, "duration") && !is_null) {
            if (!js_number(j, &h->seconds)) return false;
        } else if (key_is(key, "instrumental") && !is_null) {
            if (js_word(j, "true")) h->instrumental = true;
            else if (!js_word(j, "false")) return false;
        } else if (!js_skip(j)) {
            return false;
        }
        if (js_take(j, ',')) continue;
        return js_take(j, '}');
    }
}

size_t rubraview_lrclib_parse(proven_arena_t *arena, proven_u8str_view_t json, rubraview_lrclib_hit_t *out, size_t cap) {
    if (!arena || !out || cap == 0 || json.size == 0) return 0;
    js_t j = { .p = (const uint8_t *)json.ptr, .n = json.size, .at = 0, .arena = arena };
    if (j.n >= 3 && j.p[0] == 0xEF && j.p[1] == 0xBB && j.p[2] == 0xBF) j.at = 3;
    js_space(&j);
    if (j.at < j.n && j.p[j.at] == '{') return js_record(&j, &out[0]) ? 1 : 0;
    if (!js_take(&j, '[')) return 0;
    size_t n = 0;
    if (js_take(&j, ']')) return 0;
    for (;;) {
        js_space(&j);
        if (n < cap) { if (!js_record(&j, &out[n])) return n; n++; }
        else if (!js_skip(&j)) return n;
        if (js_take(&j, ',')) continue;
        return n;
    }
}

/* ---- the fit ---- */

/* Letters and digits only, ASCII lowered; other bytes (Hangul and the rest) kept as they are. */
static size_t fold(proven_u8str_view_t s, char *o, size_t cap) {
    size_t w = 0;
    for (size_t i = 0; i < s.size && w + 1 < cap; ++i) {
        unsigned char c = (unsigned char)s.ptr[i];
        if (c >= 0x80) o[w++] = (char)c;
        else if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')) o[w++] = (char)c;
        else if (c >= 'A' && c <= 'Z') o[w++] = (char)(c - 'A' + 'a');
    }
    o[w] = 0;
    return w;
}

/* 1 - the edit distance over the longer length; a containing one at least 0.85 of its share */
static double likeness(const char *a, const char *b) {
    char fa[128], fb[128];
    size_t na = fold((proven_u8str_view_t){ (const proven_byte_t *)a, clen(a) }, fa, sizeof fa);
    size_t nb = fold((proven_u8str_view_t){ (const proven_byte_t *)b, clen(b) }, fb, sizeof fb);
    if (na == 0 && nb == 0) return 1.0;
    if (na == 0 || nb == 0) return 0.0;
    size_t row[128];
    for (size_t j = 0; j <= nb; ++j) row[j] = j;
    for (size_t i = 1; i <= na; ++i) {
        size_t prev = row[0];
        row[0] = i;
        for (size_t j = 1; j <= nb; ++j) {
            size_t cur = row[j];
            size_t best = prev + (fa[i - 1] == fb[j - 1] ? 0u : 1u);
            if (row[j] + 1 < best) best = row[j] + 1;
            if (row[j - 1] + 1 < best) best = row[j - 1] + 1;
            row[j] = best;
            prev = cur;
        }
    }
    size_t longer = na > nb ? na : nb, shorter = na > nb ? nb : na;
    double sim = 1.0 - (double)row[nb] / (double)longer;
    if (longer - row[nb] == shorter && shorter * 2 >= longer) {   /* one inside the other */
        double inside = 0.85 * (double)shorter / (double)longer + 0.15;
        if (inside > sim) sim = inside;
    }
    return sim;
}

double rubraview_lyrics_score(const rubraview_lyrics_query_t *q, const rubraview_lrclib_hit_t *h) {
    if (!q || !h || (h->plain.size == 0 && h->synced.size == 0)) return 0.0;
    char track[RUBRAVIEW_LYRICS_FIELD], artist[RUBRAVIEW_LYRICS_FIELD];
    put_field(track, h->track);
    put_field(artist, h->artist);
    rubraview_lyrics_clean(track);
    double t = likeness(q->title, track);
    double a = q->artist[0] ? likeness(q->artist, artist) : 0.5;
    double d = 0.5;
    if (q->seconds > 0.0 && h->seconds > 0.0) {
        double diff = q->seconds > h->seconds ? q->seconds - h->seconds : h->seconds - q->seconds;
        d = diff <= 2.0 ? 1.0 : diff <= 5.0 ? 0.6 : diff <= 10.0 ? 0.25 : 0.0;
    }
    double s = 0.5 * t + 0.3 * a + 0.2 * d;
    if (h->synced.size > 0) s += 0.03;
    return s > 1.0 ? 1.0 : s;
}

int32_t rubraview_lyrics_best(const rubraview_lyrics_query_t *q, const rubraview_lrclib_hit_t *hits, size_t n, double *score) {
    int32_t best = -1;
    double top = 0.0;
    for (size_t i = 0; hits && i < n; ++i) {
        double s = rubraview_lyrics_score(q, &hits[i]);
        if (s > top) { top = s; best = (int32_t)i; }
    }
    if (score) *score = top;
    return best;
}

proven_u8str_view_t rubraview_lyrics_words(const rubraview_lrclib_hit_t *h) {
    if (!h) return PROVEN_LIT("");
    return h->synced.size > 0 ? h->synced : h->plain;
}

/* ---- uploading ---- */

static bool stamp_at(const uint8_t *p, size_t n, size_t at, size_t *end) {
    /* "[mm:ss]", "[mm:ss.xx]", "[mm:ss:xx]" */
    if (at >= n || p[at] != '[') return false;
    size_t i = at + 1, d = 0;
    while (i < n && p[i] >= '0' && p[i] <= '9') { i++; d++; }
    if (d == 0 || i >= n || p[i] != ':') return false;
    i++;
    d = 0;
    while (i < n && ((p[i] >= '0' && p[i] <= '9') || p[i] == '.' || p[i] == ':')) { i++; d++; }
    if (d == 0 || i >= n || p[i] != ']') return false;
    *end = i + 1;
    return true;
}

bool rubraview_lyrics_has_times(proven_u8str_view_t lrc) {
    const uint8_t *p = (const uint8_t *)lrc.ptr;
    size_t end = 0;
    for (size_t i = 0; i < lrc.size; ++i) {
        if ((i == 0 || p[i - 1] == '\n') && stamp_at(p, lrc.size, i, &end)) return true;
    }
    return false;
}

proven_u8str_view_t rubraview_lyrics_plain_of(proven_arena_t *arena, proven_u8str_view_t lrc) {
    if (!arena || lrc.size == 0) return PROVEN_LIT("");
    proven_result_mem_mut_t r = rubraview_arena_alloc_array(arena, lrc.size + 1, 1);
    if (!proven_is_ok(r.err)) return PROVEN_LIT("");
    uint8_t *o = (uint8_t *)r.value.ptr;
    const uint8_t *p = (const uint8_t *)lrc.ptr;
    size_t n = lrc.size, w = 0, at = 0;
    if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) at = 3;
    while (at < n) {
        size_t eol = at;
        while (eol < n && p[eol] != '\n') eol++;
        size_t i = at, e = 0;
        bool stamped = false;
        while (stamp_at(p, eol, i, &e)) { i = e; stamped = true; }
        bool header = !stamped && i < eol && p[i] == '[' && eol > i + 2 && (p[eol - 1] == ']' || (p[eol - 1] == '\r' && p[eol - 2] == ']'));
        if (header) {
            size_t c = i + 1;
            while (c < eol && ((p[c] >= 'a' && p[c] <= 'z') || (p[c] >= 'A' && p[c] <= 'Z') || p[c] == '#')) c++;
            header = c < eol && p[c] == ':';
        }
        if (!header) {
            while (i < eol && p[i] == ' ') i++;
            size_t last = eol;
            if (last > i && p[last - 1] == '\r') last--;
            proven_sys_mem_copy(o + w, p + i, last - i);
            w += last - i;
            if (eol < n) o[w++] = '\n';
        }
        at = eol < n ? eol + 1 : n;
    }
    while (w > 0 && (o[w - 1] == '\n' || o[w - 1] == ' ')) w--;
    size_t lead = 0;
    while (lead < w && o[lead] == '\n') lead++;
    o[w] = 0;
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(o + lead), .size = w - lead };
}

bool rubraview_lrclib_challenge(proven_u8str_view_t json, char prefix[128], uint8_t target[32]) {
    uint8_t scratch[1024];
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = scratch, .size = sizeof scratch });
    js_t j = { .p = (const uint8_t *)json.ptr, .n = json.size, .at = 0, .arena = &arena };
    bool have_p = false, have_t = false;
    if (!prefix || !target || !js_take(&j, '{')) return false;
    for (;;) {
        proven_u8str_view_t key = { 0 }, value = { 0 };
        if (!js_string(&j, true, &key) || !js_take(&j, ':')) return false;
        js_space(&j);
        if ((key_is(key, "prefix") || key_is(key, "target")) && j.at < j.n && j.p[j.at] == '"') {
            if (!js_string(&j, true, &value)) return false;
            if (key_is(key, "prefix")) {
                if (value.size == 0 || value.size >= 128) return false;
                proven_sys_mem_copy(prefix, value.ptr, value.size);
                prefix[value.size] = 0;
                have_p = true;
            } else {
                if (value.size != 64) return false;
                for (size_t i = 0; i < 32; ++i) {
                    int hi = hexv(value.ptr[2 * i]), lo = hexv(value.ptr[2 * i + 1]);
                    if (hi < 0 || lo < 0) return false;
                    target[i] = (uint8_t)(hi << 4 | lo);
                }
                have_t = true;
            }
        } else if (!js_skip(&j)) {
            return false;
        }
        if (js_take(&j, ',')) continue;
        return js_take(&j, '}') && have_p && have_t;
    }
}

bool rubraview_lrclib_solve(const char *prefix, const uint8_t target[32], uint64_t from, uint64_t tries, uint64_t *nonce) {
    if (!prefix || !target || !nonce) return false;
    size_t pl = clen(prefix);
    if (pl == 0 || pl >= 128) return false;
    char input[160];
    proven_sys_mem_copy(input, prefix, pl);
    for (uint64_t k = 0; k < tries; ++k) {
        uint64_t v = from + k;
        char digits[24];
        size_t d = 0;
        do { digits[d++] = (char)('0' + v % 10u); v /= 10u; } while (v);
        for (size_t i = 0; i < d; ++i) input[pl + i] = digits[d - 1 - i];
        proven_byte_t h[PROVEN_SHA256_SIZE];
        proven_sha256((proven_mem_view_t){ .ptr = (const proven_byte_t *)input, .size = pl + d }, h);
        bool ok = true;
        for (size_t i = 0; i < 32; ++i) {
            if (h[i] < target[i]) break;
            if (h[i] > target[i]) { ok = false; break; }
        }
        if (ok) { *nonce = from + k; return true; }
    }
    return false;
}

typedef struct jw { uint8_t *o; size_t cap, at; bool ok; } jw_t;

static void jw_raw(jw_t *w, const char *s, size_t n) {
    if (w->at + n + 1 > w->cap) { w->ok = false; return; }
    proven_sys_mem_copy(w->o + w->at, s, n);
    w->at += n;
}

static void jw_string(jw_t *w, const uint8_t *s, size_t n) {
    static const char HEX[] = "0123456789abcdef";
    jw_raw(w, "\"", 1);
    for (size_t i = 0; i < n && w->ok; ++i) {
        uint8_t c = s[i];
        if (c == '"') jw_raw(w, "\\\"", 2);
        else if (c == '\\') jw_raw(w, "\\\\", 2);
        else if (c == '\n') jw_raw(w, "\\n", 2);
        else if (c == '\r') jw_raw(w, "\\r", 2);
        else if (c == '\t') jw_raw(w, "\\t", 2);
        else if (c < 0x20) { char u[6] = { '\\', 'u', '0', '0', HEX[c >> 4], HEX[c & 15] }; jw_raw(w, u, 6); }
        else jw_raw(w, (const char *)&c, 1);
    }
    jw_raw(w, "\"", 1);
}

static void jw_field(jw_t *w, const char *name, const uint8_t *s, size_t n, bool *first) {
    if (!*first) jw_raw(w, ",", 1);
    *first = false;
    jw_string(w, (const uint8_t *)name, clen(name));
    jw_raw(w, ":", 1);
    jw_string(w, s, n);
}

proven_u8str_view_t rubraview_lrclib_publish_body(proven_arena_t *arena, const rubraview_lyrics_query_t *song,
                                                  proven_u8str_view_t plain, proven_u8str_view_t synced) {
    if (!arena || !song || !song->title[0] || !song->artist[0] || song->seconds < 1.0) return PROVEN_LIT("");   /* the album may be empty */
    size_t cap = 256 + 6 * (3 * RUBRAVIEW_LYRICS_FIELD + plain.size + synced.size);
    proven_result_mem_mut_t r = rubraview_arena_alloc_array(arena, cap, 1);
    if (!proven_is_ok(r.err)) return PROVEN_LIT("");
    jw_t w = { (uint8_t *)r.value.ptr, cap, 0, true };
    bool first = true;
    jw_raw(&w, "{", 1);
    jw_field(&w, "trackName", (const uint8_t *)song->title, clen(song->title), &first);
    jw_field(&w, "artistName", (const uint8_t *)song->artist, clen(song->artist), &first);
    jw_field(&w, "albumName", (const uint8_t *)song->album, clen(song->album), &first);
    char secs[24];
    int sn = rubraview_fmt(secs, sizeof secs, ",\"duration\":{}", PROVEN_ARG((int64_t)(song->seconds + 0.5)));
    if (sn > 0) jw_raw(&w, secs, (size_t)sn);
    if (plain.size) jw_field(&w, "plainLyrics", (const uint8_t *)plain.ptr, plain.size, &first);
    if (synced.size) jw_field(&w, "syncedLyrics", (const uint8_t *)synced.ptr, synced.size, &first);
    jw_raw(&w, "}", 1);
    if (!w.ok) return PROVEN_LIT("");
    w.o[w.at] = 0;
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)w.o, .size = w.at };
}
