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

/* ---- the whole EXIF block (owner, 2026-09-29: the information window) ---- */

typedef struct tiff_ctx {
    const uint8_t *base;
    size_t len;
    bool be;
} tiff_ctx_t;

static size_t type_size(uint16_t type) {
    switch (type) {
        case 1: case 2: case 6: case 7: return 1;
        case 3: case 8: return 2;
        case 4: case 9: case 11: return 4;
        case 5: case 10: case 12: return 8;
        default: return 0;
    }
}

/* Where an entry's value is, and how many bytes it has; false when it runs
   out of the block. */
static bool entry_value(const tiff_ctx_t *t, size_t entry, uint16_t *type, uint32_t *count, size_t *at, size_t *bytes) {
    uint32_t c = 0, off = 0;
    if (!tiff_read_u16(t->base, t->len, entry + 2, t->be, type) ||
        !tiff_read_u32(t->base, t->len, entry + 4, t->be, &c)) return false;
    size_t unit = type_size(*type);
    if (unit == 0 || c == 0 || c > 0x10000u) return false;
    size_t n = unit * (size_t)c;
    if (n <= 4) { *at = entry + 8; }
    else {
        if (!tiff_read_u32(t->base, t->len, entry + 8, t->be, &off)) return false;
        *at = off;
    }
    if (*at > t->len || n > t->len - *at) return false;
    *count = c;
    *bytes = n;
    return true;
}

static uint32_t entry_uint(const tiff_ctx_t *t, size_t entry) {
    uint16_t type; uint32_t count; size_t at, bytes;
    if (!entry_value(t, entry, &type, &count, &at, &bytes)) return 0;
    uint16_t v16; uint32_t v32;
    if (type == 3 && tiff_read_u16(t->base, t->len, at, t->be, &v16)) return v16;
    if ((type == 4 || type == 9) && tiff_read_u32(t->base, t->len, at, t->be, &v32)) return v32;
    if (type == 1 || type == 7) return t->base[at];
    return 0;
}

/* The `index`th rational of an entry: false when there is none or it
   divides by nothing. Signed for SRATIONAL. */
static bool entry_rational(const tiff_ctx_t *t, size_t entry, uint32_t index, double *out, uint32_t *num, uint32_t *den) {
    uint16_t type; uint32_t count; size_t at, bytes;
    if (!entry_value(t, entry, &type, &count, &at, &bytes) || (type != 5 && type != 10) || index >= count) return false;
    uint32_t n, d;
    if (!tiff_read_u32(t->base, t->len, at + index * 8, t->be, &n) ||
        !tiff_read_u32(t->base, t->len, at + index * 8 + 4, t->be, &d) || d == 0) return false;
    *out = type == 10 ? (double)(int32_t)n / (double)(int32_t)d : (double)n / (double)d;
    if (num) *num = n;
    if (den) *den = d;
    return true;
}

static void entry_text(const tiff_ctx_t *t, size_t entry, char *dst, size_t cap) {
    uint16_t type; uint32_t count; size_t at, bytes;
    dst[0] = '\0';
    if (!entry_value(t, entry, &type, &count, &at, &bytes) || (type != 2 && type != 7 && type != 1)) return;
    size_t n = 0;
    for (size_t i = 0; i < bytes && n + 1 < cap; ++i) {
        char c = (char)t->base[at + i];
        if (c == '\0') break;
        dst[n++] = (unsigned char)c < 0x20 ? ' ' : c;
    }
    while (n > 0 && dst[n - 1] == ' ') --n;
    dst[n] = '\0';
}

/* "2024:05:01 13:22:07" -> "2024-05-01 13:22:07" */
static void entry_date(const tiff_ctx_t *t, size_t entry, char *dst, size_t cap) {
    entry_text(t, entry, dst, cap);
    if (strlen(dst) >= 10 && dst[4] == ':' && dst[7] == ':') { dst[4] = '-'; dst[7] = '-'; }
}

static bool entry_degrees(const tiff_ctx_t *t, size_t entry, double *out) {
    double d = 0, m = 0, sec = 0;
    if (!entry_rational(t, entry, 0, &d, NULL, NULL)) return false;
    (void)entry_rational(t, entry, 1, &m, NULL, NULL);
    (void)entry_rational(t, entry, 2, &sec, NULL, NULL);
    *out = d + m / 60.0 + sec / 3600.0;
    return true;
}

