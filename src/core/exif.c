#include "rubraview/exif.h"
#include <string.h>

#define JPEG_MARKER_SOI 0xD8
#define JPEG_MARKER_EOI 0xD9
#define JPEG_MARKER_SOS 0xDA
#define JPEG_MARKER_APP1 0xE1
#define JPEG_MARKER_APP13 0xED

static uint16_t rd_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

typedef struct jpeg_marker_iter {
    const uint8_t *data;
    size_t size;
    size_t pos; /* offset of the next 0xFF marker byte to read */
} jpeg_marker_iter_t;

typedef struct jpeg_marker {
    uint8_t code;
    size_t  segment_start; /* offset of the 0xFF byte */
    size_t  payload_start; /* offset of the first payload byte, valid only if has_length */
    size_t  payload_len;   /* length field minus the 2 length bytes themselves */
    size_t  segment_end;   /* one past this segment's last byte; where iteration resumes */
    bool    has_length;
    bool    ok;            /* false: the buffer ended or was malformed here; iteration must stop */
} jpeg_marker_t;

/* Markers with no following length field: TEM (0x01) and the restart
   markers RST0-7 (0xD0-0xD7); EOI (0xD9) is also length-less but is
   handled as a stop condition by callers rather than "no length". */
static bool marker_has_no_length(uint8_t code) {
    return code == 0x01 || (code >= 0xD0 && code <= 0xD7);
}

static jpeg_marker_t jpeg_next_marker(jpeg_marker_iter_t *it) {
    jpeg_marker_t m = {0};

    if (it->pos + 2 > it->size || it->data[it->pos] != 0xFF) {
        return m; /* ok=false */
    }

    uint8_t code = it->data[it->pos + 1];
    m.code = code;
    m.segment_start = it->pos;

    if (code == JPEG_MARKER_EOI || marker_has_no_length(code)) {
        m.has_length = false;
        m.segment_end = it->pos + 2;
        m.ok = true;
        it->pos = m.segment_end;
        return m;
    }

    if (it->pos + 4 > it->size) return m; /* ok=false: truncated before the length field */

    uint16_t length = rd_be16(it->data + it->pos + 2);
    if (length < 2 || it->pos + 2 + length > it->size) return m; /* ok=false */

    m.has_length = true;
    m.payload_start = it->pos + 4;
    m.payload_len = (size_t)length - 2;
    m.segment_end = it->pos + 2 + length;
    m.ok = true;
    it->pos = m.segment_end;
    return m;
}

static uint16_t tiff_u16(const uint8_t *p, bool big_endian) {
    return big_endian ? (uint16_t)(((uint16_t)p[0] << 8) | p[1])
                       : (uint16_t)(((uint16_t)p[1] << 8) | p[0]);
}

static bool tiff_read_u16(const uint8_t *base, size_t base_len, size_t off, bool big_endian, uint16_t *out) {
    if (off + 2 > base_len) return false;
    *out = tiff_u16(base + off, big_endian);
    return true;
}

static bool tiff_read_u32(const uint8_t *base, size_t base_len, size_t off, bool big_endian, uint32_t *out) {
    if (off + 4 > base_len) return false;
    *out = big_endian
        ? ((uint32_t)base[off] << 24) | ((uint32_t)base[off + 1] << 16) | ((uint32_t)base[off + 2] << 8) | base[off + 3]
        : ((uint32_t)base[off + 3] << 24) | ((uint32_t)base[off + 2] << 16) | ((uint32_t)base[off + 1] << 8) | base[off];
    return true;
}

/* `base` points at the start of the TIFF header (right after "Exif\0\0"),
   bounded to base_len bytes (the remainder of the APP1 payload). */
