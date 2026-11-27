#ifndef RUBRAVIEW_ADPCM_H
#define RUBRAVIEW_ADPCM_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RV-319 (R366): our own decoders for the two ADPCM kinds voice recorders write into WAV files, which Media
 * Foundation has no decoder for (R348): IMA ADPCM (format tag 0x0011, "IMA/DVI") and Microsoft ADPCM (0x0002).
 * Both code each block on its own, so a block decodes alone and any point of the sound is reached by its block.
 * A WAVE (RIFF, RF64 or Wave64) holding one is seen by Media Foundation as a 16-bit PCM WAVE made of what its
 * blocks decode to (rubraview_adpcm_wave): its head here, the samples made block by block as they are read.
 */

#define RUBRAVIEW_ADPCM_IMA 0x0011u
#define RUBRAVIEW_ADPCM_MS  0x0002u
#define RUBRAVIEW_ADPCM_MAX_CHANNELS 8
#define RUBRAVIEW_ADPCM_MAX_COEFS 32

typedef struct rubraview_adpcm {
    uint16_t tag, channels, block_align;
    uint32_t rate;
    uint32_t frames_per_block;                          /* samples of each channel one whole block holds */
    uint16_t coef_count;                                /* Microsoft ADPCM: its predictors, the file's (7 standard) */
    int16_t  coef[RUBRAVIEW_ADPCM_MAX_COEFS][2];
} rubraview_adpcm_t;

/* From a WAVE's "fmt " body: true when it is IMA or Microsoft ADPCM of 4 bits a sample that can be decoded. */
bool rubraview_adpcm_from_fmt(const uint8_t *fmt, size_t len, rubraview_adpcm_t *out);

/* Frames a block of `len` bytes holds (a last block may be short); 0 when it cannot hold its header. */
uint32_t rubraview_adpcm_block_frames(const rubraview_adpcm_t *a, size_t len);

/* One block decoded into `out` (interleaved 16-bit, room for rubraview_adpcm_block_frames(a, len) * channels);
   returns the frames written. */
uint32_t rubraview_adpcm_decode_block(const rubraview_adpcm_t *a, const uint8_t *block, size_t len, int16_t *out);

#define RUBRAVIEW_ADPCM_HEADER_MAX 96
typedef struct rubraview_adpcm_wave {
    rubraview_adpcm_t a;
    uint8_t  header[RUBRAVIEW_ADPCM_HEADER_MAX];         /* a RIFF WAVE head of 16-bit PCM (RF64 past 4 GB) */
    uint32_t header_len;
    uint64_t data_at, data_len;                          /* the ADPCM blocks in the file */
    uint64_t frames;                                     /* the PCM the head promises: the "fact" count when given */
} rubraview_adpcm_wave_t;

/* From a file's first bytes (its fmt and the data chunk's header there) and its size: true when it is a WAVE, an
   RF64 or a Wave64 of IMA or Microsoft ADPCM. */
bool rubraview_adpcm_wave(const uint8_t *head, size_t head_len, uint64_t file_size, rubraview_adpcm_wave_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_ADPCM_H */
