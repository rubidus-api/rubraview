/* rar15_cipher.h - FultaArc internal declaration for clean-room RAR 1.5 stream cipher. MIT. */
#ifndef FULTA_RAR15_CIPHER_H
#define FULTA_RAR15_CIPHER_H

#include <stddef.h>
#include <stdint.h>

int rar15_decrypt_block(uint8_t *data, size_t data_len, const uint8_t *ks, size_t ks_len);
size_t rar15_keystream(const char *password, uint8_t *out, size_t max_out);

#endif
