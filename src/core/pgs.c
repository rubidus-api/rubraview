#include "rubraview/pgs.h"
#include <string.h>

/* A `.sup` file is a row of segments. Each one starts with "PG", the
   moment it belongs to, and what kind it is:

     0x14 PDS  the palette
     0x15 ODS  the picture, run-length coded, possibly split over several
     0x16 PCS  what is composed now: which objects, and where
     0x17 WDS  the windows they are drawn into
     0x80 END  the end of this display set

   A display set is a PCS and everything up to its END. A PCS with no
   objects is the stream saying "nothing now", which is what ends the
   subtitle before it. */

#define PGS_PDS 0x14u
#define PGS_ODS 0x15u
#define PGS_PCS 0x16u
#define PGS_WDS 0x17u
#define PGS_END 0x80u

#define PGS_HEADER 13u          /* "PG", pts, dts, type, length */

static uint32_t be16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }
static uint32_t be24(const uint8_t *p) { return (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2]; }
static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* The next segment at `at`, or false at the end (or at nonsense). */
static bool segment_at(const uint8_t *bytes, size_t size, size_t at,
                       uint8_t *out_type, double *out_pts, const uint8_t **out_body,
                       size_t *out_body_len, size_t *out_next) {
    if (at + PGS_HEADER > size) return false;
    if (bytes[at] != 'P' || bytes[at + 1] != 'G') return false;
    uint32_t pts = be32(bytes + at + 2);
    uint8_t type = bytes[at + 10];
    size_t length = be16(bytes + at + 11);
    if (at + PGS_HEADER + length > size) return false;
    *out_type = type;
    *out_pts = (double)pts / 90000.0;
    *out_body = bytes + at + PGS_HEADER;
    *out_body_len = length;
    *out_next = at + PGS_HEADER + length;
    return true;
}

/* A PCS that only sends a new palette is a step of a fade: it composes
   the same picture again and carries no object of its own. */
static bool pcs_is_palette_only(const uint8_t *body, size_t len) {
    return len >= 11 && body[8] != 0;
}

/* A PCS says how many objects are composed; none means "clear". */
static bool pcs_has_objects(const uint8_t *body, size_t len,
                            int32_t *out_width, int32_t *out_height) {
    if (len < 11) return false;
    *out_width = (int32_t)be16(body);
    *out_height = (int32_t)be16(body + 2);
    return body[10] != 0;
}

rubraview_pgs_track_t rubraview_pgs_index(proven_arena_t *arena, const uint8_t *bytes, size_t size) {
    rubraview_pgs_track_t track = {0};
    if (!arena || !bytes || size < PGS_HEADER) return track;

    /* Two passes: count the display sets that draw something, then fill
       them in, so the entries are allocated once. */
    size_t wanted = 0;
    for (int pass = 0; pass < 2; ++pass) {
        size_t at = 0, count = 0;
        bool have_set = false;
        int32_t w = 0, h = 0;

        while (at < size) {
            uint8_t type = 0;
            double pts = 0.0;
            const uint8_t *body = NULL;
            size_t len = 0, next = 0;
            if (!segment_at(bytes, size, at, &type, &pts, &body, &len, &next)) break;

            if (type == PGS_PCS && !pcs_is_palette_only(body, len)) {
                bool draws = pcs_has_objects(body, len, &w, &h);
                if (w > 0 && h > 0) { track.frame_width = w; track.frame_height = h; }

                /* Whatever was on screen ends here — either because this
                   set replaces it or because this set clears it. */
                if (have_set && pass == 1 && count > 0) {
                    track.entries[count - 1].end_seconds = pts;
                }
                if (draws) {
                    if (pass == 1) {
                        track.entries[count] = (rubraview_pgs_entry_t){
                            .start_seconds = pts,
                            .end_seconds = pts + 10.0,   /* until the set that clears it */
                            .offset = at,
                        };
                    }
                    count++;
                    have_set = true;
                } else {
                    have_set = false;
                }
            }
            at = next;
        }

        if (pass == 0) {
            wanted = count;
            if (wanted == 0) return track;
            proven_result_mem_mut_t mem = rubraview_arena_alloc_array(arena, wanted, sizeof(rubraview_pgs_entry_t));
            if (!proven_is_ok(mem.err)) return track;
            track.entries = (rubraview_pgs_entry_t*)mem.value.ptr;
            memset(track.entries, 0, wanted * sizeof(rubraview_pgs_entry_t));
        } else {
            track.count = count < wanted ? count : wanted;
        }
    }
    if (track.frame_width <= 0 || track.frame_height <= 0) {
        track.frame_width = 1920;
        track.frame_height = 1080;
    }
    return track;
}

