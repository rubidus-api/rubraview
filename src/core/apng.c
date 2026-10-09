#include "rubraview/apng.h"
#include "proven/hash.h"
#include <stdlib.h>
#include <string.h>

static const uint8_t PNG_SIGNATURE[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

#define IHDR_DATA 13
#define FCTL_DATA 26
/* A chunk around its data: the length, the type, and the check after it. */
#define CHUNK_EXTRA 12

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static bool type_is(const uint8_t *chunk, const char *type) {
    return memcmp(chunk + 4, type, 4) == 0;
}

/* The chunk at `at`: its data length, or false when it does not fit in the file. */
static bool chunk_at(const uint8_t *data, size_t size, size_t at, uint32_t *out_len) {
    if (at > size || size - at < CHUNK_EXTRA) return false;
    uint32_t len = be32(data + at);
    if (len > size - at - CHUNK_EXTRA) return false;
    *out_len = len;
    return true;
}

bool rubraview_apng_is_png(const uint8_t *data, size_t size) {
    return data && size >= sizeof(PNG_SIGNATURE) && memcmp(data, PNG_SIGNATURE, sizeof(PNG_SIGNATURE)) == 0;
}

static bool frames_push(rubraview_apng_t *apng, size_t *cap, const rubraview_apng_frame_t *frame) {
    if (apng->frame_count == *cap) {
        size_t grown = *cap ? *cap * 2 : 64;
        rubraview_apng_frame_t *more = (rubraview_apng_frame_t*)realloc(apng->frames, grown * sizeof(*more));
        if (!more) return false;
        apng->frames = more;
        *cap = grown;
    }
    apng->frames[apng->frame_count++] = *frame;
    return true;
}

bool rubraview_apng_parse(const uint8_t *data, size_t size, rubraview_apng_t *out) {
    if (!out) return false;
    *out = (rubraview_apng_t){ 0 };
    if (!rubraview_apng_is_png(data, size)) return false;

    size_t at = sizeof(PNG_SIGNATURE);
    uint32_t len = 0;
    if (!chunk_at(data, size, at, &len) || len != IHDR_DATA || !type_is(data + at, "IHDR")) return false;
    uint32_t width = be32(data + at + 8), height = be32(data + at + 12);
    if (width == 0 || height == 0 || width > INT32_MAX || height > INT32_MAX) return false;
    at += CHUNK_EXTRA + len;

    rubraview_apng_t apng = { .data = data, .size = size, .width = width, .height = height, .head_begin = at };
    size_t cap = 0;
    bool animated = false;      /* an acTL came before the picture's own data */
    bool seen_idat = false;
    bool open = false;          /* a frame whose data is still being passed */
    rubraview_apng_frame_t frame = { 0 };
    bool has_data = false;

    for (;;) {
        bool more = chunk_at(data, size, at, &len);
        const uint8_t *chunk = more ? data + at : NULL;
        bool control = more && type_is(chunk, "fcTL");
        bool end = !more || type_is(chunk, "IEND");

        if (more && !seen_idat && type_is(chunk, "IDAT")) {
            /* An ordinary PNG stops costing anything here. */
            if (!animated) return false;
            seen_idat = true;
            apng.head_end = at;
        }
        if ((control || end) && open) {
            /* The frame before ends where this begins. */
            frame.data_end = at;
            if (has_data && !frames_push(&apng, &cap, &frame)) { free(apng.frames); return false; }
            open = false;
        }
        if (end) break;

        if (!seen_idat && type_is(chunk, "acTL")) {
            animated = true;
        } else if (control) {
            if (!animated || len != FCTL_DATA) break;
            const uint8_t *d = chunk + 8;
            uint32_t w = be32(d + 4), h = be32(d + 8), x = be32(d + 12), y = be32(d + 16);
            uint32_t num = ((uint32_t)d[20] << 8) | d[21], den = ((uint32_t)d[22] << 8) | d[23];
            /* A frame lies wholly on the picture; one that does not ends the reading. */
            if (w == 0 || h == 0 || w > width || h > height || x > width - w || y > height - h) break;
            if (d[24] > RUBRAVIEW_APNG_DISPOSE_PREVIOUS || d[25] > 1) break;
            frame = (rubraview_apng_frame_t){
                .width = w, .height = h, .left = x, .top = y,
                .delay_seconds = (double)num / (double)(den ? den : 100),
                .dispose = d[24], .replaces = d[25] == 0,
                .data_begin = at + CHUNK_EXTRA + len,
            };
            /* The first frame has nothing before it to bring back. */
            if (apng.frame_count == 0 && frame.dispose == RUBRAVIEW_APNG_DISPOSE_PREVIOUS) {
                frame.dispose = RUBRAVIEW_APNG_DISPOSE_BACKGROUND;
            }
            open = true;
            has_data = false;
        } else if (open && type_is(chunk, "IDAT")) {
            /* The file's own picture is a frame only when a fcTL came before it. */
            if (apng.frame_count == 0) has_data = true;
        } else if (open && type_is(chunk, "fdAT")) {
            if (len < 4) break;
            if (seen_idat) has_data = true;
        }
        at += CHUNK_EXTRA + len;
    }

    if (!seen_idat || apng.frame_count == 0) { free(apng.frames); return false; }
    *out = apng;
    return true;
}

void rubraview_apng_free(rubraview_apng_t *apng) {
    if (!apng) return;
    free(apng->frames);
    *apng = (rubraview_apng_t){ 0 };
}

/* The chunks of the header that a frame's own PNG carries along: all but
   the animation's, which a still picture has no use for. */
static bool head_carried(const uint8_t *chunk) {
    return !type_is(chunk, "acTL") && !type_is(chunk, "fcTL") && !type_is(chunk, "fdAT");
}

static void put_chunk_check(uint8_t *chunk, uint32_t len) {
    put32(chunk + 8 + len, (uint32_t)proven_crc32((proven_mem_view_t){ .ptr = chunk + 4, .size = (size_t)len + 4 }));
}

/* One pass for both: with `dst` NULL it only counts. */
static size_t frame_png(const rubraview_apng_t *apng, size_t index, uint8_t *dst, size_t cap) {
    if (!apng || !apng->data || index >= apng->frame_count) return 0;
    const rubraview_apng_frame_t *frame = &apng->frames[index];
    const uint8_t *data = apng->data;
    size_t used = 0;
#define EMIT(bytes, count) do { \
        size_t n_ = (count); \
        if (dst) { if (cap - used < n_) return 0; memcpy(dst + used, (bytes), n_); } \
        used += n_; \
    } while (0)

    EMIT(PNG_SIGNATURE, sizeof(PNG_SIGNATURE));
    /* The file's IHDR, with this frame's size in place of the picture's. */
    size_t ihdr = used;
    EMIT(data + sizeof(PNG_SIGNATURE), CHUNK_EXTRA + IHDR_DATA);
    if (dst) {
        put32(dst + ihdr + 8, frame->width);
        put32(dst + ihdr + 12, frame->height);
        put_chunk_check(dst + ihdr, IHDR_DATA);
    }

    uint32_t len = 0;
    for (size_t at = apng->head_begin; at < apng->head_end && chunk_at(data, apng->size, at, &len); at += CHUNK_EXTRA + len) {
        if (head_carried(data + at)) EMIT(data + at, CHUNK_EXTRA + (size_t)len);
    }

    for (size_t at = frame->data_begin; at < frame->data_end && chunk_at(data, apng->size, at, &len); at += CHUNK_EXTRA + len) {
        const uint8_t *chunk = data + at;
        if (type_is(chunk, "IDAT")) {
            EMIT(chunk, CHUNK_EXTRA + (size_t)len);
        } else if (type_is(chunk, "fdAT") && len >= 4) {
            /* The same data under the name a still picture uses, without
               the number that ordered it among the frames. */
            uint32_t body = len - 4;
            size_t start = used;
            uint8_t head[8];
            put32(head, body);
            memcpy(head + 4, "IDAT", 4);
            EMIT(head, sizeof(head));
            EMIT(chunk + 12, body);
            EMIT(head, 4);   /* room for the check */
            if (dst) put_chunk_check(dst + start, body);
        }
    }

    static const uint8_t IEND[12] = { 0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82 };
    EMIT(IEND, sizeof(IEND));
#undef EMIT
    return used;
}

size_t rubraview_apng_frame_png_size(const rubraview_apng_t *apng, size_t index) {
    return frame_png(apng, index, NULL, 0);
}

size_t rubraview_apng_frame_png(const rubraview_apng_t *apng, size_t index, uint8_t *dst, size_t cap) {
    if (!dst) return 0;
    return frame_png(apng, index, dst, cap);
}
