#ifndef RUBRAVIEW_INI_H
#define RUBRAVIEW_INI_H

#include "rubraview/core.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Minimal INI reader/writer for settings.ini and keymap.ini (§3.7.5,
 * §3.17.2, §3.22.1). Lines: `[section]` headers, `key = value` pairs,
 * blank lines, and `;` or `#` full-line comments. Keys before any
 * `[section]` header belong to the global section (name.len == 0).
 * All slices are zero-copy views into the arena-owned parsed text.
 */

typedef struct rubraview_ini_entry {
    u8str_t section; /* "" for the global section */
    u8str_t key;
    u8str_t value;
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
 */
void rubraview_ini_set(proven_arena_t *arena, rubraview_ini_doc_t *doc, u8str_t section, u8str_t key, u8str_t value);

/**
 * Serialize a document back to INI text, grouping entries by section in
 * first-appearance order (global-section entries, if any, are written
 * first with no `[section]` header).
 */
u8str_t rubraview_ini_serialize(proven_arena_t *arena, const rubraview_ini_doc_t *doc);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_INI_H */
