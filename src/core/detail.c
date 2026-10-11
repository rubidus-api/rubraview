#include "rubraview/detail.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static size_t clip(int n, size_t cap) {
    if (n <= 0 || cap == 0) return 0;
    return (size_t)n < cap ? (size_t)n : cap - 1;
}

/* More text after what `buf` holds up to `at`; returns the new end. */
static size_t append(char *buf, size_t cap, size_t at, const char *format, ...) {
    if (at + 1 >= cap) return at;
    va_list args;
    va_start(args, format);
    int n = vsnprintf(buf + at, cap - at, format, args);
    va_end(args);
    return at + clip(n, cap - at);
}

void rubraview_details_heading(rubraview_details_t *d, const char *format, ...) {
    if (!d || !format || d->count >= RUBRAVIEW_DETAIL_MAX) return;
    rubraview_detail_t *line = &d->lines[d->count];
    *line = (rubraview_detail_t){ .heading = true };
    va_list args;
    va_start(args, format);
    int n = vsnprintf(line->label, sizeof(line->label), format, args);
    va_end(args);
    if (n > 0) d->count++;
}

void rubraview_details_add(rubraview_details_t *d, const char *label, const char *format, ...) {
    if (!d || !label || !format || d->count >= RUBRAVIEW_DETAIL_MAX) return;
    rubraview_detail_t *line = &d->lines[d->count];
    *line = (rubraview_detail_t){ .heading = false };
    snprintf(line->label, sizeof(line->label), "%s", label);
    va_list args;
    va_start(args, format);
    int n = vsnprintf(line->value, sizeof(line->value), format, args);
    va_end(args);
    if (n <= 0) return;
    /* A value cut short must not end inside a UTF-8 character. */
    if ((size_t)n >= sizeof(line->value)) {
        size_t end = sizeof(line->value) - 2;   /* the last byte kept */
        while (end > 0 && ((unsigned char)line->value[end] & 0xC0) == 0x80) end--;
        /* A lead byte there starts a character: whole only if all its bytes are kept. */
        unsigned char lead = (unsigned char)line->value[end];
        size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
        if (end + need > sizeof(line->value) - 1) line->value[end] = '\0';
    }
    /* Control characters would break the rows. */
    for (char *c = line->value; *c; ++c) if ((unsigned char)*c < 0x20) *c = ' ';
    d->count++;
}

size_t rubraview_detail_bitrate(char *buf, size_t cap, double bps) {
    if (!buf || cap == 0) return 0;
    buf[0] = '\0';
    if (!(bps > 0.0)) return 0;
    if (bps >= 10.0e6) return clip(snprintf(buf, cap, "%.1f Mbit/s", bps / 1.0e6), cap);
    if (bps >= 1000.0) return clip(snprintf(buf, cap, "%.0f kbit/s", bps / 1000.0), cap);
    return clip(snprintf(buf, cap, "%.0f bit/s", bps), cap);
}

size_t rubraview_detail_channels(char *buf, size_t cap, uint32_t count, uint32_t mask) {
    if (!buf || cap == 0) return 0;
    buf[0] = '\0';
    if (count == 0) return 0;
    static const char *const SPEAKER[18] = { "FL", "FR", "FC", "LFE", "BL", "BR", "FLC", "FRC", "BC", "SL", "SR",
                                             "TC", "TFL", "TFC", "TFR", "TBL", "TBC", "TBR" };
    const char *name = NULL;
    switch (mask) {
        case 0x4: name = "mono"; break;
        case 0x3: name = "stereo"; break;
        case 0xB: name = "2.1"; break;
        case 0x7: name = "3.0"; break;
        case 0x33: case 0x603: name = "4.0"; break;
        case 0x107: name = "4.0 (surround)"; break;
        case 0x37: case 0x607: name = "5.0"; break;
        case 0x3F: name = "5.1"; break;
        case 0x60F: name = "5.1 (side)"; break;
        case 0x70F: name = "6.1"; break;
        case 0x63F: name = "7.1"; break;
        case 0xFF: name = "7.1 (wide)"; break;
        default: break;
    }
    if (mask == 0) {
        if (count == 1) return clip(snprintf(buf, cap, "mono"), cap);
        if (count == 2) return clip(snprintf(buf, cap, "stereo"), cap);
        return clip(snprintf(buf, cap, "%u channels", (unsigned)count), cap);
    }
    int n = name ? snprintf(buf, cap, "%s", name) : snprintf(buf, cap, "%u channels", (unsigned)count);
    if (count <= 2 && name) return clip(n, cap);
    size_t at = clip(n, cap);
    bool first = true;
    for (uint32_t bit = 0; bit < 18; ++bit) {
        if (!(mask & (1u << bit))) continue;
        at = append(buf, cap, at, "%s%s", first ? " (" : " ", SPEAKER[bit]);
        first = false;
    }
    if (!first) at = append(buf, cap, at, ")");
    return at;
}

