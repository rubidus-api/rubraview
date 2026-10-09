#ifndef RUBRAVIEW_PAGESOURCE_H
#define RUBRAVIEW_PAGESOURCE_H

#include "rubraview/core.h"
#include "rubraview/encoding.h"
#include "fulta/arc.h"
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
 * Archive pages never touch the disk. The bytes come out of the caller's
 * in-memory archive buffer (§3.8.1's zero-disk invariant), decoded into an
 * arena. Folder pages report a path instead, which the image PAL opens
 * directly.
 *
 * Every archive is read by FultaArc (vendor/fultaarc, `fulta/arc.h`; owner,
 * 2026-10-08, D-74): ZIP, 7z, RAR 2.0-7.0, ALZ and EGG, with their solid, split and
 * password-protected forms.
 */

typedef enum rubraview_page_source_kind {
    RUBRAVIEW_PAGE_SOURCE_FOLDER = 0,
    RUBRAVIEW_PAGE_SOURCE_ARCHIVE,     /* CBZ / ZIP */
    RUBRAVIEW_PAGE_SOURCE_ARCHIVE_7Z,  /* CB7 / 7z (§3.8.2) */
    RUBRAVIEW_PAGE_SOURCE_ARCHIVE_RAR, /* CBR / RAR (owner, 2026-09-30) */
    RUBRAVIEW_PAGE_SOURCE_ARCHIVE_ALZ, /* ALZ (D-85) */
    RUBRAVIEW_PAGE_SOURCE_ARCHIVE_EGG, /* EGG (D-85) */
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

    /* Archive sources only. The archive borrows the caller's buffer and
       owns heap memory (FultaArc's index and decoder state, the volumes
       mapped for a set), which is why closing a source is not optional. */
    fulta_arc_t *arc;
    struct rubraview_page_source_io *io;
    u8str_t archive_path;

    /* The ComicInfo.xml found in the archive, if any (§3.8.5). */
    bool     has_comicinfo;
    u8str_t  comicinfo_xml;

    /* An archive that wants a password (owner, 2026-10-01): its headers or
       its pages are encrypted and none was given (`needs_password`), or the
       one given is wrong (`password_wrong` too). It then has no pages. */
    bool     needs_password;
    bool     password_wrong;
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
 *
 * A CB7 is recognised by its own signature rather than by its extension
 * and read through the 7z path instead (§3.8.2). `max_block_bytes` is
 * the §10.2 guard that only a 7z needs: its solid block, not its file,
 * is what gets allocated. A ZIP ignores it.
 */
rubraview_page_source_t rubraview_page_source_from_archive(proven_arena_t *arena,
                                                            const uint8_t *data, size_t size,
                                                            u8str_t archive_path,
                                                            u8str_t extension_filter,
                                                            rubraview_codepage_t override_choice,
                                                            uint32_t max_entry_bytes,
                                                            uint64_t max_block_bytes);

/**
 * The same, with a password for an encrypted archive (UTF-8; empty for none).
 * A split archive is read with its other volumes, found beside
 * `archive_path` by the names its format gives them and mapped by the
 * source. A RAR opened by a later volume's name (`x.part2.rar`, `x.r00`)
 * is opened from the set's first volume.
 */
rubraview_page_source_t rubraview_page_source_from_archive_password(proven_arena_t *arena,
                                                                     const uint8_t *data, size_t size,
                                                                     u8str_t archive_path,
                                                                     u8str_t extension_filter,
                                                                     rubraview_codepage_t override_choice,
                                                                     uint32_t max_entry_bytes,
                                                                     uint64_t max_block_bytes,
                                                                     u8str_t password);

/** A RAR volume after the first of its set (`x.part2.rar`, `x.r00`): it opens as its set, so lists skip it. */
bool rubraview_page_source_is_later_volume(u8str_t name);

/**
 * Release what an archive source holds. A CBZ holds nothing but arena
 * memory and this does nothing; a CB7 holds the SDK's index and its
 * decoded solid block on the heap, and this is what gives them back.
 * Always call it when the reader leaves an archive.
 */
void rubraview_page_source_close(rubraview_page_source_t *source);

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
                                                   rubraview_page_source_t *source,
                                                   size_t index,
                                                   uint32_t max_entry_bytes);

/**
 * Which page is the file the reader named, or -1.
 *
 * This cannot be a byte comparison of whole paths, and assuming it
 * could is what made `rubraview.exe test.jpg` open a *different*
 * picture: the command line says `test.jpg`, the directory listing says
 * `D:\Downloads\test.jpg`, and on Windows the same file
 * is also `TEST.JPG` and may be written with either slash. None of
 * those are equal as bytes, the search found nothing, and the viewer
 * fell back to the first page in the folder.
 *
 * So the comparison is on the file's own name, with the two separators
 * treated alike and letters compared without case — which is what
 * Windows itself considers the same file.
 */
/**
 * An archive page's size once read, from the archive's index — nothing is
 * decompressed. 0 for a folder page or an index out of range.
 */
uint64_t rubraview_page_source_entry_size(const rubraview_page_source_t *source, size_t index);

/* What the archive's own index says, for the information window (nothing
   is decoded). The entry behind a page (NULL for a folder page); the
   format's name ("ZIP", "7z", "RAR"); whether a far page decodes the ones
   before it; whether anything in it is encrypted; how many volumes have
   been read, and their size together (0 and 0 when it is one file). */
const fulta_arc_entry_t *rubraview_page_source_entry(const rubraview_page_source_t *source, size_t index);
u8str_t rubraview_page_source_format_name(const rubraview_page_source_t *source);
bool rubraview_page_source_is_solid(const rubraview_page_source_t *source);
bool rubraview_page_source_is_encrypted(const rubraview_page_source_t *source);
size_t rubraview_page_source_volumes(const rubraview_page_source_t *source, uint64_t *out_bytes);

/**
 * How large an arena rubraview_page_source_read needs for this page (RV-085:
 * the tile thread's copy is read into one of just that size, not into the
 * app's arena). 0 when rubraview_page_source_entry_size is.
 */
size_t rubraview_page_source_read_budget(const rubraview_page_source_t *source, size_t index);

/* Owner, 2026-09-29: a page far inside a solid block is read off the main
   thread. Its cost in bytes to decode first (0 for a folder page or one
   compressed on its own; an estimate from the index for a 7z), and the
   read's cancel and progress. Cancel may be called from another thread
   while a read runs; progress counts the bytes decoded so far. */
uint64_t rubraview_page_source_read_cost(const rubraview_page_source_t *source, size_t index);
void rubraview_page_source_cancel(rubraview_page_source_t *source, bool cancel);
void rubraview_page_source_progress(const rubraview_page_source_t *source, uint64_t *out_done, uint64_t *out_total);

int32_t rubraview_page_source_find(const rubraview_page_source_t *source, u8str_t path);

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
