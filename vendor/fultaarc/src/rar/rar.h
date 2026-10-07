#ifndef FA_RAR_H
#define FA_RAR_H

#include "compat.h"
#include "rar_codec.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * CBR / RAR archives (owner, 2026-09-30, plan 2026-09-30-rar-reader):
 * RAR 1.5-4.x and RAR 5.0/7.0, stored and compressed, solid or not,
 * decoded through rar_codec.h by a decoder of the project's own (no UnRAR
 * code, D-70; until it is registered, only stored pages). Shaped like the
 * 7z reader: open lists, read decodes one entry (a solid archive from the
 * start of its chain, or on from where the last read stopped), and a long
 * read can be cancelled and watched.
 *
 * Passwords and volumes (owner, 2026-10-01, plan 2026-10-01-rar-passwords-
 * volumes): RAR 3.x-5.0 encryption (AES), of the files or of the headers
 * too, with a password given as UTF-8; RAR 1.5/2.0 encryption is refused.
 * An archive split into volumes is opened from all of them, in order; an
 * entry is then a list of pieces, and one whose next piece is in a volume
 * that is missing is refused.
 */

typedef enum fa_rar_err {
    FA_RAR_OK = 0,
    FA_RAR_ERR_NOT_A_RAR,
    FA_RAR_ERR_CORRUPT,          /* the headers do not parse */
    FA_RAR_ERR_CORRUPT_STREAM,   /* decoded, but short or its CRC does not match */
    FA_RAR_ERR_ENCRYPTED,        /* headers or the file need a password */
    FA_RAR_ERR_UNSUPPORTED,      /* split across volumes, or an unknown method */
    FA_RAR_ERR_TOO_LARGE,
    FA_RAR_ERR_OUT_OF_MEMORY,
    FA_RAR_ERR_BAD_INDEX,
    FA_RAR_ERR_CANCELLED,
    FA_RAR_ERR_BAD_PASSWORD,     /* the password given is not this archive's */
    FA_RAR_ERR_MISSING_VOLUME,   /* part of the file is in a volume that is not there */
    FA_RAR_ERR_NO_CODEC,         /* it needs a RAR decoder (rar_codec.h), and none is registered */
} fa_rar_err_t;

/* A volume: its bytes, borrowed; they must outlive the archive. */
typedef struct fa_rar_volume {
    const uint8_t *data;
    size_t size;
} fa_rar_volume_t;

/* Where an entry's packed bytes are: a piece of one volume. */
typedef struct fa_rar_piece {
    uint32_t volume;
    uint64_t offset, size;
} fa_rar_piece_t;

typedef enum fa_rar_crypt {
    FA_RAR_CRYPT_NONE = 0,
    FA_RAR_CRYPT_OLD,            /* RAR 1.5 / 2.0: not read */
    FA_RAR_CRYPT_30,             /* RAR 3.x / 4.x: AES-128 */
    FA_RAR_CRYPT_50,             /* RAR 5.0: AES-256 */
} fa_rar_crypt_t;

typedef struct fa_rar_entry {
    u8str_t  name;          /* '/' between folders; UTF-8 unless `name_is_legacy` */
    bool     name_is_legacy;/* RAR 4 without Unicode: the bytes of a code page, as ZIP's are */
    uint64_t size, packed_size;   /* size FA_RAR_SIZE_UNKNOWN: the header does not say */
    uint64_t data_offset;   /* of its first piece, in its volume */
    uint32_t first_piece, piece_count;   /* in the archive's `pieces` */
    uint32_t crc32;         /* the last piece's: the whole file's (a MAC when `hash_mac`) */
    bool     has_crc;
    uint8_t  hash[32];      /* RAR 5 BLAKE2sp of the file (a MAC when `hash_mac`), spec 5.3 */
    bool     has_hash;
    unsigned method;        /* 0 stored; else the decoder's: 15, 20, 26, 29, 36, 50, 70 (255: unknown) */
    uint64_t dict_size;
    bool     solid;         /* continues the previous file's stream */
    bool     encrypted;
    bool     split;         /* a piece of it is in a volume that is not there */
    /* how it is encrypted */
    fa_rar_crypt_t crypt;
    bool     salt_set, psw_check_set, hash_mac;
    uint8_t  lg2_count;
    uint8_t  salt[16], iv[16], psw_check[8];
    uint8_t  psw_check_sum[4];          /* RAR 5: SHA-256 of psw_check, to tell damage from a wrong password */
    uint8_t  mac_salt[16], mac_lg2;     /* RAR 5: the checksum key's, from the last part (spec 4.2) */
} fa_rar_entry_t;

typedef struct fa_rar_archive {
    const uint8_t *data;    /* the first volume's bytes; borrowed */
    size_t size;
    const fa_rar_volume_t *volumes;   /* all of them, in order (a copy in the arena) */
    size_t volume_count;
    fa_rar_piece_t *pieces;
    size_t piece_count;
    bool rar5, solid_archive, headers_encrypted;
    bool is_volume;         /* the archive says it is one of a set */
    bool new_numbering;     /* `.partN.rar`, not `.rar, .r00, ...` */
    fa_rar_entry_t *entries;   /* files only, in the archive's order */
    size_t entry_count;
    void *state;            /* the decoder, the password and keys, where a solid stream got to; heap */
} fa_rar_archive_t;