enum { IFD_MAIN, IFD_EXIF, IFD_GPS };

static void read_ifd(const tiff_ctx_t *t, uint32_t at, int kind, rubraview_exif_info_t *x,
                     uint32_t *exif_ifd, uint32_t *gps_ifd) {
    uint16_t n;
    if (!tiff_read_u16(t->base, t->len, at, t->be, &n) || n > 1000) return;
    char lat_ref = 0, lon_ref = 0;
    bool have_lat = false, have_lon = false;
    double lat = 0, lon = 0;
    int alt_below = 0;
    for (uint16_t i = 0; i < n; ++i) {
        size_t e = (size_t)at + 2 + (size_t)i * 12;
        uint16_t tag;
        if (!tiff_read_u16(t->base, t->len, e, t->be, &tag)) return;
        double v;
        uint32_t num, den;
        char ref[4];
        if (kind == IFD_MAIN) {
            switch (tag) {
                case 0x010F: entry_text(t, e, x->make, sizeof(x->make)); break;
                case 0x0110: entry_text(t, e, x->model, sizeof(x->model)); break;
                case 0x0112: x->orientation = (int32_t)entry_uint(t, e); break;
                case 0x0131: entry_text(t, e, x->software, sizeof(x->software)); break;
                case 0x0132: entry_date(t, e, x->date_modified, sizeof(x->date_modified)); break;
                case 0x013B: entry_text(t, e, x->artist, sizeof(x->artist)); break;
                case 0x8298: entry_text(t, e, x->copyright, sizeof(x->copyright)); break;
                case 0x8769: if (exif_ifd) *exif_ifd = entry_uint(t, e); break;
                case 0x8825: if (gps_ifd) *gps_ifd = entry_uint(t, e); break;
                default: break;
            }
        } else if (kind == IFD_EXIF) {
            switch (tag) {
                case 0x829A:
                    if (entry_rational(t, e, 0, &v, &num, &den)) { x->has_exposure = true; x->exposure_num = num; x->exposure_den = den; }
                    break;
                case 0x829D: if (entry_rational(t, e, 0, &v, NULL, NULL)) { x->has_fnumber = true; x->fnumber = v; } break;
                case 0x8827: x->iso = entry_uint(t, e); break;
                case 0x9003: entry_date(t, e, x->date_taken, sizeof(x->date_taken)); break;
                case 0x9204: if (entry_rational(t, e, 0, &v, NULL, NULL)) { x->has_bias = true; x->exposure_bias = v; } break;
                case 0x9209: x->has_flash = true; x->flash = (uint16_t)entry_uint(t, e); break;
                case 0x920A: if (entry_rational(t, e, 0, &v, NULL, NULL)) { x->has_focal = true; x->focal_mm = v; } break;
                case 0xA002: x->pixel_x = entry_uint(t, e); break;
                case 0xA003: x->pixel_y = entry_uint(t, e); break;
                case 0xA405: x->focal_35mm = entry_uint(t, e); break;
                case 0xA434: entry_text(t, e, x->lens, sizeof(x->lens)); break;
                default: break;
            }
        } else {
            switch (tag) {
                case 1: entry_text(t, e, ref, sizeof(ref)); lat_ref = ref[0]; break;
                case 2: have_lat = entry_degrees(t, e, &lat); break;
                case 3: entry_text(t, e, ref, sizeof(ref)); lon_ref = ref[0]; break;
                case 4: have_lon = entry_degrees(t, e, &lon); break;
                case 5: alt_below = (int)entry_uint(t, e); break;
                case 6:
                    if (entry_rational(t, e, 0, &v, NULL, NULL)) { x->has_altitude = true; x->altitude_m = v; }
                    break;
                default: break;
            }
        }
    }
    if (kind == IFD_GPS) {
        if (have_lat && have_lon) {
            x->has_gps = true;
            x->latitude = lat_ref == 'S' ? -lat : lat;
            x->longitude = lon_ref == 'W' ? -lon : lon;
        }
        if (x->has_altitude && alt_below == 1) x->altitude_m = -x->altitude_m;
    }
}