size_t rubraview_detail_h264_profile(char *buf, size_t cap, uint32_t profile, uint32_t level) {
    if (!buf || cap == 0) return 0;
    buf[0] = '\0';
    const char *name = NULL;
    switch (profile) {
        case 44: name = "CAVLC 4:4:4 Intra"; break;
        case 66: name = "Baseline"; break;
        case 77: name = "Main"; break;
        case 88: name = "Extended"; break;
        case 100: name = "High"; break;
        case 110: name = "High 10"; break;
        case 118: name = "Multiview High"; break;
        case 122: name = "High 4:2:2"; break;
        case 128: name = "Stereo High"; break;
        case 244: name = "High 4:4:4 Predictive"; break;
        default: break;
    }
    size_t at = 0;
    if (name) at = clip(snprintf(buf, cap, "%s", name), cap);
    else if (profile) at = clip(snprintf(buf, cap, "profile %u", (unsigned)profile), cap);
    if (level) {
        at = append(buf, cap, at, "%slevel %u", at ? ", " : "", (unsigned)(level / 10));
        if (level % 10) at = append(buf, cap, at, ".%u", (unsigned)(level % 10));
    }
    return at;
}

size_t rubraview_detail_hevc_profile(char *buf, size_t cap, uint32_t profile, uint32_t level) {
    if (!buf || cap == 0) return 0;
    buf[0] = '\0';
    static const char *const NAME[] = { NULL, "Main", "Main 10", "Main Still Picture", "Range Extensions" };
    size_t at = 0;
    if (profile >= 1 && profile <= 4) at = clip(snprintf(buf, cap, "%s", NAME[profile]), cap);
    else if (profile) at = clip(snprintf(buf, cap, "profile %u", (unsigned)profile), cap);
    if (level) {
        /* general_level_idc is thirty times the level. */
        at = append(buf, cap, at, "%slevel %u", at ? ", " : "", (unsigned)(level / 30));
        if (level % 30) at = append(buf, cap, at, ".%u", (unsigned)(level % 30 / 3));
    }
    return at;
}

size_t rubraview_detail_aac_profile(char *buf, size_t cap, uint32_t indication) {
    if (!buf || cap == 0) return 0;
    buf[0] = '\0';
    static const struct { uint32_t code; const char *name; int level; } KNOWN[] = {
        { 0x28, "AAC-LC", 1 }, { 0x29, "AAC-LC", 2 }, { 0x2A, "AAC-LC", 4 }, { 0x2B, "AAC-LC", 5 },
        { 0x2C, "HE-AAC", 2 }, { 0x2D, "HE-AAC", 3 }, { 0x2E, "HE-AAC", 4 }, { 0x2F, "HE-AAC", 5 },
        { 0x30, "HE-AAC v2", 2 }, { 0x31, "HE-AAC v2", 3 }, { 0x32, "HE-AAC v2", 4 }, { 0x33, "HE-AAC v2", 5 },
        { 0x50, "AAC-LC", 7 }, { 0x51, "AAC-LC", 7 }, { 0x52, "HE-AAC", 7 },
    };
    for (size_t i = 0; i < sizeof(KNOWN) / sizeof(KNOWN[0]); ++i) {
        if (KNOWN[i].code == indication) return clip(snprintf(buf, cap, "%s, level %d", KNOWN[i].name, KNOWN[i].level), cap);
    }
    if (indication == 0 || indication == 0xFE || indication == 0xFF) return 0;
    return clip(snprintf(buf, cap, "profile and level 0x%02X", (unsigned)indication), cap);
}

size_t rubraview_detail_ratio(char *buf, size_t cap, double stored, double raw) {
    if (!buf || cap == 0) return 0;
    buf[0] = '\0';
    if (!(stored > 0.0) || !(raw > 0.0)) return 0;
    return clip(snprintf(buf, cap, "1 : %.*f (%.*f %% of the size unpacked)",
                         raw / stored >= 100.0 ? 0 : 1, raw / stored,
                         stored / raw * 100.0 >= 10.0 ? 0 : 1, stored / raw * 100.0), cap);
}

/* ---- pictures ---- */

static uint32_t be16(const uint8_t *p) { return ((uint32_t)p[0] << 8) | p[1]; }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint32_t le16(const uint8_t *p) { return ((uint32_t)p[1] << 8) | p[0]; }
static uint32_t le32(const uint8_t *p) { return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0]; }

