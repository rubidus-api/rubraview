#include "rubraview/textenc.h"
#include "rubraview/utf8.h"
#include "proven/utf.h"

#include <stdlib.h>
#include <string.h>

/* ---- the list ---- */

static const struct {
    uint32_t codepage;
    const char *name, *word;
} ENCODINGS[RUBRAVIEW_TEXTENC_COUNT] = {
    [RUBRAVIEW_TEXTENC_AUTO]      = { 0,     "Auto",                       "auto" },
    [RUBRAVIEW_TEXTENC_UTF8]      = { 65001, "UTF-8",                      "utf8" },
    [RUBRAVIEW_TEXTENC_UTF16LE]   = { 1200,  "UTF-16 LE",                  "utf16le" },
    [RUBRAVIEW_TEXTENC_UTF16BE]   = { 1201,  "UTF-16 BE",                  "utf16be" },
    [RUBRAVIEW_TEXTENC_CP949]     = { 949,   "CP949 (Korean)",             "cp949" },
    [RUBRAVIEW_TEXTENC_ISO2022KR] = { 50225, "ISO-2022-KR (Korean)",       "iso2022kr" },
    [RUBRAVIEW_TEXTENC_JOHAB]     = { 1361,  "Johab (Korean)",             "johab" },
    [RUBRAVIEW_TEXTENC_SJIS]      = { 932,   "Shift_JIS (Japanese)",       "sjis" },
    [RUBRAVIEW_TEXTENC_EUCJP]     = { 20932, "EUC-JP (Japanese)",          "eucjp" },
    [RUBRAVIEW_TEXTENC_ISO2022JP] = { 50222, "ISO-2022-JP (Japanese)",     "iso2022jp" },
    [RUBRAVIEW_TEXTENC_GBK]       = { 936,   "GBK (Chinese, simplified)",  "gbk" },
    [RUBRAVIEW_TEXTENC_GB18030]   = { 54936, "GB18030 (Chinese)",          "gb18030" },
    [RUBRAVIEW_TEXTENC_HZ]        = { 52936, "HZ (Chinese, simplified)",   "hz" },
    [RUBRAVIEW_TEXTENC_BIG5]      = { 950,   "Big5 (Chinese, traditional)", "big5" },
    [RUBRAVIEW_TEXTENC_CNS]       = { 20000, "CNS 11643 (Chinese, traditional)", "cns" },
    [RUBRAVIEW_TEXTENC_CP1252]    = { 1252,  "Western (CP1252)",           "cp1252" },
    [RUBRAVIEW_TEXTENC_CP437]     = { 437,   "DOS (CP437)",                "cp437" },
};

uint32_t rubraview_textenc_codepage(rubraview_textenc_t enc) {
    return enc < RUBRAVIEW_TEXTENC_COUNT ? ENCODINGS[enc].codepage : 0;
}

const char *rubraview_textenc_short(rubraview_textenc_t enc) {
    switch (enc) {
        case RUBRAVIEW_TEXTENC_UTF8:      return "UTF8";
        case RUBRAVIEW_TEXTENC_UTF16LE:   return "UTF16\nLE";
        case RUBRAVIEW_TEXTENC_UTF16BE:   return "UTF16\nBE";
        case RUBRAVIEW_TEXTENC_CP949:     return "EUC\nKR";
        case RUBRAVIEW_TEXTENC_ISO2022KR: return "ISO\nKR";
        case RUBRAVIEW_TEXTENC_JOHAB:     return "JOHAB";
        case RUBRAVIEW_TEXTENC_SJIS:      return "SHFT\nJIS";
        case RUBRAVIEW_TEXTENC_EUCJP:     return "EUC\nJP";
        case RUBRAVIEW_TEXTENC_ISO2022JP: return "ISO\nJP";
        case RUBRAVIEW_TEXTENC_GBK:       return "SIMP\nCHN";
        case RUBRAVIEW_TEXTENC_GB18030:   return "GB\n18030";
        case RUBRAVIEW_TEXTENC_HZ:        return "HZ";
        case RUBRAVIEW_TEXTENC_BIG5:      return "TRAD\nCHN";
        case RUBRAVIEW_TEXTENC_CNS:       return "EUC\nTW";
        case RUBRAVIEW_TEXTENC_CP1252:    return "WEST";
        case RUBRAVIEW_TEXTENC_CP437:     return "DOS";
        case RUBRAVIEW_TEXTENC_AUTO:
        default:                          return "AUTO";
    }
}

const char *rubraview_textenc_name(rubraview_textenc_t enc) {
    return enc < RUBRAVIEW_TEXTENC_COUNT ? ENCODINGS[enc].name : "?";
}

