#include "rubraview/sevenzip.h"
#include "rubraview/utf8.h"

#include <stdlib.h>
#include <string.h>

/* The vendored LZMA SDK supplies the 7z container parser and the LZMA,
   LZMA2, PPMd, BCJ and delta decoders (owner decision D-3). Only this
   module includes it, per the RFC-0001 §8.1 one-module rule. AES is
   deliberately not vendored, so an encrypted archive surfaces as an
   unsupported coder rather than as a password prompt this viewer has no
   way to answer. */
#include "7z.h"
#include "7zCrc.h"
#include "7zAlloc.h"
#include "LzmaDec.h"
#include "Lzma2Dec.h"

/* ------------------------------------------------------------------ */
/* A seekable stream over a buffer that is already in memory.
   `7zFile.c` is not vendored: it opens files, and §3.8.1 forbids this
   viewer from putting archive content on disk at all. */

typedef struct mem_seek_stream {
    ISeekInStream vt;
    const uint8_t *data;
    size_t size;
    size_t pos;
} mem_seek_stream_t;

static SRes mem_stream_read(ISeekInStreamPtr pp, void *buf, size_t *size) {
    mem_seek_stream_t *s = Z7_CONTAINER_FROM_VTBL(pp, mem_seek_stream_t, vt);
    size_t want = *size;
    size_t left = s->size - s->pos;
    if (want > left) want = left;
    if (want > 0) memcpy(buf, s->data + s->pos, want);
    s->pos += want;
    *size = want;   /* a short read is end-of-stream, not an error */
    return SZ_OK;
}

static SRes mem_stream_seek(ISeekInStreamPtr pp, Int64 *pos, ESzSeek origin) {
    mem_seek_stream_t *s = Z7_CONTAINER_FROM_VTBL(pp, mem_seek_stream_t, vt);
    Int64 base = 0;
    switch (origin) {
        case SZ_SEEK_SET: base = 0; break;
        case SZ_SEEK_CUR: base = (Int64)s->pos; break;
        case SZ_SEEK_END: base = (Int64)s->size; break;
        default: return SZ_ERROR_PARAM;
    }
    Int64 target = base + *pos;
    if (target < 0 || (UInt64)target > (UInt64)s->size) return SZ_ERROR_PARAM;
    s->pos = (size_t)target;
    *pos = target;
    return SZ_OK;
}

/* ------------------------------------------------------------------ */
/* Everything the archive owns on the heap. The SDK allocates its index
   with malloc/free rather than from the caller's arena on purpose: the
   decoded block is reallocated every time the reader crosses into
   another solid block, and an arena cannot take that memory back. */

#define SZ_LOOK_BUFFER 65536u

typedef struct sz_state {
    CSzArEx db;
    mem_seek_stream_t stream;
    CLookToRead2 look;
    Byte look_buffer[SZ_LOOK_BUFFER];
    ISzAlloc alloc_main;
    ISzAlloc alloc_temp;

    /* §3.8.2's persistent decoder: the block that is currently unpacked,
       kept so a run of pages inside one solid block decodes once. */
    UInt32 block_index;
    Byte  *block;
    size_t block_size;
    bool   opened;

    /* A block too large to hold (owner, 2026-09-28: a CB7 is often one
       solid block of the whole book, several GB) is streamed instead: one
       decoder kept where the reader is, so turning forwards goes on from
       there and only going back starts again. */
    struct {
        UInt32      folder;     /* 0xFFFFFFFF: none */
        UInt32      method;
        CLzmaDec    lzma;
        CLzma2Dec   lzma2;
        bool        has_lzma, has_lzma2;
        UInt64      out_pos;    /* bytes of the block decoded so far */
        UInt64      out_size;   /* the block's whole size */
        const Byte *pack;
        UInt64      pack_left;
        Byte       *chunk;
    } s;
} sz_state_t;

#define SZ_METHOD_COPY  0u
#define SZ_METHOD_LZMA2 0x21u
#define SZ_METHOD_LZMA  0x30101u
#define SZ_STREAM_CHUNK (1u << 20)
#define SZ_STREAM_MAX_DICT ((UInt64)1 << 30)   /* the decoder's window: the only large thing streaming holds */

static void *sz_malloc(ISzAllocPtr p, size_t size) { (void)p; return size ? malloc(size) : NULL; }
static void  sz_free(ISzAllocPtr p, void *addr) { (void)p; free(addr); }

