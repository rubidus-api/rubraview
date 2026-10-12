#include "rubraview/bigtiff.h"
#include <string.h>

#define MAX_DIRECTORIES 4096u
#define MAX_ENTRIES 4096u

static uint16_t rd16(const uint8_t *p, bool le) {
    return (uint16_t)(le ? p[0] | (p[1] << 8) : (p[0] << 8) | p[1]);
}
static uint64_t rd64(const uint8_t *p, bool le) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | p[le ? 7 - i : i];
    return v;
}
static void wr16(uint8_t *p, bool le, uint32_t v) {
    p[le ? 0 : 1] = (uint8_t)v;
    p[le ? 1 : 0] = (uint8_t)(v >> 8);
}
static void wr32(uint8_t *p, bool le, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[le ? i : 3 - i] = (uint8_t)(v >> (8 * i));
}

bool rubraview_bigtiff_is(const uint8_t *b, size_t size) {
    if (!b || size < 16) return false;
    bool le = b[0] == 'I' && b[1] == 'I';
    if (!le && !(b[0] == 'M' && b[1] == 'M')) return false;
    return rd16(b + 2, le) == 43 && rd16(b + 4, le) == 8 && rd16(b + 6, le) == 0;
}

/* The bytes one value of a type takes; 0 for a type that is not known. */
static uint32_t type_bytes(uint16_t type) {
    switch (type) {
        case 1: case 2: case 6: case 7: return 1;
        case 3: case 8: return 2;
        case 4: case 9: case 11: case 13: return 4;
        case 5: case 10: case 12: case 16: case 17: case 18: return 8;
        default: return 0;
    }
}

/* A tag whose value is the place of another directory. */
static bool points_at_directory(uint16_t tag, uint16_t type) {
    return type == 13 || type == 18 || tag == 330 || tag == 34665 || tag == 34853 || tag == 40965;
}

/* One walk for both: with `out` NULL only the room is counted. */
static size_t convert(const uint8_t *b, size_t size, uint8_t *out) {
    if (!rubraview_bigtiff_is(b, size)) return 0;
    bool le = b[0] == 'I';
    uint64_t dir = rd64(b + 8, le);
    uint64_t at = (uint64_t)size + (size & 1u);          /* where the next thing is written; even */
    uint64_t first = at;
    uint8_t *previous_next = NULL;                       /* the last directory's "next" field */

    for (uint32_t d = 0; dir != 0; ++d) {
        if (d >= MAX_DIRECTORIES || dir > size || size - dir < 8) return 0;
        uint64_t entries = rd64(b + dir, le);
        if (entries == 0 || entries > MAX_ENTRIES || (size - dir - 8) / 20 < entries ||
            size - dir - 8 - entries * 20 < 8) return 0;
        const uint8_t *in = b + dir + 8;

        uint32_t kept = 0;
        for (uint64_t i = 0; i < entries; ++i) {
            if (!points_at_directory(rd16(in + i * 20, le), rd16(in + i * 20 + 2, le))) ++kept;
        }
        if (kept == 0) return 0;
        uint64_t table = at;
        uint64_t more = table + 2u + (uint64_t)kept * 12u + 4u;   /* values that do not fit in the entry */
        if (previous_next) wr32(previous_next, le, (uint32_t)table);
        if (out) wr16(out + table, le, kept);
        uint8_t *to = out ? out + table + 2 : NULL;

        for (uint64_t i = 0; i < entries; ++i) {
            const uint8_t *e = in + i * 20;
            uint16_t tag = rd16(e, le), type = rd16(e + 2, le);
            uint64_t count = rd64(e + 4, le);
            if (points_at_directory(tag, type)) continue;
            uint32_t each = type_bytes(type);
            if (each == 0 || count == 0 || count > 0xFFFFFFFFu / 8u) return 0;
            uint64_t total = count * each;
            uint64_t where = total <= 8 ? 0 : rd64(e + 12, le);
            if (total > 8 && (where > size || size - where < total)) return 0;
            const uint8_t *values = total <= 8 ? e + 12 : b + where;

            bool narrow = each == 8 && type >= 16;        /* eight-byte whole numbers become four-byte ones */
            uint16_t new_type = !narrow ? type : type == 17 ? 9 : 4;
            uint64_t new_total = narrow ? count * 4u : total;
            if (narrow) {
                for (uint64_t k = 0; k < count; ++k) {
                    uint64_t v = rd64(values + k * 8, le);
                    if (type == 17 ? (v > 0x7FFFFFFFu && v < 0xFFFFFFFF80000000u) : v > 0xFFFFFFFFu) return 0;
                }
            }
            /* Where the entry's four bytes point, or what they hold. */
            bool in_entry = new_total <= 4;
            bool stays = !in_entry && !narrow && total > 8;   /* already out of the entry, in the file */
            uint64_t target = stays ? where : more;
            if (!in_entry && target > 0xFFFFFFFFu) return 0;
            if (to) {
                wr16(to, le, tag);
                wr16(to + 2, le, new_type);
                wr32(to + 4, le, (uint32_t)count);
                uint8_t *dst = in_entry ? to + 8 : stays ? NULL : out + more;
                if (in_entry) memset(to + 8, 0, 4); else wr32(to + 8, le, (uint32_t)target);
                if (dst && narrow) {
                    for (uint64_t k = 0; k < count; ++k) wr32(dst + k * 4, le, (uint32_t)rd64(values + k * 8, le));
                } else if (dst) {
                    memcpy(dst, values, (size_t)new_total);
                }
                to += 12;
            }
            if (!in_entry && !stays) {
                more += new_total + (new_total & 1u);
                if (out && (new_total & 1u)) out[more - 1] = 0;
            }
        }
        if (to) wr32(to, le, 0);
        previous_next = to;
        at = more;
        if (at > 0xFFFFFFFFu) return 0;
        dir = rd64(in + entries * 20, le);
    }
    if (at == first) return 0;
    if (out) {
        if (size & 1u) out[size] = 0;
        wr16(out + 2, le, 42);
        wr32(out + 4, le, (uint32_t)first);
    }
    return (size_t)(at - size);
}

size_t rubraview_bigtiff_classic_extra(const uint8_t *bytes, size_t size) {
    return convert(bytes, size, NULL);
}

bool rubraview_bigtiff_to_classic(const uint8_t *bytes, size_t size, uint8_t *out, size_t out_size) {
    size_t extra = convert(bytes, size, NULL);
    if (extra == 0 || !out || out_size < size + extra) return false;
    memcpy(out, bytes, size);
    return convert(bytes, size, out) == extra;
}