static bool parse_tiff_block(const uint8_t *base, size_t len, rubraview_exif_info_t *x) {
    if (len < 8) return false;
    tiff_ctx_t t = { .base = base, .len = len };
    if (base[0] == 'I' && base[1] == 'I') t.be = false;
    else if (base[0] == 'M' && base[1] == 'M') t.be = true;
    else return false;
    uint16_t magic;
    uint32_t ifd0;
    if (!tiff_read_u16(base, len, 2, t.be, &magic) || magic != 42 ||
        !tiff_read_u32(base, len, 4, t.be, &ifd0) || ifd0 < 8 || ifd0 >= len) return false;
    uint32_t exif_ifd = 0, gps_ifd = 0;
    read_ifd(&t, ifd0, IFD_MAIN, x, &exif_ifd, &gps_ifd);
    /* one level down, never back into IFD0 */
    if (exif_ifd >= 8 && exif_ifd < len && exif_ifd != ifd0) read_ifd(&t, exif_ifd, IFD_EXIF, x, NULL, NULL);
    if (gps_ifd >= 8 && gps_ifd < len && gps_ifd != ifd0 && gps_ifd != exif_ifd) read_ifd(&t, gps_ifd, IFD_GPS, x, NULL, NULL);
    x->found = true;
    return true;
}

static uint32_t rd_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t rd_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool starts_exif(const uint8_t *p, size_t n) {
    return n >= 6 && memcmp(p, "Exif\0\0", 6) == 0;
}

bool rubraview_exif_read(const uint8_t *data, size_t size, rubraview_exif_info_t *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!data || size < 8) return false;

    /* JPEG: the APP1 segment that starts "Exif\0\0" */
    if (data[0] == 0xFF && data[1] == JPEG_MARKER_SOI) {
        jpeg_marker_iter_t it = { .data = data, .size = size, .pos = 2 };
        for (;;) {
            jpeg_marker_t m = jpeg_next_marker(&it);
            if (!m.ok || m.code == JPEG_MARKER_SOS || m.code == JPEG_MARKER_EOI) break;
            if (m.code == JPEG_MARKER_APP1 && m.has_length && starts_exif(data + m.payload_start, m.payload_len)) {
                return parse_tiff_block(data + m.payload_start + 6, m.payload_len - 6, out);
            }
        }
        return false;
    }
    /* TIFF: the file is the block */
    if ((data[0] == 'I' && data[1] == 'I' && data[2] == 42 && data[3] == 0) ||
        (data[0] == 'M' && data[1] == 'M' && data[2] == 0 && data[3] == 42)) {
        return parse_tiff_block(data, size, out);
    }
    /* PNG: the eXIf chunk */
    if (size >= 8 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0) {
        size_t pos = 8;
        while (pos + 8 <= size) {
            uint32_t len = rd_be32(data + pos);
            if (len > size - pos - 8) break;
            if (memcmp(data + pos + 4, "eXIf", 4) == 0) {
                const uint8_t *b = data + pos + 8;
                return starts_exif(b, len) ? parse_tiff_block(b + 6, len - 6, out) : parse_tiff_block(b, len, out);
            }
            if (memcmp(data + pos + 4, "IEND", 4) == 0) break;
            pos += 12u + len;
        }
        return false;
    }
    /* WebP: the EXIF chunk */
    if (size >= 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WEBP", 4) == 0) {
        size_t pos = 12;
        while (pos + 8 <= size) {
            uint32_t len = rd_le32(data + pos + 4);
            if (len > size - pos - 8) break;
            if (memcmp(data + pos, "EXIF", 4) == 0) {
                const uint8_t *b = data + pos + 8;
                return starts_exif(b, len) ? parse_tiff_block(b + 6, len - 6, out) : parse_tiff_block(b, len, out);
            }
            pos += 8u + len + (len & 1u);
        }
        return false;
    }
    /* anything else (HEIF): "Exif\0\0" and a TIFF header near the start */
    size_t window = size < (1u << 20) ? size : (1u << 20);
    for (size_t i = 0; i + 10 <= window; ++i) {
        if (starts_exif(data + i, window - i) && (data[i + 6] == 'I' || data[i + 6] == 'M')) {
            return parse_tiff_block(data + i + 6, size - i - 6, out);
        }
    }
    return false;
}