/* Text out of a file: printable ASCII kept, the rest a dot; at most `cap - 1`. */
static void plain(char *out, size_t cap, const uint8_t *p, size_t n) {
    size_t k = 0;
    for (; k < n && k + 1 < cap && p[k]; ++k) out[k] = (p[k] >= 0x20 && p[k] < 0x7F) ? (char)p[k] : '.';
    out[k] = '\0';
}

/* The IJG quality that scales the standard luminance table to this one. */
static int jpeg_quality(const uint32_t q[64]) {
    static const uint8_t STD[64] = {   /* in zig-zag order, as DQT stores them */
        16, 11, 12, 14, 12, 10, 16, 14, 13, 14, 18, 17, 16, 19, 24, 40, 26, 24, 22, 22, 24, 49, 35, 37, 29, 40, 58, 51, 61, 60, 57, 51,
        56, 55, 64, 72, 92, 78, 64, 68, 87, 69, 55, 56, 80, 109, 81, 87, 95, 98, 103, 104, 103, 62, 77, 113, 121, 112, 100, 120, 92, 101, 103, 99 };
    double sum = 0.0;
    bool ones = true;
    for (int i = 0; i < 64; ++i) {
        sum += (double)q[i] * 100.0 / (double)STD[i];
        if (q[i] != 1) ones = false;
    }
    if (ones) return 100;
    double scale = sum / 64.0;
    double quality = scale <= 100.0 ? (200.0 - scale) / 2.0 : 5000.0 / scale;
    int rounded = (int)(quality + 0.5);
    return rounded < 1 ? 1 : rounded > 100 ? 100 : rounded;
}