static rubraview_sz_err_t map_sres(SRes res) {
    switch (res) {
        case SZ_OK: return RUBRAVIEW_SZ_OK;
        case SZ_ERROR_NO_ARCHIVE: return RUBRAVIEW_SZ_ERR_NOT_A_7Z;
        case SZ_ERROR_UNSUPPORTED: return RUBRAVIEW_SZ_ERR_UNSUPPORTED_CODER;
        case SZ_ERROR_CRC: return RUBRAVIEW_SZ_ERR_CORRUPT_STREAM;
        case SZ_ERROR_MEM: return RUBRAVIEW_SZ_ERR_OUT_OF_MEMORY;
        default: return RUBRAVIEW_SZ_ERR_CORRUPT;
    }
}

/* The SDK stores names as UTF-16; everything above this line is UTF-8.
   Surrogate pairs are joined rather than passed through, so a name
   containing an emoji or a rare Han character survives the crossing. */
static u8str_t name_to_utf8(proven_arena_t *arena, const UInt16 *src, size_t units) {
    u8str_t empty = { .ptr = "", .len = 0 };
    if (units == 0) return empty;

    size_t cap = units * 4 + 1;
    proven_result_mem_mut_t res = proven_arena_alloc(arena, cap);
    if (!proven_is_ok(res.err)) return empty;
    char *out = (char*)(void*)res.value.ptr;

    size_t pos = 0;
    for (size_t i = 0; i < units; ++i) {
        uint32_t cp = src[i];
        if (cp >= 0xD800u && cp <= 0xDBFFu && i + 1 < units &&
            src[i + 1] >= 0xDC00u && src[i + 1] <= 0xDFFFu) {
            cp = 0x10000u + ((cp - 0xD800u) << 10) + (src[i + 1] - 0xDC00u);
            i++;
        }
        rubraview_utf8_encode(cp, (uint8_t*)out, cap - 1, &pos);
    }
    out[pos] = '\0';   /* §7.2.3: allocate len + 1 and terminate */
    return (u8str_t){ .ptr = out, .len = pos };
}

/* ------------------------------------------------------------------ */

static const uint8_t SZ_SIGNATURE[6] = { '7', 'z', 0xBC, 0xAF, 0x27, 0x1C };

rubraview_sz_result_t rubraview_sz_open(proven_arena_t *arena,
                                        const uint8_t *data, size_t size,
                                        uint64_t max_folder_bytes) {
    rubraview_sz_result_t result = { .err = RUBRAVIEW_SZ_ERR_NOT_A_7Z, .value = {0} };
    if (!arena || !data || size < 32) return result;
    if (memcmp(data, SZ_SIGNATURE, sizeof(SZ_SIGNATURE)) != 0) return result;

    /* The CRC table is global to the SDK and idempotent to build. */
    CrcGenerateTable();

    sz_state_t *st = (sz_state_t*)calloc(1, sizeof(sz_state_t));
    if (!st) { result.err = RUBRAVIEW_SZ_ERR_OUT_OF_MEMORY; return result; }

    st->alloc_main.Alloc = sz_malloc; st->alloc_main.Free = sz_free;
    st->alloc_temp.Alloc = sz_malloc; st->alloc_temp.Free = sz_free;

    st->stream.vt.Read = mem_stream_read;
    st->stream.vt.Seek = mem_stream_seek;
    st->stream.data = data;
    st->stream.size = size;
    st->stream.pos = 0;

    LookToRead2_CreateVTable(&st->look, False);
    st->look.buf = st->look_buffer;
    st->look.bufSize = SZ_LOOK_BUFFER;
    st->look.realStream = &st->stream.vt;
    LookToRead2_INIT(&st->look)

    SzArEx_Init(&st->db);
    st->opened = true;
    st->block_index = 0xFFFFFFFFu;
    st->s.folder = 0xFFFFFFFFu;

    SRes res = SzArEx_Open(&st->db, &st->look.vt, &st->alloc_main, &st->alloc_temp);
    if (res != SZ_OK) {
        result.err = map_sres(res);
        SzArEx_Free(&st->db, &st->alloc_main);
        free(st);
        return result;
    }

    /* Directories carry no data; the reader never wants them, and the
       page source would have to filter them out anyway. */
    size_t files = 0;
    for (UInt32 i = 0; i < st->db.NumFiles; ++i) {
        if (!SzArEx_IsDir(&st->db, i)) files++;
    }

    rubraview_sz_entry_t *entries = NULL;
    if (files > 0) {
        proven_result_mem_mut_t res_e = rubraview_arena_alloc_array(arena, files, sizeof(*entries));
        entries = proven_is_ok(res_e.err) ? (rubraview_sz_entry_t*)(void*)res_e.value.ptr : NULL;
        if (!entries) {
            SzArEx_Free(&st->db, &st->alloc_main);
            free(st);
            result.err = RUBRAVIEW_SZ_ERR_OUT_OF_MEMORY;
            return result;
        }
    }

    size_t out = 0;
    for (UInt32 i = 0; i < st->db.NumFiles; ++i) {
        if (SzArEx_IsDir(&st->db, i)) continue;

        size_t units = SzArEx_GetFileNameUtf16(&st->db, i, NULL); /* includes the terminator */
        u8str_t name = { .ptr = "", .len = 0 };
        if (units > 1 && units < 4096) {
            UInt16 stack_name[1024];
            UInt16 *buf = units <= 1024 ? stack_name : (UInt16*)malloc(units * sizeof(UInt16));
            if (buf) {
                SzArEx_GetFileNameUtf16(&st->db, i, buf);
                name = name_to_utf8(arena, buf, units - 1);
                if (buf != stack_name) free(buf);
            }
        }

        entries[out].name = name;
        entries[out].size = SzArEx_GetFileSize(&st->db, i);
        entries[out].file_index = i;
        out++;
    }

    result.err = RUBRAVIEW_SZ_OK;
    result.value.data = data;
    result.value.size = size;
    result.value.entries = entries;
    result.value.entry_count = out;
    result.value.max_folder_bytes = max_folder_bytes;
    result.value.state = st;
    return result;
}

