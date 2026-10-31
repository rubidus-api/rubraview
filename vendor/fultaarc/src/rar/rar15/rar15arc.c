/* rar15arc.c - reader for RAR 1.5 archives on top of proven_c_lib; see rar15arc.h.
 * Section numbers in the comments refer to docs/spec/rar15.md.
 * SPDX-License-Identifier: MIT */
#include "rar15arc.h"

#include <stdckdint.h>

#include "proven/fs.h"
#include "proven/hash.h"

static proven_u16 u16(const proven_byte_t *p) { return (proven_u16)(p[0] | p[1] << 8); }
static proven_u32 u32(const proven_byte_t *p) {
    return (proven_u32)p[0] | (proven_u32)p[1] << 8 | (proven_u32)p[2] << 16 | (proven_u32)p[3] << 24;
}

static const proven_byte_t MARK[7] = {0x52, 0x61, 0x72, 0x21, 0x1a, 0x07, 0x00}; /* 1.1 */

/* 1.2: the low 16 bits of CRC-32 over `n` header bytes */
static bool check16(const proven_byte_t *p, proven_size_t n, proven_u16 want) {
    return (proven_u16)(proven_crc32((proven_mem_view_t){.ptr = p, .size = n}) & 0xffffu) == want;
}

static bool has_mark(proven_mem_view_t v) {
    if (v.size < 7)
        return false;
    for (int i = 0; i < 7; i++)
        if (v.ptr[i] != MARK[i])
            return false;
    return true;
}

static void release(proven_allocator_t alloc, void *p) {
    if (p)
        alloc.free_fn(alloc.ctx, p);
}

/* append pk bytes to the packed data of an entry that spans volumes (1.6) */
static rar15_err_t join_part(rar15_archive_t *a, rar15_entry_t *e, const proven_byte_t *data, proven_size_t pk) {
    if (pk == 0)
        return PROVEN_OK;
    proven_size_t have = e->packed.size, want;
    if (ckd_add(&want, have, pk))
        return PROVEN_ERR_OVERFLOW;
    proven_result_mem_mut_t r;
    if (e->joined) {
        r = a->alloc.realloc_fn(a->alloc.ctx, e->joined, e->joined_cap, want, 1);
    } else {
        r = a->alloc.alloc_fn(a->alloc.ctx, want, 1);
        if (proven_is_ok(r.err) && have)
            (void)proven_mem_copy(r.value.ptr, want, e->packed);
    }
    if (!proven_is_ok(r.err))
        return r.err;
    e->joined = r.value.ptr;
    e->joined_cap = want;
    (void)proven_mem_copy(e->joined + have, pk, (proven_mem_view_t){.ptr = data, .size = pk});
    e->packed = (proven_mem_view_t){.ptr = e->joined, .size = want};
    e->packed_size = (proven_u32)want;
    return PROVEN_OK;
}

/* rar14.md 1.7: the older format's cipher. Three key bytes; for each data byte k2 += k3, k1 += k2, and the plain
 * byte is the cipher byte - k1. */
typedef struct {
    proven_byte_t k1, k2, k3;
} keys14;

static keys14 keys14_of(proven_u8str_view_t pw) {                   /* sum, XOR, and (k3 + c) rotated left by one */
    keys14 k = {0, 0, 0};
    for (proven_size_t i = 0; i < pw.size; i++) {
        proven_byte_t c = pw.ptr[i];
        k.k1 = (proven_byte_t)(k.k1 + c);
        k.k2 ^= c;
        k.k3 = (proven_byte_t)(k.k3 + c);
        k.k3 = (proven_byte_t)((k.k3 << 1) | (k.k3 >> 7));
    }
    return k;
}

static void decipher14(keys14 *k, proven_byte_t *dst, const proven_byte_t *src, proven_size_t n) {
    for (proven_size_t i = 0; i < n; i++) {
        k->k2 = (proven_byte_t)(k->k2 + k->k3);
        k->k1 = (proven_byte_t)(k->k1 + k->k2);
        dst[i] = (proven_byte_t)(src[i] - k->k1);
    }
}

static rar15_err_t grow_entries(rar15_archive_t *a) {
    if (a->count < a->cap)
        return PROVEN_OK;
    proven_size_t ncap = a->cap ? a->cap * 2 : 16, bytes, old = a->cap * sizeof(rar15_entry_t);
    if (ckd_mul(&bytes, ncap, sizeof(rar15_entry_t)))
        return PROVEN_ERR_OVERFLOW;
    proven_result_mem_mut_t r = a->entries ? a->alloc.realloc_fn(a->alloc.ctx, a->entries, old, bytes, alignof(rar15_entry_t))
                                           : a->alloc.alloc_fn(a->alloc.ctx, bytes, alignof(rar15_entry_t));
    if (!proven_is_ok(r.err))
        return r.err;
    a->entries = (rar15_entry_t *)(void *)r.value.ptr;
    a->cap = ncap;
    return PROVEN_OK;
}

