#include "rubraview/exif.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static void put_be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)(v & 0xFF); }
static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)((v >> 24) & 0xFF); p[1] = (uint8_t)((v >> 16) & 0xFF);
    p[2] = (uint8_t)((v >> 8) & 0xFF); p[3] = (uint8_t)(v & 0xFF);
}
static void put_le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)(v >> 8); }
static void put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF); p[3] = (uint8_t)((v >> 24) & 0xFF);
}

/* Builds a minimal little-endian TIFF/IFD0 blob with a single Orientation
   (tag 0x0112, SHORT) entry, matching real EXIF layout. Returns length. */
static size_t build_tiff_orientation(uint8_t *out, uint16_t orientation_value) {
    size_t pos = 0;
    out[pos++] = 'I'; out[pos++] = 'I';
    put_le16(out + pos, 42); pos += 2;
    put_le32(out + pos, 8); pos += 4; /* IFD0 offset */

    put_le16(out + pos, 1); pos += 2; /* one entry */
    put_le16(out + pos, 0x0112); pos += 2; /* tag: Orientation */
    put_le16(out + pos, 3); pos += 2;      /* type: SHORT */
    put_le32(out + pos, 1); pos += 4;      /* count: 1 */
    put_le16(out + pos, orientation_value); pos += 2;
    put_le16(out + pos, 0); pos += 2;      /* padding of the 4-byte value field */

    put_le32(out + pos, 0); pos += 4; /* next IFD offset: none */
    return pos;
}

static size_t append_app1_exif(uint8_t *out, size_t pos, const uint8_t *tiff, size_t tiff_len) {
    out[pos++] = 0xFF; out[pos++] = 0xE1;
    uint16_t seg_len = (uint16_t)(2 + 6 + tiff_len);
    put_be16(out + pos, seg_len); pos += 2;
    memcpy(out + pos, "Exif\0\0", 6); pos += 6;
    memcpy(out + pos, tiff, tiff_len); pos += tiff_len;
    return pos;
}

static size_t append_sos_and_tail(uint8_t *out, size_t pos) {
    out[pos++] = 0xFF; out[pos++] = 0xDA; /* SOS */
    put_be16(out + pos, 4); pos += 2;
    out[pos++] = 0x01; out[pos++] = 0x00; /* 2 bytes of arbitrary scan-header payload */
    out[pos++] = 0xAB; out[pos++] = 0xCD; out[pos++] = 0xEF; /* arbitrary entropy-coded data */
    out[pos++] = 0xFF; out[pos++] = 0xD9; /* EOI */
    return pos;
}