static bool jpeg_details(const uint8_t *b, size_t size, rubraview_details_t *out, uint32_t *raw_bits) {
    static const char *const PROCESS[16] = {
        "baseline DCT, Huffman", "extended sequential DCT, Huffman", "progressive DCT, Huffman", "lossless, Huffman",
        NULL, "differential sequential DCT, Huffman", "differential progressive DCT, Huffman", "differential lossless, Huffman",
        NULL, "extended sequential DCT, arithmetic", "progressive DCT, arithmetic", "lossless, arithmetic",
        NULL, "differential sequential DCT, arithmetic", "differential progressive DCT, arithmetic", "differential lossless, arithmetic" };
    rubraview_details_heading(out, "JPEG");
    size_t at = 2;
    int tables = 0, luma_quality = -1, chroma_quality = -1;
    bool sixteen_bit_tables = false, seen_frame = false;
    while (at + 4 <= size) {
        if (b[at] != 0xFF) { at++; continue; }
        uint8_t marker = b[at + 1];
        if (marker == 0xFF) { at++; continue; }
        if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) { at += 2; continue; }
        if (marker == 0xD9) break;
        size_t len = be16(b + at + 2);
        if (len < 2 || at + 2 + len > size) break;
        const uint8_t *p = b + at + 4;
        size_t n = len - 2;
        if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC && n >= 6) {
            const char *process = PROCESS[marker - 0xC0];
            uint32_t precision = p[0], height = be16(p + 1), width = be16(p + 3), comps = p[5];
            if (!seen_frame) {
                rubraview_details_add(out, "Process", "%s", process ? process : "unknown");
                rubraview_details_add(out, "Sample precision", "%u bits", (unsigned)precision);
                rubraview_details_add(out, "Frame", "%u x %u", (unsigned)width, (unsigned)height);
                const char *space = comps == 1 ? "greyscale" : comps == 3 ? "three (YCbCr or RGB)" : comps == 4 ? "four (CMYK or YCCK)" : "";
                rubraview_details_add(out, "Components", "%u%s%s", (unsigned)comps, space[0] ? ": " : "", space);
                if (raw_bits) *raw_bits = precision * comps;
                if (comps >= 3 && n >= 6 + 3 * (size_t)comps) {
                    uint32_t h0 = p[7] >> 4, v0 = p[7] & 15, h1 = p[10] >> 4, v1 = p[10] & 15;
                    const char *name = "";
                    if (h1 && v1) {
                        uint32_t hr = h0 / h1, vr = v0 / v1;
                        name = (hr == 1 && vr == 1) ? "4:4:4 (none)" : (hr == 2 && vr == 1) ? "4:2:2"
                             : (hr == 2 && vr == 2) ? "4:2:0" : (hr == 4 && vr == 1) ? "4:1:1"
                             : (hr == 1 && vr == 2) ? "4:4:0" : (hr == 4 && vr == 2) ? "4:1:0" : "";
                    }
                    rubraview_details_add(out, "Chroma sampling", "%s%s%ux%u, %ux%u, %ux%u", name, name[0] ? "; " : "",
                                          (unsigned)h0, (unsigned)v0, (unsigned)h1, (unsigned)v1,
                                          (unsigned)(p[13] >> 4), (unsigned)(p[13] & 15));
                }
            }
            seen_frame = true;
        } else if (marker == 0xDB) {
            size_t k = 0;
            while (k < n) {
                uint32_t precision = p[k] >> 4, id = p[k] & 15;
                size_t need = precision ? 128 : 64;
                if (k + 1 + need > n) break;
                uint32_t q[64];
                for (int i = 0; i < 64; ++i) q[i] = precision ? be16(p + k + 1 + 2 * (size_t)i) : p[k + 1 + (size_t)i];
                if (precision) sixteen_bit_tables = true;
                if (id == 0) luma_quality = jpeg_quality(q);
                else if (chroma_quality < 0) chroma_quality = jpeg_quality(q);   /* against the luminance table: a rough word */
                tables++;
                k += 1 + need;
            }
        } else if (marker == 0xDD && n >= 2) {
            if (be16(p)) rubraview_details_add(out, "Restart interval", "%u blocks", (unsigned)be16(p));
        } else if (marker == 0xE0 && n >= 14 && memcmp(p, "JFIF\0", 5) == 0) {
            static const char *const UNIT[3] = { "aspect ratio", "dots an inch", "dots a centimetre" };
            rubraview_details_add(out, "JFIF", "version %u.%02u, %u x %u %s", (unsigned)p[5], (unsigned)p[6],
                                  (unsigned)be16(p + 8), (unsigned)be16(p + 10), p[7] < 3 ? UNIT[p[7]] : "");
            if (p[12] && p[13]) rubraview_details_add(out, "JFIF thumbnail", "%u x %u", (unsigned)p[12], (unsigned)p[13]);
        } else if (marker == 0xE1 && n >= 6 && memcmp(p, "Exif\0\0", 6) == 0) {
            rubraview_details_add(out, "EXIF block", "%zu bytes", n);
        } else if (marker == 0xE1 && n >= 28 && memcmp(p, "http://ns.adobe.com/xap/1.0/", 28) == 0) {
            rubraview_details_add(out, "XMP block", "%zu bytes", n);
        } else if (marker == 0xE2 && n >= 14 && memcmp(p, "ICC_PROFILE\0", 12) == 0) {
            if (p[12] == 1) rubraview_details_add(out, "ICC profile", "%u part%s", (unsigned)p[13], p[13] == 1 ? "" : "s");
        } else if (marker == 0xE2 && n >= 4 && memcmp(p, "MPF\0", 4) == 0) {
            rubraview_details_add(out, "Multi-picture", "yes (MPF)");
        } else if (marker == 0xED && n >= 14 && memcmp(p, "Photoshop 3.0\0", 14) == 0) {
            rubraview_details_add(out, "Photoshop block", "%zu bytes (IPTC)", n);
        } else if (marker == 0xEE && n >= 12 && memcmp(p, "Adobe", 5) == 0) {
            static const char *const TRANSFORM[3] = { "none (RGB or CMYK)", "YCbCr", "YCCK" };
            rubraview_details_add(out, "Adobe marker", "colour transform %s", p[11] < 3 ? TRANSFORM[p[11]] : "unknown");
        } else if (marker == 0xFE && n > 0) {
            char text[100];
            plain(text, sizeof(text), p, n);
            rubraview_details_add(out, "Comment", "%s", text);
        } else if (marker == 0xDA) {
            break;   /* the picture's data: nothing more is read */
        }
        at += 2 + len;
    }
    if (luma_quality >= 0) {
        if (chroma_quality >= 0) rubraview_details_add(out, "Quality (estimate)", "%d, colour tables %d; %d table%s%s", luma_quality,
                                                       chroma_quality, tables, tables == 1 ? "" : "s", sixteen_bit_tables ? ", 16-bit" : "");
        else rubraview_details_add(out, "Quality (estimate)", "%d; %d table%s%s", luma_quality, tables, tables == 1 ? "" : "s",
                                   sixteen_bit_tables ? ", 16-bit" : "");
    }
    return true;
}

