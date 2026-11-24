/* rar15_cipher.c - RAR 1.5 stream cipher: block XOR + keystream generator.
 *
 * Implements rar15_decrypt_block and rar15_keystream in standalone clean-room C99 from
 * mathematics alone using the IEEE 802.3 CRC-32 polynomial (0xEDB88320, generated from its
 * definition on the stack for full reentrancy with zero global state or external libc calls)
 * and the Cycle 323/324 verified key-schedule and 5-word (<=54-bit initial) 16-bit recurrence --
 * zero runtime oracle calls, zero stored keystreams, zero external/third-party archiver source.
 *
 * Provenance (Amendment 2026-11-16, Cycles 323-324):
 *   APPROVED-PRIOR : RECALL-208 (small 16-bit word state hypothesis class, non-physical),
 *                    RECALL-209 (IEEE 802.3 CRC-32 polynomial 0xEDB88320 candidate structure).
 *   DATA-DERIVED   : 77-byte prefix truncation, unsigned byte promotion, R/H/S accumulators,
 *                    5x16-bit live loop state (w, m, k2, hr, s_state), +0x1234 step,
 *                    ror16(_,2)/ror16(_,1), K2^=M, W>>8; unique up to Cycle 324 Z_2^3/Z_2^4
 *                    black-box gauge equivalence.
 *
 * SPDX-License-Identifier: MIT
 */
#include "rar15.h"

static void build_crc32_table(uint32_t tbl[256]) {
    for (uint32_t i = 0; i < 256u; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        tbl[i] = c;
    }
}

static uint16_t rol16(uint16_t x, unsigned r) {
    r &= 15u;
    if (!r) return x;
    return (uint16_t)((((uint32_t)x << r) | ((uint32_t)x >> (16u - r))) & 0xFFFFu);
}

static uint16_t ror16(uint16_t x, unsigned r) {
    r &= 15u;
    if (!r) return x;
    return (uint16_t)((((uint32_t)x >> r) | ((uint32_t)x << (16u - r))) & 0xFFFFu);
}

int rar15_decrypt_block(uint8_t *data, size_t data_len, const uint8_t *ks, size_t ks_len) {
    if (!data || !ks) return RAR15_ERR_PARAM;
    if (ks_len < data_len) return RAR15_ERR_BUFFER;
    for (size_t i = 0; i < data_len; i++)
        data[i] ^= ks[i];
    return RAR15_OK;
}

size_t rar15_keystream(const char *password, uint8_t *out, size_t max_out) {
    if (!password || !out || max_out == 0)
        return 0;

    uint32_t crc_tbl[256];
    build_crc32_table(crc_tbl);

    size_t plen = 0;
    while (plen < 77u && password[plen] != '\0')
        plen++;

    const uint8_t *pw = (const uint8_t *)password;
    uint32_t r_state = 0xFFFFFFFFu;
    uint16_t h_state = 0u;
    uint16_t s_state = 0xFFFFu;
    for (size_t i = 0; i < plen; i++) {
        uint8_t c = pw[i];
        r_state = (r_state >> 8) ^ crc_tbl[(r_state ^ c) & 0xFFu];
        h_state = (uint16_t)(h_state + (uint16_t)(crc_tbl[c] >> 16) + (uint16_t)c);
        s_state ^= (uint16_t)((crc_tbl[c] & 0xFFFFu) ^ (uint16_t)c);
    }

    uint16_t w = (uint16_t)(r_state & 0xFFFFu);
    uint16_t m = rol16((uint16_t)((r_state >> 16) & 0xFFFFu), 1u);
    uint16_t k2 = 0xFFFFu;
    uint16_t hr = h_state;

    for (size_t i = 0; i < max_out; i++) {
        w = (uint16_t)(w + 0x1234u);
        uint32_t t = crc_tbl[(w >> 1) & 0xFFu];
        s_state = (uint16_t)(s_state + (uint16_t)(t >> 16));
        m = (uint16_t)(ror16(m, 2u) ^ ror16((uint16_t)(t & 0xFFFFu), 1u));
        k2 ^= m;
        hr = ror16(hr, 2u);
        w ^= (uint16_t)(s_state ^ hr ^ k2);
        out[i] = (uint8_t)(w >> 8);
    }
    return max_out;
}