static void stream_end(sz_state_t *st) {
    if (st->s.has_lzma) LzmaDec_Free(&st->s.lzma, &st->alloc_main);
    if (st->s.has_lzma2) Lzma2Dec_Free(&st->s.lzma2, &st->alloc_main);
    st->s.has_lzma = st->s.has_lzma2 = false;
    st->s.folder = 0xFFFFFFFFu;
}

/* Starts `folder` from its first byte. Only a block of one coder reading
   one packed stream is streamed — LZMA, LZMA2 or stored, which is what a
   comic's CB7 holds; anything else is refused as too large, as before. */
static rubraview_sz_err_t stream_begin(sz_state_t *st, UInt32 folder) {
    stream_end(st);
    const CSzAr *ar = &st->db.db;
    const Byte *data = ar->CodersData + ar->FoCodersOffsets[folder];
    CSzData sd = { .Data = data, .Size = ar->FoCodersOffsets[(size_t)folder + 1] - ar->FoCodersOffsets[folder] };
    CSzFolder f;
    if (SzGetNextFolderItem(&f, &sd) != SZ_OK) return RUBRAVIEW_SZ_ERR_CORRUPT;
    if (f.NumCoders != 1 || f.NumPackStreams != 1) return RUBRAVIEW_SZ_ERR_TOO_LARGE;
    const CSzCoderInfo *c = &f.Coders[0];
    UInt32 pack_index = ar->FoStartPackStreamIndex[folder];
    UInt64 start = st->db.dataPos + ar->PackPositions[pack_index];
    UInt64 size = ar->PackPositions[(size_t)pack_index + 1] - ar->PackPositions[pack_index];
    if (start > st->stream.size || size > st->stream.size - start) return RUBRAVIEW_SZ_ERR_CORRUPT;
    const Byte *props = data + c->PropsOffset;

    if (c->MethodID == SZ_METHOD_LZMA) {
        if (c->PropsSize != LZMA_PROPS_SIZE) return RUBRAVIEW_SZ_ERR_CORRUPT;
        UInt64 dict = (UInt64)props[1] | ((UInt64)props[2] << 8) | ((UInt64)props[3] << 16) | ((UInt64)props[4] << 24);
        if (dict > SZ_STREAM_MAX_DICT) return RUBRAVIEW_SZ_ERR_TOO_LARGE;
        LzmaDec_Construct(&st->s.lzma);
        if (LzmaDec_Allocate(&st->s.lzma, props, c->PropsSize, &st->alloc_main) != SZ_OK) return RUBRAVIEW_SZ_ERR_OUT_OF_MEMORY;
        st->s.has_lzma = true;
        LzmaDec_Init(&st->s.lzma);
    } else if (c->MethodID == SZ_METHOD_LZMA2) {
        if (c->PropsSize != 1 || props[0] > 40) return RUBRAVIEW_SZ_ERR_CORRUPT;
        UInt64 dict = props[0] == 40 ? 0xFFFFFFFFu : ((UInt64)(2 | (props[0] & 1)) << (props[0] / 2 + 11));
        if (dict > SZ_STREAM_MAX_DICT) return RUBRAVIEW_SZ_ERR_TOO_LARGE;
        Lzma2Dec_Construct(&st->s.lzma2);
        if (Lzma2Dec_Allocate(&st->s.lzma2, props[0], &st->alloc_main) != SZ_OK) return RUBRAVIEW_SZ_ERR_OUT_OF_MEMORY;
        st->s.has_lzma2 = true;
        Lzma2Dec_Init(&st->s.lzma2);
    } else if (c->MethodID != SZ_METHOD_COPY) {
        return RUBRAVIEW_SZ_ERR_TOO_LARGE;
    }
    if (!st->s.chunk) {
        st->s.chunk = (Byte*)malloc(SZ_STREAM_CHUNK);
        if (!st->s.chunk) { stream_end(st); return RUBRAVIEW_SZ_ERR_OUT_OF_MEMORY; }
    }
    st->s.folder = folder;
    st->s.method = c->MethodID;
    st->s.out_pos = 0;
    st->s.out_size = SzAr_GetFolderUnpackSize(ar, folder);
    st->s.pack = st->stream.data + start;
    st->s.pack_left = size;
    return RUBRAVIEW_SZ_OK;
}