/* one more file block: a new entry, or the next part of the last one (1.6) */
static rar15_err_t add_part(rar15_archive_t *a, const proven_byte_t *h, proven_size_t hs, proven_size_t pk,
                            proven_size_t nl) {
    proven_u16 fl = u16(h + 3);
    const proven_byte_t *data = h + hs;
    if ((fl & RAR15_FILE_FROM_PREV) && a->count > 0) {
        rar15_entry_t *e = &a->entries[a->count - 1];
        e->flags = fl;
        e->crc = u32(h + 0x10);
        if (pk == 0)
            return PROVEN_OK;
        return join_part(a, e, data, pk);
    }
    rar15_err_t ge = grow_entries(a);
    if (ge != PROVEN_OK)
        return ge;
    rar15_entry_t *e = &a->entries[a->count++];
    *e = (rar15_entry_t){0};
    e->name = (proven_u8str_view_t){.ptr = h + 32, .size = nl};      /* 1.4 */
    e->size = u32(h + 0x0b);
    e->packed_size = (proven_u32)pk;
    e->crc = u32(h + 0x10);
    e->dos_time = u16(h + 0x14);
    e->dos_date = u16(h + 0x16);
    e->version = h[0x18];
    e->method = h[0x19];
    e->attr = u32(h + 0x1c);
    e->flags = fl;
    e->header_check_ok = check16(h + 2, 30 + nl, u16(h));            /* 1.2: up to the end of the name */
    if ((fl & RAR15_FILE_COMMENT) && hs >= 32 + nl + 13)             /* 1.4: an inner block after the name */
        e->comment_block = (proven_mem_view_t){.ptr = h + 32 + nl, .size = hs - 32 - nl};
    e->packed = (proven_mem_view_t){.ptr = data, .size = pk};
    return PROVEN_OK;
}

static const proven_byte_t MARK14[4] = {0x52, 0x45, 0x7e, 0x5e}; /* rar14.md 1.1 */

static bool has_mark14(proven_mem_view_t v) {
    return v.size >= 7 && v.ptr[0] == MARK14[0] && v.ptr[1] == MARK14[1] && v.ptr[2] == MARK14[2] && v.ptr[3] == MARK14[3];
}

/* rar14.md 1.3: the 16-bit sum of a file: for each byte s = rol16(s + b, 1) */
static proven_u16 sum16(proven_mem_view_t d) {
    proven_u32 s = 0;
    for (proven_size_t i = 0; i < d.size; i++) {
        s = (s + d.ptr[i]) & 0xffffu;
        s = ((s << 1) | (s >> 15)) & 0xffffu;
    }
    return (proven_u16)s;
}

/* rar14.md 1.1-1.5: the older container: marker, main header (u16 size, flag byte), then file headers of 21 bytes
 * + name, each followed by its packed data */