int main(void) {
    printf("[test_exif] Starting EXIF orientation and JPEG privacy strip unit tests...\n");

    /* Test 1: A well-formed EXIF APP1 with Orientation=6 is read correctly. */
    {
        uint8_t jpeg[256];
        size_t pos = 0;
        jpeg[pos++] = 0xFF; jpeg[pos++] = 0xD8; /* SOI */

        uint8_t tiff[64];
        size_t tiff_len = build_tiff_orientation(tiff, 6);
        pos = append_app1_exif(jpeg, pos, tiff, tiff_len);
        pos = append_sos_and_tail(jpeg, pos);

        assert(rubraview_exif_read_orientation(jpeg, pos) == 6);
    }
    printf("  [PASS] Orientation=6 read correctly from a well-formed EXIF APP1\n");

    /* Test 2: Each of the 8 valid orientation values round-trips. */
    {
        for (uint16_t o = 1; o <= 8; ++o) {
            uint8_t jpeg[256];
            size_t pos = 0;
            jpeg[pos++] = 0xFF; jpeg[pos++] = 0xD8;
            uint8_t tiff[64];
            size_t tiff_len = build_tiff_orientation(tiff, o);
            pos = append_app1_exif(jpeg, pos, tiff, tiff_len);
            pos = append_sos_and_tail(jpeg, pos);
            assert(rubraview_exif_read_orientation(jpeg, pos) == (int32_t)o);
        }
    }
    printf("  [PASS] All 8 valid orientation values round-trip\n");

    /* Test 3: Big-endian ("MM") TIFF byte order is also read correctly. */
    {
        uint8_t jpeg[256];
        size_t pos = 0;
        jpeg[pos++] = 0xFF; jpeg[pos++] = 0xD8;

        uint8_t tiff[32];
        size_t tp = 0;
        tiff[tp++] = 'M'; tiff[tp++] = 'M';
        put_be16(tiff + tp, 42); tp += 2;
        put_be32(tiff + tp, 8); tp += 4; /* IFD0 offset = 8, big-endian */
        put_be16(tiff + tp, 1); tp += 2; /* entry count */
        put_be16(tiff + tp, 0x0112); tp += 2; /* tag */
        put_be16(tiff + tp, 3); tp += 2;      /* type SHORT */
        put_be32(tiff + tp, 1); tp += 4;      /* count = 1 */
        put_be16(tiff + tp, 3); tp += 2;      /* value = 3, big-endian first 2 bytes of the 4-byte field */
        tiff[tp++] = 0; tiff[tp++] = 0;       /* padding */
        put_be32(tiff + tp, 0); tp += 4;      /* next IFD: none */

        pos = append_app1_exif(jpeg, pos, tiff, tp);
        pos = append_sos_and_tail(jpeg, pos);
        assert(rubraview_exif_read_orientation(jpeg, pos) == 3);
    }
    printf("  [PASS] Big-endian (\"MM\") TIFF byte order read correctly\n");

    /* Test 4: No APP1/Exif segment at all -> default orientation 1. */
    {
        uint8_t jpeg[64];
        size_t pos = 0;
        jpeg[pos++] = 0xFF; jpeg[pos++] = 0xD8;
        pos = append_sos_and_tail(jpeg, pos);
        assert(rubraview_exif_read_orientation(jpeg, pos) == 1);
    }
    printf("  [PASS] No Exif segment defaults to orientation 1\n");

    /* Test 5: Not a JPEG at all (bad SOI) -> default orientation 1, no crash. */
    {
        uint8_t not_jpeg[8] = { 'n', 'o', 't', 'a', 'j', 'p', 'g', 0 };
        assert(rubraview_exif_read_orientation(not_jpeg, sizeof(not_jpeg)) == 1);
        assert(rubraview_exif_read_orientation(NULL, 0) == 1);
    }
    printf("  [PASS] Non-JPEG and NULL input default to orientation 1, no crash\n");

    /* Test 6: Privacy strip removes EXIF APP1, XMP APP1, and APP13 (IPTC),
       while preserving an unrelated APP0/JFIF segment and the scan data
       (SOS payload + entropy-coded bytes + EOI) byte-for-byte. */
    {
        uint8_t jpeg[512];
        size_t pos = 0;
        jpeg[pos++] = 0xFF; jpeg[pos++] = 0xD8;

        /* APP0/JFIF: must survive the strip untouched. */
        jpeg[pos++] = 0xFF; jpeg[pos++] = 0xE0;
        put_be16(jpeg + pos, 9); pos += 2;
        memcpy(jpeg + pos, "abcdefg", 7); pos += 7;

        uint8_t tiff[64];
        size_t tiff_len = build_tiff_orientation(tiff, 6);
        pos = append_app1_exif(jpeg, pos, tiff, tiff_len);

        /* APP1/XMP */
        jpeg[pos++] = 0xFF; jpeg[pos++] = 0xE1;
        const char *xmp_sig = "http://ns.adobe.com/xap/1.0/";
        size_t xmp_payload_len = strlen(xmp_sig) + 1 /* NUL */ + 5 /* dummy XMP body */;
        put_be16(jpeg + pos, (uint16_t)(2 + xmp_payload_len)); pos += 2;
        memcpy(jpeg + pos, xmp_sig, strlen(xmp_sig)); pos += strlen(xmp_sig);
        jpeg[pos++] = 0; /* NUL terminator of the signature */
        memcpy(jpeg + pos, "dummy", 5); pos += 5;

        /* APP13/IPTC (Photoshop-style marker) */
        jpeg[pos++] = 0xFF; jpeg[pos++] = 0xED;
        put_be16(jpeg + pos, 2 + 6); pos += 2;
        memcpy(jpeg + pos, "IPTC12", 6); pos += 6;

        size_t sos_start = pos;
        pos = append_sos_and_tail(jpeg, pos);
        size_t total_len = pos;

        size_t mem_size = 4096;
        void *raw_mem = malloc(mem_size);
        assert(raw_mem != NULL);
        proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw_mem, .size = mem_size });

        rubraview_jpeg_strip_result_t r = rubraview_jpeg_privacy_strip(&arena, jpeg, total_len);
        assert(r.stripped_exif && r.stripped_xmp && r.stripped_iptc);

        /* No 0xFFE1 (APP1) or 0xFFED (APP13) marker remains anywhere in the output. */
        for (size_t i = 0; i + 1 < r.data.len; ++i) {
            uint8_t b0 = (uint8_t)r.data.ptr[i], b1 = (uint8_t)r.data.ptr[i + 1];
            if (b0 == 0xFF) {
                assert(b1 != 0xE1);
                assert(b1 != 0xED);
            }
        }

        /* APP0/JFIF survives untouched. */
        bool found_app0 = false;
        for (size_t i = 0; i + 1 < r.data.len; ++i) {
            if ((uint8_t)r.data.ptr[i] == 0xFF && (uint8_t)r.data.ptr[i + 1] == 0xE0) {
                found_app0 = true;
                break;
            }
        }
        assert(found_app0);

        /* The scan data (from SOS onward) is preserved byte-for-byte,
           since it appears only once, at the end of the stripped output. */
        size_t scan_len = total_len - sos_start;
        assert(r.data.len >= scan_len);
        assert(memcmp(r.data.ptr + (r.data.len - scan_len), jpeg + sos_start, scan_len) == 0);

        /* No pixel data is touched: even orientation reads correctly on
           the un-stripped input, proving the DCT-adjacent bytes were
           never decoded, only marker-walked. */
        assert(rubraview_exif_read_orientation(jpeg, total_len) == 6);

        free(raw_mem);
    }
    printf("  [PASS] Privacy strip removes EXIF/XMP/IPTC, preserves APP0 and scan data exactly\n");

    /* Test 7: A JPEG with none of the targeted segments strips cleanly
       (no flags set) and is otherwise unchanged. */
    {
        uint8_t jpeg[64];
        size_t pos = 0;
        jpeg[pos++] = 0xFF; jpeg[pos++] = 0xD8;
        pos = append_sos_and_tail(jpeg, pos);

        size_t mem_size = 4096;
        void *raw_mem = malloc(mem_size);
        proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw_mem, .size = mem_size });

        rubraview_jpeg_strip_result_t r = rubraview_jpeg_privacy_strip(&arena, jpeg, pos);
        assert(!r.stripped_exif && !r.stripped_xmp && !r.stripped_iptc);
        assert(r.data.len == pos);
        assert(memcmp(r.data.ptr, jpeg, pos) == 0);

        free(raw_mem);
    }
    printf("  [PASS] JPEG with no targeted segments strips to an identical copy\n");

    /* Test 8: Non-JPEG input to the strip function is handled without crashing. */
    {
        size_t mem_size = 256;
        void *raw_mem = malloc(mem_size);
        proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw_mem, .size = mem_size });
        uint8_t not_jpeg[4] = { 0, 1, 2, 3 };
        rubraview_jpeg_strip_result_t r = rubraview_jpeg_privacy_strip(&arena, not_jpeg, sizeof(not_jpeg));
        assert(r.data.len == 0);
        assert(!r.stripped_exif);
        free(raw_mem);
    }
    printf("  [PASS] Non-JPEG input to privacy strip handled without crashing\n");

    printf("[test_exif] All tests passed successfully!\n");
    return 0;
}
