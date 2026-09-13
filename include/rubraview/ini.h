#ifndef RUBRAVIEW_INI_H
#define RUBRAVIEW_INI_H

#include "rubraview/core.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The configuration file format (§3.7.5, §3.17.2, §3.22.1, D-13): the
 * lines that a TOML 1.0 parser and a plain line-based INI reader both
 * accept with the same meaning.
 *
 * **Written** — only this, so every file rubraview writes is both:
 *   - UTF-8 without a BOM; a comment is a whole line starting `#`
 *   - `[section]` and `key = value`, names of `[a-z0-9_-]`, a key once
 *     per section, no key before the first section
 *   - values: `true`/`false`, `-?[0-9]+`, `-?[0-9]+.[0-9]+`, or a string
 *     in double quotes with exactly two escapes, `\\` and `\"`
 *
 * **Read** — that, and whatever older files and hand edits contain: a
 * BOM, `;` comments, bare unquoted text (`decoder = ffmpeg`), repeated
 * keys (the last wins), keys before any section (the global section,
 * name.len == 0). Nobody's existing file stops loading; the next save
 * writes it in the subset.
 *
 * `scripts/check-conf-format.py` runs Python's `tomllib` and
 * `configparser` on what the writer produces, so "both" is measured.
 */

typedef enum rubraview_ini_kind {
    RUBRAVIEW_INI_STRING = 0,   /* written quoted */
    RUBRAVIEW_INI_BOOL,
    RUBRAVIEW_INI_INT,
    RUBRAVIEW_INI_FLOAT,
} rubraview_ini_kind_t;

typedef struct rubraview_ini_entry {
    u8str_t section; /* "" for the global section */
    u8str_t key;
    u8str_t value;   /* the meaning: quotes removed, escapes undone */
    rubraview_ini_kind_t kind;
} rubraview_ini_entry_t;

typedef struct rubraview_ini_doc {
    rubraview_ini_entry_t *entries;
    size_t count;
    size_t capacity;
} rubraview_ini_doc_t;

/**
 * Parse INI text into a document. The document's entries and the slices
 * within them borrow `text` (or arena-copied text if the caller passed a
 * temporary buffer) — text must outlive the document. Entries preserve
 * source order; a repeated key keeps every occurrence (last-wins lookup
 * via rubraview_ini_get).
 */
rubraview_ini_doc_t rubraview_ini_parse(proven_arena_t *arena, u8str_t text);

/**
 * Look up a key within a section (case-sensitive section/key names,
 * matching the file's exact spelling). Returns NULL if absent. When the
 * key appears more than once, the last occurrence wins (later entries in
 * a file override earlier ones, matching typical INI merge semantics).
 */
const u8str_t *rubraview_ini_get(const rubraview_ini_doc_t *doc, u8str_t section, u8str_t key);

bool rubraview_ini_get_bool(const rubraview_ini_doc_t *doc, u8str_t section, u8str_t key, bool default_value);
long long rubraview_ini_get_int(const rubraview_ini_doc_t *doc, u8str_t section, u8str_t key, long long default_value);
double rubraview_ini_get_float(const rubraview_ini_doc_t *doc, u8str_t section, u8str_t key, double default_value);

/**
 * Insert or update (in place, preserving position) a key's value within a
 * section. Grows the document's backing storage in `arena` as needed.
 *
 * The plain setter works the kind out from the text — `true`, `12`, `0.5`
 * are written bare, anything else quoted. A caller that knows the type
 * says so with a typed setter: a folder named `2024` must stay a string.
 */
void rubraview_ini_set(proven_arena_t *arena, rubraview_ini_doc_t *doc, u8str_t section, u8str_t key, u8str_t value);
void rubraview_ini_set_string(proven_arena_t *arena, rubraview_ini_doc_t *doc, u8str_t section, u8str_t key, u8str_t value);
void rubraview_ini_set_bool(proven_arena_t *arena, rubraview_ini_doc_t *doc, u8str_t section, u8str_t key, bool value);
void rubraview_ini_set_int(proven_arena_t *arena, rubraview_ini_doc_t *doc, u8str_t section, u8str_t key, long long value);
void rubraview_ini_set_float(proven_arena_t *arena, rubraview_ini_doc_t *doc, u8str_t section, u8str_t key, double value);

/** A name the subset allows for a section or a key: `[a-z0-9_-]+`. */
bool rubraview_ini_name_ok(u8str_t name);

/**
 * Serialize a document in the subset, grouping entries by section in
 * first-appearance order. Global-section entries are not part of the
 * subset: they are written first, with no header, only so that nothing
 * a caller put in is lost — the format gate refuses a file that has any.
 */
u8str_t rubraview_ini_serialize(proven_arena_t *arena, const rubraview_ini_doc_t *doc);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_INI_H */
