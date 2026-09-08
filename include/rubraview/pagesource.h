#ifndef RUBRAVIEW_PAGESOURCE_H
#define RUBRAVIEW_PAGESOURCE_H

#include "rubraview/core.h"
#include "rubraview/archive.h"
#include "rubraview/encoding.h"
#include "rubraview/sort.h"
#include "rubraview/pal/pal_fs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Where pages come from (RFC-0001 §3.1, §3.8.1). A folder of images and
 * a CBZ archive are the same thing to the reader — an ordered run of
 * pages — so they are the same thing to the viewer: one ordered list of
 * page names, and one call that produces a page's bytes.
 *
 * Archive pages never touch the disk. The bytes come straight out of
 * the caller's in-memory archive buffer (§3.8.1's zero-disk invariant),
 * decompressed into an arena when the entry is deflated. Folder pages
 * report a path instead, which the image PAL opens directly.
 */

typedef enum rubraview_page_source_kind {
    RUBRAVIEW_PAGE_SOURCE_FOLDER = 0,
    RUBRAVIEW_PAGE_SOURCE_ARCHIVE,
} rubraview_page_source_kind_t;

typedef struct rubraview_page_ref {
    u8str_t name;         /* the page's display name, already UTF-8 */
    u8str_t path;         /* folder sources: the file to open. Empty for archive pages. */
    size_t  entry_index;  /* archive sources: the index within the archive */
} rubraview_page_ref_t;

typedef struct rubraview_page_source {
    rubraview_page_source_kind_t kind;
    rubraview_page_ref_t *pages;
    size_t page_count;

    /* Archive sources only. The archive borrows the caller's buffer. */
    rubraview_zip_archive_t archive;
    u8str_t archive_path;

    /* The ComicInfo.xml found in the archive, if any (§3.8.5). */
    bool     has_comicinfo;
    u8str_t  comicinfo_xml;
} rubraview_page_source_t;

/**
 * Build a source from an already-listed directory: image files only, in
 * the requested order. This is what M2's sibling indexing produced,
 * expressed as a page source.
 */
rubraview_page_source_t rubraview_page_source_from_listing(proven_arena_t *arena,
                                                            const rubraview_fs_listing_t *listing,
                                                            u8str_t extension_filter,
                                                            rubraview_sort_mode_t mode,
                                                            bool ascending);

/**
 * Build a source from a CBZ held in memory. Entries that are not images
 * are skipped, `ComicInfo.xml` is picked out for the caller, and the
 * remaining pages are ordered naturally — the numbering inside a comic
 * archive is exactly the case §3.2.3's natural sort exists for.
 *
 * `override_choice` selects how legacy filenames are read (§3.8.3); the
 * transcoding itself happens through the filesystem PAL, so a name that
 * cannot be transcoded keeps its raw bytes rather than vanishing.
 */
rubraview_page_source_t rubraview_page_source_from_archive(proven_arena_t *arena,
                                                            const uint8_t *data, size_t size,
                                                            u8str_t archive_path,
                                                            u8str_t extension_filter,
                                                            rubraview_codepage_t override_choice,
                                                            uint32_t max_entry_bytes);

/**
 * Produce a page's bytes. For an archive page this inflates the entry
 * into `arena` (or returns a zero-copy view when it is stored); for a
 * folder page it returns an empty slice and the caller opens
 * `pages[index].path` through the image PAL instead.
 */
typedef struct rubraview_page_bytes {
    u8str_t data;      /* empty for a folder page, or on failure */
    bool    from_disk; /* true when the caller should open the path itself */
    bool    ok;
} rubraview_page_bytes_t;

rubraview_page_bytes_t rubraview_page_source_read(proven_arena_t *arena,
                                                   const rubraview_page_source_t *source,
                                                   size_t index,
                                                   uint32_t max_entry_bytes);

/**
 * §3.8.1 point 4: the next or previous archive in the same directory,
 * so reaching the last page of `Vol 01.cbz` continues into `Vol 02.cbz`.
 * Returns an empty slice when there is none in that direction.
 */
u8str_t rubraview_page_source_sibling_archive(proven_arena_t *arena,
                                              const rubraview_fs_listing_t *listing,
                                              u8str_t current_archive_path,
                                              bool forward);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAGESOURCE_H */
