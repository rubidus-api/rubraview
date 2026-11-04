#include "rubraview/core.h"
#include "rubraview/detail.h"
#include "rubraview/lang.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static size_t clip(int n, size_t cap) {
    if (n <= 0 || cap == 0) return 0;
    return (size_t)n < cap ? (size_t)n : cap - 1;
}

/* More text after what `buf` holds up to `at`; returns the new end. */
static size_t append_args(char *buf, size_t cap, size_t at, const char *format, const proven_arg_t *fmt_args, size_t fmt_count) {
    if (at + 1 >= cap) return at;
    int n = rubraview_fmt_length(proven_fmt_to_internal(buf + at, cap - at, format, fmt_args, fmt_count));
    return at + clip(n, cap - at);
}
#define append(buf, cap, at, format, ...) append_args(buf, cap, at, format, RUBRAVIEW_FMT_ARGS(__VA_ARGS__))

void rubraview_details_heading_args(rubraview_details_t *d, const char *format, const proven_arg_t *fmt_args, size_t fmt_count) {
    if (!d || !format || d->count >= RUBRAVIEW_DETAIL_MAX) return;
    rubraview_detail_t *line = &d->lines[d->count];
    *line = (rubraview_detail_t){ .heading = true };
    int n = rubraview_fmt_length(proven_fmt_to_internal(line->label, sizeof(line->label), format, fmt_args, fmt_count));
    if (n > 0) d->count++;
}

void rubraview_details_add_args(rubraview_details_t *d, const char *label, const char *format, const proven_arg_t *fmt_args, size_t fmt_count) {
    if (!d || !label || !format || d->count >= RUBRAVIEW_DETAIL_MAX) return;
    rubraview_detail_t *line = &d->lines[d->count];
    *line = (rubraview_detail_t){ .heading = false };
    rubraview_fmt(line->label, sizeof(line->label), "{}", PROVEN_ARG(label));
    int n = rubraview_fmt_length(proven_fmt_to_internal(line->value, sizeof(line->value), format, fmt_args, fmt_count));
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
    if (bps >= 10.0e6) return clip(rubraview_fmt(buf, cap, T("{:.1} Mbit/s"), PROVEN_ARG((double)(bps / 1.0e6))), cap);
    if (bps >= 1000.0) return clip(rubraview_fmt(buf, cap, T("{:.0} kbit/s"), PROVEN_ARG((double)(bps / 1000.0))), cap);
    return clip(rubraview_fmt(buf, cap, T("{:.0} bit/s"), PROVEN_ARG((double)bps)), cap);
}

size_t rubraview_detail_channels(char *buf, size_t cap, uint32_t count, uint32_t mask) {
    if (!buf || cap == 0) return 0;
    buf[0] = '\0';
    if (count == 0) return 0;
    const char *const SPEAKER[18] = { T("FL"), T("FR"), T("FC"), T("LFE"), T("BL"), T("BR"), T("FLC"), T("FRC"), T("BC"), T("SL"), T("SR"),
                                             T("TC"), T("TFL"), T("TFC"), T("TFR"), T("TBL"), T("TBC"), T("TBR") };
    const char *name = NULL;
    switch (mask) {
        case 0x4: name = T("mono"); break;
        case 0x3: name = T("stereo"); break;
        case 0xB: name = "2.1"; break;
        case 0x7: name = "3.0"; break;
        case 0x33: case 0x603: name = "4.0"; break;
        case 0x107: name = T("4.0 (surround)"); break;
        case 0x37: case 0x607: name = "5.0"; break;
        case 0x3F: name = "5.1"; break;
        case 0x60F: name = T("5.1 (side)"); break;
        case 0x70F: name = "6.1"; break;
        case 0x63F: name = "7.1"; break;
        case 0xFF: name = T("7.1 (wide)"); break;
        default: break;
    }
    if (mask == 0) {
        if (count == 1) return clip(rubraview_fmt(buf, cap, T("mono")), cap);
        if (count == 2) return clip(rubraview_fmt(buf, cap, T("stereo")), cap);
        return clip(rubraview_fmt(buf, cap, T("{} channels"), PROVEN_ARG((unsigned)count)), cap);
    }
    int n = name ? rubraview_fmt(buf, cap, "{}", PROVEN_ARG(name)) : rubraview_fmt(buf, cap, T("{} channels"), PROVEN_ARG((unsigned)count));
    if (count <= 2 && name) return clip(n, cap);
    size_t at = clip(n, cap);
    bool first = true;
    for (uint32_t bit = 0; bit < 18; ++bit) {
        if (!(mask & (1u << bit))) continue;
        at = append(buf, cap, at, "{}{}", PROVEN_ARG(first ? " (" : " "), PROVEN_ARG(SPEAKER[bit]));
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
        case 44: name = T("CAVLC 4:4:4 Intra"); break;
        case 66: name = T("Baseline"); break;
        case 77: name = T("Main"); break;
        case 88: name = T("Extended"); break;
        case 100: name = T("High"); break;
        case 110: name = T("High 10"); break;
        case 118: name = T("Multiview High"); break;
        case 122: name = T("High 4:2:2"); break;
        case 128: name = T("Stereo High"); break;
        case 244: name = T("High 4:4:4 Predictive"); break;
        default: break;
    }
    size_t at = 0;
    if (name) at = clip(rubraview_fmt(buf, cap, "{}", PROVEN_ARG(name)), cap);
    else if (profile) at = clip(rubraview_fmt(buf, cap, T("profile {}"), PROVEN_ARG((unsigned)profile)), cap);
    if (level) {
        at = append(buf, cap, at, T("{}level {}"), PROVEN_ARG(at ? ", " : ""), PROVEN_ARG((unsigned)(level / 10)));
        if (level % 10) at = append(buf, cap, at, ".{}", PROVEN_ARG((unsigned)(level % 10)));
    }
    return at;
}

size_t rubraview_detail_hevc_profile(char *buf, size_t cap, uint32_t profile, uint32_t level) {
    if (!buf || cap == 0) return 0;
    buf[0] = '\0';
    const char *const NAME[] = { NULL, T("Main"), T("Main 10"), T("Main Still Picture"), T("Range Extensions") };
    size_t at = 0;
    if (profile >= 1 && profile <= 4) at = clip(rubraview_fmt(buf, cap, "{}", PROVEN_ARG(NAME[profile])), cap);
    else if (profile) at = clip(rubraview_fmt(buf, cap, T("profile {}"), PROVEN_ARG((unsigned)profile)), cap);
    if (level) {
        /* general_level_idc is thirty times the level. */
        at = append(buf, cap, at, T("{}level {}"), PROVEN_ARG(at ? ", " : ""), PROVEN_ARG((unsigned)(level / 30)));
        if (level % 30) at = append(buf, cap, at, ".{}", PROVEN_ARG((unsigned)(level % 30 / 3)));
    }
    return at;
}

size_t rubraview_detail_aac_profile(char *buf, size_t cap, uint32_t indication) {
    if (!buf || cap == 0) return 0;
    buf[0] = '\0';
    const struct { uint32_t code; const char *name; int level; } KNOWN[] = {
        { 0x28, T("AAC-LC"), 1 }, { 0x29, T("AAC-LC"), 2 }, { 0x2A, T("AAC-LC"), 4 }, { 0x2B, T("AAC-LC"), 5 },
        { 0x2C, T("HE-AAC"), 2 }, { 0x2D, T("HE-AAC"), 3 }, { 0x2E, T("HE-AAC"), 4 }, { 0x2F, T("HE-AAC"), 5 },
        { 0x30, T("HE-AAC v2"), 2 }, { 0x31, T("HE-AAC v2"), 3 }, { 0x32, T("HE-AAC v2"), 4 }, { 0x33, T("HE-AAC v2"), 5 },
        { 0x50, T("AAC-LC"), 7 }, { 0x51, T("AAC-LC"), 7 }, { 0x52, T("HE-AAC"), 7 },
    };
    for (size_t i = 0; i < sizeof(KNOWN) / sizeof(KNOWN[0]); ++i) {
        if (KNOWN[i].code == indication) return clip(rubraview_fmt(buf, cap, T("{}, level {}"), PROVEN_ARG(KNOWN[i].name), PROVEN_ARG((int)KNOWN[i].level)), cap);
    }
    if (indication == 0 || indication == 0xFE || indication == 0xFF) return 0;
    return clip(rubraview_fmt(buf, cap, T("profile and level 0x{:02X}"), PROVEN_ARG((unsigned)indication)), cap);
}