static bool png_details(const uint8_t *b, size_t size, rubraview_details_t *out, uint32_t *raw_bits) {
    rubraview_details_heading(out, "PNG");
    size_t at = 8;
    uint64_t idat = 0;
    uint32_t idat_chunks = 0, texts = 0;
    bool first_idat = true;
    while (at + 12 <= size) {
        uint32_t len = be32(b + at);
        const uint8_t *type = b + at + 4, *p = b + at + 8;
        if ((uint64_t)at + 12 + len > size) break;
        if (memcmp(type, "IHDR", 4) == 0 && len >= 13) {
            static const char *const KIND[7] = { "greyscale", NULL, "truecolour (RGB)", "palette", "greyscale with alpha", NULL, "truecolour with alpha (RGBA)" };
            static const uint8_t CHANNELS[7] = { 1, 0, 3, 1, 2, 0, 4 };
            uint32_t depth = p[8], kind = p[9];
            rubraview_details_add(out, "Bit depth", "%u bits a %s", (unsigned)depth, kind == 3 ? "palette index" : "channel");
            rubraview_details_add(out, "Colour type", "%u: %s", (unsigned)kind, kind < 7 && KIND[kind] ? KIND[kind] : "unknown");
            rubraview_details_add(out, "Compression", "deflate (method %u), filter method %u", (unsigned)p[10], (unsigned)p[11]);
            rubraview_details_add(out, "Interlace", "%s", p[12] == 1 ? "Adam7" : "none");
            if (raw_bits && kind < 7) *raw_bits = depth * CHANNELS[kind];
        } else if (memcmp(type, "PLTE", 4) == 0) {
            rubraview_details_add(out, "Palette", "%u colours", (unsigned)(len / 3));
        } else if (memcmp(type, "tRNS", 4) == 0) {
            rubraview_details_add(out, "Transparency", "tRNS, %u bytes", (unsigned)len);
        } else if (memcmp(type, "gAMA", 4) == 0 && len >= 4) {
            uint32_t g = be32(p);
            if (g) rubraview_details_add(out, "Gamma", "%.5g (file gamma %.5f)", 100000.0 / (double)g, (double)g / 100000.0);
        } else if (memcmp(type, "sRGB", 4) == 0 && len >= 1) {
            static const char *const INTENT[4] = { "perceptual", "relative colorimetric", "saturation", "absolute colorimetric" };
            rubraview_details_add(out, "sRGB", "rendering intent %s", p[0] < 4 ? INTENT[p[0]] : "unknown");
        } else if (memcmp(type, "cHRM", 4) == 0 && len >= 32) {
            rubraview_details_add(out, "Chromaticities", "white %.4f %.4f, R %.3f %.3f, G %.3f %.3f, B %.3f %.3f",
                                  be32(p) / 1e5, be32(p + 4) / 1e5, be32(p + 8) / 1e5, be32(p + 12) / 1e5,
                                  be32(p + 16) / 1e5, be32(p + 20) / 1e5, be32(p + 24) / 1e5, be32(p + 28) / 1e5);
        } else if (memcmp(type, "iCCP", 4) == 0) {
            char name[80];
            plain(name, sizeof(name), p, len);
            rubraview_details_add(out, "ICC profile", "\"%s\"", name);
        } else if (memcmp(type, "cICP", 4) == 0 && len >= 4) {
            rubraview_details_add(out, "Colour (cICP)", "primaries %u, transfer %u, matrix %u, %s range",
                                  (unsigned)p[0], (unsigned)p[1], (unsigned)p[2], p[3] ? "full" : "limited");
        } else if (memcmp(type, "pHYs", 4) == 0 && len >= 9) {
            if (p[8] == 1) rubraview_details_add(out, "Pixel size", "%u x %u a metre (%.0f x %.0f dpi)", (unsigned)be32(p), (unsigned)be32(p + 4),
                                                 be32(p) * 0.0254, be32(p + 4) * 0.0254);
            else rubraview_details_add(out, "Pixel size", "aspect %u : %u", (unsigned)be32(p), (unsigned)be32(p + 4));
        } else if (memcmp(type, "tIME", 4) == 0 && len >= 7) {
            rubraview_details_add(out, "Last changed", "%04u-%02u-%02u %02u:%02u:%02u UTC", (unsigned)be16(p), (unsigned)p[2], (unsigned)p[3],
                                  (unsigned)p[4], (unsigned)p[5], (unsigned)p[6]);
        } else if (memcmp(type, "acTL", 4) == 0 && len >= 8) {
            uint32_t plays = be32(p + 4);
            if (plays) rubraview_details_add(out, "Animation", "%u frames, played %u time%s", (unsigned)be32(p), (unsigned)plays, plays == 1 ? "" : "s");
            else rubraview_details_add(out, "Animation", "%u frames, played without end", (unsigned)be32(p));
        } else if (memcmp(type, "bKGD", 4) == 0) {
            rubraview_details_add(out, "Background", "given (bKGD)");
        } else if (memcmp(type, "eXIf", 4) == 0) {
            rubraview_details_add(out, "EXIF block", "%u bytes", (unsigned)len);
        } else if ((memcmp(type, "tEXt", 4) == 0 || memcmp(type, "zTXt", 4) == 0 || memcmp(type, "iTXt", 4) == 0) && texts < 16) {
            char key[24], text[100];
            plain(key, sizeof(key), p, len);
            size_t k = strlen(key);
            text[0] = '\0';
            if (type[0] == 't' && k + 1 < len) {
                size_t n = len - k - 1;
                /* Latin-1 in the file; what is not ASCII becomes a dot. */
                size_t w = 0;
                for (; w < n && w + 1 < sizeof(text); ++w) text[w] = (p[k + 1 + w] >= 0x20 && p[k + 1 + w] < 0x7F) ? (char)p[k + 1 + w] : '.';
                text[w] = '\0';
            }
            char label[24];
            snprintf(label, sizeof(label), "Text: %.16s", key);
            rubraview_details_add(out, label, "%s", text[0] ? text : type[0] == 'z' ? "(compressed)" : type[0] == 'i' ? "(international text)" : "(empty)");
            texts++;
        } else if (memcmp(type, "IDAT", 4) == 0) {
            if (first_idat && len >= 2) {
                static const char *const LEVEL[4] = { "fastest", "fast", "default", "maximum" };
                unsigned cinfo = (unsigned)p[0] >> 4;
                rubraview_details_add(out, "Deflate stream", "%u-byte window, compressed at \"%s\"", cinfo <= 7 ? 1u << (cinfo + 8) : 0u,
                                      LEVEL[p[1] >> 6]);
                first_idat = false;
            }
            idat += len;
            idat_chunks++;
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }
        at += 12 + (size_t)len;
    }
    if (idat_chunks) rubraview_details_add(out, "Picture data", "%llu bytes in %u chunk%s", (unsigned long long)idat, (unsigned)idat_chunks, idat_chunks == 1 ? "" : "s");
    return true;
}