static int32_t clamp_byte(double v) {
    int32_t i = (int32_t)(v + 0.5);
    if (i < 0) return 0;
    return i > 255 ? 255 : i;
}

/* Y'CbCr (BT.709, the range Blu-ray uses) and an alpha, as ARGB. */
static uint32_t ycbcr_to_argb(uint8_t y, uint8_t cb, uint8_t cr, uint8_t alpha) {
    double yy = ((double)y - 16.0) * 1.164383;
    double b = yy + 2.112402 * ((double)cb - 128.0);
    double g = yy - 0.213249 * ((double)cb - 128.0) - 0.532909 * ((double)cr - 128.0);
    double r = yy + 1.792741 * ((double)cr - 128.0);
    int32_t ri = clamp_byte(r), gi = clamp_byte(g), bi = clamp_byte(b);
    return ((uint32_t)alpha << 24) | ((uint32_t)ri << 16) | ((uint32_t)gi << 8) | (uint32_t)bi;
}

/*
 * The picture: runs of one colour, a line at a time.
 *
 *   CC                one pixel of colour CC
 *   00 00             the line ends here
 *   00 LL             LL pixels of colour 0        (LL < 64)
 *   00 4L LL          up to 16383 pixels of colour 0
 *   00 8L CC          L pixels of colour CC        (L < 64)
 *   00 CL LL CC       up to 16383 pixels of colour CC
 */
static void decode_rle(const uint8_t *data, size_t size, int32_t width, int32_t height, uint8_t *out) {
    memset(out, 0, (size_t)width * (size_t)height);
    size_t at = 0;
    int32_t x = 0, y = 0;
    while (at < size && y < height) {
        uint8_t first = data[at++];
        uint32_t run = 1;
        uint8_t colour = first;
        if (first == 0x00) {
            if (at >= size) break;
            uint8_t second = data[at++];
            if (second == 0x00) {                 /* end of line */
                x = 0;
                y++;
                continue;
            }
            colour = 0;
            if ((second & 0xC0u) == 0x40u) {      /* 00 4L LL */
                if (at >= size) break;
                run = ((uint32_t)(second & 0x3Fu) << 8) | data[at++];
            } else if ((second & 0xC0u) == 0x80u) { /* 00 8L CC */
                if (at >= size) break;
                run = second & 0x3Fu;
                colour = data[at++];
            } else if ((second & 0xC0u) == 0xC0u) { /* 00 CL LL CC */
                if (at + 1 >= size) break;
                run = ((uint32_t)(second & 0x3Fu) << 8) | data[at++];
                colour = data[at++];
            } else {                                /* 00 LL */
                run = second & 0x3Fu;
            }
        }
        if (run == 0) continue;
        if (x + (int32_t)run > width) run = (uint32_t)(width - x);
        if (run > 0) memset(out + (size_t)y * (size_t)width + (size_t)x, colour, run);
        x += (int32_t)run;
        /* A line ends when the stream says so and not before: a run that
           fills the width is still followed by its own `00 00`. */
    }
}