static int32_t orientation_from_tiff(const uint8_t *base, size_t base_len) {
    if (base_len < 8) return 1;

    bool big_endian;
    if (base[0] == 'I' && base[1] == 'I') big_endian = false;
    else if (base[0] == 'M' && base[1] == 'M') big_endian = true;
    else return 1;

    uint16_t magic;
    if (!tiff_read_u16(base, base_len, 2, big_endian, &magic) || magic != 42) return 1;

    uint32_t ifd0_off;
    if (!tiff_read_u32(base, base_len, 4, big_endian, &ifd0_off)) return 1;

    uint16_t entry_count;
    if (!tiff_read_u16(base, base_len, ifd0_off, big_endian, &entry_count)) return 1;

    for (uint16_t i = 0; i < entry_count; ++i) {
        size_t entry_off = (size_t)ifd0_off + 2 + (size_t)i * 12;
        uint16_t tag;
        if (!tiff_read_u16(base, base_len, entry_off, big_endian, &tag)) return 1;
        if (tag == 0x0112) {
            uint16_t value;
            if (!tiff_read_u16(base, base_len, entry_off + 8, big_endian, &value)) return 1;
            return (value >= 1 && value <= 8) ? (int32_t)value : 1;
        }
    }
    return 1;
}

int32_t rubraview_exif_read_orientation(const uint8_t *jpeg, size_t size) {
    if (!jpeg || size < 2 || jpeg[0] != 0xFF || jpeg[1] != JPEG_MARKER_SOI) return 1;

    jpeg_marker_iter_t it = { .data = jpeg, .size = size, .pos = 2 };
    while (true) {
        jpeg_marker_t m = jpeg_next_marker(&it);
        if (!m.ok) break;

        if (m.code == JPEG_MARKER_APP1 && m.has_length && m.payload_len >= 6 &&
            memcmp(jpeg + m.payload_start, "Exif\0\0", 6) == 0) {
            return orientation_from_tiff(jpeg + m.payload_start + 6, m.payload_len - 6);
        }

        if (m.code == JPEG_MARKER_SOS || m.code == JPEG_MARKER_EOI) break;
    }
    return 1;
}

rubraview_jpeg_strip_result_t rubraview_jpeg_privacy_strip(proven_arena_t *arena, const uint8_t *jpeg, size_t size) {
    rubraview_jpeg_strip_result_t result = {0};
    result.data = (u8str_t){ .ptr = "", .len = 0 };

    if (!arena || !jpeg || size < 2 || jpeg[0] != 0xFF || jpeg[1] != JPEG_MARKER_SOI) {
        return result;
    }

    proven_result_mem_mut_t res = proven_arena_alloc(arena, size); /* output is never larger than input */
    if (!proven_is_ok(res.err)) return result;
    uint8_t *out = res.value.ptr;
    size_t out_pos = 0;

    memcpy(out, jpeg, 2);
    out_pos = 2;

    jpeg_marker_iter_t it = { .data = jpeg, .size = size, .pos = 2 };
    while (true) {
        jpeg_marker_t m = jpeg_next_marker(&it);
        if (!m.ok) break;

        bool strip_this = false;
        if (m.code == JPEG_MARKER_APP1 && m.has_length) {
            if (m.payload_len >= 6 && memcmp(jpeg + m.payload_start, "Exif\0\0", 6) == 0) {
                strip_this = true;
                result.stripped_exif = true;
            } else if (m.payload_len >= 29 && memcmp(jpeg + m.payload_start, "http://ns.adobe.com/xap/1.0/\0", 29) == 0) {
                strip_this = true;
                result.stripped_xmp = true;
            }
        } else if (m.code == JPEG_MARKER_APP13) {
            strip_this = true;
            result.stripped_iptc = true;
        }

        if (!strip_this) {
            size_t seg_len = m.segment_end - m.segment_start;
            memcpy(out + out_pos, jpeg + m.segment_start, seg_len);
            out_pos += seg_len;
        }

        if (m.code == JPEG_MARKER_SOS || m.code == JPEG_MARKER_EOI) break;
    }

    if (it.pos < size) {
        size_t tail_len = size - it.pos;
        memcpy(out + out_pos, jpeg + it.pos, tail_len);
        out_pos += tail_len;
    }

    proven_result_mem_mut_t shrunk = proven_arena_realloc_aligned(arena, out, size, out_pos, PROVEN_DEFAULT_ALIGNMENT);
    uint8_t *final_ptr = proven_is_ok(shrunk.err) ? shrunk.value.ptr : out;

    result.data = (u8str_t){ .ptr = (const char*)final_ptr, .len = out_pos };
    return result;
}