static rar15_err_t walk14(rar15_archive_t *a, proven_mem_view_t v) {
    const proven_byte_t *b = v.ptr;
    proven_size_t len = v.size, hs = u16(b + 4);
    proven_byte_t fl = b[6];
    if (hs < 7 || hs > len)
        return RAR15_E_HEADER;
    a->old_format = true;
    if ((fl & RAR15_MAIN_COMMENT) && hs >= 7 + 2 && !a->comment_block.ptr)   /* rar14.md 1.6 */
        a->comment_block = (proven_mem_view_t){.ptr = b + 7, .size = hs - 7};
    a->old_comment_packed = (fl & 0x10) != 0;
    a->main_flags = (proven_u16)(fl & (RAR15_MAIN_VOLUME | RAR15_MAIN_COMMENT | RAR15_MAIN_LOCKED | RAR15_MAIN_SOLID));
    a->main_check_ok = true;
    proven_size_t p = hs;
    while (len - p >= 21) {
        const proven_byte_t *h = b + p;
        proven_size_t pk = u32(h), fhs = u16(h + 10), nl = h[19];
        if (fhs < 21 + nl || fhs > len - p)
            break;
        bool cut = pk > len - p - fhs;
        if (cut)
            pk = len - p - fhs;
        proven_byte_t ffl = h[17];
        if ((ffl & RAR15_FILE_FROM_PREV) && a->count > 0) {          /* the next part of a split file */
            rar15_entry_t *e = &a->entries[a->count - 1];
            e->flags = ffl;
            e->crc = u16(h + 8);
            rar15_err_t je = join_part(a, e, h + fhs, pk);
            if (je != PROVEN_OK)
                return je;
        } else {
            rar15_err_t ge = grow_entries(a);
            if (ge != PROVEN_OK)
                return ge;
            rar15_entry_t *e = &a->entries[a->count++];
            *e = (rar15_entry_t){0};
            e->name = (proven_u8str_view_t){.ptr = h + 21, .size = nl};
            e->packed_size = (proven_u32)pk;
            e->size = u32(h + 4);
            e->crc = u16(h + 8);
            e->dos_time = u16(h + 12);
            e->dos_date = u16(h + 14);
            e->attr = h[16];
            e->flags = ffl;
            e->version = h[18];
            e->method = h[20];
            e->header_check_ok = true;                               /* the older headers carry no check */
            if ((ffl & RAR15_FILE_COMMENT) && fhs >= 21 + nl + 2)    /* rar14.md 1.6: a file comment after the name */
                e->comment_block = (proven_mem_view_t){.ptr = h + 21 + nl, .size = fhs - 21 - nl};
            e->packed = (proven_mem_view_t){.ptr = h + fhs, .size = pk};
        }
        if (cut) {
            a->entries[a->count - 1].truncated = true;
            break;
        }
        p += fhs + pk;
    }
    return PROVEN_OK;
}

/* 1.8: where the archive starts in a file: 0, or behind a self-extracting module ('MZ' at 0, 'RSFX' at 28) at the
 * first "Rar!" found at offsets 39..32787, which must be the whole marker. SIZE_MAX: not an archive. */
static proven_size_t archive_start(proven_mem_view_t v) {
    if (has_mark(v) || has_mark14(v))
        return 0;
    if (v.size < 39 + 7 || v.ptr[0] != 'M' || v.ptr[1] != 'Z' || v.ptr[28] != 'R' || v.ptr[29] != 'S' || v.ptr[30] != 'F' ||
        v.ptr[31] != 'X')
        return (proven_size_t)-1;
    for (proven_size_t o = 39; o <= 32787 && o + 7 <= v.size; o++) {
        proven_mem_view_t at = {.ptr = v.ptr + o, .size = v.size - o};
        if (v.ptr[o] == MARK[0] && v.ptr[o + 1] == MARK[1] && v.ptr[o + 2] == MARK[2] && v.ptr[o + 3] == MARK[3])
            return has_mark(at) ? o : (proven_size_t)-1;
        if (has_mark14(at))                                          /* rar14.md 1.8: the older marker, same search */
            return o;
    }
    return (proven_size_t)-1;
}

/* 1.1: the blocks of one volume */
static rar15_err_t walk(rar15_archive_t *a, proven_mem_view_t v, bool first) {
    proven_size_t start = archive_start(v);
    if (start == (proven_size_t)-1)
        return RAR15_E_NOT_ARCHIVE;
    if (first)
        a->sfx_size = start;
    if (has_mark14((proven_mem_view_t){.ptr = v.ptr + start, .size = v.size - start}))
        return walk14(a, (proven_mem_view_t){.ptr = v.ptr + start, .size = v.size - start});
    const proven_byte_t *b = v.ptr + start;
    proven_size_t len = v.size - start, p = 7;
    while (len - p >= 7) {
        proven_byte_t type = b[p + 2];
        proven_size_t hs = u16(b + p + 5);
        if (hs < 7 || hs > len - p)
            break;
        if (type == 0x73) {                                          /* 1.3 */
            a->main_flags = u16(b + p + 3);
            a->main_check_ok = hs >= 13 && check16(b + p + 2, 11, u16(b + p));
            if ((a->main_flags & RAR15_MAIN_COMMENT) && hs >= 13 + 13 && !a->comment_block.ptr)
                a->comment_block = (proven_mem_view_t){.ptr = b + p + 13, .size = hs - 13};
            p += hs;
        } else if (type == 0x74) {                                   /* 1.4 */
            if (hs < 32)
                return RAR15_E_HEADER;
            proven_size_t pk = u32(b + p + 7), nl = u16(b + p + 0x1a);
            if (32 + nl > hs)
                return RAR15_E_HEADER;
            bool cut = pk > len - p - hs;                            /* the file ends inside the packed data: */
            if (cut)                                                 /* keep what there is, the walk ends here */
                pk = len - p - hs;
            rar15_err_t e = add_part(a, b + p, hs, pk, nl);
            if (e != PROVEN_OK)
                return e;
            if (cut) {
                a->entries[a->count - 1].truncated = true;
                break;
            }
            p += hs + pk;
        } else {                                                     /* other block types are passed over (1.1) */
            if (type == 0x76 && (a->main_flags & RAR15_MAIN_AUTHENTICITY) && !a->av_block.ptr)
                a->av_block = (proven_mem_view_t){.ptr = b + p, .size = hs};   /* 1.9 */
            proven_size_t extra = 0;
            if ((u16(b + p + 3) & 0x8000u) && hs >= 11)
                extra = u32(b + p + 7);
            if (extra > len - p - hs)
                break;
            p += hs + extra;
        }
    }
    return PROVEN_OK;
}

