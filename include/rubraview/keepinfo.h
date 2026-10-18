#ifndef RUBRAVIEW_KEEPINFO_H
#define RUBRAVIEW_KEEPINFO_H

#include "rubraview/core.h"
#include "rubraview/export.h"
#include "rubraview/fileinfo.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A picture's details kept through a save that writes its pixels again
 * (owner, 2026-10-18: "새 파일포맷으로 저장할 때 촬영정보나 주석 등의 파일
 * 추가정보를 보존하여 담을 수 없다면 ... 결과 파일과 동일 이름의 txt 파일로").
 *
 * The writers (WIC, libwebp) write pixels only. What the source said is
 * read from its bytes and put into the file written, by bytes, where the
 * format has a place for it: EXIF as a block (JPEG APP1, PNG eXIf, WebP
 * EXIF), XMP as its packet, a JPEG's IPTC block as it was, comments in the
 * format's own comment (JPEG COM, PNG iTXt, GIF's comment extension), and
 * what has no place of its own written in words into that comment. What
 * still cannot go in is reported, for a .txt beside the file.
 */

typedef enum rubraview_keep_details {
    RUBRAVIEW_KEEP_DETAILS_DROP = 0,   /* as before 1.17.0: the pixels only */
    RUBRAVIEW_KEEP_DETAILS_IN_FILE,    /* in the file written, where it has a place */
    RUBRAVIEW_KEEP_DETAILS_OR_TXT,     /* and what it cannot hold, in a .txt beside it */
} rubraview_keep_details_t;

#define RUBRAVIEW_DETAILS_MAX_COMMENTS 16
#define RUBRAVIEW_DETAILS_MAX_CHUNKS   32

typedef struct rubraview_details_src {
    bool    any;                 /* something worth keeping was found */
    u8str_t exif;                /* the EXIF block as TIFF bytes ("II*\0" / "MM\0*"), when it can be copied */
    u8str_t xmp;                 /* the XMP packet */
    u8str_t iptc_app13;          /* a JPEG's APP13 payload ("Photoshop 3.0\0..."), as it was */
    bool    has_iptc;            /* IPTC fields were read (in words below) */
    u8str_t comments[RUBRAVIEW_DETAILS_MAX_COMMENTS];   /* UTF-8 */
    size_t  comment_count;
    u8str_t png_chunks[RUBRAVIEW_DETAILS_MAX_CHUNKS];   /* a PNG's tEXt / zTXt / iTXt chunks, whole */
    size_t  png_chunk_count;
    size_t  unread;              /* compressed texts that were not read (named in `words`) */
    u8str_t words;               /* EXIF and IPTC as the information window words them, then the comments; CRLF lines */
    rubraview_info_t info;       /* the same, as headings and pairs (1.18.0: what the .txt is written from) */
    bool    beside;              /* 1.18.0: read from a Rubraview .txt beside the picture, not from the picture */
} rubraview_details_src_t;

/** What `bytes` (a picture's file) carries. False, `any` false, when nothing. */
bool rubraview_details_read(proven_arena_t *arena, const uint8_t *bytes, size_t size, rubraview_details_src_t *out);

typedef struct rubraview_details_put {
    u8str_t file;        /* the file with what could go in; the file as given when nothing could */
    bool    all_kept;    /* everything the source had is in it */
} rubraview_details_put_t;

/**
 * Puts what `src` holds into `file`, a picture just written as `format`.
 * The EXIF copied says orientation 1: the pixels were turned upright
 * before they were written. Never makes a file its format's readers would
 * refuse: a block too long for its place is left out (and `all_kept` is false).
 */
rubraview_details_put_t rubraview_details_put(proven_arena_t *arena, rubraview_export_format_t format,
                                              u8str_t file, const rubraview_details_src_t *src);

/**
 * The .txt's text (1.18.0, owner 2026-10-18: "첫행에 뭔가 식별자 ... 루브라뷰와 버젼 번호 ...
 * 형식은 toml 부분집합"), a subset of TOML 1.0, CRLF, which `rubraview_details_from_txt` reads:
 *
 *     # Rubraview <version> details
 *     file = "photo.bmp"              the picture it belongs to (its name, no folder)
 *     source = "photo.jpg"            the picture the details were read from
 *
 *     ["EXIF"]                        a heading of the information window, quoted
 *     "Camera" = "Canon EOS R5"       a pair: quoted key, basic string
 *     [comments]
 *     items = ["first", "second"]     an array of basic strings, on one line
 *     [xmp]
 *     packet = "<x:xmpmeta ...>"      the packet, its line ends written as \n
 *
 * Only that: `# comment` lines, `[bare]` / `["quoted"]` tables, `key = "string"` and
 * `key = ["string", ...]`, keys bare or quoted, strings with \" \\ \n \r \t and \u four hex digits.
 */
u8str_t rubraview_details_txt(proven_arena_t *arena, const rubraview_details_src_t *src, u8str_t source_name, u8str_t file_name);

/**
 * 1.18.0: a .txt beside `picture_name` read back as its details, when it is Rubraview's
 * (its first line `# Rubraview <version> details`) and its `file` is that picture (the
 * case of letters aside). False otherwise, or when it does not parse; `out` then empty.
 */
bool rubraview_details_from_txt(proven_arena_t *arena, u8str_t text, u8str_t picture_name, rubraview_details_src_t *out);

/** The k-th .txt a picture's details may be in: `photo.txt` (0), `photo-1.txt`, `photo-2.txt`... */
u8str_t rubraview_details_txt_candidate(proven_arena_t *arena, u8str_t picture_path, unsigned k);

/** `extra` (from a .txt) added to `into` (from the picture): what `into` lacks — tables, comments, XMP. */
void rubraview_details_merge(proven_arena_t *arena, rubraview_details_src_t *into, const rubraview_details_src_t *extra);

/**
 * The .txt beside `written`: its name with `.txt`, or `-1`, `-2`... before
 * it when that is taken (`exists` asks). Empty after 9999.
 */
u8str_t rubraview_details_txt_path(proven_arena_t *arena, u8str_t written,
                                   bool (*exists)(u8str_t path, void *ctx), void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_KEEPINFO_H */
