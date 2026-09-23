#include "rubraview/vobsub.h"
#include <string.h>

/* ---- the index file ---- */

static bool line_starts(u8str_t line, const char *word, u8str_t *rest) {
    size_t n = strlen(word);
    if (line.len < n) return false;
    for (size_t i = 0; i < n; ++i) {
        char a = line.ptr[i], b = word[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (a != b) return false;
    }
    *rest = (u8str_t){ .ptr = line.ptr + n, .len = line.len - n };
    while (rest->len > 0 && (rest->ptr[0] == ' ' || rest->ptr[0] == '\t')) { rest->ptr++; rest->len--; }
    return true;
}

static u8str_t next_line(u8str_t *text) {
    size_t i = 0;
    while (i < text->len && text->ptr[i] != '\n') i++;
    u8str_t line = { .ptr = text->ptr, .len = i };
    if (line.len > 0 && line.ptr[line.len - 1] == '\r') line.len--;
    text->ptr += i < text->len ? i + 1 : i;
    text->len -= i < text->len ? i + 1 : i;
    return line;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* `00:01:02:500` — hours, minutes, seconds, milliseconds. A leading `-`
   turns up in `delay:`, which winds every later timestamp back. */
static bool parse_stamp(u8str_t text, double *out_seconds) {
    double sign = 1.0;
    size_t i = 0;
    while (i < text.len && (text.ptr[i] == ' ' || text.ptr[i] == '+')) i++;
    if (i < text.len && text.ptr[i] == '-') { sign = -1.0; i++; }
    int part[4] = { 0, 0, 0, 0 };
    int found = 0;
    int value = 0;
    bool digits = false;
    for (; i <= text.len && found < 4; ++i) {
        char c = i < text.len ? text.ptr[i] : ':';
        if (c >= '0' && c <= '9') { value = value * 10 + (c - '0'); digits = true; continue; }
        if (c == ':' || c == '.' || c == ',') {
            if (!digits) return false;
            part[found++] = value;
            value = 0;
            digits = false;
            if (i == text.len) break;
            continue;
        }
        break;
    }
    if (found < 4) {
        if (found == 3 && digits) part[found++] = value;
        else if (found < 3) return false;
    }
    *out_seconds = sign * (part[0] * 3600.0 + part[1] * 60.0 + part[2] + part[3] / 1000.0);
    return true;
}

/* `palette: 000000, 1a1a1a, ...` — sixteen colours, RGB hex. */
static void parse_palette(u8str_t rest, uint32_t *palette) {
    size_t entry = 0;
    size_t i = 0;
    while (i < rest.len && entry < 16) {
        while (i < rest.len && (rest.ptr[i] == ' ' || rest.ptr[i] == ',' || rest.ptr[i] == '\t')) i++;
        uint32_t value = 0;
        int digits = 0;
        while (i < rest.len && hex_digit(rest.ptr[i]) >= 0 && digits < 6) {
            value = (value << 4) | (uint32_t)hex_digit(rest.ptr[i]);
            digits++;
            i++;
        }
        if (digits == 0) break;
        palette[entry++] = 0xFF000000u | value;
        while (i < rest.len && rest.ptr[i] != ',') i++;
    }
}

size_t rubraview_vobsub_languages(u8str_t idx_text, u8str_t *out, size_t max) {
    size_t count = 0;
    u8str_t text = idx_text;
    while (text.len > 0 && count < max) {
        u8str_t line = next_line(&text), rest;
        if (!line_starts(line, "id:", &rest)) continue;
        size_t n = 0;
        while (n < rest.len && rest.ptr[n] != ',' && rest.ptr[n] != ' ') n++;
        out[count++] = (u8str_t){ .ptr = rest.ptr, .len = n };
    }
    return count;
}

/* ---- the program stream ---- */

typedef struct {
    const uint8_t *data;
    size_t         size;
    size_t         at;
} reader_t;

static uint32_t be16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }

/* Collects one subpicture's bytes, starting at `filepos`: the packets of
   the program stream from there until the subpicture says it is whole.
   Returns how many bytes were written, and the first PTS seen. */
static size_t read_spu(reader_t *r, size_t filepos, uint8_t *out, size_t out_size, double *out_pts) {
    size_t written = 0, want = 0;
    *out_pts = -1.0;
    size_t at = filepos;
    while (at + 6 <= r->size) {
        const uint8_t *p = r->data + at;
        if (p[0] != 0 || p[1] != 0 || p[2] != 1) break;
        uint32_t id = p[3];
        if (id == 0xBA) {                      /* pack header */
            if (at + 14 > r->size) break;
            size_t len = (p[4] & 0xC0) == 0x40 ? 14 + (size_t)(r->data[at + 13] & 7) : 12;
            at += len;
            continue;
        }
        if (id == 0xB9) break;                 /* end of stream */
        if (at + 6 > r->size) break;
        size_t packet = be16(p + 4);
        if (packet == 0 || at + 6 + packet > r->size) break;
        if (id == 0xBD) {                      /* private stream 1: where subpictures live */
            const uint8_t *pes = p + 6;
            size_t header = 3 + (size_t)pes[2];
            if (header < packet) {
                if ((pes[1] & 0xC0) == 0x80 && (pes[2] >= 5)) {   /* a PTS is here */
                    const uint8_t *t = pes + 3;
                    uint64_t pts = ((uint64_t)(t[0] & 0x0E) << 29) | ((uint64_t)t[1] << 22) |
                                   ((uint64_t)(t[2] & 0xFE) << 14) | ((uint64_t)t[3] << 7) |
                                   ((uint64_t)t[4] >> 1);
                    if (*out_pts < 0.0) *out_pts = (double)pts / 90000.0;
                }
                const uint8_t *payload = pes + header;
                size_t length = packet - header;
                if (length > 1) {              /* the first byte names the substream */
                    payload++;
                    length--;
                    if (written == 0 && length >= 2) want = be16(payload);
                    if (written + length > out_size) length = out_size - written;
                    memcpy(out + written, payload, length);
                    written += length;
                    if (want > 0 && written >= want) return want < out_size ? want : out_size;
                }
            }
        }
        at += 6 + packet;
    }
    return written;
}

/* ---- the subpicture ---- */

typedef struct {
    const uint8_t *data;
    size_t         size;
    size_t         byte;
    bool           high;      /* the upper nibble of `byte` is next */
} nibbles_t;

static uint32_t take_nibble(nibbles_t *n) {
    if (n->byte >= n->size) return 0;
    uint32_t v = n->high ? (uint32_t)(n->data[n->byte] >> 4) : (uint32_t)(n->data[n->byte] & 0x0F);
    if (n->high) n->high = false;
    else { n->high = true; n->byte++; }
    return v;
}

static void align_byte(nibbles_t *n) {
    if (!n->high) { n->high = true; n->byte++; }
}

/* One field of the picture: every other line, run-length coded. A run is
   one to four nibbles — count and colour — and a count of zero means "to
   the end of this line". */
static void decode_field(const uint8_t *data, size_t size, size_t offset,
                         int32_t width, int32_t height, int32_t first_line, uint8_t *out) {
    nibbles_t n = { .data = data, .size = size, .byte = offset, .high = true };
    for (int32_t line = first_line; line < height; line += 2) {
        int32_t x = 0;
        while (x < width) {
            uint32_t v = take_nibble(&n);
            if (v < 4) {
                v = (v << 4) | take_nibble(&n);
                if (v < 0x10) {
                    v = (v << 4) | take_nibble(&n);
                    if (v < 0x40) v = (v << 4) | take_nibble(&n);
                }
            }
            uint32_t count = v >> 2;
            uint8_t colour = (uint8_t)(v & 3);
            if (count == 0 || (int32_t)count > width - x) count = (uint32_t)(width - x);
            memset(out + (size_t)line * (size_t)width + (size_t)x, colour, count);
            x += (int32_t)count;
        }
        align_byte(&n);
        if (n.byte >= size) return;
    }
}

/* The control sequences: when to show it, where, in which colours. */
static bool read_controls(const uint8_t *spu, size_t size, size_t control,
                          double *out_show, double *out_hide, bool *out_forced,
                          int32_t *x1, int32_t *x2, int32_t *y1, int32_t *y2,
                          uint8_t *palette_index, uint8_t *alpha, size_t *field_offset) {
    bool have_area = false, have_offsets = false;
    *out_show = 0.0;
    *out_hide = -1.0;
    *out_forced = false;
    size_t at = control;
    for (int guard = 0; guard < 64 && at + 4 <= size; ++guard) {
        double when = (double)be16(spu + at) * 1024.0 / 90000.0;
        size_t next = be16(spu + at + 2);
        size_t cmd = at + 4;
        bool end = false;
        while (cmd < size && !end) {
            switch (spu[cmd]) {
                case 0x00: *out_forced = true; *out_show = when; cmd += 1; break;
                case 0x01: *out_show = when; cmd += 1; break;
                case 0x02: *out_hide = when; cmd += 1; break;
                case 0x03:
                    if (cmd + 3 > size) return false;
                    palette_index[3] = (uint8_t)(spu[cmd + 1] >> 4);
                    palette_index[2] = (uint8_t)(spu[cmd + 1] & 0x0F);
                    palette_index[1] = (uint8_t)(spu[cmd + 2] >> 4);
                    palette_index[0] = (uint8_t)(spu[cmd + 2] & 0x0F);
                    cmd += 3;
                    break;
                case 0x04:
                    if (cmd + 3 > size) return false;
                    alpha[3] = (uint8_t)(spu[cmd + 1] >> 4);
                    alpha[2] = (uint8_t)(spu[cmd + 1] & 0x0F);
                    alpha[1] = (uint8_t)(spu[cmd + 2] >> 4);
                    alpha[0] = (uint8_t)(spu[cmd + 2] & 0x0F);
                    cmd += 3;
                    break;
                case 0x05:
                    if (cmd + 7 > size) return false;
                    *x1 = (int32_t)(((uint32_t)spu[cmd + 1] << 4) | (spu[cmd + 2] >> 4));
                    *x2 = (int32_t)(((uint32_t)(spu[cmd + 2] & 0x0F) << 8) | spu[cmd + 3]);
                    *y1 = (int32_t)(((uint32_t)spu[cmd + 4] << 4) | (spu[cmd + 5] >> 4));
                    *y2 = (int32_t)(((uint32_t)(spu[cmd + 5] & 0x0F) << 8) | spu[cmd + 6]);
                    have_area = true;
                    cmd += 7;
                    break;
                case 0x06:
                    if (cmd + 5 > size) return false;
                    field_offset[0] = be16(spu + cmd + 1);
                    field_offset[1] = be16(spu + cmd + 3);
                    have_offsets = true;
                    cmd += 5;
                    break;
                case 0xFF: end = true; break;
                default: return false;   /* an order we do not know: the rest cannot be trusted */
            }
        }
        if (next == at || next < control || next + 4 > size) break;
        at = next;
    }
    return have_area && have_offsets;
}

/* The index: the palette, the frame size and, for the chosen language,
   when each subtitle starts and where its picture is. */
rubraview_vobsub_track_t rubraview_vobsub_index(proven_arena_t *arena, u8str_t idx_text, size_t stream) {
    rubraview_vobsub_track_t track = {0};
    if (!arena) return track;
    track.frame_width = 720;
    track.frame_height = 480;
    for (int i = 0; i < 16; ++i) track.palette[i] = 0xFF000000u | (uint32_t)(i * 0x111111);

    /* Two passes: the first counts this language's lines so the entries
       are allocated once. */
    size_t wanted = 0, block = (size_t)-1;
    u8str_t text = idx_text, rest;
    while (text.len > 0) {
        u8str_t line = next_line(&text);
        if (line_starts(line, "id:", &rest)) {
            block = block == (size_t)-1 ? 0 : block + 1;
            track.stream_count++;
        } else if (line_starts(line, "timestamp:", &rest) && block == stream) {
            wanted++;
        }
    }
    if (wanted == 0) return track;

    proven_result_mem_mut_t mem = proven_arena_alloc(arena, wanted * sizeof(rubraview_vobsub_entry_t));
    if (!proven_is_ok(mem.err)) return track;
    rubraview_vobsub_entry_t *entries = (rubraview_vobsub_entry_t*)mem.value.ptr;

    size_t count = 0;
    double delay = 0.0;
    block = (size_t)-1;
    text = idx_text;
    while (text.len > 0 && count < wanted) {
        u8str_t line = next_line(&text);
        if (line_starts(line, "size:", &rest)) {
            int32_t w = 0, h = 0;
            size_t i = 0;
            while (i < rest.len && rest.ptr[i] >= '0' && rest.ptr[i] <= '9') w = w * 10 + (rest.ptr[i++] - '0');
            while (i < rest.len && (rest.ptr[i] == 'x' || rest.ptr[i] == 'X' || rest.ptr[i] == ' ')) i++;
            while (i < rest.len && rest.ptr[i] >= '0' && rest.ptr[i] <= '9') h = h * 10 + (rest.ptr[i++] - '0');
            if (w > 0 && h > 0) { track.frame_width = w; track.frame_height = h; }
            continue;
        }
        if (line_starts(line, "palette:", &rest)) { parse_palette(rest, track.palette); continue; }
        if (line_starts(line, "delay:", &rest)) {
            double d = 0.0;
            if (parse_stamp(rest, &d)) delay += d;
            continue;
        }
        if (line_starts(line, "id:", &rest)) {
            block = block == (size_t)-1 ? 0 : block + 1;
            if (block == stream) {
                size_t n = 0;
                while (n < rest.len && rest.ptr[n] != ',' && rest.ptr[n] != ' ') n++;
                track.language = (u8str_t){ .ptr = rest.ptr, .len = n };
            }
            continue;
        }
        if (!line_starts(line, "timestamp:", &rest) || block != stream) continue;

        double stamp = 0.0;
        if (!parse_stamp(rest, &stamp)) continue;
        u8str_t after = rest, pos;
        while (after.len > 0 && !line_starts(after, "filepos:", &pos)) { after.ptr++; after.len--; }
        if (after.len == 0) continue;
        size_t filepos = 0, i = 0;
        while (i < pos.len && hex_digit(pos.ptr[i]) >= 0) filepos = (filepos << 4) | (size_t)hex_digit(pos.ptr[i++]);
        entries[count++] = (rubraview_vobsub_entry_t){ .start_seconds = stamp + delay, .filepos = filepos };
    }

    track.entries = entries;
    track.count = count;
    return track;
}

bool rubraview_vobsub_decode(const rubraview_vobsub_track_t *track, size_t index,
                             const uint8_t *sub_bytes, size_t sub_size,
                             uint8_t *pixels, rubraview_vobsub_cue_t *out_cue) {
    if (!track || !sub_bytes || !pixels || !out_cue || index >= track->count) return false;
    size_t filepos = track->entries[index].filepos;
    if (filepos >= sub_size) return false;

    reader_t reader = { .data = sub_bytes, .size = sub_size, .at = 0 };
    static uint8_t spu[64 * 1024];   /* one subpicture; the format caps it at 64 KB */
    double pts = -1.0;
    size_t spu_size = read_spu(&reader, filepos, spu, sizeof(spu), &pts);
    if (spu_size < 4) return false;
    size_t control = be16(spu + 2);
    if (control + 4 > spu_size) return false;

    double show = 0.0, hide = -1.0;
    bool forced = false;
    int32_t x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    uint8_t palette_index[4] = { 0, 1, 2, 3 }, alpha[4] = { 0, 15, 15, 15 };
    size_t field_offset[2] = { 4, 4 };
    if (!read_controls(spu, spu_size, control, &show, &hide, &forced,
                       &x1, &x2, &y1, &y2, palette_index, alpha, field_offset)) {
        return false;
    }
    int32_t width = x2 - x1 + 1, height = y2 - y1 + 1;
    if (width <= 0 || height <= 0) return false;
    if ((size_t)width * (size_t)height > RUBRAVIEW_VOBSUB_MAX_PIXELS) return false;

    memset(pixels, 0, (size_t)width * (size_t)height);
    decode_field(spu, spu_size, field_offset[0], width, height, 0, pixels);
    decode_field(spu, spu_size, field_offset[1], width, height, 1, pixels);

    double start = track->entries[index].start_seconds;
    rubraview_vobsub_cue_t cue = {
        .start_seconds = start + show,
        .end_seconds = start + (hide > show ? hide : show + 5.0),
        .x = x1, .y = y1, .width = width, .height = height,
        .indices = pixels,
        .forced = forced,
    };
    for (int i = 0; i < 4; ++i) {
        uint32_t rgb = track->palette[palette_index[i] & 0x0F] & 0x00FFFFFFu;
        cue.colors[i] = ((uint32_t)(alpha[i] * 17u) << 24) | rgb;
    }
    *out_cue = cue;
    return true;
}

int32_t rubraview_vobsub_at(const rubraview_vobsub_track_t *track, double seconds) {
    if (!track || track->count == 0) return -1;
    for (size_t i = 0; i < track->count; ++i) {
        double start = track->entries[i].start_seconds;
        if (seconds < start) break;
        double end = i + 1 < track->count ? track->entries[i + 1].start_seconds : start + 10.0;
        if (seconds < end) return (int32_t)i;
    }
    return -1;
}

void rubraview_vobsub_pixels(const rubraview_vobsub_cue_t *cue, uint32_t *out) {
    if (!cue || !out || !cue->indices) return;
    uint32_t premultiplied[4];
    for (int i = 0; i < 4; ++i) {
        uint32_t argb = cue->colors[i];
        uint32_t a = argb >> 24;
        uint32_t r = ((argb >> 16) & 0xFF) * a / 255;
        uint32_t g = ((argb >> 8) & 0xFF) * a / 255;
        uint32_t b = (argb & 0xFF) * a / 255;
        premultiplied[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
    size_t n = (size_t)cue->width * (size_t)cue->height;
    for (size_t i = 0; i < n; ++i) out[i] = premultiplied[cue->indices[i] & 3];
}
