/* rar15.h - minimal header for clean-room rar15_cipher.c (fultaarc-cleanroom-rar15-enc3). MIT. */
#ifndef FULTA_RAR15_ENC_RAR15_H
#define FULTA_RAR15_ENC_RAR15_H

#include <stddef.h>
#include <stdint.h>

#define RAR15_OK         0
#define RAR15_ERR_PARAM  (-1)
#define RAR15_ERR_BUFFER (-6)

int rar15_decrypt_block(uint8_t *data, size_t data_len, const uint8_t *ks, size_t ks_len);
size_t rar15_keystream(const char *password, uint8_t *out, size_t max_out);

#endif