rar15_err_t rar15_archive_open_mem(rar15_archive_t *a, proven_allocator_t alloc, const proven_mem_view_t *volumes,
                                   proven_size_t nvol) {
    if (!a || !proven_alloc_is_valid(alloc) || !volumes || nvol == 0)
        return PROVEN_ERR_INVALID_ARG;
    *a = (rar15_archive_t){.alloc = alloc, .usable = true};
    for (proven_size_t i = 0; i < nvol; i++) {
        rar15_err_t e = walk(a, volumes[i], i == 0);
        if (e != PROVEN_OK) {
            rar15_archive_close(a);
            return e;
        }
    }
    return PROVEN_OK;
}

static rar15_err_t read_volume(proven_allocator_t alloc, proven_u8str_view_t path, proven_size_t max_bytes,
                               proven_mem_mut_t *out) {
    proven_result_mem_mut_t r =
        max_bytes ? proven_fs_read_all_bounded(alloc, path, max_bytes) : proven_fs_read_all(alloc, path);
    if (!proven_is_ok(r.err))
        return r.err;
    *out = r.value;
    return PROVEN_OK;
}

/* open over buffers this archive owns from now on (also on failure: they are freed) */
static rar15_err_t open_owned(rar15_archive_t *a, proven_allocator_t alloc, proven_mem_mut_t *bufs, proven_size_t n) {
    rar15_err_t e = PROVEN_OK;
    proven_result_mem_mut_t v = alloc.alloc_fn(alloc.ctx, n * sizeof(proven_mem_view_t), alignof(proven_mem_view_t));
    if (!proven_is_ok(v.err)) {
        e = v.err;
    } else {
        proven_mem_view_t *views = (proven_mem_view_t *)(void *)v.value.ptr;
        for (proven_size_t i = 0; i < n; i++)
            views[i] = (proven_mem_view_t){.ptr = bufs[i].ptr, .size = bufs[i].size};
        e = rar15_archive_open_mem(a, alloc, views, n);
        release(alloc, views);
    }
    if (e != PROVEN_OK) {
        for (proven_size_t i = 0; i < n; i++)
            release(alloc, bufs[i].ptr);
        release(alloc, bufs);
        return e;
    }
    a->owned = bufs;
    a->owned_count = n;
    return PROVEN_OK;
}

rar15_err_t rar15_archive_open_paths(rar15_archive_t *a, proven_allocator_t alloc, const proven_u8str_view_t *paths,
                                     proven_size_t npaths, proven_size_t max_bytes) {
    if (!a || !proven_alloc_is_valid(alloc) || !paths || npaths == 0)
        return PROVEN_ERR_INVALID_ARG;
    proven_result_mem_mut_t r = alloc.alloc_fn(alloc.ctx, npaths * sizeof(proven_mem_mut_t), alignof(proven_mem_mut_t));
    if (!proven_is_ok(r.err))
        return r.err;
    proven_mem_mut_t *bufs = (proven_mem_mut_t *)(void *)r.value.ptr;
    for (proven_size_t i = 0; i < npaths; i++) {
        rar15_err_t e = read_volume(alloc, paths[i], max_bytes, &bufs[i]);
        if (e != PROVEN_OK) {
            for (proven_size_t k = 0; k < i; k++)
                release(alloc, bufs[k].ptr);
            release(alloc, bufs);
            return e;
        }
    }
    return open_owned(a, alloc, bufs, npaths);
}

/* the main block's flags of a volume in memory, 0 when it has none where 1.1 puts it */
static proven_u16 peek_main_flags(proven_mem_view_t v) {
    proven_size_t o = archive_start(v);
    if (o == (proven_size_t)-1)
        return 0;
    if (has_mark14((proven_mem_view_t){.ptr = v.ptr + o, .size = v.size - o}))
        return v.ptr[o + 6];                                         /* the flag byte: 0x01 = volume, as in 1.3 */
    if (v.size - o < 7 + 7 || v.ptr[o + 7 + 2] != 0x73)
        return 0;
    return u16(v.ptr + o + 7 + 3);
}