static bool gif_details(const uint8_t *b, size_t size, rubraview_details_t *out, uint32_t *raw_bits) {
    if (size < 13) return false;
    rubraview_details_heading(out, "GIF");
    rubraview_details_add(out, "Version", "%.3s", (const char*)b + 3);
    rubraview_details_add(out, "Canvas", "%u x %u", (unsigned)le16(b + 6), (unsigned)le16(b + 8));
    uint8_t flags = b[10];
    if (flags & 0x80) rubraview_details_add(out, "Global palette", "%u colours%s, background index %u", 2u << (flags & 7),
                                            (flags & 8) ? ", sorted" : "", (unsigned)b[11]);
    else rubraview_details_add(out, "Global palette", "none");
    rubraview_details_add(out, "Colour resolution", "%u bits", (unsigned)(((flags >> 4) & 7) + 1));
    if (b[12]) rubraview_details_add(out, "Pixel aspect", "%.3f", ((double)b[12] + 15.0) / 64.0);
    if (raw_bits) *raw_bits = 8;
    size_t at = 13 + ((flags & 0x80) ? 3u * (2u << (flags & 7)) : 0u);
    uint32_t frames = 0, local = 0, interlaced = 0, transparent = 0, comments = 0;
    int64_t loops = -1;
    uint64_t centiseconds = 0;
    while (at < size) {
        uint8_t block = b[at++];
        if (block == 0x3B) break;
        if (block == 0x2C) {
            if (at + 9 > size) break;
            uint8_t f = b[at + 8];
            frames++;
            if (f & 0x80) local++;
            if (f & 0x40) interlaced++;
            at += 9 + ((f & 0x80) ? 3u * (2u << (f & 7)) : 0u);
            at++;   /* the LZW code size */
        } else if (block == 0x21) {
            if (at + 2 > size) break;
            uint8_t label = b[at++];
            if (label == 0xF9 && at + 5 <= size && b[at] == 4) {
                if (b[at + 1] & 1) transparent++;
                centiseconds += le16(b + at + 2);
            } else if (label == 0xFF && at + 17 <= size && b[at] == 11 &&
                       (memcmp(b + at + 1, "NETSCAPE2.0", 11) == 0 || memcmp(b + at + 1, "ANIMEXTS1.0", 11) == 0) &&
                       b[at + 12] == 3 && b[at + 13] == 1) {
                loops = (int64_t)le16(b + at + 14);
            } else if (label == 0xFE) {
                if (comments == 0 && at + 1 < size) {
                    char text[100];
                    size_t n = b[at];
                    if (at + 1 + n > size) n = size - at - 1;
                    plain(text, sizeof(text), b + at + 1, n);
                    rubraview_details_add(out, "Comment", "%s", text);
                }
                comments++;
            }
        } else {
            break;   /* not a block this format has */
        }
        /* The sub-blocks that follow, to the empty one. */
        while (at < size && b[at]) at += 1 + (size_t)b[at];
        at++;
    }
    rubraview_details_add(out, "Frames", "%u%s", (unsigned)frames, frames > 1 ? "" : " (a still picture)");
    if (frames > 1) {
        rubraview_details_add(out, "Playing time", "%.2f s a round", (double)centiseconds / 100.0);
        if (loops == 0) rubraview_details_add(out, "Repeats", "without end");
        else if (loops > 0) rubraview_details_add(out, "Repeats", "%lld more time%s", (long long)loops, loops == 1 ? "" : "s");
        else rubraview_details_add(out, "Repeats", "not said (played once)");
    }
    if (local) rubraview_details_add(out, "Local palettes", "%u frame%s", (unsigned)local, local == 1 ? "" : "s");
    if (interlaced) rubraview_details_add(out, "Interlaced", "%u frame%s", (unsigned)interlaced, interlaced == 1 ? "" : "s");
    rubraview_details_add(out, "Transparency", "%s", transparent ? "yes" : "none");
    rubraview_details_add(out, "Compression", "LZW");
    return true;
}