bool rubraview_pgs_decode(const rubraview_pgs_track_t *track, size_t index,
                          const uint8_t *bytes, size_t size,
                          uint8_t *pixels, rubraview_pgs_cue_t *out_cue) {
    if (!track || !bytes || !pixels || !out_cue || index >= track->count) return false;

    rubraview_pgs_cue_t cue = {
        .start_seconds = track->entries[index].start_seconds,
        .end_seconds = track->entries[index].end_seconds,
        .indices = pixels,
    };
    /* A picture with no palette of its own is black on nothing. */
    for (int i = 0; i < 256; ++i) cue.colors[i] = 0;

    static uint8_t object[RUBRAVIEW_PGS_MAX_PIXELS];   /* the run-length bytes, gathered */
    size_t object_len = 0;
    int32_t width = 0, height = 0;
    int32_t wanted_object = -1;      /* the one the composition places */
    bool have_object = false, have_placement = false, gathering = false;

    size_t at = track->entries[index].offset;
    while (at < size) {
        uint8_t type = 0;
        double pts = 0.0;
        const uint8_t *body = NULL;
        size_t len = 0, next = 0;
        if (!segment_at(bytes, size, at, &type, &pts, &body, &len, &next)) break;

        if (type == PGS_PCS && at != track->entries[index].offset) break;   /* the next set */
        if (type == PGS_PCS && len >= 19) {
            /* The first composition object says which picture, and where.
               A set that places two (subtitles above and below the frame)
               is drawn as its first one — see T081. */
            wanted_object = (int32_t)be16(body + 11);
            cue.x = (int32_t)be16(body + 15);
            cue.y = (int32_t)be16(body + 17);
            cue.forced = (body[14] & 0x40u) != 0;
            have_placement = true;
        } else if (type == PGS_PDS && len >= 2) {
            for (size_t p = 2; p + 5 <= len; p += 5) {
                uint8_t id = body[p];
                cue.colors[id] = ycbcr_to_argb(body[p + 1], body[p + 3], body[p + 2], body[p + 4]);
            }
        } else if (type == PGS_ODS && len >= 4) {
            size_t data_at = 4;
            if ((body[3] & 0x80u) != 0) {          /* the first piece carries the size */
                /* A picture this composition does not place is another
                   object of the same set; leave it alone. */
                gathering = wanted_object < 0 || (int32_t)be16(body) == wanted_object;
                if (gathering && !have_object) {
                    if (len < 11) return false;
                    width = (int32_t)be16(body + 7);
                    height = (int32_t)be16(body + 9);
                    data_at = 11;
                    object_len = 0;
                    (void)be24(body + 4);          /* the whole object's length, not needed */
                } else {
                    gathering = false;
                }
            }
            if (!gathering) { at = next; continue; }
            size_t piece = len - data_at;
            if (object_len + piece > sizeof(object)) piece = sizeof(object) - object_len;
            memcpy(object + object_len, body + data_at, piece);
            object_len += piece;
            have_object = true;
        } else if (type == PGS_END) {
            break;
        }
        at = next;
    }

    if (!have_object || width <= 0 || height <= 0) return false;
    if ((size_t)width * (size_t)height > RUBRAVIEW_PGS_MAX_PIXELS) return false;
    if (!have_placement) { cue.x = 0; cue.y = 0; }

    decode_rle(object, object_len, width, height, pixels);
    cue.width = width;
    cue.height = height;
    *out_cue = cue;
    return true;
}

int32_t rubraview_pgs_at(const rubraview_pgs_track_t *track, double seconds) {
    if (!track || track->count == 0) return -1;
    for (size_t i = 0; i < track->count; ++i) {
        if (seconds < track->entries[i].start_seconds) break;
        if (seconds < track->entries[i].end_seconds) return (int32_t)i;
    }
    return -1;
}

void rubraview_pgs_pixels(const rubraview_pgs_cue_t *cue, uint32_t *out) {
    if (!cue || !out || !cue->indices) return;
    uint32_t premultiplied[256];
    for (int i = 0; i < 256; ++i) {
        uint32_t argb = cue->colors[i];
        uint32_t a = argb >> 24;
        uint32_t r = ((argb >> 16) & 0xFF) * a / 255;
        uint32_t g = ((argb >> 8) & 0xFF) * a / 255;
        uint32_t b = (argb & 0xFF) * a / 255;
        premultiplied[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
    size_t n = (size_t)cue->width * (size_t)cue->height;
    for (size_t i = 0; i < n; ++i) out[i] = premultiplied[cue->indices[i]];
}