/* Bytes [offset, offset + n) of `folder` into `dst`. The decoder never
   runs past the end of what was asked, so the next page goes on from
   exactly there. */
static rubraview_sz_err_t stream_read(sz_state_t *st, UInt32 folder, UInt64 offset, size_t n, Byte *dst) {
    if (st->s.folder != folder || offset < st->s.out_pos) {
        rubraview_sz_err_t begun = stream_begin(st, folder);
        if (begun != RUBRAVIEW_SZ_OK) return begun;
    }
    if (offset > st->s.out_size || n > st->s.out_size - offset) return RUBRAVIEW_SZ_ERR_CORRUPT;
    UInt64 end = offset + n;
    while (st->s.out_pos < end) {
        UInt64 left = end - st->s.out_pos;
        SizeT out_len = left < SZ_STREAM_CHUNK ? (SizeT)left : SZ_STREAM_CHUNK;
        SizeT in_len = st->s.pack_left > (UInt64)(SizeT)-1 ? (SizeT)-1 : (SizeT)st->s.pack_left;
        ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
        SRes res = SZ_OK;
        if (st->s.method == SZ_METHOD_LZMA) {
            res = LzmaDec_DecodeToBuf(&st->s.lzma, st->s.chunk, &out_len, st->s.pack, &in_len, LZMA_FINISH_ANY, &status);
        } else if (st->s.method == SZ_METHOD_LZMA2) {
            res = Lzma2Dec_DecodeToBuf(&st->s.lzma2, st->s.chunk, &out_len, st->s.pack, &in_len, LZMA_FINISH_ANY, &status);
        } else {
            if (out_len > in_len) out_len = in_len;
            memcpy(st->s.chunk, st->s.pack, out_len);
            in_len = out_len;
        }
        if (res != SZ_OK) { stream_end(st); return RUBRAVIEW_SZ_ERR_CORRUPT_STREAM; }
        if (out_len == 0) { stream_end(st); return RUBRAVIEW_SZ_ERR_CORRUPT_STREAM; }   /* no progress: the stream is short */
        st->s.pack += in_len;
        st->s.pack_left -= in_len;
        UInt64 from = st->s.out_pos, to = st->s.out_pos + out_len;
        if (to > offset) {
            UInt64 a = from > offset ? from : offset;
            memcpy(dst + (a - offset), st->s.chunk + (a - from), (size_t)(to - a));
        }
        st->s.out_pos = to;
    }
    return RUBRAVIEW_SZ_OK;
}