typedef struct fa_rar_result {
    fa_rar_err_t err;
    fa_rar_archive_t value;
} fa_rar_result_t;

/** A RAR signature at the start (or in the first megabyte, after an SFX stub). */
bool fa_rar_is_rar(const uint8_t *data, size_t size);

/*
 * The decoder that decompresses and decrypts (rar_codec.h; the project's own,
 * D-70). The headers are read without it, and a stored,
 * unencrypted file too; the rest is ERR_NO_CODEC until one is given. Set
 * once at start, before any archive is read.
 */
struct fa_rar_codec;
void fa_rar_set_codec(const struct fa_rar_codec *codec);
const struct fa_rar_codec *fa_rar_codec(void);

/** Reading entry `index` needs the module, and none was given. */
bool fa_rar_needs_codec(const fa_rar_archive_t *archive, size_t index);

[[nodiscard]] fa_rar_result_t fa_rar_open(proven_arena_t *arena, const uint8_t *data, size_t size);

/**
 * Open from every volume of a set, the first first (one volume is a plain
 * archive). `password` (UTF-8, may be empty) is needed now only when the
 * headers are encrypted: without it ERR_ENCRYPTED, with a wrong one
 * ERR_BAD_PASSWORD. The volume array is copied; the bytes are borrowed.
 */
[[nodiscard]] fa_rar_result_t fa_rar_open_volumes(proven_arena_t *arena, const fa_rar_volume_t *volumes,
                                                                size_t volume_count, u8str_t password);

/**
 * The password for encrypted files, checked at once: RAR 5 by the check
 * value it stores, RAR 4 by decoding the smallest encrypted file that
 * starts a stream. OK, ERR_BAD_PASSWORD, or what that decoding said. The
 * password is kept (in memory only) until the archive is closed.
 */
fa_rar_err_t fa_rar_set_password(fa_rar_archive_t *archive, u8str_t password);

/** Some file is encrypted and no password that opens it has been given. */
bool fa_rar_needs_password(const fa_rar_archive_t *archive);

/* ---- volumes, by name ---- */

typedef struct fa_rar_volume_info {
    bool is_volume;         /* the main header says so (unknown under encrypted RAR 5 headers) */
    bool new_numbering;
    bool first;             /* the first of its set, as far as the header tells */
    bool headers_encrypted;
} fa_rar_volume_info_t;

/** What the start of an archive says about the set it belongs to. */
bool fa_rar_volume_info(const uint8_t *data, size_t size, fa_rar_volume_info_t *out);

typedef enum fa_rar_volume_name_kind {
    FA_RAR_NAME_PLAIN = 0,   /* `x.rar`, `x.cbr`: a set only if its header says so */
    FA_RAR_NAME_PART,        /* `x.part3.rar` */
    FA_RAR_NAME_OLD,         /* `x.r00`, `x.s12` (the `x.rar` before them is PLAIN) */
} fa_rar_volume_name_kind_t;

fa_rar_volume_name_kind_t fa_rar_volume_name_kind(u8str_t name);

/** The volume number in a name: 1 for `x.part1.rar` and `x.rar`, 2 for `x.r00`; 0 when none. */
unsigned fa_rar_volume_number(u8str_t name);

/** The first volume's path of the set `path` is in (`old` numbering: `.rar, .r00, ...`). */
u8str_t fa_rar_first_volume(proven_arena_t *arena, u8str_t path, bool old);

/** The next volume's path after `path`, in RAR's volume naming. */
u8str_t fa_rar_next_volume(proven_arena_t *arena, u8str_t path, bool old);

typedef struct fa_rar_data_result {
    fa_rar_err_t err;
    u8str_t data;           /* in the caller's arena on success */
} fa_rar_data_result_t;

/**
 * Entry `index` streamed into `write` (a solid archive's chain before it
 * decoded and discarded), its CRC / BLAKE2sp checked at the end: the bytes
 * are handed over before the check, so OK is what says they were right.
 * For entries too large to hold (spec 2.3's 64-bit sizes).
 */
fa_rar_err_t fa_rar_extract(fa_rar_archive_t *archive, size_t index,
                                          fa_rar_write_fn write, void *ctx);

[[nodiscard]] fa_rar_data_result_t fa_rar_read_entry(proven_arena_t *arena, fa_rar_archive_t *archive,
                                                                    size_t index, uint64_t max_entry_bytes);

/** Bytes to be decoded to read `index` now (the chain before it in a solid archive included). */
uint64_t fa_rar_read_cost(const fa_rar_archive_t *archive, size_t index);

/** Stop a read under way from another thread (true), or allow reads again (false). */
void fa_rar_cancel(fa_rar_archive_t *archive, bool cancel);

/** How far the read under way has got, in decoded bytes. */
void fa_rar_progress(const fa_rar_archive_t *archive, uint64_t *out_done, uint64_t *out_total);

void fa_rar_close(fa_rar_archive_t *archive);

#ifdef __cplusplus
}
#endif

#endif /* FA_RAR_H */