const char *rubraview_textenc_word(rubraview_textenc_t enc) {
    return enc < RUBRAVIEW_TEXTENC_COUNT ? ENCODINGS[enc].word : "auto";
}

rubraview_textenc_t rubraview_textenc_from_word(proven_u8str_view_t word) {
    for (int i = 0; i < RUBRAVIEW_TEXTENC_COUNT; ++i) {
        size_t n = strlen(ENCODINGS[i].word);
        if (word.size == n && memcmp(word.ptr, ENCODINGS[i].word, n) == 0) return (rubraview_textenc_t)i;
    }
    return RUBRAVIEW_TEXTENC_AUTO;
}

/* ---- UTF-8 ---- */

/* The length of the UTF-8 sequence at `b` (at most `left` bytes), 0 when
   it is not one; -1 when it is cut short by the end. */
/* One UTF-8 character's length at `b` by proven's decoder: -1 when the bytes end inside one, 0 when it is not one. */
static int utf8_seq(const uint8_t *b, size_t left) {
    proven_utf8_char_t c = proven_utf8_decode_next(proven_u8str_view_from_bytes(b, left), 0);
    if (c.err == PROVEN_OK) return (int)c.len;
    return c.err == PROVEN_ERR_NEED_MORE ? -1 : 0;
}

bool rubraview_textenc_is_utf8(const uint8_t *b, size_t size, bool cut_ok) {
    for (size_t i = 0; i < size;) {
        int n = utf8_seq(b + i, size - i);
        if (n < 0) return cut_ok;
        if (n == 0) return false;
        i += (size_t)n;
    }
    return true;
}

/* `cp` (never a surrogate here) written as UTF-8 by proven's encoder; its length. */
static size_t put_cp(char *out, uint32_t cp) {
    return proven_utf8_encode_one(cp, (proven_byte_t *)out);
}

char *rubraview_textenc_utf16_to_utf8(const uint8_t *b, size_t size, bool big_endian, size_t *out_len) {
    size_t units = size / 2;
    char *out = (char *)malloc(units * 3 + 1);
    if (!out) return NULL;
    size_t at = 0;
    for (size_t i = 0; i < units; ++i) {
        uint32_t u = big_endian ? ((uint32_t)b[2 * i] << 8 | b[2 * i + 1]) : ((uint32_t)b[2 * i + 1] << 8 | b[2 * i]);
        if (u >= 0xD800 && u <= 0xDBFF && i + 1 < units) {
            uint32_t v = big_endian ? ((uint32_t)b[2 * i + 2] << 8 | b[2 * i + 3]) : ((uint32_t)b[2 * i + 3] << 8 | b[2 * i + 2]);
            if (v >= 0xDC00 && v <= 0xDFFF) { u = 0x10000 + ((u - 0xD800) << 10) + (v - 0xDC00); ++i; }
            else u = 0xFFFD;
        } else if (u >= 0xD800 && u <= 0xDFFF) {
            u = 0xFFFD;
        }
        at += put_cp(out + at, u);
    }
    out[at] = '\0';
    if (out_len) *out_len = at;
    return out;
}

char *rubraview_text_clean(const char *utf8, size_t len, size_t *out_len) {
    /* At worst a bad byte (1) becomes U+FFFD (3) and a control (1) U+00B7 (2). */
    char *out = (char *)malloc(len * 3 + 1);
    if (!out) return NULL;
    const uint8_t *b = (const uint8_t *)utf8;
    size_t at = 0;
    for (size_t i = 0; i < len;) {
        uint8_t c = b[i];
        if (c == '\r') {
            out[at++] = '\n';
            i += (i + 1 < len && b[i + 1] == '\n') ? 2 : 1;
            continue;
        }
        if (c < 0x20 && c != '\n' && c != '\t') { at += put_cp(out + at, 0xB7); ++i; continue; }
        if (c == 0x7F) { at += put_cp(out + at, 0xB7); ++i; continue; }
        int n = utf8_seq(b + i, len - i);
        if (n <= 0) { at += put_cp(out + at, 0xFFFD); ++i; continue; }
        memcpy(out + at, b + i, (size_t)n);
        at += (size_t)n;
        i += (size_t)n;
    }
    out[at] = '\0';
    if (out_len) *out_len = at;
    return out;
}

/* ---- the guess ---- */

static bool has(const uint8_t *b, size_t size, const char *needle) {
    size_t n = strlen(needle);
    for (size_t i = 0; i + n <= size; ++i) if (memcmp(b + i, needle, n) == 0) return true;
    return false;
}

static bool in(uint8_t c, uint8_t lo, uint8_t hi) { return c >= lo && c <= hi; }

/* How Korean, Japanese or Chinese the bytes read in one double-byte
   encoding: each character scores where it falls (Hangul syllables for
   Korean, kana for Japanese, the hanzi rows each script uses most, their
   punctuation), and a byte that is not a character there costs ten. */