rar15_err_t rar15_archive_open_path(rar15_archive_t *a, proven_allocator_t alloc, proven_u8str_view_t path,
                                    proven_size_t max_bytes) {
    if (!a || !proven_alloc_is_valid(alloc) || !path.ptr || path.size == 0)
        return PROVEN_ERR_INVALID_ARG;
    enum { MAXVOL = 101 };                                           /* NAME.RAR, NAME.R00 .. NAME.R99 (1.6) */
    proven_result_mem_mut_t r = alloc.alloc_fn(alloc.ctx, MAXVOL * sizeof(proven_mem_mut_t), alignof(proven_mem_mut_t));
    if (!proven_is_ok(r.err))
        return r.err;
    proven_mem_mut_t *bufs = (proven_mem_mut_t *)(void *)r.value.ptr;
    proven_size_t n = 0;
    rar15_err_t e = read_volume(alloc, path, max_bytes, &bufs[0]);
    if (e != PROVEN_OK) {
        release(alloc, bufs);
        return e;
    }
    n = 1;
    proven_mem_view_t first = {.ptr = bufs[0].ptr, .size = bufs[0].size};
    if ((peek_main_flags(first) & RAR15_MAIN_VOLUME) && path.size >= 3) {
        proven_result_mem_mut_t nm = alloc.alloc_fn(alloc.ctx, path.size, 1);
        if (proven_is_ok(nm.err)) {
            proven_byte_t *s = nm.value.ptr;
            (void)proven_mem_copy(s, path.size, proven_mem_view_from_u8(path));
            s[path.size - 3] = (s[path.size - 3] >= 'a' && s[path.size - 3] <= 'z') ? 'r' : 'R';   /* NAME.EXE -> NAME.R00 */
            for (unsigned k = 0; k < 100 && n < MAXVOL; k++) {       /* the last two characters become digits */
                s[path.size - 2] = (proven_byte_t)('0' + k / 10);
                s[path.size - 1] = (proven_byte_t)('0' + k % 10);
                e = read_volume(alloc, (proven_u8str_view_t){.ptr = s, .size = path.size}, max_bytes, &bufs[n]);
                if (e != PROVEN_OK)
                    break;
                n++;
            }
            release(alloc, s);
            if (e == PROVEN_ERR_NOT_FOUND)                           /* the set ends where no further file is */
                e = PROVEN_OK;
        } else {
            e = nm.err;
        }
        if (e != PROVEN_OK) {                                        /* a volume that is there and cannot be read */
            for (proven_size_t k = 0; k < n; k++)
                release(alloc, bufs[k].ptr);
            release(alloc, bufs);
            return e;
        }
    }
    return open_owned(a, alloc, bufs, n);
}

void rar15_archive_set_password(rar15_archive_t *a, proven_u8str_view_t password) {
    if (a)
        a->password = password;
}

void rar15_archive_close(rar15_archive_t *a) {
    if (!a || !proven_alloc_is_valid(a->alloc))
        return;
    for (proven_size_t i = 0; i < a->count; i++)
        release(a->alloc, a->entries[i].joined);
    release(a->alloc, a->entries);
    for (proven_size_t i = 0; i < a->owned_count; i++)
        release(a->alloc, a->owned[i].ptr);
    release(a->alloc, a->owned);
    release(a->alloc, a->state);
    *a = (rar15_archive_t){0};
}

/* one entry with the state as it stands (1.5, 1.7, section 2) */
/* What an entry can unpack to at most. A step gives at most 266 bytes (2.9-2.11) and takes at least two flag bits
 * and one more bit, and a flag byte costs at least 5 bits (2.2, 2.6): under 1000 bytes for each packed byte. A
 * header that claims more cannot be met by its data, so no more than this is ever allocated or asked for. */
static proven_size_t reachable(const rar15_entry_t *e) {
    proven_size_t lim;
    if (e->method == 0x30 || e->method == 0)                         /* stored in either format */
        lim = e->packed.size;
    else if (ckd_mul(&lim, e->packed.size, (proven_size_t)4096) || ckd_add(&lim, lim, (proven_size_t)65536))
        return e->size;
    return e->size < lim ? e->size : lim;
}