rubraview_sz_data_result_t rubraview_sz_read_entry(proven_arena_t *arena,
                                                   rubraview_sz_archive_t *archive,
                                                   size_t entry_index,
                                                   uint64_t max_entry_bytes) {
    rubraview_sz_data_result_t out = { .err = RUBRAVIEW_SZ_ERR_BAD_INDEX,
                                       .data = { .ptr = "", .len = 0 } };
    if (!arena || !archive || !archive->state || entry_index >= archive->entry_count) return out;

    sz_state_t *st = (sz_state_t*)archive->state;
    const rubraview_sz_entry_t *entry = &archive->entries[entry_index];
    UInt32 file_index = entry->file_index;

    if (entry->size > max_entry_bytes) { out.err = RUBRAVIEW_SZ_ERR_TOO_LARGE; return out; }

    /* An empty file belongs to no folder; the SDK marks that with an
       out-of-range folder index rather than with a flag. */
    UInt32 folder = st->db.FileToFolder ? st->db.FileToFolder[file_index] : 0xFFFFFFFFu;
    if (folder >= st->db.db.NumFolders) {
        out.err = entry->size == 0 ? RUBRAVIEW_SZ_OK : RUBRAVIEW_SZ_ERR_CORRUPT;
        return out;
    }

    /* §10.2, and the reason this guard is on the folder rather than the
       file: a solid block is allocated whole, so a 4 KB page inside a
       40 GB block still costs 40 GB. Checked before anything is
       committed, and only when the block is not already in hand. */
    if (folder != st->block_index) {
        UInt64 folder_bytes = SzAr_GetFolderUnpackSize(&st->db.db, folder);
        if (folder_bytes > archive->max_folder_bytes) {
            /* Too large to hold: streamed, keeping only this page. */
            UInt64 first = st->db.UnpackPositions[st->db.FolderToFile[folder]];
            UInt64 at = st->db.UnpackPositions[file_index];
            if (at < first) { out.err = RUBRAVIEW_SZ_ERR_CORRUPT; return out; }
            proven_result_mem_mut_t res_s = proven_arena_alloc(arena, (size_t)entry->size + 1);
            if (!proven_is_ok(res_s.err)) { out.err = RUBRAVIEW_SZ_ERR_OUT_OF_MEMORY; return out; }
            rubraview_sz_err_t got = stream_read(st, folder, at - first, (size_t)entry->size, (Byte*)res_s.value.ptr);
            if (got != RUBRAVIEW_SZ_OK) { out.err = got; return out; }
            res_s.value.ptr[entry->size] = '\0';
            out.err = RUBRAVIEW_SZ_OK;
            out.data = (u8str_t){ .ptr = (const char*)res_s.value.ptr, .len = (size_t)entry->size };
            return out;
        }
    }

    size_t offset = 0, processed = 0;
    SRes res = SzArEx_Extract(&st->db, &st->look.vt, file_index,
                              &st->block_index, &st->block, &st->block_size,
                              &offset, &processed,
                              &st->alloc_main, &st->alloc_temp);
    if (res != SZ_OK) {
        out.err = map_sres(res);
        return out;
    }
    if (processed != entry->size) {
        /* The index and the stream disagree about how long this file is. */
        out.err = RUBRAVIEW_SZ_ERR_CORRUPT_STREAM;
        return out;
    }

    proven_result_mem_mut_t res_c = proven_arena_alloc(arena, processed + 1);
    if (!proven_is_ok(res_c.err)) { out.err = RUBRAVIEW_SZ_ERR_OUT_OF_MEMORY; return out; }
    char *copy = (char*)(void*)res_c.value.ptr;
    if (processed > 0) memcpy(copy, st->block + offset, processed);
    copy[processed] = '\0';

    out.err = RUBRAVIEW_SZ_OK;
    out.data = (u8str_t){ .ptr = copy, .len = processed };
    return out;
}

void rubraview_sz_close(rubraview_sz_archive_t *archive) {
    if (!archive || !archive->state) return;
    sz_state_t *st = (sz_state_t*)archive->state;
    if (st->block) { ISzAlloc_Free(&st->alloc_main, st->block); st->block = NULL; }
    stream_end(st);
    free(st->s.chunk);
    if (st->opened) SzArEx_Free(&st->db, &st->alloc_main);
    free(st);
    archive->state = NULL;
    archive->entries = NULL;
    archive->entry_count = 0;
}
