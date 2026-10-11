#include "rubraview/webp_write.h"

#include "src/webp/encode.h"
#include "src/webp/mux.h"

#include <stdlib.h>
#include <string.h>

#define WEBP_MAX_SIDE 16383

static bool config_for(const rubraview_webp_options_t *options, WebPConfig *config) {
    int quality = options ? options->quality : 80;
    int effort = options ? options->effort : 4;
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;
    if (effort < 0) effort = 0;
    if (effort > 6) effort = 6;
    if (!WebPConfigInit(config)) return false;
    config->quality = (float)quality;
    config->method = effort;
    if (options && options->lossless) {
        config->lossless = 1;
        config->exact = 0;   /* the colour under a wholly transparent pixel is not kept */
    }
    return WebPValidateConfig(config) != 0;
}

/* BGRA rows, premultiplied or not, into a picture's own ARGB words. */
static bool picture_take(WebPPicture *picture, const uint8_t *bgra, int32_t stride, bool premultiplied, bool rgba) {
    picture->use_argb = 1;
    if (!WebPPictureAlloc(picture)) return false;
    for (int y = 0; y < picture->height; ++y) {
        const uint8_t *src = bgra + (size_t)y * (size_t)stride;
        uint32_t *dst = picture->argb + (size_t)y * (size_t)picture->argb_stride;
        for (int x = 0; x < picture->width; ++x, src += 4) {
            uint32_t b = src[rgba ? 2 : 0], g = src[1], r = src[rgba ? 0 : 2], a = src[3];
            if (premultiplied && a != 255u) {
                if (a == 0u) {
                    b = g = r = 0u;
                } else {
                    b = (b * 255u + a / 2u) / a; if (b > 255u) b = 255u;
                    g = (g * 255u + a / 2u) / a; if (g > 255u) g = 255u;
                    r = (r * 255u + a / 2u) / a; if (r > 255u) r = 255u;
                }
            }
            dst[x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    return true;
}

static bool size_fits(int32_t width, int32_t height, int32_t stride) {
    return width > 0 && height > 0 && width <= WEBP_MAX_SIDE && height <= WEBP_MAX_SIDE && stride >= width * 4;
}

bool rubraview_webp_write(const uint8_t *bgra, int32_t width, int32_t height, int32_t stride,
                          const rubraview_webp_options_t *options, uint8_t **out_bytes, size_t *out_len) {
    if (out_bytes) *out_bytes = NULL;
    if (out_len) *out_len = 0;
    if (!bgra || !out_bytes || !out_len || !size_fits(width, height, stride)) return false;

    WebPConfig config;
    WebPPicture picture;
    WebPMemoryWriter writer;
    if (!config_for(options, &config) || !WebPPictureInit(&picture)) return false;
    picture.width = width;
    picture.height = height;
    WebPMemoryWriterInit(&writer);
    picture.writer = WebPMemoryWrite;
    picture.custom_ptr = &writer;

    bool ok = picture_take(&picture, bgra, stride, options && options->premultiplied, options && options->rgba) &&
              WebPEncode(&config, &picture) != 0 && writer.size > 0;
    WebPPictureFree(&picture);
    if (!ok) {
        WebPMemoryWriterClear(&writer);
        return false;
    }
    *out_bytes = writer.mem;   /* WebPMalloc'd: rubraview_webp_free gives it back */
    *out_len = writer.size;
    return true;
}

struct rubraview_webp_anim {
    WebPAnimEncoder *encoder;
    WebPConfig       config;
    int32_t          width, height;
    int              timestamp_ms;
    bool             premultiplied, rgba;
    bool             failed;
};

rubraview_webp_anim_t *rubraview_webp_anim_begin(int32_t width, int32_t height, int32_t loop_count,
                                                 const rubraview_webp_options_t *options) {
    if (!size_fits(width, height, width * 4)) return NULL;
    rubraview_webp_anim_t *anim = (rubraview_webp_anim_t*)calloc(1, sizeof(*anim));
    if (!anim) return NULL;
    WebPAnimEncoderOptions encoder_options;
    if (!config_for(options, &anim->config) || !WebPAnimEncoderOptionsInit(&encoder_options)) {
        free(anim);
        return NULL;
    }
    encoder_options.anim_params.loop_count = loop_count > 0 ? loop_count : 0;
    encoder_options.allow_mixed = 0;
    anim->encoder = WebPAnimEncoderNew(width, height, &encoder_options);
    if (!anim->encoder) {
        free(anim);
        return NULL;
    }
    anim->width = width;
    anim->height = height;
    anim->premultiplied = options && options->premultiplied;
    anim->rgba = options && options->rgba;
    return anim;
}

bool rubraview_webp_anim_add(rubraview_webp_anim_t *anim, const uint8_t *bgra, int32_t stride, int32_t duration_ms) {
    if (!anim || anim->failed || !bgra || stride < anim->width * 4) return false;
    WebPPicture picture;
    if (!WebPPictureInit(&picture)) { anim->failed = true; return false; }
    picture.width = anim->width;
    picture.height = anim->height;
    bool ok = picture_take(&picture, bgra, stride, anim->premultiplied, anim->rgba) &&
              WebPAnimEncoderAdd(anim->encoder, &picture, anim->timestamp_ms, &anim->config) != 0;
    WebPPictureFree(&picture);
    if (!ok) { anim->failed = true; return false; }
    if (duration_ms <= 0) duration_ms = 10;
    if (duration_ms > 0xFFFFFF) duration_ms = 0xFFFFFF;   /* a frame's duration is 24 bits */
    anim->timestamp_ms += duration_ms;
    return true;
}

bool rubraview_webp_anim_end(rubraview_webp_anim_t *anim, uint8_t **out_bytes, size_t *out_len) {
    if (out_bytes) *out_bytes = NULL;
    if (out_len) *out_len = 0;
    if (!anim) return false;
    bool ok = false;
    WebPData data;
    WebPDataInit(&data);
    if (!anim->failed && out_bytes && out_len && anim->timestamp_ms > 0 &&
        WebPAnimEncoderAdd(anim->encoder, NULL, anim->timestamp_ms, NULL) != 0 &&   /* the last frame's end */
        WebPAnimEncoderAssemble(anim->encoder, &data) != 0 && data.size > 0) {
        *out_bytes = (uint8_t*)data.bytes;   /* WebPMalloc'd */
        *out_len = data.size;
        ok = true;
    } else {
        WebPDataClear(&data);
    }
    WebPAnimEncoderDelete(anim->encoder);
    free(anim);
    return ok;
}

void rubraview_webp_anim_abort(rubraview_webp_anim_t *anim) {
    if (!anim) return;
    WebPAnimEncoderDelete(anim->encoder);
    free(anim);
}

void rubraview_webp_free(uint8_t *bytes) {
    if (bytes) WebPFree(bytes);
}