/* `size`: e->size, or less when the header claims more than the data can give (reachable) */
static rar15_extracted_t decode(rar15_archive_t *a, const rar15_entry_t *e, proven_mem_mut_t dest, proven_size_t size) {
    rar15_extracted_t x = {.err = PROVEN_OK, .data = {.ptr = dest.ptr, .size = 0}};
    bool solid = (a->main_flags & RAR15_MAIN_SOLID) != 0;
    proven_mem_view_t packed = e->packed;
    proven_byte_t *plain = (proven_byte_t *)0;
    if (e->flags & RAR15_FILE_PASSWORD) {                            /* 1.10: the 1.5 key stream is not known; */
        if (!a->old_format || !a->password.ptr) {                    /* rar14.md 1.7: the older cipher is */
            x.err = RAR15_E_ENCRYPTED;
            a->usable = false;
            return x;
        }
        if (packed.size) {
            proven_result_mem_mut_t r = a->alloc.alloc_fn(a->alloc.ctx, packed.size, 1);
            if (!proven_is_ok(r.err)) {
                x.err = r.err;
                a->usable = false;
                return x;
            }
            plain = r.value.ptr;
            keys14 k = keys14_of(a->password);                       /* the keys start anew at each file and run */
            decipher14(&k, plain, packed.ptr, packed.size);          /* on across the parts of a split one */
            packed = (proven_mem_view_t){.ptr = plain, .size = packed.size};
        }
    }
    if (e->version != (a->old_format ? 2 : 0x0f)) {
        release(a->alloc, plain);
        x.err = RAR15_E_VERSION;
        a->usable = false;
        return x;
    }
    if (e->method == (a->old_format ? 0 : 0x30)) {                   /* 1.5: stored; the state is left alone (1.7) */
        proven_size_t n = packed.size < size ? packed.size : size;
        if (n)
            (void)proven_mem_copy(dest.ptr, dest.size, (proven_mem_view_t){.ptr = packed.ptr, .size = n});
        x.data.size = n;
        if (n < e->size)
            x.err = (e->flags & RAR15_FILE_TO_NEXT) ? RAR15_E_INCOMPLETE : RAR15_E_DATA;
    } else {
        if (!solid || !a->usable)
            rar15_init(a->state);
        proven_byte_t none = 0;
        proven_size_t produced = 0;
        x.unpack_status = rar15_unpack(a->state, packed.ptr ? packed.ptr : &none, packed.size,
                                       size ? dest.ptr : &none, size, &produced);
        if (x.unpack_status == RAR15_OK && size < e->size)           /* not possible by the bound above */
            x.unpack_status = RAR15_E_TRUNCATED;
        a->usable = x.unpack_status == RAR15_OK;
        x.data.size = produced;
        if (x.unpack_status != RAR15_OK)
            x.err = (e->flags & RAR15_FILE_TO_NEXT) ? RAR15_E_INCOMPLETE : RAR15_E_DATA;
    }
    release(a->alloc, plain);
    proven_mem_view_t got = {.ptr = x.data.ptr, .size = x.data.size};
    if (x.err == PROVEN_OK && (a->old_format ? (proven_u32)sum16(got) : proven_crc32(got)) != e->crc)
        x.err = RAR15_E_CRC;                                         /* 1.4: reported; the bytes are kept */
    return x;
}

static rar15_err_t need_state(rar15_archive_t *a) {
    if (a->state)
        return PROVEN_OK;
    proven_result_mem_mut_t r = a->alloc.alloc_fn(a->alloc.ctx, sizeof(rar15_state), alignof(rar15_state));
    if (!proven_is_ok(r.err))
        return r.err;
    a->state = (rar15_state *)(void *)r.value.ptr;
    rar15_init(a->state);
    a->next = 0;
    a->usable = true;
    return PROVEN_OK;
}

rar15_extracted_t rar15_archive_extract(rar15_archive_t *a, proven_size_t i, proven_mem_mut_t dest) {
    rar15_extracted_t x = {.err = PROVEN_ERR_INVALID_ARG};
    if (!a || i >= a->count)
        return x;
    const rar15_entry_t *e = &a->entries[i];
    proven_size_t size = reachable(e);
    if (dest.size < size || (size && !dest.ptr)) {
        x.err = PROVEN_ERR_OUT_OF_BOUNDS;
        return x;
    }
    if ((x.err = need_state(a)) != PROVEN_OK)
        return x;
    if (a->main_flags & RAR15_MAIN_SOLID) {                          /* 1.7: the entries before this one first */
        if (i < a->next) {
            rar15_init(a->state);
            a->next = 0;
            a->usable = true;
        }
        while (a->next < i) {
            rar15_extracted_t skip = rar15_archive_extract_alloc(a, a->next);   /* advances a->next */
            if (skip.err == PROVEN_ERR_NOMEM || skip.err == PROVEN_ERR_OUT_OF_BOUNDS) {
                x.err = skip.err;
                return x;
            }
            rar15_extracted_free(a, &skip);
        }
    }
    x = decode(a, e, dest, size);
    a->next = i + 1;
    return x;
}