static bool webp_details(const uint8_t *b, size_t size, rubraview_details_t *out, uint32_t *raw_bits) {
    rubraview_details_heading(out, "WebP");
    size_t at = 12;
    uint32_t frames = 0;
    bool lossy = false, lossless = false, alpha = false;
    while (at + 8 <= size) {
        const uint8_t *type = b + at, *p = b + at + 8;
        uint32_t len = le32(b + at + 4);
        size_t room = size - at - 8;
        size_t n = len < room ? len : room;
        if (memcmp(type, "VP8X", 4) == 0 && n >= 10) {
            uint8_t f = p[0];
            rubraview_details_add(out, "Container", "extended (VP8X)%s%s%s%s%s", (f & 0x20) ? ", ICC profile" : "", (f & 0x10) ? ", alpha" : "",
                                  (f & 0x08) ? ", EXIF" : "", (f & 0x04) ? ", XMP" : "", (f & 0x02) ? ", animation" : "");
            rubraview_details_add(out, "Canvas", "%u x %u", (unsigned)((le32(p + 4) & 0xFFFFFF) + 1), (unsigned)((le32(p + 6) >> 8) + 1));
            if (f & 0x10) alpha = true;
        } else if (memcmp(type, "ANIM", 4) == 0 && n >= 6) {
            uint32_t loops = le16(p + 4);
            if (loops) rubraview_details_add(out, "Repeats", "%u time%s", (unsigned)loops, loops == 1 ? "" : "s");
            else rubraview_details_add(out, "Repeats", "without end");
        } else if (memcmp(type, "ANMF", 4) == 0 && n >= 16) {
            frames++;
            /* The frame's own picture is a chunk inside this one. */
            if (n >= 24) {
                if (memcmp(p + 16, "VP8L", 4) == 0) lossless = true;
                else if (memcmp(p + 16, "VP8 ", 4) == 0 || memcmp(p + 16, "ALPH", 4) == 0) lossy = true;
            }
        } else if (memcmp(type, "VP8 ", 4) == 0 && n >= 10) {
            lossy = true;
            if (frames == 0 && p[3] == 0x9D && p[4] == 0x01 && p[5] == 0x2A) {
                rubraview_details_add(out, "Picture", "VP8 key frame, %u x %u, version %u", (unsigned)(le16(p + 6) & 0x3FFF),
                                      (unsigned)(le16(p + 8) & 0x3FFF), (unsigned)((p[0] >> 1) & 7));
            }
        } else if (memcmp(type, "VP8L", 4) == 0 && n >= 5) {
            lossless = true;
            if (p[0] == 0x2F) {
                uint32_t bits = le32(p + 1);
                rubraview_details_add(out, "Picture", "VP8L, %u x %u%s", (unsigned)((bits & 0x3FFF) + 1), (unsigned)(((bits >> 14) & 0x3FFF) + 1),
                                      (bits >> 28) & 1 ? ", with alpha" : "");
                if ((bits >> 28) & 1) alpha = true;
            }
        } else if (memcmp(type, "ALPH", 4) == 0 && n >= 1) {
            alpha = true;
            rubraview_details_add(out, "Alpha plane", "%s, filter %u", (p[0] & 3) == 1 ? "lossless" : "stored", (unsigned)((p[0] >> 2) & 3));
        } else if (memcmp(type, "ICCP", 4) == 0) {
            rubraview_details_add(out, "ICC profile", "%u bytes", (unsigned)len);
        } else if (memcmp(type, "EXIF", 4) == 0) {
            rubraview_details_add(out, "EXIF block", "%u bytes", (unsigned)len);
        } else if (memcmp(type, "XMP ", 4) == 0) {
            rubraview_details_add(out, "XMP block", "%u bytes", (unsigned)len);
        }
        at += 8 + (size_t)len + (len & 1);
    }
    rubraview_details_add(out, "Compression", "%s", lossy && lossless ? "lossy and lossless frames" : lossless ? "lossless (VP8L)"
                                                   : lossy ? "lossy (VP8)" : "unknown");
    if (frames) rubraview_details_add(out, "Frames", "%u", (unsigned)frames);
    if (raw_bits) *raw_bits = alpha ? 32 : 24;
    return true;
}