static double score(rubraview_textenc_t enc, const uint8_t *b, size_t size) {
    double s = 0.0;
    size_t i = 0;
    while (i < size) {
        uint8_t c = b[i];
        if (c < 0x80) { ++i; continue; }
        bool last = i + 1 >= size;   /* a character cut by the window's end costs nothing */
        uint8_t t = last ? 0 : b[i + 1];
        switch (enc) {
            case RUBRAVIEW_TEXTENC_CP949:
                if (!in(c, 0x81, 0xFE)) { s -= 10; ++i; continue; }
                if (last) return s;
                if (!(in(t, 0x41, 0x5A) || in(t, 0x61, 0x7A) || in(t, 0x81, 0xFE))) { s -= 10; ++i; continue; }
                if (in(c, 0xB0, 0xC8) && in(t, 0xA1, 0xFE)) s += 2.0;          /* KS X 1001 Hangul */
                else if (c <= 0xC6 && t < 0xA1) s += 1.0;                      /* the extension's Hangul */
                else if (c == 0xA1 || c == 0xA3) s += 0.5;                     /* symbols, full-width */
                i += 2;
                break;
            case RUBRAVIEW_TEXTENC_SJIS:
                if (in(c, 0xA1, 0xDF)) { s += 0.3; ++i; continue; }             /* half-width kana */
                if (!(in(c, 0x81, 0x9F) || in(c, 0xE0, 0xFC))) { s -= 10; ++i; continue; }
                if (last) return s;
                if (!(in(t, 0x40, 0x7E) || in(t, 0x80, 0xFC))) { s -= 10; ++i; continue; }
                if (c == 0x82 && in(t, 0x9F, 0xF1)) s += 2.0;                  /* hiragana */
                else if (c == 0x83 && in(t, 0x40, 0x96)) s += 2.0;             /* katakana */
                else if (in(c, 0x88, 0x9F) || in(c, 0xE0, 0xEA)) s += 1.0;     /* kanji */
                else if (c == 0x81) s += 0.5;                                  /* 、。「」 */
                i += 2;
                break;
            case RUBRAVIEW_TEXTENC_EUCJP:
                if (c == 0x8E) {
                    if (last) return s;
                    if (!in(t, 0xA1, 0xDF)) { s -= 10; ++i; continue; }
                    s += 0.3; i += 2; continue;
                }
                if (c == 0x8F) {
                    if (i + 2 >= size) return s;
                    if (!in(t, 0xA1, 0xFE) || !in(b[i + 2], 0xA1, 0xFE)) { s -= 10; ++i; continue; }
                    i += 3; continue;
                }
                if (!in(c, 0xA1, 0xFE)) { s -= 10; ++i; continue; }
                if (last) return s;
                if (!in(t, 0xA1, 0xFE)) { s -= 10; ++i; continue; }
                if ((c == 0xA4 && t <= 0xF3) || (c == 0xA5 && t <= 0xF6)) s += 2.0;   /* kana */
                else if (in(c, 0xB0, 0xF4)) s += 1.0;                                  /* kanji */
                else if (c == 0xA1) s += 0.5;
                i += 2;
                break;
            case RUBRAVIEW_TEXTENC_GBK:
            case RUBRAVIEW_TEXTENC_GB18030:
                if (!in(c, 0x81, 0xFE)) { s -= 10; ++i; continue; }
                if (last) return s;
                if (enc == RUBRAVIEW_TEXTENC_GB18030 && in(t, 0x30, 0x39)) {   /* four bytes */
                    if (i + 3 >= size) return s;
                    if (!in(b[i + 2], 0x81, 0xFE) || !in(b[i + 3], 0x30, 0x39)) { s -= 10; ++i; continue; }
                    s += 0.3; i += 4; continue;
                }
                if (!(in(t, 0x40, 0x7E) || in(t, 0x80, 0xFE))) { s -= 10; ++i; continue; }
                if (in(c, 0xB0, 0xF7) && in(t, 0xA1, 0xFE)) s += 1.5;          /* GB2312 hanzi */
                else if ((c == 0xA1 && in(t, 0xA1, 0xFE)) || (c == 0xA3 && in(t, 0xA1, 0xFE))) s += 1.0;   /* 。，、 */
                else s += 0.3;
                i += 2;
                break;
            case RUBRAVIEW_TEXTENC_BIG5:
                if (!in(c, 0x81, 0xFE)) { s -= 10; ++i; continue; }
                if (last) return s;
                if (!(in(t, 0x40, 0x7E) || in(t, 0xA1, 0xFE))) { s -= 10; ++i; continue; }
                if (in(c, 0xA4, 0xC6)) s += 1.5;                               /* the commoner hanzi */
                else if (c == 0xA1) s += 1.0;                                  /* ，、。 */
                else if (in(c, 0xC9, 0xF9)) s += 1.0;                          /* the rarer ones */
                else if (in(c, 0xC7, 0xC8) || c > 0xF9 || c < 0xA1) s -= 2.0;  /* not Big5's own rows */
                else s += 0.3;
                i += 2;
                break;
            default:
                return s;
        }
    }
    return s;
}