rar15_extracted_t rar15_archive_extract_alloc(rar15_archive_t *a, proven_size_t i) {
    rar15_extracted_t x = {.err = PROVEN_ERR_INVALID_ARG};
    if (!a || i >= a->count)
        return x;
    proven_size_t size = reachable(&a->entries[i]);
    if (a->max_unpacked && size > a->max_unpacked) {
        x.err = PROVEN_ERR_OUT_OF_BOUNDS;
        return x;
    }
    proven_size_t cap = size ? size : 1;
    proven_result_mem_mut_t r = a->alloc.alloc_fn(a->alloc.ctx, cap, 1);
    if (!proven_is_ok(r.err)) {
        x.err = r.err;
        return x;
    }
    x = rar15_archive_extract(a, i, (proven_mem_mut_t){.ptr = r.value.ptr, .size = cap});
    x.data.ptr = r.value.ptr;
    x.cap = cap;
    return x;
}

void rar15_extracted_free(rar15_archive_t *a, rar15_extracted_t *x) {
    if (!a || !x)
        return;
    if (x->cap)
        release(a->alloc, x->data.ptr);
    x->data = (proven_mem_mut_t){0};
    x->cap = 0;
}

/* rar14.md 1.6: after the main header's 7 bytes: u16 size (of what follows it), u16 text length, then the text as a
 * compressed stream (section 2 of rar15.md, fresh state) enciphered with the fixed keys 0, 7, 77 */
static rar15_extracted_t comment14(rar15_archive_t *a, proven_mem_view_t blk, bool packed) {
    rar15_extracted_t x = {.err = RAR15_E_HEADER};
    if (blk.size >= 2 && !packed) {                                  /* stored: u16 length, the text (an archive comment
                                                                      * without main flag 0x10; every file comment) */
        proven_size_t tl = u16(blk.ptr);
        if (tl > blk.size - 2)
            return x;
        proven_result_mem_mut_t tr = a->alloc.alloc_fn(a->alloc.ctx, tl ? tl : 1, 1);
        if (!proven_is_ok(tr.err)) {
            x.err = tr.err;
            return x;
        }
        if (tl)
            (void)proven_mem_copy(tr.value.ptr, tl, (proven_mem_view_t){.ptr = blk.ptr + 2, .size = tl});
        x.data = (proven_mem_mut_t){.ptr = tr.value.ptr, .size = tl};
        x.cap = tl ? tl : 1;
        x.err = PROVEN_OK;
        return x;
    }
    if (blk.size < 4)
        return x;
    proven_size_t sz = u16(blk.ptr), len = u16(blk.ptr + 2);
    if (sz < 2 || sz - 2 > blk.size - 4)
        return x;
    proven_size_t dn = sz - 2, cap = len ? len : 1;
    proven_result_mem_mut_t r = a->alloc.alloc_fn(a->alloc.ctx, cap, 1);
    proven_result_mem_mut_t pr = a->alloc.alloc_fn(a->alloc.ctx, dn ? dn : 1, 1);
    proven_result_mem_mut_t sr = a->alloc.alloc_fn(a->alloc.ctx, sizeof(rar15_state), alignof(rar15_state));
    if (!proven_is_ok(r.err) || !proven_is_ok(pr.err) || !proven_is_ok(sr.err)) {
        release(a->alloc, proven_is_ok(r.err) ? r.value.ptr : (proven_byte_t *)0);
        release(a->alloc, proven_is_ok(pr.err) ? pr.value.ptr : (proven_byte_t *)0);
        release(a->alloc, proven_is_ok(sr.err) ? sr.value.ptr : (proven_byte_t *)0);
        x.err = PROVEN_ERR_NOMEM;
        return x;
    }
    keys14 k = {0, 7, 77};
    decipher14(&k, pr.value.ptr, blk.ptr + 4, dn);
    rar15_state *st = (rar15_state *)(void *)sr.value.ptr;
    proven_size_t produced = 0;
    rar15_init(st);
    x.unpack_status = rar15_unpack(st, pr.value.ptr, dn, r.value.ptr, len, &produced);
    release(a->alloc, st);
    release(a->alloc, pr.value.ptr);
    x.data = (proven_mem_mut_t){.ptr = r.value.ptr, .size = produced};
    x.cap = cap;
    x.err = x.unpack_status == RAR15_OK ? PROVEN_OK : RAR15_E_DATA;
    return x;
}

