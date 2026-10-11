#ifndef RUBRAVIEW_DETAIL_H
#define RUBRAVIEW_DETAIL_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What a file says about how it was made (owner, 2026-10-11: "파일의 코덱이나
 * 압축률, 각종 옵션이나 설정 등이 이미지 파일과 영상, 음성 파일 등에 대해
 * 모두 가능한 한 모든 정보를 다 보여주게"). A list of headings and
 * label / value lines of fixed size, so a media backend can fill it on
 * its own thread with no allocator, and the information window prints it
 * as it stands.
 *
 * A film's and a song's list is written by the backend that opened it,
 * from the container's and the codecs' own headers. A picture's is read
 * here, from the file's bytes.
 */

#define RUBRAVIEW_DETAIL_MAX 224

typedef struct rubraview_detail {
    bool heading;
    char label[24];
    char value[136];
} rubraview_detail_t;

typedef struct rubraview_details {
    rubraview_detail_t lines[RUBRAVIEW_DETAIL_MAX];
    size_t count;
} rubraview_details_t;

void rubraview_details_heading(rubraview_details_t *d, const char *format, ...);
/* A line whose value comes out empty is not added. */
void rubraview_details_add(rubraview_details_t *d, const char *label, const char *format, ...);

/* Formatters; each writes into `buf` and returns the length. */
size_t rubraview_detail_bitrate(char *buf, size_t cap, double bits_per_second);   /* "128 kbit/s", "12.5 Mbit/s" */
/* "stereo", "5.1 (FL FR FC LFE BL BR)", "3 channels"; `mask` is the WAVE speaker mask, 0 when unknown. */
size_t rubraview_detail_channels(char *buf, size_t cap, uint32_t count, uint32_t mask);
/* "High, level 4.1" from H.264's profile_idc and level_idc; HEVC likewise ("Main 10, level 5.1"). */
size_t rubraview_detail_h264_profile(char *buf, size_t cap, uint32_t profile, uint32_t level);
size_t rubraview_detail_hevc_profile(char *buf, size_t cap, uint32_t profile, uint32_t level);
/* MPEG-4's audioProfileLevelIndication: "AAC-LC, level 2", "HE-AAC v2, level 4". */
size_t rubraview_detail_aac_profile(char *buf, size_t cap, uint32_t indication);
/* "1 : 12.4 (8.1 % of the size unpacked)": `stored` bytes holding `raw` bytes' worth. */
size_t rubraview_detail_ratio(char *buf, size_t cap, double stored_bytes, double raw_bytes);

/*
 * A picture file's own account of itself: JPEG (the process, the
 * precision, the chroma sampling, an estimate of the quality from its
 * tables, the restart interval, what blocks it carries), PNG, GIF, WebP
 * and BMP likewise. `out_raw_bits` receives the bits one pixel takes
 * uncompressed, when the file says (0 otherwise). Returns false when the
 * bytes are none of these.
 */
bool rubraview_image_details(const uint8_t *bytes, size_t size, rubraview_details_t *out, uint32_t *out_raw_bits);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_DETAIL_H */