static rubraview_textenc_t language_choice(const char *ui_lang) {
    if (!ui_lang) return RUBRAVIEW_TEXTENC_AUTO;
    if (strncmp(ui_lang, "ko", 2) == 0) return RUBRAVIEW_TEXTENC_CP949;
    if (strncmp(ui_lang, "ja", 2) == 0) return RUBRAVIEW_TEXTENC_SJIS;
    if (strcmp(ui_lang, "zh-Hant") == 0) return RUBRAVIEW_TEXTENC_BIG5;
    if (strncmp(ui_lang, "zh", 2) == 0) return RUBRAVIEW_TEXTENC_GBK;
    return RUBRAVIEW_TEXTENC_AUTO;
}

rubraview_textenc_t rubraview_textenc_guess(const uint8_t *b, size_t size, const char *ui_lang, size_t *bom) {
    if (bom) *bom = 0;
    if (size >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) { if (bom) *bom = 3; return RUBRAVIEW_TEXTENC_UTF8; }
    if (size >= 2 && b[0] == 0xFF && b[1] == 0xFE) { if (bom) *bom = 2; return RUBRAVIEW_TEXTENC_UTF16LE; }
    if (size >= 2 && b[0] == 0xFE && b[1] == 0xFF) { if (bom) *bom = 2; return RUBRAVIEW_TEXTENC_UTF16BE; }
    if (size > 65536) size = 65536;

    /* Escapes: the 7-bit encodings say what they are. */
    size_t head = size < 4096 ? size : 4096;
    if (has(b, head, "\x1b$)C")) return RUBRAVIEW_TEXTENC_ISO2022KR;
    if (has(b, size, "\x1b$B") || has(b, size, "\x1b$@") || has(b, size, "\x1b(J")) return RUBRAVIEW_TEXTENC_ISO2022JP;
    bool high = false;
    for (size_t i = 0; i < size && !high; ++i) high = b[i] >= 0x80;
    if (!high && has(b, size, "~{") && has(b, size, "~}")) return RUBRAVIEW_TEXTENC_HZ;

    /* UTF-16 without a mark: one byte in two is zero. */
    size_t zero_even = 0, zero_odd = 0, pairs = head / 2;
    for (size_t i = 0; i + 1 < head; i += 2) { zero_even += b[i] == 0; zero_odd += b[i + 1] == 0; }
    if (pairs >= 8) {
        if (zero_odd * 10 > pairs * 3 && zero_even * 20 < pairs) return RUBRAVIEW_TEXTENC_UTF16LE;
        if (zero_even * 10 > pairs * 3 && zero_odd * 20 < pairs) return RUBRAVIEW_TEXTENC_UTF16BE;
    }

    if (!high || rubraview_textenc_is_utf8(b, size, true)) return RUBRAVIEW_TEXTENC_UTF8;

    static const rubraview_textenc_t CANDIDATES[] = {
        RUBRAVIEW_TEXTENC_CP949, RUBRAVIEW_TEXTENC_SJIS, RUBRAVIEW_TEXTENC_EUCJP,
        RUBRAVIEW_TEXTENC_GBK, RUBRAVIEW_TEXTENC_GB18030, RUBRAVIEW_TEXTENC_BIG5,
    };
    double scores[sizeof(CANDIDATES) / sizeof(CANDIDATES[0])];
    size_t best = 0;
    for (size_t k = 0; k < sizeof(CANDIDATES) / sizeof(CANDIDATES[0]); ++k) {
        scores[k] = score(CANDIDATES[k], b, size);
        if (scores[k] > scores[best]) best = k;
    }
    /* GB18030 only for what GBK cannot read (its four-byte characters). */
    if (CANDIDATES[best] == RUBRAVIEW_TEXTENC_GB18030 && scores[3] >= scores[best]) best = 3;
    if (scores[best] <= 0.0) return RUBRAVIEW_TEXTENC_CP1252;

    /* Close to the best (within 5 %): the reader's own language wins. */
    rubraview_textenc_t mine = language_choice(ui_lang);
    for (size_t k = 0; k < sizeof(CANDIDATES) / sizeof(CANDIDATES[0]); ++k) {
        if (CANDIDATES[k] == mine && scores[k] > 0.0 && scores[k] >= scores[best] * 0.95) return mine;
    }
    return CANDIDATES[best];
}