/* 1.3: an inner block: u16 check, type 0x75, u16 flags, u16 size, u16 text length, u8 0x0f, u8 method, u16 check of
 * the text, then the text: stored (method 0x30) or packed, decoded from a fresh state */
static rar15_extracted_t comment(rar15_archive_t *a, proven_mem_view_t blk, bool of_file) {
    rar15_extracted_t x = {.err = PROVEN_ERR_NOT_FOUND};
    if (!blk.ptr)
        return x;
    if (a->old_format)
        return comment14(a, blk, !of_file && a->old_comment_packed);
    x.err = RAR15_E_HEADER;
    if (blk.size < 13 || blk.ptr[2] != 0x75)
        return x;
    proven_size_t isz = u16(blk.ptr + 5), len = u16(blk.ptr + 7);
    proven_byte_t method = blk.ptr[10];
    if (isz < 13 || isz > blk.size)
        return x;
    proven_mem_view_t data = {.ptr = blk.ptr + 13, .size = isz - 13};
    proven_size_t cap = len ? len : 1;
    proven_result_mem_mut_t r = a->alloc.alloc_fn(a->alloc.ctx, cap, 1);
    if (!proven_is_ok(r.err)) {
        x.err = r.err;
        return x;
    }
    x.data.ptr = r.value.ptr;
    x.cap = cap;
    x.err = PROVEN_OK;
    if (method == 0x30) {
        proven_size_t n = data.size < len ? data.size : len;
        if (n)
            (void)proven_mem_copy(x.data.ptr, cap, (proven_mem_view_t){.ptr = data.ptr, .size = n});
        x.data.size = n;
        if (n < len)
            x.err = RAR15_E_DATA;
    } else {
        proven_result_mem_mut_t sr = a->alloc.alloc_fn(a->alloc.ctx, sizeof(rar15_state), alignof(rar15_state));
        if (!proven_is_ok(sr.err)) {
            rar15_extracted_free(a, &x);
            x.err = sr.err;
            return x;
        }
        rar15_state *st = (rar15_state *)(void *)sr.value.ptr;
        proven_byte_t none = 0;
        proven_size_t produced = 0;
        rar15_init(st);
        x.unpack_status = rar15_unpack(st, data.size ? data.ptr : &none, data.size, x.data.ptr, len, &produced);
        release(a->alloc, st);
        x.data.size = produced;
        if (x.unpack_status != RAR15_OK)
            x.err = RAR15_E_DATA;
    }
    if (x.err == PROVEN_OK &&
        (proven_u16)(proven_crc32((proven_mem_view_t){.ptr = x.data.ptr, .size = x.data.size}) & 0xffffu) != u16(blk.ptr + 11))
        x.err = RAR15_E_CRC;
    return x;
}

rar15_extracted_t rar15_archive_comment(rar15_archive_t *a) {
    if (!a)
        return (rar15_extracted_t){.err = PROVEN_ERR_INVALID_ARG};
    return comment(a, a->comment_block, false);
}

rar15_extracted_t rar15_entry_comment(rar15_archive_t *a, proven_size_t i) {
    if (!a || i >= a->count)
        return (rar15_extracted_t){.err = PROVEN_ERR_INVALID_ARG};
    return comment(a, a->entries[i].comment_block, true);
}

const char *rar15_err_text(rar15_err_t e) {
    switch (e) {
    case PROVEN_OK: return "ok";
    case RAR15_E_NOT_ARCHIVE: return "not a RAR 1.5 archive";
    case RAR15_E_HEADER: return "broken block header";
    case RAR15_E_ENCRYPTED: return "encrypted (not supported)";
    case RAR15_E_VERSION: return "written by another format version (not supported)";
    case RAR15_E_DATA: return "damaged or cut data";
    case RAR15_E_CRC: return "CRC failed";
    case RAR15_E_INCOMPLETE: return "continues in a volume that is missing";
    case PROVEN_ERR_NOMEM: return "out of memory";
    case PROVEN_ERR_NOT_FOUND: return "file not found";
    case PROVEN_ERR_IO: return "input / output error";
    case PROVEN_ERR_PERMISSION: return "permission denied";
    case PROVEN_ERR_OUT_OF_BOUNDS: return "too large";
    case PROVEN_ERR_INVALID_ARG: return "invalid argument";
    default: return "error";
    }
}