size_t rubraview_detail_ratio(char *buf, size_t cap, double stored, double raw) {
    if (!buf || cap == 0) return 0;
    buf[0] = '\0';
    if (!(stored > 0.0) || !(raw > 0.0)) return 0;
    /* RV-220: the decimals chosen by the numbers, as the format's two forms */
    bool whole = raw / stored >= 100.0, whole_pct = stored / raw * 100.0 >= 10.0;
    const char *f = whole ? (whole_pct ? T("1 : {:.0} ({:.0} % of the size unpacked)") : T("1 : {:.0} ({:.1} % of the size unpacked)"))
                          : (whole_pct ? T("1 : {:.1} ({:.0} % of the size unpacked)") : T("1 : {:.1} ({:.1} % of the size unpacked)"));
    return clip(rubraview_fmt(buf, cap, f, PROVEN_ARG(raw / stored), PROVEN_ARG(stored / raw * 100.0)), cap);
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
    const uint8_t STD[64] = {   /* in zig-zag order, as DQT stores them */
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

static bool jpeg_details(proven_mem_view_t b_view, rubraview_details_t *out, uint32_t *raw_bits) {
    const uint8_t *b = (const uint8_t *)b_view.ptr;
    size_t size = (size_t)b_view.size;
    const char *const PROCESS[16] = {
        T("baseline DCT, Huffman"), T("extended sequential DCT, Huffman"), T("progressive DCT, Huffman"), T("lossless, Huffman"),
        NULL, T("differential sequential DCT, Huffman"), T("differential progressive DCT, Huffman"), T("differential lossless, Huffman"),
        NULL, T("extended sequential DCT, arithmetic"), T("progressive DCT, arithmetic"), T("lossless, arithmetic"),
        NULL, T("differential sequential DCT, arithmetic"), T("differential progressive DCT, arithmetic"), T("differential lossless, arithmetic") };
    rubraview_details_heading(out, T("JPEG"));
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
                rubraview_details_add(out, T("Process"), "{}", PROVEN_ARG(process ? process : T("unknown")));
                rubraview_details_add(out, T("Sample precision"), T("{} bits"), PROVEN_ARG((unsigned)precision));
                rubraview_details_add(out, T("Frame"), "{} x {}", PROVEN_ARG((unsigned)width), PROVEN_ARG((unsigned)height));
                const char *space = comps == 1 ? T("greyscale") : comps == 3 ? T("three (YCbCr or RGB)") : comps == 4 ? T("four (CMYK or YCCK)") : "";
                rubraview_details_add(out, T("Components"), "{}{}{}", PROVEN_ARG((unsigned)comps), PROVEN_ARG(space[0] ? ": " : ""), PROVEN_ARG(space));
                if (raw_bits) *raw_bits = precision * comps;
                if (comps >= 3 && n >= 6 + 3 * (size_t)comps) {
                    uint32_t h0 = p[7] >> 4, v0 = p[7] & 15, h1 = p[10] >> 4, v1 = p[10] & 15;
                    const char *name = "";
                    if (h1 && v1) {
                        uint32_t hr = h0 / h1, vr = v0 / v1;
                        name = (hr == 1 && vr == 1) ? T("4:4:4 (none)") : (hr == 2 && vr == 1) ? "4:2:2"
                             : (hr == 2 && vr == 2) ? "4:2:0" : (hr == 4 && vr == 1) ? "4:1:1"
                             : (hr == 1 && vr == 2) ? "4:4:0" : (hr == 4 && vr == 2) ? "4:1:0" : "";
                    }
                    rubraview_details_add(out, T("Chroma sampling"), "{}{}{}x{}, {}x{}, {}x{}", PROVEN_ARG(name), PROVEN_ARG(name[0] ? "; " : ""),
                                          PROVEN_ARG((unsigned)h0), PROVEN_ARG((unsigned)v0), PROVEN_ARG((unsigned)h1), PROVEN_ARG((unsigned)v1),
                                          PROVEN_ARG((unsigned)(p[13] >> 4)), PROVEN_ARG((unsigned)(p[13] & 15)));
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
            if (be16(p)) rubraview_details_add(out, T("Restart interval"), T("{} blocks"), PROVEN_ARG((unsigned)be16(p)));
        } else if (marker == 0xE0 && n >= 14 && proven_sys_mem_cmp(p, "JFIF\0", 5) == 0) {
            const char *const UNIT[3] = { T("aspect ratio"), T("dots an inch"), T("dots a centimetre") };
            rubraview_details_add(out, T("JFIF"), T("version {}.{:02}, {} x {} {}"), PROVEN_ARG((unsigned)p[5]), PROVEN_ARG((unsigned)p[6]),
                                  PROVEN_ARG((unsigned)be16(p + 8)), PROVEN_ARG((unsigned)be16(p + 10)), PROVEN_ARG(p[7] < 3 ? UNIT[p[7]] : ""));
            if (p[12] && p[13]) rubraview_details_add(out, T("JFIF thumbnail"), "{} x {}", PROVEN_ARG((unsigned)p[12]), PROVEN_ARG((unsigned)p[13]));
        } else if (marker == 0xE1 && n >= 6 && proven_sys_mem_cmp(p, "Exif\0\0", 6) == 0) {
            rubraview_details_add(out, T("EXIF block"), T("{} bytes"), PROVEN_ARG((size_t)n));
        } else if (marker == 0xE1 && n >= 28 && proven_sys_mem_cmp(p, "http://ns.adobe.com/xap/1.0/", 28) == 0) {
            rubraview_details_add(out, T("XMP block"), T("{} bytes"), PROVEN_ARG((size_t)n));
        } else if (marker == 0xE2 && n >= 14 && proven_sys_mem_cmp(p, "ICC_PROFILE\0", 12) == 0) {
            if (p[12] == 1) rubraview_details_add(out, T("ICC profile"), T("{} part{}"), PROVEN_ARG((unsigned)p[13]), PROVEN_ARG(p[13] == 1 ? "" : TC("plural", "s")));
        } else if (marker == 0xE2 && n >= 4 && proven_sys_mem_cmp(p, "MPF\0", 4) == 0) {
            rubraview_details_add(out, T("Multi-picture"), T("yes (MPF)"));
        } else if (marker == 0xED && n >= 14 && proven_sys_mem_cmp(p, "Photoshop 3.0\0", 14) == 0) {
            rubraview_details_add(out, T("Photoshop block"), T("{} bytes (IPTC)"), PROVEN_ARG((size_t)n));
        } else if (marker == 0xEE && n >= 12 && proven_sys_mem_cmp(p, "Adobe", 5) == 0) {
            const char *const TRANSFORM[3] = { T("none (RGB or CMYK)"), T("YCbCr"), T("YCCK") };
            rubraview_details_add(out, T("Adobe marker"), T("colour transform {}"), PROVEN_ARG(p[11] < 3 ? TRANSFORM[p[11]] : T("unknown")));
        } else if (marker == 0xFE && n > 0) {
            char text[100];
            plain(text, sizeof(text), p, n);
            rubraview_details_add(out, T("Comment"), "{}", PROVEN_ARG(text));
        } else if (marker == 0xDA) {
            break;   /* the picture's data: nothing more is read */
        }
        at += 2 + len;
    }
    if (luma_quality >= 0) {
        if (chroma_quality >= 0) rubraview_details_add(out, T("Quality (estimate)"), T("{}, colour tables {}; {} table{}{}"), PROVEN_ARG((int)luma_quality),
                                                       PROVEN_ARG((int)chroma_quality), PROVEN_ARG((int)tables), PROVEN_ARG(tables == 1 ? "" : TC("plural", "s")), PROVEN_ARG(sixteen_bit_tables ? T(", 16-bit") : ""));
        else rubraview_details_add(out, T("Quality (estimate)"), T("{}; {} table{}{}"), PROVEN_ARG((int)luma_quality), PROVEN_ARG((int)tables), PROVEN_ARG(tables == 1 ? "" : TC("plural", "s")),
                                   PROVEN_ARG(sixteen_bit_tables ? T(", 16-bit") : ""));
    }
    return true;
}

static bool png_details(proven_mem_view_t b_view, rubraview_details_t *out, uint32_t *raw_bits) {
    const uint8_t *b = (const uint8_t *)b_view.ptr;
    size_t size = (size_t)b_view.size;
    rubraview_details_heading(out, T("PNG"));
    size_t at = 8;
    uint64_t idat = 0;
    uint32_t idat_chunks = 0, texts = 0;
    bool first_idat = true;
    while (at + 12 <= size) {
        uint32_t len = be32(b + at);
        const uint8_t *type = b + at + 4, *p = b + at + 8;
        if ((uint64_t)at + 12 + len > size) break;
        if (proven_sys_mem_cmp(type, "IHDR", 4) == 0 && len >= 13) {
            const char *const KIND[7] = { T("greyscale"), NULL, T("truecolour (RGB)"), T("palette"), T("greyscale with alpha"), NULL, T("truecolour with alpha (RGBA)") };
            const uint8_t CHANNELS[7] = { 1, 0, 3, 1, 2, 0, 4 };
            uint32_t depth = p[8], kind = p[9];
            rubraview_details_add(out, T("Bit depth"), T("{} bits a {}"), PROVEN_ARG((unsigned)depth), PROVEN_ARG(kind == 3 ? T("palette index") : T("channel")));
            rubraview_details_add(out, T("Colour type"), "{}: {}", PROVEN_ARG((unsigned)kind), PROVEN_ARG(kind < 7 && KIND[kind] ? KIND[kind] : T("unknown")));
            rubraview_details_add(out, T("Compression"), T("deflate (method {}), filter method {}"), PROVEN_ARG((unsigned)p[10]), PROVEN_ARG((unsigned)p[11]));
            rubraview_details_add(out, T("Interlace"), "{}", PROVEN_ARG(p[12] == 1 ? T("Adam7") : T("none")));
            if (raw_bits && kind < 7) *raw_bits = depth * CHANNELS[kind];
        } else if (proven_sys_mem_cmp(type, "PLTE", 4) == 0) {
            rubraview_details_add(out, T("Palette"), T("{} colours"), PROVEN_ARG((unsigned)(len / 3)));
        } else if (proven_sys_mem_cmp(type, "tRNS", 4) == 0) {
            rubraview_details_add(out, T("Transparency"), T("tRNS, {} bytes"), PROVEN_ARG((unsigned)len));
        } else if (proven_sys_mem_cmp(type, "gAMA", 4) == 0 && len >= 4) {
            uint32_t g = be32(p);
            if (g) rubraview_details_add(out, T("Gamma"), T("{} (file gamma {:.5})"), PROVEN_ARG(rubraview_g_text((char[32]){0}, (double)(100000.0 / (double)g), 5)), PROVEN_ARG((double)g / 100000.0));
        } else if (proven_sys_mem_cmp(type, "sRGB", 4) == 0 && len >= 1) {
            const char *const INTENT[4] = { T("perceptual"), T("relative colorimetric"), T("saturation"), T("absolute colorimetric") };
            rubraview_details_add(out, T("sRGB"), T("rendering intent {}"), PROVEN_ARG(p[0] < 4 ? INTENT[p[0]] : T("unknown")));
        } else if (proven_sys_mem_cmp(type, "cHRM", 4) == 0 && len >= 32) {
            rubraview_details_add(out, T("Chromaticities"), T("white {:.4} {:.4}, R {:.3} {:.3}, G {:.3} {:.3}, B {:.3} {:.3}"),
                                  PROVEN_ARG((double)(be32(p) / 1e5)), PROVEN_ARG((double)(be32(p + 4) / 1e5)), PROVEN_ARG((double)(be32(p + 8) / 1e5)), PROVEN_ARG((double)(be32(p + 12) / 1e5)),
                                  PROVEN_ARG((double)(be32(p + 16) / 1e5)), PROVEN_ARG((double)(be32(p + 20) / 1e5)), PROVEN_ARG((double)(be32(p + 24) / 1e5)), PROVEN_ARG((double)(be32(p + 28) / 1e5)));
        } else if (proven_sys_mem_cmp(type, "iCCP", 4) == 0) {
            char name[80];
            plain(name, sizeof(name), p, len);
            rubraview_details_add(out, T("ICC profile"), "\"{}\"", PROVEN_ARG(name));
        } else if (proven_sys_mem_cmp(type, "cICP", 4) == 0 && len >= 4) {
            rubraview_details_add(out, T("Colour (cICP)"), T("primaries {}, transfer {}, matrix {}, {} range"),
                                  PROVEN_ARG((unsigned)p[0]), PROVEN_ARG((unsigned)p[1]), PROVEN_ARG((unsigned)p[2]), PROVEN_ARG(p[3] ? T("full") : T("limited")));
        } else if (proven_sys_mem_cmp(type, "pHYs", 4) == 0 && len >= 9) {
            if (p[8] == 1) rubraview_details_add(out, T("Pixel size"), T("{} x {} a metre ({:.0} x {:.0} dpi)"), PROVEN_ARG((unsigned)be32(p)), PROVEN_ARG((unsigned)be32(p + 4)),
                                                 PROVEN_ARG((double)(be32(p) * 0.0254)), PROVEN_ARG((double)(be32(p + 4) * 0.0254)));
            else rubraview_details_add(out, T("Pixel size"), T("aspect {} : {}"), PROVEN_ARG((unsigned)be32(p)), PROVEN_ARG((unsigned)be32(p + 4)));
        } else if (proven_sys_mem_cmp(type, "tIME", 4) == 0 && len >= 7) {
            rubraview_details_add(out, T("Last changed"), T("{:04}-{:02}-{:02} {:02}:{:02}:{:02} UTC"), PROVEN_ARG((unsigned)be16(p)), PROVEN_ARG((unsigned)p[2]), PROVEN_ARG((unsigned)p[3]),
                                  PROVEN_ARG((unsigned)p[4]), PROVEN_ARG((unsigned)p[5]), PROVEN_ARG((unsigned)p[6]));
        } else if (proven_sys_mem_cmp(type, "acTL", 4) == 0 && len >= 8) {
            uint32_t plays = be32(p + 4);
            if (plays) rubraview_details_add(out, T("Animation"), T("{} frames, played {} time{}"), PROVEN_ARG((unsigned)be32(p)), PROVEN_ARG((unsigned)plays), PROVEN_ARG(plays == 1 ? "" : TC("plural", "s")));
            else rubraview_details_add(out, T("Animation"), T("{} frames, played without end"), PROVEN_ARG((unsigned)be32(p)));
        } else if (proven_sys_mem_cmp(type, "bKGD", 4) == 0) {
            rubraview_details_add(out, T("Background"), T("given (bKGD)"));
        } else if (proven_sys_mem_cmp(type, "eXIf", 4) == 0) {
            rubraview_details_add(out, T("EXIF block"), T("{} bytes"), PROVEN_ARG((unsigned)len));
        } else if ((proven_sys_mem_cmp(type, "tEXt", 4) == 0 || proven_sys_mem_cmp(type, "zTXt", 4) == 0 || proven_sys_mem_cmp(type, "iTXt", 4) == 0) && texts < 16) {
            char key[24], text[100];
            plain(key, sizeof(key), p, len);
            size_t k = proven_cstr_len(key);
            text[0] = '\0';
            if (type[0] == 't' && k + 1 < len) {
                size_t n = len - k - 1;
                /* Latin-1 in the file; what is not ASCII becomes a dot. */
                size_t w = 0;
                for (; w < n && w + 1 < sizeof(text); ++w) text[w] = (p[k + 1 + w] >= 0x20 && p[k + 1 + w] < 0x7F) ? (char)p[k + 1 + w] : '.';
                text[w] = '\0';
            }
            char label[24];
            rubraview_fmt(label, sizeof(label), T("Text: {:.16}"), PROVEN_ARG(key));
            rubraview_details_add(out, label, "{}", PROVEN_ARG(text[0] ? text : type[0] == 'z' ? T("(compressed)") : type[0] == 'i' ? T("(international text)") : T("(empty)")));
            texts++;
        } else if (proven_sys_mem_cmp(type, "IDAT", 4) == 0) {
            if (first_idat && len >= 2) {
                const char *const LEVEL[4] = { T("fastest"), T("fast"), T("default"), T("maximum") };
                unsigned cinfo = (unsigned)p[0] >> 4;
                rubraview_details_add(out, T("Deflate stream"), T("{}-byte window, compressed at \"{}\""), PROVEN_ARG((unsigned)(cinfo <= 7 ? 1u << (cinfo + 8) : 0u)),
                                      PROVEN_ARG(LEVEL[p[1] >> 6]));
                first_idat = false;
            }
            idat += len;
            idat_chunks++;
        } else if (proven_sys_mem_cmp(type, "IEND", 4) == 0) {
            break;
        }
        at += 12 + (size_t)len;
    }
    if (idat_chunks) rubraview_details_add(out, T("Picture data"), T("{} bytes in {} chunk{}"), PROVEN_ARG((unsigned long long)idat), PROVEN_ARG((unsigned)idat_chunks), PROVEN_ARG(idat_chunks == 1 ? "" : TC("plural", "s")));
    return true;
}

static bool gif_details(proven_mem_view_t b_view, rubraview_details_t *out, uint32_t *raw_bits) {
    const uint8_t *b = (const uint8_t *)b_view.ptr;
    size_t size = (size_t)b_view.size;
    if (size < 13) return false;
    rubraview_details_heading(out, T("GIF"));
    rubraview_details_add(out, T("Version"), "{:.3}", PROVEN_ARG((const char*)b + 3));
    rubraview_details_add(out, T("Canvas"), "{} x {}", PROVEN_ARG((unsigned)le16(b + 6)), PROVEN_ARG((unsigned)le16(b + 8)));
    uint8_t flags = b[10];
    if (flags & 0x80) rubraview_details_add(out, T("Global palette"), T("{} colours{}, background index {}"), PROVEN_ARG((unsigned)(2u << (flags & 7))),
                                            PROVEN_ARG((flags & 8) ? T(", sorted") : ""), PROVEN_ARG((unsigned)b[11]));
    else rubraview_details_add(out, T("Global palette"), T("none"));
    rubraview_details_add(out, T("Colour resolution"), T("{} bits"), PROVEN_ARG((unsigned)(((flags >> 4) & 7) + 1)));
    if (b[12]) rubraview_details_add(out, T("Pixel aspect"), "{:.3}", PROVEN_ARG((double)(((double)b[12] + 15.0) / 64.0)));
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
                       (proven_sys_mem_cmp(b + at + 1, "NETSCAPE2.0", 11) == 0 || proven_sys_mem_cmp(b + at + 1, "ANIMEXTS1.0", 11) == 0) &&
                       b[at + 12] == 3 && b[at + 13] == 1) {
                loops = (int64_t)le16(b + at + 14);
            } else if (label == 0xFE) {
                if (comments == 0 && at + 1 < size) {
                    char text[100];
                    size_t n = b[at];
                    if (at + 1 + n > size) n = size - at - 1;
                    plain(text, sizeof(text), b + at + 1, n);
                    rubraview_details_add(out, T("Comment"), "{}", PROVEN_ARG(text));
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
    rubraview_details_add(out, T("Frames"), "{}{}", PROVEN_ARG((unsigned)frames), PROVEN_ARG(frames > 1 ? "" : T(" (a still picture)")));
    if (frames > 1) {
        rubraview_details_add(out, T("Playing time"), T("{:.2} s a round"), PROVEN_ARG((double)centiseconds / 100.0));
        if (loops == 0) rubraview_details_add(out, T("Repeats"), T("without end"));
        else if (loops > 0) rubraview_details_add(out, T("Repeats"), T("{} more time{}"), PROVEN_ARG((long long)loops), PROVEN_ARG(loops == 1 ? "" : TC("plural", "s")));
        else rubraview_details_add(out, T("Repeats"), T("not said (played once)"));
    }
    if (local) rubraview_details_add(out, T("Local palettes"), T("{} frame{}"), PROVEN_ARG((unsigned)local), PROVEN_ARG(local == 1 ? "" : TC("plural", "s")));
    if (interlaced) rubraview_details_add(out, T("Interlaced"), T("{} frame{}"), PROVEN_ARG((unsigned)interlaced), PROVEN_ARG(interlaced == 1 ? "" : TC("plural", "s")));
    rubraview_details_add(out, T("Transparency"), "{}", PROVEN_ARG(transparent ? T("yes") : T("none")));
    rubraview_details_add(out, T("Compression"), T("LZW"));
    return true;
}

static bool webp_details(proven_mem_view_t b_view, rubraview_details_t *out, uint32_t *raw_bits) {
    const uint8_t *b = (const uint8_t *)b_view.ptr;
    size_t size = (size_t)b_view.size;
    rubraview_details_heading(out, T("WebP"));
    size_t at = 12;
    uint32_t frames = 0;
    bool lossy = false, lossless = false, alpha = false;
    while (at + 8 <= size) {
        const uint8_t *type = b + at, *p = b + at + 8;
        uint32_t len = le32(b + at + 4);
        size_t room = size - at - 8;
        size_t n = len < room ? len : room;
        if (proven_sys_mem_cmp(type, "VP8X", 4) == 0 && n >= 10) {
            uint8_t f = p[0];
            rubraview_details_add(out, T("Container"), T("extended (VP8X){}{}{}{}{}"), PROVEN_ARG((f & 0x20) ? T(", ICC profile") : ""), PROVEN_ARG((f & 0x10) ? T(", alpha") : ""),
                                  PROVEN_ARG((f & 0x08) ? T(", EXIF") : ""), PROVEN_ARG((f & 0x04) ? T(", XMP") : ""), PROVEN_ARG((f & 0x02) ? T(", animation") : ""));
            rubraview_details_add(out, T("Canvas"), "{} x {}", PROVEN_ARG((unsigned)((le32(p + 4) & 0xFFFFFF) + 1)), PROVEN_ARG((unsigned)((le32(p + 6) >> 8) + 1)));
            if (f & 0x10) alpha = true;
        } else if (proven_sys_mem_cmp(type, "ANIM", 4) == 0 && n >= 6) {
            uint32_t loops = le16(p + 4);
            if (loops) rubraview_details_add(out, T("Repeats"), T("{} time{}"), PROVEN_ARG((unsigned)loops), PROVEN_ARG(loops == 1 ? "" : TC("plural", "s")));
            else rubraview_details_add(out, T("Repeats"), T("without end"));
        } else if (proven_sys_mem_cmp(type, "ANMF", 4) == 0 && n >= 16) {
            frames++;
            /* The frame's own picture is a chunk inside this one. */
            if (n >= 24) {
                if (proven_sys_mem_cmp(p + 16, "VP8L", 4) == 0) lossless = true;
                else if (proven_sys_mem_cmp(p + 16, "VP8 ", 4) == 0 || proven_sys_mem_cmp(p + 16, "ALPH", 4) == 0) lossy = true;
            }
        } else if (proven_sys_mem_cmp(type, "VP8 ", 4) == 0 && n >= 10) {
            lossy = true;
            if (frames == 0 && p[3] == 0x9D && p[4] == 0x01 && p[5] == 0x2A) {
                rubraview_details_add(out, T("Picture"), T("VP8 key frame, {} x {}, version {}"), PROVEN_ARG((unsigned)(le16(p + 6) & 0x3FFF)),
                                      PROVEN_ARG((unsigned)(le16(p + 8) & 0x3FFF)), PROVEN_ARG((unsigned)((p[0] >> 1) & 7)));
            }
        } else if (proven_sys_mem_cmp(type, "VP8L", 4) == 0 && n >= 5) {
            lossless = true;
            if (p[0] == 0x2F) {
                uint32_t bits = le32(p + 1);
                rubraview_details_add(out, T("Picture"), T("VP8L, {} x {}{}"), PROVEN_ARG((unsigned)((bits & 0x3FFF) + 1)), PROVEN_ARG((unsigned)(((bits >> 14) & 0x3FFF) + 1)),
                                      PROVEN_ARG((bits >> 28) & 1 ? T(", with alpha") : ""));
                if ((bits >> 28) & 1) alpha = true;
            }
        } else if (proven_sys_mem_cmp(type, "ALPH", 4) == 0 && n >= 1) {
            alpha = true;
            rubraview_details_add(out, T("Alpha plane"), T("{}, filter {}"), PROVEN_ARG((p[0] & 3) == 1 ? T("lossless") : T("stored")), PROVEN_ARG((unsigned)((p[0] >> 2) & 3)));
        } else if (proven_sys_mem_cmp(type, "ICCP", 4) == 0) {
            rubraview_details_add(out, T("ICC profile"), T("{} bytes"), PROVEN_ARG((unsigned)len));
        } else if (proven_sys_mem_cmp(type, "EXIF", 4) == 0) {
            rubraview_details_add(out, T("EXIF block"), T("{} bytes"), PROVEN_ARG((unsigned)len));
        } else if (proven_sys_mem_cmp(type, "XMP ", 4) == 0) {
            rubraview_details_add(out, T("XMP block"), T("{} bytes"), PROVEN_ARG((unsigned)len));
        }
        at += 8 + (size_t)len + (len & 1);
    }
    rubraview_details_add(out, T("Compression"), "{}", PROVEN_ARG(lossy && lossless ? T("lossy and lossless frames") : lossless ? T("lossless (VP8L)")
                                                   : lossy ? T("lossy (VP8)") : T("unknown")));
    if (frames) rubraview_details_add(out, T("Frames"), "{}", PROVEN_ARG((unsigned)frames));
    if (raw_bits) *raw_bits = alpha ? 32 : 24;
    return true;
}

static bool bmp_details(proven_mem_view_t b_view, rubraview_details_t *out, uint32_t *raw_bits) {
    const uint8_t *b = (const uint8_t *)b_view.ptr;
    size_t size = (size_t)b_view.size;
    if (size < 30) return false;
    uint32_t header = le32(b + 14);
    rubraview_details_heading(out, T("BMP"));
    const char *version = header == 12 ? T("OS/2 1.x (BITMAPCOREHEADER)") : header == 40 ? T("Windows 3 (BITMAPINFOHEADER)")
                        : header == 52 ? T("BITMAPV2INFOHEADER") : header == 56 ? T("BITMAPV3INFOHEADER")
                        : header == 64 ? T("OS/2 2.x") : header == 108 ? T("Windows 95 (BITMAPV4HEADER)")
                        : header == 124 ? T("Windows 98 (BITMAPV5HEADER)") : T("unknown");
    rubraview_details_add(out, T("Header"), T("{}, {} bytes"), PROVEN_ARG(version), PROVEN_ARG((unsigned)header));
    if (header == 12) {
        rubraview_details_add(out, T("Bits a pixel"), "{}", PROVEN_ARG((unsigned)le16(b + 24)));
        if (raw_bits) *raw_bits = le16(b + 24);
        return true;
    }
    if (size < 54) return true;
    int32_t height = (int32_t)le32(b + 22);
    uint32_t bits = le16(b + 28), compression = le32(b + 30), colours = le32(b + 46);
    const char *const METHOD[7] = { T("none (BI_RGB)"), T("RLE 8-bit"), T("RLE 4-bit"), T("bit fields"), T("JPEG"), T("PNG"), T("bit fields with alpha") };
    rubraview_details_add(out, T("Bits a pixel"), "{}", PROVEN_ARG((unsigned)bits));
    rubraview_details_add(out, T("Compression"), "{}", PROVEN_ARG(compression < 7 ? METHOD[compression] : T("unknown")));
    rubraview_details_add(out, T("Row order"), "{}", PROVEN_ARG(height < 0 ? T("top down") : T("bottom up")));
    if (bits <= 8) rubraview_details_add(out, T("Palette"), T("{} colours"), PROVEN_ARG((unsigned)(colours ? colours : 1u << bits)));
    if (le32(b + 38) && le32(b + 42)) rubraview_details_add(out, T("Pixel size"), T("{} x {} a metre ({:.0} x {:.0} dpi)"), PROVEN_ARG((unsigned)le32(b + 38)),
                                                              PROVEN_ARG((unsigned)le32(b + 42)), PROVEN_ARG((double)(le32(b + 38) * 0.0254)), PROVEN_ARG((double)(le32(b + 42) * 0.0254)));
    if (header >= 108 && size >= 14 + 60) {
        uint32_t space = le32(b + 14 + 56);
        const char *name = space == 0 ? T("calibrated RGB") : space == 0x73524742 ? T("sRGB") : space == 0x57696E20 ? T("the system's")
                         : space == 0x4C494E4B ? T("a linked profile") : space == 0x4D424544 ? T("an embedded profile") : T("unknown");
        rubraview_details_add(out, T("Colour space"), "{}", PROVEN_ARG(name));
    }
    if (raw_bits) *raw_bits = bits;
    return true;
}

/* ---- TIFF, and the camera files built on it (D-120) ---- */

typedef struct tiff_view { const uint8_t *b; size_t size; bool big_endian, big; } tiff_view_t;

static uint64_t tiff_n(const tiff_view_t *t, size_t at, uint32_t bytes) {
    if (at > t->size || t->size - at < bytes) return 0;
    uint64_t v = 0;
    for (uint32_t i = 0; i < bytes; ++i) {
        uint64_t byte = t->b[at + (t->big_endian ? i : bytes - 1 - i)];
        v = (v << 8) | byte;
    }
    return v;
}

/* Where an entry's values are: in the entry when they fit, else where it points. */
static size_t tiff_values(const tiff_view_t *t, size_t entry, uint32_t type, uint64_t count) {
    const uint8_t WIDTH[19] = { 0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8, 4, 0, 0, 8, 8, 8 };
    uint64_t width = type < 19 ? WIDTH[type] : 0;
    size_t field = entry + (t->big ? 12u : 8u);
    uint32_t room = t->big ? 8u : 4u;
    if (width == 0 || count == 0 || count > t->size) return 0;
    if (width * count <= room) return field;
    uint64_t at = tiff_n(t, field, room);
    return at < t->size && t->size - at >= width * count ? (size_t)at : 0;
}

static uint64_t tiff_value(const tiff_view_t *t, size_t entry, uint32_t type, uint64_t count, uint64_t index) {
    size_t at = tiff_values(t, entry, type, count);
    if (!at || index >= count) return 0;
    uint32_t width = type == 3 || type == 8 ? 2u : type == 4 || type == 9 || type == 13 ? 4u : type == 16 || type == 17 || type == 18 ? 8u : 1u;
    return tiff_n(t, at + (size_t)index * width, width);
}

static bool tiff_details(proven_mem_view_t b_view, rubraview_details_t *out, uint32_t *raw_bits) {
    const uint8_t *b = (const uint8_t *)b_view.ptr;
    size_t size = (size_t)b_view.size;
    tiff_view_t t = { .b = b, .size = size, .big_endian = b[0] == 'M' };
    uint32_t magic = (uint32_t)tiff_n(&t, 2, 2);
    if (magic != 42 && magic != 43) return false;
    t.big = magic == 43;
    if (t.big && (size < 16 || tiff_n(&t, 4, 2) != 8)) return false;
    uint64_t first = t.big ? tiff_n(&t, 8, 8) : tiff_n(&t, 4, 4);
    uint32_t count_bytes = t.big ? 8u : 2u, entry_bytes = t.big ? 20u : 12u, next_bytes = t.big ? 8u : 4u;
    if (first < 8 || first >= size || size - first < count_bytes) return false;

    rubraview_details_heading(out, T("TIFF"));
    rubraview_details_add(out, T("Kind"), "{}, {}", PROVEN_ARG(t.big ? T("BigTIFF (64-bit offsets)") : T("classic")), PROVEN_ARG(t.big_endian ? T("big-endian (MM)") : T("little-endian (II)")));

    uint64_t compression = 1, photometric = 99, samples = 1, rows = 0, strips = 0, tile_w = 0, tile_h = 0, tiles = 0;
    uint64_t planar = 1, predictor = 1, format = 1, extra = 0, subs = 0, bits_count = 0, bits = 1, cfa_w = 0, cfa_h = 0;
    char make[48] = "", model[48] = "", software[64] = "", dng[16] = "";
    bool exif = false, icc = false, xmp = false, gps = false, mixed_bits = false;
    uint64_t entries = tiff_n(&t, (size_t)first, count_bytes);
    size_t at = (size_t)first + count_bytes;
    for (uint64_t i = 0; i < entries && i < 4096 && at <= size && size - at >= entry_bytes; ++i, at += entry_bytes) {
        uint32_t tag = (uint32_t)tiff_n(&t, at, 2), type = (uint32_t)tiff_n(&t, at + 2, 2);
        uint64_t count = t.big ? tiff_n(&t, at + 4, 8) : tiff_n(&t, at + 4, 4);
        uint64_t v = tiff_value(&t, at, type, count, 0);
        size_t text = type == 2 ? tiff_values(&t, at, type, count) : 0;
        switch (tag) {
            case 258:
                bits = v; bits_count = count;
                for (uint64_t k = 1; k < count && k < 8; ++k) if (tiff_value(&t, at, type, count, k) != v) mixed_bits = true;
                break;
            case 259: compression = v; break;
            case 262: photometric = v; break;
            case 271: if (text) plain(make, sizeof(make), b + text, (size_t)count); break;
            case 272: if (text) plain(model, sizeof(model), b + text, (size_t)count); break;
            case 273: strips = count; break;
            case 277: samples = v; break;
            case 278: rows = v; break;
            case 284: planar = v; break;
            case 305: if (text) plain(software, sizeof(software), b + text, (size_t)count); break;
            case 317: predictor = v; break;
            case 322: tile_w = v; break;
            case 323: tile_h = v; break;
            case 324: tiles = count; break;
            case 330: subs = count; break;
            case 338: extra = count; break;
            case 339: format = v; break;
            case 700: xmp = true; break;
            case 33421: cfa_w = v; cfa_h = tiff_value(&t, at, type, count, 1); break;
            case 34665: exif = true; break;
            case 34675: icc = true; break;
            case 34853: gps = true; break;
            case 50706:
                if (count == 4) rubraview_fmt(dng, sizeof(dng), "{}.{}.{}.{}", PROVEN_ARG((unsigned)tiff_value(&t, at, type, count, 0)), PROVEN_ARG((unsigned)tiff_value(&t, at, type, count, 1)),
                                         PROVEN_ARG((unsigned)tiff_value(&t, at, type, count, 2)), PROVEN_ARG((unsigned)tiff_value(&t, at, type, count, 3)));
                break;
            default: break;
        }
    }
    /* The pages: each directory names the next. */
    uint32_t pages = 1;
    for (uint64_t dir = first; pages < 10000; ) {
        uint64_t n = tiff_n(&t, (size_t)dir, count_bytes);
        uint64_t link = dir + count_bytes + n * entry_bytes;
        if (n == 0 || link >= size || size - link < next_bytes) break;
        uint64_t next = tiff_n(&t, (size_t)link, next_bytes);
        if (next < 8 || next >= size || size - next < count_bytes || next == dir) break;
        dir = next;
        ++pages;
    }
    if (pages > 1) rubraview_details_add(out, T("Directories"), T("{} (pages, or sizes of one picture)"), PROVEN_ARG((unsigned)pages));

    const struct { uint32_t code; const char *name; } METHOD[] = {
        { 1, T("none") }, { 2, T("CCITT Huffman (fax)") }, { 3, T("CCITT Group 3 (fax)") }, { 4, T("CCITT Group 4 (fax)") }, { 5, T("LZW") },
        { 6, T("JPEG (the old way)") }, { 7, T("JPEG") }, { 8, T("Deflate") }, { 32773, T("PackBits") }, { 32946, T("Deflate") },
        { 34712, T("JPEG 2000") }, { 34892, T("lossy JPEG (DNG)") }, { 34925, T("LZMA") }, { 50000, T("Zstandard") }, { 50001, T("WebP") },
        { 50002, T("JPEG XL") }, { 52546, T("JPEG XL (DNG)") }, { 32767, T("Sony ARW") }, { 34713, T("Nikon NEF") }, { 65000, T("Kodak DCR") },
    };
    const char *method = NULL;
    for (size_t i = 0; i < sizeof(METHOD) / sizeof(METHOD[0]); ++i) if (METHOD[i].code == compression) method = METHOD[i].name;
    if (method) rubraview_details_add(out, T("Compression"), "{}", PROVEN_ARG(method));
    else rubraview_details_add(out, T("Compression"), T("method {}"), PROVEN_ARG((unsigned)compression));
    if ((compression == 5 || compression == 8 || compression == 32946 || compression == 50000) && predictor > 1) {
        rubraview_details_add(out, T("Predictor"), "{}", PROVEN_ARG(predictor == 2 ? T("horizontal differences") : predictor == 3 ? T("floating-point") : T("unknown")));
    }
    const char *const COLOUR[11] = { T("white is zero"), T("black is zero"), T("RGB"), T("palette"), T("transparency mask"), T("CMYK"), T("YCbCr"),
                                            NULL, T("CIE L*a*b*"), T("ICC L*a*b*"), T("ITU L*a*b*") };
    if (photometric < 11 && COLOUR[photometric]) rubraview_details_add(out, T("Colour"), "{}", PROVEN_ARG(COLOUR[photometric]));
    else if (photometric == 32803) rubraview_details_add(out, T("Colour"), T("colour filter array (the sensor's mosaic)"));
    else if (photometric == 34892) rubraview_details_add(out, T("Colour"), T("linear raw"));
    if (samples == 0 || samples > 64) samples = 1;
    if (bits_count) {
        const char *kind = format == 3 ? T(" floating-point") : format == 2 ? T(" signed") : "";
        if (mixed_bits) rubraview_details_add(out, T("Samples"), T("{} a pixel, of differing depths"), PROVEN_ARG((unsigned)samples));
        else rubraview_details_add(out, T("Samples"), T("{} a pixel, {}{} bits each"), PROVEN_ARG((unsigned)samples), PROVEN_ARG((unsigned)bits), PROVEN_ARG(kind));
    }
    if (extra) rubraview_details_add(out, T("Extra samples"), T("{} (transparency or more)"), PROVEN_ARG((unsigned)extra));
    if (samples > 1) rubraview_details_add(out, T("Planes"), "{}", PROVEN_ARG(planar == 2 ? T("each sample in its own plane") : T("samples together, pixel by pixel")));
    if (tile_w && tile_h) rubraview_details_add(out, T("Layout"), T("{} tiles of {} x {}"), PROVEN_ARG((unsigned)tiles), PROVEN_ARG((unsigned)tile_w), PROVEN_ARG((unsigned)tile_h));
    else if (strips) rubraview_details_add(out, T("Layout"), T("{} strip{} of {} row{}"), PROVEN_ARG((unsigned)strips), PROVEN_ARG(strips == 1 ? "" : TC("plural", "s")), PROVEN_ARG((unsigned)rows), PROVEN_ARG(rows == 1 ? "" : TC("plural", "s")));
    if (cfa_w && cfa_h) rubraview_details_add(out, T("Mosaic"), "{} x {}", PROVEN_ARG((unsigned)cfa_w), PROVEN_ARG((unsigned)cfa_h));
    if (subs) rubraview_details_add(out, T("Sub-pictures"), T("{} (the full picture of a camera file is one of them)"), PROVEN_ARG((unsigned)subs));
    if (make[0] || model[0]) rubraview_details_add(out, T("Camera"), "{}{}{}", PROVEN_ARG(make), PROVEN_ARG(make[0] && model[0] ? " " : ""), PROVEN_ARG(model));
    if (dng[0]) rubraview_details_add(out, T("DNG version"), "{}", PROVEN_ARG(dng));
    if (software[0]) rubraview_details_add(out, T("Written by"), "{}", PROVEN_ARG(software));
    if (exif || icc || xmp || gps) {
        rubraview_details_add(out, T("Carries"), "{}{}{}{}", PROVEN_ARG(exif ? T("EXIF ") : ""), PROVEN_ARG(gps ? T("GPS ") : ""), PROVEN_ARG(icc ? T("ICC-profile ") : ""), PROVEN_ARG(xmp ? T("XMP ") : ""));
    }
    if (raw_bits && bits_count && !mixed_bits && bits * samples <= 512) *raw_bits = (uint32_t)(bits * samples);
    return true;
}

/* ---- AVIF, HEIF and what else is boxes after `ftyp` (D-120) ---- */

typedef struct bmff_facts {
    uint32_t width, height, channels, bits, items, tracks;
    bool av1, hevc, alpha, icc, nclx, mirrored, sequence;
    uint32_t av1_profile, av1_level, av1_bits, av1_mono, av1_sub_x, av1_sub_y;
    uint32_t hevc_profile, hevc_level, hevc_bits, hevc_chroma;
    uint32_t primaries, transfer, matrix, full_range, turns;
} bmff_facts_t;

/* One box at `at`: its kind, where its body is and how long. False when it does not fit. */
static bool bmff_box(const uint8_t *b, size_t size, size_t at, const uint8_t **kind, size_t *body, size_t *length) {
    if (at > size || size - at < 8) return false;
    uint64_t len = be32(b + at);
    size_t head = 8;
    if (len == 1) {
        if (size - at < 16) return false;
        len = ((uint64_t)be32(b + at + 8) << 32) | be32(b + at + 12);
        head = 16;
    } else if (len == 0) {
        len = size - at;
    }
    if (len < head || len > size - at) return false;
    *kind = b + at + 4;
    *body = at + head;
    *length = (size_t)len - head;
    return true;
}

static void bmff_walk(proven_mem_view_t b_view, size_t from, size_t to, int depth, bmff_facts_t *f) {
    const uint8_t *b = (const uint8_t *)b_view.ptr;
    size_t size = (size_t)b_view.size;
    const uint8_t *kind;
    size_t body, len;
    for (size_t at = from; at < to && bmff_box(b, to, at, &kind, &body, &len); at = body + len) {
        const uint8_t *p = b + body;
        (void)size;
        if (!proven_sys_mem_cmp(kind, "meta", 4) && len >= 4 && depth < 6) bmff_walk(rubraview_mem_view(b, size), body + 4, body + len, depth + 1, f);
        else if ((!proven_sys_mem_cmp(kind, "iprp", 4) || !proven_sys_mem_cmp(kind, "ipco", 4) || !proven_sys_mem_cmp(kind, "moov", 4)) && depth < 6) {
            if (!proven_sys_mem_cmp(kind, "moov", 4)) f->sequence = true;
            bmff_walk(rubraview_mem_view(b, size), body, body + len, depth + 1, f);
        }
        else if (!proven_sys_mem_cmp(kind, "trak", 4)) f->tracks++;
        else if (!proven_sys_mem_cmp(kind, "iinf", 4) && len >= 6) f->items = p[0] == 0 ? be16(p + 4) : len >= 8 ? be32(p + 4) : 0;
        else if (!proven_sys_mem_cmp(kind, "ispe", 4) && len >= 12) {
            uint32_t w = be32(p + 4), h = be32(p + 8);
            if ((uint64_t)w * h > (uint64_t)f->width * f->height) { f->width = w; f->height = h; }
        }
        else if (!proven_sys_mem_cmp(kind, "pixi", 4) && len >= 6 && !f->channels) { f->channels = p[4]; f->bits = p[5]; }
        else if (!proven_sys_mem_cmp(kind, "av1C", 4) && len >= 4 && !f->av1) {
            f->av1 = true;
            f->av1_profile = p[1] >> 5; f->av1_level = p[1] & 31;
            f->av1_bits = (p[2] & 0x40) ? ((p[2] & 0x20) ? 12 : 10) : 8;
            f->av1_mono = (p[2] >> 4) & 1; f->av1_sub_x = (p[2] >> 3) & 1; f->av1_sub_y = (p[2] >> 2) & 1;
        }
        else if (!proven_sys_mem_cmp(kind, "hvcC", 4) && len >= 19 && !f->hevc) {
            f->hevc = true;
            f->hevc_profile = p[1] & 31; f->hevc_level = p[12]; f->hevc_chroma = p[16] & 3; f->hevc_bits = 8u + (p[17] & 7u);
        }
        else if (!proven_sys_mem_cmp(kind, "colr", 4) && len >= 4) {
            if (!proven_sys_mem_cmp(p, "nclx", 4) && len >= 11) {
                f->nclx = true; f->primaries = be16(p + 4); f->transfer = be16(p + 6); f->matrix = be16(p + 8); f->full_range = p[10] >> 7;
            } else if (!proven_sys_mem_cmp(p, "prof", 4) || !proven_sys_mem_cmp(p, "rICC", 4)) f->icc = true;
        }
        else if (!proven_sys_mem_cmp(kind, "auxC", 4) && len > 8) {
            for (size_t i = 4; i + 5 <= len; ++i) if (!proven_sys_mem_cmp(p + i, "alpha", 5)) f->alpha = true;
        }
        else if (!proven_sys_mem_cmp(kind, "irot", 4) && len >= 1) f->turns = p[0] & 3;
        else if (!proven_sys_mem_cmp(kind, "imir", 4)) f->mirrored = true;
    }
}

static const char *cicp_primaries(uint32_t v) {
    return v == 1 ? T("BT.709 / sRGB") : v == 9 ? T("BT.2020") : v == 12 ? T("Display P3") : v == 11 ? T("DCI-P3") : v == 5 || v == 6 ? T("BT.601") : NULL;
}

static const char *cicp_transfer(uint32_t v) {
    return v == 13 ? T("sRGB") : v == 1 || v == 6 || v == 14 || v == 15 ? T("BT.709") : v == 16 ? T("PQ (HDR)") : v == 18 ? T("HLG (HDR)") : v == 8 ? T("linear") : NULL;
}

static bool bmff_details(proven_mem_view_t b_view, rubraview_details_t *out, uint32_t *raw_bits) {
    const uint8_t *b = (const uint8_t *)b_view.ptr;
    size_t size = (size_t)b_view.size;
    const uint8_t *kind;
    size_t body, len;
    if (!bmff_box(b, size, 0, &kind, &body, &len) || proven_sys_mem_cmp(kind, "ftyp", 4) != 0 || len < 8) return false;
    char major[5] = "", also[72] = "";
    plain(major, sizeof(major), b + body, 4);
    bool avif = false, heif = false, canon = !proven_sys_mem_cmp(b + body, "crx ", 4);
    size_t used = 0;
    for (size_t at = body; at + 4 <= body + len; at += at == body ? 8 : 4) {
        const uint8_t *brand = b + at;
        if (!proven_sys_mem_cmp(brand, "avif", 4) || !proven_sys_mem_cmp(brand, "avis", 4)) avif = true;
        if (!proven_sys_mem_cmp(brand, "heic", 4) || !proven_sys_mem_cmp(brand, "heix", 4) || !proven_sys_mem_cmp(brand, "hevc", 4) || !proven_sys_mem_cmp(brand, "heim", 4) ||
            !proven_sys_mem_cmp(brand, "heis", 4) || !proven_sys_mem_cmp(brand, "mif1", 4) || !proven_sys_mem_cmp(brand, "msf1", 4)) heif = true;
        if (at != body && used + 6 < sizeof(also)) {
            char one[5];
            plain(one, sizeof(one), brand, 4);
            used += (size_t)rubraview_fmt(also + used, sizeof(also) - used, "{}{}", PROVEN_ARG(used ? " " : ""), PROVEN_ARG(one));
        }
    }
    if (!avif && !heif && !canon) return false;

    bmff_facts_t f = {0};
    bmff_walk(rubraview_mem_view(b, size), 0, size, 0, &f);
    rubraview_details_heading(out, "{}", PROVEN_ARG(avif ? T("AVIF") : canon ? T("Canon CR3") : T("HEIF")));
    rubraview_details_add(out, T("Brand"), "{}", PROVEN_ARG(major));
    rubraview_details_add(out, T("Also reads as"), "{}", PROVEN_ARG(also));
    if (f.items) rubraview_details_add(out, T("Items"), T("{} (pictures, their parts and what is said of them)"), PROVEN_ARG((unsigned)f.items));
    if (f.sequence) rubraview_details_add(out, T("Sequence"), T("yes, {} track{}"), PROVEN_ARG((unsigned)f.tracks), PROVEN_ARG(f.tracks == 1 ? "" : TC("plural", "s")));
    if (f.width && f.height) rubraview_details_add(out, T("Largest picture"), "{} x {}", PROVEN_ARG((unsigned)f.width), PROVEN_ARG((unsigned)f.height));
    if (f.av1) {
        const char *const PROFILE[3] = { T("Main"), T("High"), T("Professional") };
        const char *chroma = f.av1_mono ? T("grey") : f.av1_sub_x && f.av1_sub_y ? "4:2:0" : f.av1_sub_x ? "4:2:2" : "4:4:4";
        rubraview_details_add(out, T("Codec"), T("AV1, {} profile, level {}.{}"), PROVEN_ARG(f.av1_profile < 3 ? PROFILE[f.av1_profile] : T("unknown")),
                              PROVEN_ARG((unsigned)(2 + (f.av1_level >> 2))), PROVEN_ARG((unsigned)(f.av1_level & 3)));
        rubraview_details_add(out, T("Coded as"), T("{} bits a sample, {}"), PROVEN_ARG((unsigned)f.av1_bits), PROVEN_ARG(chroma));
    } else if (f.hevc) {
        char profile[64];
        const char *const CHROMA[4] = { T("grey"), "4:2:0", "4:2:2", "4:4:4" };
        rubraview_detail_hevc_profile(profile, sizeof(profile), f.hevc_profile, f.hevc_level);
        rubraview_details_add(out, T("Codec"), T("HEVC, {}"), PROVEN_ARG(profile));
        rubraview_details_add(out, T("Coded as"), T("{} bits a sample, {}"), PROVEN_ARG((unsigned)f.hevc_bits), PROVEN_ARG(CHROMA[f.hevc_chroma]));
    }
    if (f.channels) rubraview_details_add(out, T("Pixels"), T("{} channel{} of {} bits"), PROVEN_ARG((unsigned)f.channels), PROVEN_ARG(f.channels == 1 ? "" : TC("plural", "s")), PROVEN_ARG((unsigned)f.bits));
    rubraview_details_add(out, T("Transparency"), "{}", PROVEN_ARG(f.alpha ? T("yes, a picture of its own") : ""));
    if (f.nclx) {
        const char *primaries = cicp_primaries(f.primaries), *transfer = cicp_transfer(f.transfer);
        char a[24], c[24];
        rubraview_fmt(a, sizeof(a), T("primaries {}"), PROVEN_ARG((unsigned)f.primaries));
        rubraview_fmt(c, sizeof(c), T("transfer {}"), PROVEN_ARG((unsigned)f.transfer));
        rubraview_details_add(out, T("Colour"), T("{}, {}, matrix {}, {} range"), PROVEN_ARG(primaries ? primaries : a), PROVEN_ARG(transfer ? transfer : c),
                              PROVEN_ARG((unsigned)f.matrix), PROVEN_ARG(f.full_range ? T("full") : T("limited")));
    }
    if (f.icc) rubraview_details_add(out, T("Colour profile"), T("ICC, in the file"));
    if (f.turns || f.mirrored) {
        rubraview_details_add(out, T("Shown"), "{}{}{}", PROVEN_ARG(f.turns == 1 ? T("turned a quarter to the left") : f.turns == 2 ? T("upside down")
                              : f.turns == 3 ? T("turned a quarter to the right") : ""), PROVEN_ARG(f.turns && f.mirrored ? ", " : ""), PROVEN_ARG(f.mirrored ? T("mirrored") : ""));
    }
    uint32_t depth = f.av1 ? f.av1_bits : f.hevc ? f.hevc_bits : f.bits;
    uint32_t channels = f.channels ? f.channels : (f.av1 && f.av1_mono) ? 1u : 3u;
    if (raw_bits && depth && depth <= 16) *raw_bits = depth * (channels + (f.alpha && channels < 4 ? 1u : 0u));
    return true;
}

static bool jxl_details(proven_mem_view_t b_view, rubraview_details_t *out, uint32_t *raw_bits);

bool rubraview_image_details(proven_mem_view_t bytes_view, rubraview_details_t *out, uint32_t *out_raw_bits) {
    const uint8_t *bytes = (const uint8_t *)bytes_view.ptr;
    size_t size = (size_t)bytes_view.size;
    if (out_raw_bits) *out_raw_bits = 0;
    if (!bytes || !out || size < 12) return false;
    if (bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) return jpeg_details(rubraview_mem_view(bytes, size), out, out_raw_bits);
    if (proven_sys_mem_cmp(bytes, "\x89PNG\r\n\x1A\n", 8) == 0) return png_details(rubraview_mem_view(bytes, size), out, out_raw_bits);
    if (proven_sys_mem_cmp(bytes, "GIF87a", 6) == 0 || proven_sys_mem_cmp(bytes, "GIF89a", 6) == 0) return gif_details(rubraview_mem_view(bytes, size), out, out_raw_bits);
    if (proven_sys_mem_cmp(bytes, "RIFF", 4) == 0 && proven_sys_mem_cmp(bytes + 8, "WEBP", 4) == 0) return webp_details(rubraview_mem_view(bytes, size), out, out_raw_bits);
    if (bytes[0] == 'B' && bytes[1] == 'M') return bmp_details(rubraview_mem_view(bytes, size), out, out_raw_bits);
    if ((bytes[0] == 'I' && bytes[1] == 'I') || (bytes[0] == 'M' && bytes[1] == 'M')) return tiff_details(rubraview_mem_view(bytes, size), out, out_raw_bits);
    if (rubraview_image_is_jxl(rubraview_mem_view(bytes, size))) return jxl_details(rubraview_mem_view(bytes, size), out, out_raw_bits);
    if (proven_sys_mem_cmp(bytes + 4, "ftyp", 4) == 0) return bmff_details(rubraview_mem_view(bytes, size), out, out_raw_bits);
    return false;
}

bool rubraview_image_is_jxl(proven_mem_view_t b_view) {
    const uint8_t *b = (const uint8_t *)b_view.ptr;
    size_t size = (size_t)b_view.size;
    if (!b || size < 2) return false;
    if (b[0] == 0xFF && b[1] == 0x0A) return true;
    return size >= 12 && proven_sys_mem_cmp(b, "\0\0\0\x0CJXL \r\n\x87\n", 12) == 0;
}

/* RV-110: a JPEG XL says in its first few bytes whether it moves. The
   codestream's header is bits, lowest first: the picture's size, then
   the metadata, where "have_animation" follows the orientation, the
   intrinsic size and the preview's size when those are there. */
typedef struct jxl_bits { const uint8_t *b; size_t size; size_t at; bool short_of; } jxl_bits_t;

static uint32_t jxl_u(jxl_bits_t *r, uint32_t count) {
    uint32_t v = 0;
    for (uint32_t i = 0; i < count; ++i, ++r->at) {
        if (r->at / 8 >= r->size) { r->short_of = true; return 0; }
        v |= (uint32_t)((r->b[r->at / 8] >> (r->at % 8)) & 1u) << i;
    }
    return v;
}

/* One of four widths, chosen by two bits; the value itself is not wanted. */
static void jxl_skip_u32(jxl_bits_t *r, uint32_t b0, uint32_t b1, uint32_t b2, uint32_t b3) {
    uint32_t which = jxl_u(r, 2);
    (void)jxl_u(r, which == 0 ? b0 : which == 1 ? b1 : which == 2 ? b2 : b3);
}

static void jxl_skip_size(jxl_bits_t *r) {
    bool small = jxl_u(r, 1) != 0;
    if (small) (void)jxl_u(r, 5); else jxl_skip_u32(r, 9, 13, 18, 30);
    if (jxl_u(r, 3) == 0) {                          /* no named ratio: the width is written too */
        if (small) (void)jxl_u(r, 5); else jxl_skip_u32(r, 9, 13, 18, 30);
    }
}

static void jxl_skip_preview(jxl_bits_t *r) {
    bool div8 = jxl_u(r, 1) != 0;
    if (div8) jxl_skip_u32(r, 0, 0, 5, 9); else jxl_skip_u32(r, 6, 8, 10, 12);
    if (jxl_u(r, 3) == 0) {
        if (div8) jxl_skip_u32(r, 0, 0, 5, 9); else jxl_skip_u32(r, 6, 8, 10, 12);
    }
}

/* D-120: what a JPEG XL says before its pixels. A value of one of four
   kinds, chosen by two bits: a fixed number, or a base and some bits. */
static uint32_t jxl_u32(jxl_bits_t *r, const uint32_t base[4], const uint32_t bits[4]) {
    uint32_t which = jxl_u(r, 2);
    return base[which] + (bits[which] ? jxl_u(r, bits[which]) : 0);
}

static void jxl_size(jxl_bits_t *r, uint32_t *width, uint32_t *height) {
    static const uint32_t ONE[4] = { 1, 1, 1, 1 }, WIDE[4] = { 9, 13, 18, 30 };
    static const uint32_t NUM[8] = { 0, 1, 12, 4, 3, 16, 5, 2 }, DEN[8] = { 1, 1, 10, 3, 2, 9, 4, 1 };
    bool small = jxl_u(r, 1) != 0;
    *height = small ? (jxl_u(r, 5) + 1) * 8 : jxl_u32(r, ONE, WIDE);
    uint32_t ratio = jxl_u(r, 3);
    if (ratio == 0) *width = small ? (jxl_u(r, 5) + 1) * 8 : jxl_u32(r, ONE, WIDE);
    else *width = (uint32_t)((uint64_t)*height * NUM[ratio] / DEN[ratio]);
}

static void jxl_codestream_details(proven_mem_view_t b_view, rubraview_details_t *out, uint32_t *raw_bits) {
    const uint8_t *b = (const uint8_t *)b_view.ptr;
    size_t size = (size_t)b_view.size;
    if (size < 3 || b[0] != 0xFF || b[1] != 0x0A) return;
    jxl_bits_t r = { .b = b, .size = size < 96 ? size : 96, .at = 16 };
    uint32_t width = 0, height = 0, bits = 8, extra = 0;
    bool floating = false;
    jxl_size(&r, &width, &height);
    if (r.short_of) return;
    rubraview_details_add(out, T("Picture"), "{} x {}", PROVEN_ARG((unsigned)width), PROVEN_ARG((unsigned)height));
    if (!jxl_u(&r, 1)) {                              /* not all as it comes */
        if (jxl_u(&r, 1)) {                           /* extra fields */
            uint32_t orientation = jxl_u(&r, 3) + 1;
            if (jxl_u(&r, 1)) { uint32_t w, h; jxl_size(&r, &w, &h); }
            if (jxl_u(&r, 1)) jxl_skip_preview(&r);
            if (jxl_u(&r, 1)) {
                static const uint32_t TB[4] = { 100, 1000, 1, 1 }, TN[4] = { 0, 0, 10, 30 };
                static const uint32_t DB[4] = { 1, 1001, 1, 1 }, DN[4] = { 0, 0, 8, 10 };
                static const uint32_t LB[4] = { 0, 0, 0, 0 }, LN[4] = { 0, 3, 16, 32 };
                uint32_t ticks = jxl_u32(&r, TB, TN), per = jxl_u32(&r, DB, DN), loops = jxl_u32(&r, LB, LN);
                (void)jxl_u(&r, 1);                   /* time codes */
                if (!r.short_of && per) {
                    if (loops) rubraview_details_add(out, T("Animation"), T("{} ticks a second, played {} time{}"), PROVEN_ARG(rubraview_g_text((char[32]){0}, (double)((double)ticks / per), 4)), PROVEN_ARG((unsigned)loops), PROVEN_ARG(loops == 1 ? "" : TC("plural", "s")));
                    else rubraview_details_add(out, T("Animation"), T("{} ticks a second, played without end"), PROVEN_ARG(rubraview_g_text((char[32]){0}, (double)((double)ticks / per), 4)));
                }
            }
            if (!r.short_of && orientation > 1) rubraview_details_add(out, T("Orientation"), T("{} (as EXIF counts)"), PROVEN_ARG((unsigned)orientation));
        }
        static const uint32_t INT_BASE[4] = { 8, 10, 12, 1 }, INT_BITS[4] = { 0, 0, 0, 6 };
        static const uint32_t FB[4] = { 32, 16, 24, 1 }, FN[4] = { 0, 0, 0, 6 };
        static const uint32_t EB[4] = { 0, 1, 2, 1 }, EN[4] = { 0, 0, 4, 12 };
        floating = jxl_u(&r, 1) != 0;
        bits = floating ? jxl_u32(&r, FB, FN) : jxl_u32(&r, INT_BASE, INT_BITS);
        if (floating) (void)jxl_u(&r, 4);
        (void)jxl_u(&r, 1);                           /* 16-bit buffers are enough */
        extra = jxl_u32(&r, EB, EN);
        if (r.short_of) return;
    }
    rubraview_details_add(out, T("Samples"), T("{} bits{}"), PROVEN_ARG((unsigned)bits), PROVEN_ARG(floating ? T(", floating-point") : ""));
    if (extra) rubraview_details_add(out, T("Extra channels"), T("{} (transparency or more)"), PROVEN_ARG((unsigned)extra));
    if (raw_bits && bits <= 64) *raw_bits = bits * (3 + (extra ? 1u : 0u));
}

static bool jxl_details(proven_mem_view_t b_view, rubraview_details_t *out, uint32_t *raw_bits) {
    const uint8_t *b = (const uint8_t *)b_view.ptr;
    size_t size = (size_t)b_view.size;
    rubraview_details_heading(out, T("JPEG XL"));
    if (b[0] == 0xFF) {
        rubraview_details_add(out, T("Kind"), T("a bare codestream"));
        jxl_codestream_details(rubraview_mem_view(b, size), out, raw_bits);
        return true;
    }
    rubraview_details_add(out, T("Kind"), T("in boxes (ISO container)"));
    uint32_t parts = 0, level = 0;
    bool repacked = false, exif = false, xml = false, squeezed = false, provenance = false, seen = false;
    const uint8_t *kind;
    size_t body, len;
    for (size_t at = 12; bmff_box(b, size, at, &kind, &body, &len); at = body + len) {
        if (!proven_sys_mem_cmp(kind, "jxll", 4) && len >= 1) level = b[body];
        else if (!proven_sys_mem_cmp(kind, "jbrd", 4)) repacked = true;
        else if (!proven_sys_mem_cmp(kind, "Exif", 4)) exif = true;
        else if (!proven_sys_mem_cmp(kind, "xml ", 4)) xml = true;
        else if (!proven_sys_mem_cmp(kind, "brob", 4)) squeezed = true;
        else if (!proven_sys_mem_cmp(kind, "jumb", 4)) provenance = true;
        else if (!proven_sys_mem_cmp(kind, "jxlc", 4)) {
            if (!seen) jxl_codestream_details(rubraview_mem_view(b + body, len), out, raw_bits);
            seen = true;
        } else if (!proven_sys_mem_cmp(kind, "jxlp", 4)) {
            if (!seen && len > 4) jxl_codestream_details(rubraview_mem_view(b + body + 4, len - 4), out, raw_bits);
            seen = true;
            ++parts;
        }
    }
    if (level) rubraview_details_add(out, T("Level"), "{}", PROVEN_ARG((unsigned)level));
    if (parts > 1) rubraview_details_add(out, T("Codestream"), T("in {} parts"), PROVEN_ARG((unsigned)parts));
    rubraview_details_add(out, T("From a JPEG"), "{}", PROVEN_ARG(repacked ? T("yes: the JPEG can be had back, bit for bit") : ""));
    if (exif || xml || squeezed || provenance) {
        rubraview_details_add(out, T("Carries"), "{}{}{}{}", PROVEN_ARG(exif ? T("EXIF ") : ""), PROVEN_ARG(xml ? T("XMP ") : ""), PROVEN_ARG(squeezed ? T("compressed-metadata ") : ""), PROVEN_ARG(provenance ? T("provenance (JUMBF) ") : ""));
    }
    return true;
}

static bool jxl_codestream_moves(proven_mem_view_t b_view) {
    const uint8_t *b = (const uint8_t *)b_view.ptr;
    size_t size = (size_t)b_view.size;
    if (size < 3 || b[0] != 0xFF || b[1] != 0x0A) return false;
    jxl_bits_t r = { .b = b, .size = size < 64 ? size : 64, .at = 16 };
    jxl_skip_size(&r);
    if (jxl_u(&r, 1)) return false;                  /* all as it comes: a still picture */
    if (!jxl_u(&r, 1)) return false;                 /* no extra fields */
    (void)jxl_u(&r, 3);                              /* orientation */
    if (jxl_u(&r, 1)) jxl_skip_size(&r);
    if (jxl_u(&r, 1)) jxl_skip_preview(&r);
    bool moves = jxl_u(&r, 1) != 0;
    return moves && !r.short_of;
}

static bool jxl_moves(proven_mem_view_t b_view) {
    const uint8_t *b = (const uint8_t *)b_view.ptr;
    size_t size = (size_t)b_view.size;
    if (b[0] == 0xFF) return jxl_codestream_moves(rubraview_mem_view(b, size));
    /* In boxes: the codestream is the `jxlc` box, or begins in the first `jxlp` (after its four-byte number). */
    for (size_t at = 12; at + 8 <= size; ) {
        uint64_t len = be32(b + at);
        size_t head = 8;
        if (len == 1) {
            if (at + 16 > size) return false;
            len = ((uint64_t)be32(b + at + 8) << 32) | be32(b + at + 12);
            head = 16;
        } else if (len == 0) {
            len = size - at;
        }
        if (len < head) return false;
        if (proven_sys_mem_cmp(b + at + 4, "jxlc", 4) == 0) return jxl_codestream_moves(rubraview_mem_view(b + at + head, size - at - head));
        if (proven_sys_mem_cmp(b + at + 4, "jxlp", 4) == 0) {
            return size - at - head > 4 && jxl_codestream_moves(rubraview_mem_view(b + at + head + 4, size - at - head - 4));
        }
        if (len > size - at) return false;
        at += (size_t)len;
    }
    return false;
}

bool rubraview_image_is_animated(proven_mem_view_t b_view) {
    const uint8_t *b = (const uint8_t *)b_view.ptr;
    size_t size = (size_t)b_view.size;
    if (!b || size < 16) return false;
    if (rubraview_image_is_jxl(rubraview_mem_view(b, size))) return jxl_moves(rubraview_mem_view(b, size));
    if (proven_sys_mem_cmp(b, "\x89PNG\r\n\x1A\n", 8) == 0) {
        for (size_t at = 8; at + 12 <= size; ) {
            uint32_t len = be32(b + at);
            if (proven_sys_mem_cmp(b + at + 4, "acTL", 4) == 0) return len >= 8 && at + 16 <= size && be32(b + at + 8) > 1;
            if (proven_sys_mem_cmp(b + at + 4, "IDAT", 4) == 0 || proven_sys_mem_cmp(b + at + 4, "IEND", 4) == 0) return false;
            if ((uint64_t)at + 12 + len > size) return false;
            at += 12 + (size_t)len;
        }
        return false;
    }
    if (proven_sys_mem_cmp(b, "RIFF", 4) == 0 && proven_sys_mem_cmp(b + 8, "WEBP", 4) == 0) {
        return size >= 21 && proven_sys_mem_cmp(b + 12, "VP8X", 4) == 0 && (b[20] & 0x02) != 0;
    }
    /* RV-110: an AVIF sequence says so in its brands (`avis`). */
    if (proven_sys_mem_cmp(b + 4, "ftyp", 4) == 0) {
        size_t box = be32(b);
        if (box < 16 || box > size) box = size < 4096 ? size : 4096;
        for (size_t at = 8; at + 4 <= box; at += at == 8 ? 8 : 4) {
            if (proven_sys_mem_cmp(b + at, "avis", 4) == 0) return true;
        }
        return false;
    }
    if (proven_sys_mem_cmp(b, "GIF87a", 6) == 0 || proven_sys_mem_cmp(b, "GIF89a", 6) == 0) {
        size_t at = 13 + ((b[10] & 0x80) ? 3u * (2u << (b[10] & 7)) : 0u);
        uint32_t frames = 0;
        while (at < size) {
            uint8_t block = b[at++];
            if (block == 0x2C) {
                if (at + 9 > size) break;
                if (++frames > 1) return true;
                uint8_t f = b[at + 8];
                at += 9 + ((f & 0x80) ? 3u * (2u << (f & 7)) : 0u) + 1;
            } else if (block == 0x21) {
                at++;
            } else {
                break;
            }
            while (at < size && b[at]) at += 1 + (size_t)b[at];
            at++;
        }
        return false;
    }
    return false;
}
