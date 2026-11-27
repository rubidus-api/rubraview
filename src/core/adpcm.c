#include "rubraview/adpcm.h"
#include "rubraview/media_head.h"

/* RV-319 (R366): IMA ADPCM as Microsoft's WAVE files carry it (the IMA's step tables, a 4-byte header a channel
   a block, channels interleaved four bytes at a time, the low nibble first) and Microsoft ADPCM (its seven
   predictors or the file's, a 7-byte header a channel, the high nibble first, channels alternating nibbles).
   The sums are the ones FFmpeg's decoders make, so the samples are the same to the bit. */

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static int16_t sle16(const uint8_t *p) { return (int16_t)le16(p); }
static uint32_t le32(const uint8_t *p) { return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static int16_t clip16(int32_t v) { return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }

static const int16_t IMA_STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
    118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767,
};
static const int8_t IMA_INDEX[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

static const int16_t MS_ADAPT[16] = { 230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230 };
static const int16_t MS_COEF[7][2] = { { 256, 0 }, { 512, -256 }, { 0, 0 }, { 192, 64 }, { 240, 0 }, { 460, -208 }, { 392, -232 } };

bool rubraview_adpcm_from_fmt(const uint8_t *fmt, size_t len, rubraview_adpcm_t *out) {
    if (!fmt || !out || len < 16) return false;
    proven_sys_mem_set(out, 0, sizeof *out);
    out->tag = le16(fmt);
    out->channels = le16(fmt + 2);
    out->rate = le32(fmt + 4);
    out->block_align = le16(fmt + 12);
    uint16_t bits = le16(fmt + 14);
    uint16_t ch = out->channels;
    if (ch == 0 || ch > RUBRAVIEW_ADPCM_MAX_CHANNELS || out->rate == 0 || bits != 4) return false;
    if (out->tag == RUBRAVIEW_ADPCM_IMA) {
        /* whole groups of four bytes a channel after the headers */
        if (out->block_align < 4u * ch + 4u * ch || (out->block_align - 4u * ch) % (4u * ch) != 0) return false;
        out->frames_per_block = (uint32_t)(out->block_align - 4u * ch) * 2u / ch + 1u;
        return true;
    }
    if (out->tag == RUBRAVIEW_ADPCM_MS) {
        if (ch > 2 || out->block_align < 7u * ch) return false;   /* nibbles alternate between at most two channels */
        out->frames_per_block = (uint32_t)(out->block_align - 7u * ch) * 2u / ch + 2u;
        out->coef_count = 7;
        proven_sys_mem_copy(out->coef, MS_COEF, sizeof MS_COEF);
        if (len >= 22) {   /* cbSize, samples a block, the predictors' count and pairs */
            uint16_t n = le16(fmt + 20);
            if (n >= 1 && n <= RUBRAVIEW_ADPCM_MAX_COEFS && len >= 22u + 4u * n) {
                out->coef_count = n;
                for (uint16_t i = 0; i < n; ++i) { out->coef[i][0] = sle16(fmt + 22 + 4 * i); out->coef[i][1] = sle16(fmt + 24 + 4 * i); }
            }
        }
        return true;
    }
    return false;
}

uint32_t rubraview_adpcm_block_frames(const rubraview_adpcm_t *a, size_t len) {
    if (!a || a->channels == 0) return 0;
    size_t ch = a->channels;
    if (len > a->block_align) len = a->block_align;
    if (a->tag == RUBRAVIEW_ADPCM_IMA) {
        if (len < 4 * ch) return 0;
        size_t groups = (len - 4 * ch) / (4 * ch);   /* only whole groups of every channel */
        return (uint32_t)(groups * 8u + 1u);
    }
    if (a->tag == RUBRAVIEW_ADPCM_MS) {
        if (len < 7 * ch) return 0;
        return (uint32_t)((len - 7 * ch) * 2u / ch + 2u);
    }
    return 0;
}

static int16_t ima_nibble(int32_t *pred, int32_t *index, unsigned n) {
    int32_t step = IMA_STEP[*index];
    int32_t diff = ((2 * (int32_t)(n & 7u) + 1) * step) >> 3;
    int32_t p = (n & 8u) ? *pred - diff : *pred + diff;
    *pred = clip16(p);
    int32_t i = *index + IMA_INDEX[n & 15u];
    *index = i < 0 ? 0 : i > 88 ? 88 : i;
    return (int16_t)*pred;
}

uint32_t rubraview_adpcm_decode_block(const rubraview_adpcm_t *a, const uint8_t *block, size_t len, int16_t *out) {
    uint32_t frames = rubraview_adpcm_block_frames(a, len);
    if (!frames || !block || !out) return 0;
    size_t ch = a->channels;
    if (a->tag == RUBRAVIEW_ADPCM_IMA) {
        int32_t pred[RUBRAVIEW_ADPCM_MAX_CHANNELS], index[RUBRAVIEW_ADPCM_MAX_CHANNELS];
        for (size_t c = 0; c < ch; ++c) {
            pred[c] = sle16(block + 4 * c);
            index[c] = block[4 * c + 2] > 88 ? 88 : block[4 * c + 2];
            out[c] = (int16_t)pred[c];
        }
        const uint8_t *p = block + 4 * ch;
        uint32_t groups = (frames - 1u) / 8u;
        for (uint32_t g = 0; g < groups; ++g) {
            for (size_t c = 0; c < ch; ++c) {
                int16_t *o = out + (1u + 8u * g) * ch + c;
                for (int k = 0; k < 4; ++k, ++p) {
                    o[(size_t)(2 * k) * ch] = ima_nibble(&pred[c], &index[c], *p & 15u);
                    o[(size_t)(2 * k + 1) * ch] = ima_nibble(&pred[c], &index[c], *p >> 4);
                }
            }
        }
        return frames;
    }
    /* Microsoft ADPCM */
    int32_t delta[2], s1[2], s2[2], c1[2], c2[2];
    for (size_t c = 0; c < ch; ++c) {
        unsigned k = block[c];
        if (k >= a->coef_count) k = a->coef_count - 1u;
        c1[c] = a->coef[k][0];
        c2[c] = a->coef[k][1];
        delta[c] = sle16(block + ch + 2 * c);
        s1[c] = sle16(block + 3 * ch + 2 * c);
        s2[c] = sle16(block + 5 * ch + 2 * c);
        out[c] = (int16_t)s2[c];
        out[ch + c] = (int16_t)s1[c];
    }
    const uint8_t *p = block + 7 * ch;
    size_t total = (size_t)(frames - 2u) * ch;   /* nibbles after the header samples */
    for (size_t i = 0; i < total; ++i) {
        unsigned n = (i & 1u) ? (p[i >> 1] & 15u) : (p[i >> 1] >> 4);
        size_t c = ch == 2 ? (i & 1u) : 0;
        int32_t pred = (s1[c] * c1[c] + s2[c] * c2[c]) / 256;
        int32_t sn = (n & 8u) ? (int32_t)n - 16 : (int32_t)n;
        int32_t v = clip16(pred + sn * delta[c]);
        s2[c] = s1[c];
        s1[c] = v;
        int64_t d = ((int64_t)MS_ADAPT[n] * delta[c]) >> 8;
        delta[c] = d < 16 ? 16 : d > 0x7FFFFFFF / 768 ? 0x7FFFFFFF / 768 : (int32_t)d;
        out[2u * ch + i] = (int16_t)v;
    }
    return frames;
}

bool rubraview_adpcm_wave(const uint8_t *head, size_t head_len, uint64_t file_size, rubraview_adpcm_wave_t *out) {
    if (!out) return false;
    proven_sys_mem_set(out, 0, sizeof *out);
    rubraview_wave_chunks_t c;
    if (!rubraview_media_wave_chunks(head, head_len, file_size, &c)) return false;
    if (!rubraview_adpcm_from_fmt(c.fmt, c.fmt_len, &out->a)) return false;
    out->data_at = c.data_at;
    out->data_len = c.data_len;
    uint64_t blocks = c.data_len / out->a.block_align, rest = c.data_len % out->a.block_align;
    uint64_t frames = blocks * out->a.frames_per_block + rubraview_adpcm_block_frames(&out->a, (size_t)rest);
    if (c.fact && c.fact < frames) frames = c.fact;   /* the last block's padding is no sound */
    out->frames = frames;
    uint8_t pcm[16];
    uint16_t align = (uint16_t)(2u * out->a.channels);
    pcm[0] = 1; pcm[1] = 0;   /* PCM */
    pcm[2] = (uint8_t)out->a.channels; pcm[3] = (uint8_t)(out->a.channels >> 8);
    for (int i = 0; i < 4; ++i) pcm[4 + i] = (uint8_t)(out->a.rate >> (8 * i));
    uint32_t byte_rate = out->a.rate * align;
    for (int i = 0; i < 4; ++i) pcm[8 + i] = (uint8_t)(byte_rate >> (8 * i));
    pcm[12] = (uint8_t)align; pcm[13] = (uint8_t)(align >> 8);
    pcm[14] = 16; pcm[15] = 0;
    out->header_len = rubraview_media_wave_head(out->header, sizeof out->header, pcm, 16, frames * align, align);
    return out->header_len > 0 && frames > 0;
}