static bool bmp_details(const uint8_t *b, size_t size, rubraview_details_t *out, uint32_t *raw_bits) {
    if (size < 30) return false;
    uint32_t header = le32(b + 14);
    rubraview_details_heading(out, "BMP");
    const char *version = header == 12 ? "OS/2 1.x (BITMAPCOREHEADER)" : header == 40 ? "Windows 3 (BITMAPINFOHEADER)"
                        : header == 52 ? "BITMAPV2INFOHEADER" : header == 56 ? "BITMAPV3INFOHEADER"
                        : header == 64 ? "OS/2 2.x" : header == 108 ? "Windows 95 (BITMAPV4HEADER)"
                        : header == 124 ? "Windows 98 (BITMAPV5HEADER)" : "unknown";
    rubraview_details_add(out, "Header", "%s, %u bytes", version, (unsigned)header);
    if (header == 12) {
        rubraview_details_add(out, "Bits a pixel", "%u", (unsigned)le16(b + 24));
        if (raw_bits) *raw_bits = le16(b + 24);
        return true;
    }
    if (size < 54) return true;
    int32_t height = (int32_t)le32(b + 22);
    uint32_t bits = le16(b + 28), compression = le32(b + 30), colours = le32(b + 46);
    static const char *const METHOD[7] = { "none (BI_RGB)", "RLE 8-bit", "RLE 4-bit", "bit fields", "JPEG", "PNG", "bit fields with alpha" };
    rubraview_details_add(out, "Bits a pixel", "%u", (unsigned)bits);
    rubraview_details_add(out, "Compression", "%s", compression < 7 ? METHOD[compression] : "unknown");
    rubraview_details_add(out, "Row order", "%s", height < 0 ? "top down" : "bottom up");
    if (bits <= 8) rubraview_details_add(out, "Palette", "%u colours", (unsigned)(colours ? colours : 1u << bits));
    if (le32(b + 38) && le32(b + 42)) rubraview_details_add(out, "Pixel size", "%u x %u a metre (%.0f x %.0f dpi)", (unsigned)le32(b + 38),
                                                              (unsigned)le32(b + 42), le32(b + 38) * 0.0254, le32(b + 42) * 0.0254);
    if (header >= 108 && size >= 14 + 60) {
        uint32_t space = le32(b + 14 + 56);
        const char *name = space == 0 ? "calibrated RGB" : space == 0x73524742 ? "sRGB" : space == 0x57696E20 ? "the system's"
                         : space == 0x4C494E4B ? "a linked profile" : space == 0x4D424544 ? "an embedded profile" : "unknown";
        rubraview_details_add(out, "Colour space", "%s", name);
    }
    if (raw_bits) *raw_bits = bits;
    return true;
}

bool rubraview_image_details(const uint8_t *bytes, size_t size, rubraview_details_t *out, uint32_t *out_raw_bits) {
    if (out_raw_bits) *out_raw_bits = 0;
    if (!bytes || !out || size < 12) return false;
    if (bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) return jpeg_details(bytes, size, out, out_raw_bits);
    if (memcmp(bytes, "\x89PNG\r\n\x1A\n", 8) == 0) return png_details(bytes, size, out, out_raw_bits);
    if (memcmp(bytes, "GIF87a", 6) == 0 || memcmp(bytes, "GIF89a", 6) == 0) return gif_details(bytes, size, out, out_raw_bits);
    if (memcmp(bytes, "RIFF", 4) == 0 && memcmp(bytes + 8, "WEBP", 4) == 0) return webp_details(bytes, size, out, out_raw_bits);
    if (bytes[0] == 'B' && bytes[1] == 'M') return bmp_details(bytes, size, out, out_raw_bits);
    return false;
}
