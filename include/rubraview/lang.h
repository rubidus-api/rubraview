#ifndef RUBRAVIEW_LANG_H
#define RUBRAVIEW_LANG_H

#include "rubraview/core.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Language packs (RV-114, D-149). The English text is the key: a pack
 * maps it to another language's text, and a text the pack does not have
 * stays English. A pack is a UTF-8 text file, one entry a line:
 *
 *     # rubraview language pack
 *     language = "ko"
 *     name = "한국어"
 *     "Open file" = "파일 열기"
 *     toolbox "Open" = "열기"          (a context before the English)
 *     "Delay %s" = "간격 %s"
 *
 * Inside quotes \" \\ \n and \t are read as such. A pack is data from
 * outside and cannot break the program: an entry whose English has printf
 * conversions is taken only when its translation has the same ones in the
 * same order; the file, the number of entries and each text are bounded;
 * a line that cannot be read is skipped and counted.
 */

#define RUBRAVIEW_LANG_MAX_FILE    (4u * 1024u * 1024u)
#define RUBRAVIEW_LANG_MAX_ENTRIES 20000u
#define RUBRAVIEW_LANG_MAX_TEXT    4096u

typedef struct rubraview_lang_entry {
    uint32_t hash;
    const char *context;    /* "" when none */
    const char *english;
    const char *text;       /* NUL-terminated */
    size_t text_len;
} rubraview_lang_entry_t;

typedef struct rubraview_lang {
    char code[24];          /* language = "..." */
    char name[64];          /* name = "..." */
    rubraview_lang_entry_t *slots;   /* open addressing, a power of two */
    size_t slot_count;
    size_t count;
    char *store;            /* every string of the pack, one block */
    size_t store_used, store_size;
} rubraview_lang_t;

typedef struct rubraview_lang_stats {
    size_t entries;         /* taken */
    size_t broken;          /* lines that could not be read */
    size_t refused;         /* conversions that did not match the English */
    size_t too_many;        /* past RUBRAVIEW_LANG_MAX_ENTRIES */
} rubraview_lang_stats_t;

/** Reads a pack. False when the text is over RUBRAVIEW_LANG_MAX_FILE or
    memory runs out; a pack with no usable entry still loads (and is empty).
    `stats` may be NULL. */
bool rubraview_lang_load(rubraview_lang_t *lang, u8str_t text, rubraview_lang_stats_t *stats);
void rubraview_lang_free(rubraview_lang_t *lang);

/** The pack's text for `english` in `context` ("" or NULL for none), or NULL. */
const rubraview_lang_entry_t *rubraview_lang_find(const rubraview_lang_t *lang, const char *context,
                                                  const char *english, size_t english_len);

/** Whether `translation` has the same printf conversions as `english`, in
    the same order (%% is not one). */
bool rubraview_lang_same_conversions(u8str_t english, u8str_t translation);

/* ---- the pack in use, for the whole program ---- */

/** The pack the lookups below use; NULL goes back to English. The pack is
    not copied and must outlive its use. */
void rubraview_lang_use(const rubraview_lang_t *lang);
const rubraview_lang_t *rubraview_lang_current(void);

/** The text in the language in use, or `english` itself. NUL-terminated.
    format_arg keeps the compiler checking a format looked up this way
    against its arguments (a pack's own is checked as it is read). */
#if defined(__GNUC__) || defined(__clang__)
#define RUBRAVIEW_FORMAT_ARG(n) __attribute__((format_arg(n)))
#else
#define RUBRAVIEW_FORMAT_ARG(n)
#endif
const char *rubraview_tr(const char *english) RUBRAVIEW_FORMAT_ARG(1);
const char *rubraview_tr_ctx(const char *context, const char *english) RUBRAVIEW_FORMAT_ARG(2);
u8str_t rubraview_tr_u8(u8str_t english);
u8str_t rubraview_tr_ctx_u8(const char *context, u8str_t english);

/** Marks a literal for check-lang.py and looks it up. */
#define T(lit) rubraview_tr("" lit)
#define TU8(lit) rubraview_tr_u8(U8(lit))
#define TC(ctx, lit) rubraview_tr_ctx(ctx, "" lit)
/** Marks a literal for check-lang.py without looking it up (it is looked
    up later, where it is shown). */
#define N_(lit) ("" lit)

/** Display columns of UTF-8 text on a character grid: East Asian Wide and
    Fullwidth characters take two, combining marks none, others one. */
size_t rubraview_text_columns(u8str_t text);
/** Bytes of `text` that fit in `columns`, never splitting a character. */
size_t rubraview_text_fit(u8str_t text, size_t columns);
/** Whether a code point takes two columns. */
bool rubraview_codepoint_is_wide(uint32_t cp);

/** A language's own name for itself, by its code as the setting stores it
    ("ko" is "한국어", "zh-hant" "繁體中文"), or NULL: shown as it is in
    every language, so it is found whatever language is on screen. */
const char *rubraview_lang_native_name(const char *code);

/** Which pack a Windows language tag ("ko-KR", "zh-TW", "ja") wants, from
    `available` codes ("ko", "ja", "zh-Hans", "zh-Hant"); NULL when none
    (English). Whole tag first, then the script, then the language. */
const char *rubraview_lang_pick(const char *tag, const char *const *available, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_LANG_H */
