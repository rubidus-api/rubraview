#include "rubraview/avif_read.h"

#include "avif/avif.h"

#include <stdlib.h>
#include <string.h>

struct rubraview_avif {
    avifDecoder *decoder;
    int32_t width, height;
};

bool rubraview_avif_is(const uint8_t *bytes, size_t size) {
    if (!bytes || size < 16 || memcmp(bytes + 4, "ftyp", 4) != 0) return false;
    uint32_t box = ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | bytes[3];
    if (box < 16 || box > size) box = (uint32_t)(size < 4096 ? size : 4096);
    /* The major brand, then (after the minor version) the compatible ones. */
    for (size_t at = 8; at + 4 <= box; at += at == 8 ? 8 : 4) {
        if (memcmp(bytes + at, "avif", 4) == 0 || memcmp(bytes + at, "avis", 4) == 0) return true;
    }
    return false;
}

rubraview_avif_t *rubraview_avif_open(const uint8_t *bytes, size_t size, int32_t max_side,
                                      int32_t *out_width, int32_t *out_height, size_t *out_frames) {
    if (out_width) *out_width = 0;
    if (out_height) *out_height = 0;
    if (out_frames) *out_frames = 0;
    if (!rubraview_avif_is(bytes, size) || max_side <= 0) return NULL;

    rubraview_avif_t *avif = (rubraview_avif_t*)calloc(1, sizeof(*avif));
    if (!avif) return NULL;
    avif->decoder = avifDecoderCreate();
    if (!avif->decoder) { free(avif); return NULL; }
    avif->decoder->codecChoice = AVIF_CODEC_CHOICE_DAV1D;
    avif->decoder->maxThreads = 4;
    avif->decoder->imageDimensionLimit = (uint32_t)max_side;
    /* Exif and XMP are read by the viewer's own code from the file. */
    avif->decoder->ignoreExif = AVIF_TRUE;
    avif->decoder->ignoreXMP = AVIF_TRUE;

    if (avifDecoderSetIOMemory(avif->decoder, bytes, size) != AVIF_RESULT_OK ||
        avifDecoderParse(avif->decoder) != AVIF_RESULT_OK ||
        avif->decoder->imageCount < 1 || !avif->decoder->image ||
        avif->decoder->image->width == 0 || avif->decoder->image->height == 0 ||
        avif->decoder->image->width > (uint32_t)max_side || avif->decoder->image->height > (uint32_t)max_side) {
        rubraview_avif_close(avif);
        return NULL;
    }
    avif->width = (int32_t)avif->decoder->image->width;
    avif->height = (int32_t)avif->decoder->image->height;
    if (out_width) *out_width = avif->width;
    if (out_height) *out_height = avif->height;
    if (out_frames) *out_frames = (size_t)avif->decoder->imageCount;
    return avif;
}

bool rubraview_avif_frame(rubraview_avif_t *avif, size_t index, uint8_t *dst, size_t stride,
                          bool premultiplied, double *out_seconds) {
    if (out_seconds) *out_seconds = 0.0;
    if (!avif || !dst || index >= (size_t)avif->decoder->imageCount || stride < (size_t)avif->width * 4u) return false;
    if (avifDecoderNthImage(avif->decoder, (uint32_t)index) != AVIF_RESULT_OK) return false;
    const avifImage *image = avif->decoder->image;
    /* A frame of another size than the first is not one the canvas can take. */
    if (!image || (int32_t)image->width != avif->width || (int32_t)image->height != avif->height) return false;

    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, image);
    rgb.format = AVIF_RGB_FORMAT_BGRA;
    rgb.depth = 8;
    rgb.alphaPremultiplied = premultiplied ? AVIF_TRUE : AVIF_FALSE;
    rgb.maxThreads = 1;
    rgb.pixels = dst;
    rgb.rowBytes = (uint32_t)stride;
    if (avifImageYUVToRGB(image, &rgb) != AVIF_RESULT_OK) return false;
    if (out_seconds && avif->decoder->imageCount > 1) *out_seconds = avif->decoder->imageTiming.duration;
    return true;
}

int32_t rubraview_avif_orientation(const rubraview_avif_t *avif) {
    const avifImage *image = avif ? avif->decoder->image : NULL;
    if (!image) return 1;
    /* A turn (anti-clockwise, in quarter turns) and then a mirroring, as libavif's own tools map them. */
    int angle = (image->transformFlags & AVIF_TRANSFORM_IROT) ? (image->irot.angle & 3) : 0;
    bool mirrored = (image->transformFlags & AVIF_TRANSFORM_IMIR) != 0;
    int axis = mirrored ? (image->imir.axis & 1) : 0;   /* 0: top and bottom change places; 1: left and right */
    if (!mirrored) {
        static const int32_t TURNED[4] = { 1, 8, 3, 6 };
        return TURNED[angle];
    }
    static const int32_t BOTH[4][2] = { { 4, 2 }, { 5, 7 }, { 2, 4 }, { 7, 5 } };
    return BOTH[angle][axis];
}

double rubraview_avif_frame_seconds(rubraview_avif_t *avif, size_t index) {
    avifImageTiming timing;
    if (!avif || index >= (size_t)avif->decoder->imageCount || avif->decoder->imageCount < 2) return 0.0;
    if (avifDecoderNthImageTiming(avif->decoder, (uint32_t)index, &timing) != AVIF_RESULT_OK) return 0.0;
    return timing.duration;
}

void rubraview_avif_close(rubraview_avif_t *avif) {
    if (!avif) return;
    if (avif->decoder) avifDecoderDestroy(avif->decoder);
    free(avif);
}
