#ifndef RUBRAVIEW_TEXTENC_H
#define RUBRAVIEW_TEXTENC_H

/*
 * RFC-0005 §2: the encodings a text file is read in, and the guess of
 * which one a file is in. Decoding itself is the system's (a code page
 * through the PAL) except for UTF-8 and UTF-16, which are done here.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum rubraview_textenc {
    RUBRAVIEW_TEXTENC_AUTO = 0,     /* "guess" (settings and the menu); never a guess's answer */
    RUBRAVIEW_TEXTENC_UTF8,
    RUBRAVIEW_TEXTENC_UTF16LE,
    RUBRAVIEW_TEXTENC_UTF16BE,
    RUBRAVIEW_TEXTENC_CP949,        /* Korean: EUC-KR and its extension (UHC) */
    RUBRAVIEW_TEXTENC_ISO2022KR,
    RUBRAVIEW_TEXTENC_JOHAB,
    RUBRAVIEW_TEXTENC_SJIS,         /* Japanese: Shift_JIS / CP932 */
    RUBRAVIEW_TEXTENC_EUCJP,
    RUBRAVIEW_TEXTENC_ISO2022JP,
    RUBRAVIEW_TEXTENC_GBK,          /* Chinese, simplified: GB2312 / GBK / CP936 */
    RUBRAVIEW_TEXTENC_GB18030,
    RUBRAVIEW_TEXTENC_HZ,
    RUBRAVIEW_TEXTENC_BIG5,         /* Chinese, traditional: Big5 / CP950 (HKSCS read as this, RFC-0005 §12) */
    RUBRAVIEW_TEXTENC_CNS,          /* EUC-TW / CNS 11643 */
    RUBRAVIEW_TEXTENC_CP1252,
    RUBRAVIEW_TEXTENC_CP437,
    RUBRAVIEW_TEXTENC_COUNT
} rubraview_textenc_t;

/** Windows' code page for it (65001 for UTF-8, 1200 / 1201 for UTF-16). */
uint32_t rubraview_textenc_codepage(rubraview_textenc_t enc);
/** What the menu and the information bar call it: "UTF-8", "CP949 (Korean)". */
const char *rubraview_textenc_name(rubraview_textenc_t enc);
/** 1.25.0 (owner, 2026-10-20): the encoding tile's short code, on one or two rows: "EUC\nKR", "UTF8", "SHFT\nJIS". */
const char *rubraview_textenc_short(rubraview_textenc_t enc);
/** The word settings.ini keeps: "auto", "utf8", "cp949", ... */
const char *rubraview_textenc_word(rubraview_textenc_t enc);
/** The encoding a settings word names; AUTO for one not known. */
rubraview_textenc_t rubraview_textenc_from_word(u8str_t word);

/**
 * §2.2: which encoding `bytes` (the start of a file; 64 KB is plenty)
 * look like. `ui_lang` is Windows' display language ("ko", "ja",
 * "zh-Hans", "zh-Hant", or NULL) and breaks a tie between the East Asian
 * double-byte ones. `*bom` is set to the length of a byte order mark to
 * skip (0 when there is none).
 */
rubraview_textenc_t rubraview_textenc_guess(const uint8_t *bytes, size_t size, const char *ui_lang, size_t *bom);

/** Whether `bytes` are UTF-8 throughout; a sequence cut by the end is allowed when `cut_ok`. */
bool rubraview_textenc_is_utf8(const uint8_t *bytes, size_t size, bool cut_ok);

/**
 * UTF-16 (`big_endian` or not) as UTF-8, malloc'd, NUL-terminated; a lone
 * surrogate becomes U+FFFD. NULL when out of memory.
 */
char *rubraview_textenc_utf16_to_utf8(const uint8_t *bytes, size_t size, bool big_endian, size_t *out_len);

/**
 * §2.3: decoded text made ready to show, in place where it can be, the
 * result malloc'd: bad UTF-8 becomes U+FFFD, "\r\n" and "\r" become "\n",
 * and a control character other than tab and line feed shows as U+00B7.
 */
char *rubraview_text_clean(const char *utf8, size_t len, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_TEXTENC_H */
