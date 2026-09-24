#include "rubraview/lyrics.h"
#include <string.h>
#include <stdlib.h>

typedef struct line_reader {
    const char *ptr;
    size_t len, pos;
} line_reader_t;

static u8str_t read_line(line_reader_t *r) {
    size_t start = r->pos;
    while (r->pos < r->len && r->ptr[r->pos] != '\n') r->pos++;

    size_t end = r->pos;
    if (end > start && r->ptr[end - 1] == '\r') end--;
    if (r->pos < r->len) r->pos++;

    return (u8str_t){ .ptr = r->ptr + start, .len = end - start };
}

static u8str_t trim(u8str_t s) {
    while (s.len > 0 && (s.ptr[0] == ' ' || s.ptr[0] == '\t')) { s.ptr++; s.len--; }
    while (s.len > 0 && (s.ptr[s.len - 1] == ' ' || s.ptr[s.len - 1] == '\t')) s.len--;
    return s;
}

/* ---- .lrc ---- */

typedef struct lyric_buf {
    rubraview_lyric_line_t *data;
    size_t count, capacity;
} lyric_buf_t;

static bool lyric_push(proven_arena_t *arena, lyric_buf_t *b, rubraview_lyric_line_t line) {
    if (b->count >= b->capacity) {
        size_t cap = b->capacity == 0 ? 64 : b->capacity * 2;
        proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, cap, sizeof(*b->data));
        if (!proven_is_ok(res.err)) return false;
        rubraview_lyric_line_t *data = (rubraview_lyric_line_t*)(void*)res.value.ptr;
        if (b->data && b->count > 0) memcpy(data, b->data, b->count * sizeof(*b->data));
        b->data = data;
        b->capacity = cap;
    }
    b->data[b->count++] = line;
    return true;
}

/* `[mm:ss.xx]` — and the fraction may be two digits (hundredths) or
   three (milliseconds), which different tools write differently. */
static bool parse_lrc_stamp(u8str_t s, double *out) {
    long minutes = 0, seconds = 0;
    double fraction = 0.0;
    size_t i = 0;

    while (i < s.len && s.ptr[i] >= '0' && s.ptr[i] <= '9') {
        minutes = minutes * 10 + (s.ptr[i++] - '0');
    }
    if (i == 0 || i >= s.len || s.ptr[i] != ':') return false;
    i++;

    size_t seconds_start = i;
    while (i < s.len && s.ptr[i] >= '0' && s.ptr[i] <= '9') {
        seconds = seconds * 10 + (s.ptr[i++] - '0');
    }
    if (i == seconds_start) return false;

    if (i < s.len && (s.ptr[i] == '.' || s.ptr[i] == ':')) {
        i++;
        double scale = 1.0;
        double value = 0.0;
        while (i < s.len && s.ptr[i] >= '0' && s.ptr[i] <= '9') {
            value = value * 10.0 + (s.ptr[i++] - '0');
            scale *= 10.0;
        }
        if (scale > 1.0) fraction = value / scale;
    }
    if (i != s.len) return false;

    *out = (double)minutes * 60.0 + (double)seconds + fraction;
    return true;
}

static void sort_lyrics(rubraview_lyric_line_t *lines, size_t count) {
    for (size_t i = 1; i < count; ++i) {
        rubraview_lyric_line_t key = lines[i];
        size_t j = i;
        while (j > 0 && lines[j - 1].time_seconds > key.time_seconds) {
            lines[j] = lines[j - 1];
            j--;
        }
        lines[j] = key;
    }
}

rubraview_lyrics_t rubraview_lyrics_parse(proven_arena_t *arena, u8str_t text) {
    rubraview_lyrics_t lyrics = {0};
    if (!arena || text.len == 0) return lyrics;

    line_reader_t r = { .ptr = text.ptr, .len = text.len, .pos = 0 };
    lyric_buf_t lines = {0};

    while (r.pos < r.len) {
        u8str_t line = trim(read_line(&r));
        if (line.len == 0) continue;

        /* A line may carry several timestamps: a chorus is written once
           and pointed at from each place it occurs. */
        double stamps[16];
        size_t stamp_count = 0;
        size_t i = 0;

        while (i < line.len && line.ptr[i] == '[' && stamp_count < 16) {
            size_t close = i + 1;
            while (close < line.len && line.ptr[close] != ']') close++;
            if (close >= line.len) break;

            u8str_t inside = { .ptr = line.ptr + i + 1, .len = close - i - 1 };
            double when = 0.0;

            if (parse_lrc_stamp(inside, &when)) {
                stamps[stamp_count++] = when;
            } else {
                /* Not a time: one of the metadata tags. */
                if (rubraview_u8_starts_with_ci(inside, "ti:")) {
                    lyrics.title = trim((u8str_t){ .ptr = inside.ptr + 3, .len = inside.len - 3 });
                } else if (rubraview_u8_starts_with_ci(inside, "ar:")) {
                    lyrics.artist = trim((u8str_t){ .ptr = inside.ptr + 3, .len = inside.len - 3 });
                } else if (rubraview_u8_starts_with_ci(inside, "al:")) {
                    lyrics.album = trim((u8str_t){ .ptr = inside.ptr + 3, .len = inside.len - 3 });
                } else if (rubraview_u8_starts_with_ci(inside, "offset:")) {
                    char buffer[32];
                    size_t n = inside.len - 7;
                    if (n < sizeof(buffer)) {
                        memcpy(buffer, inside.ptr + 7, n);
                        buffer[n] = '\0';
                        /* The tag is in milliseconds, and positive means
                           the words come *earlier*. */
                        lyrics.offset_seconds = -strtod(buffer, NULL) / 1000.0;
                    }
                }
            }
            i = close + 1;
        }

        if (stamp_count == 0) continue;

        u8str_t body = trim((u8str_t){ .ptr = line.ptr + i, .len = line.len - i });
        for (size_t k = 0; k < stamp_count; ++k) {
            lyric_push(arena, &lines, (rubraview_lyric_line_t){
                .time_seconds = stamps[k], .text = body });
        }
    }

    sort_lyrics(lines.data, lines.count);
    lyrics.lines = lines.data;
    lyrics.count = lines.count;
    return lyrics;
}

int32_t rubraview_lyrics_index_at(const rubraview_lyrics_t *lyrics, double time_seconds) {
    if (!lyrics || lyrics->count == 0) return -1;

    double t = time_seconds - lyrics->offset_seconds;
    if (t < lyrics->lines[0].time_seconds) return -1;

    size_t low = 0, high = lyrics->count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (lyrics->lines[mid].time_seconds <= t) low = mid + 1;
        else high = mid;
    }
    return (int32_t)low - 1;
}

double rubraview_lyrics_time_of(const rubraview_lyrics_t *lyrics, int32_t index) {
    if (!lyrics || index < 0 || (size_t)index >= lyrics->count) return -1.0;
    return lyrics->lines[index].time_seconds + lyrics->offset_seconds;
}

/* ---- .cue ---- */

/* A quoted field, or the rest of the line when it is not quoted. */
static u8str_t quoted_or_rest(u8str_t s) {
    s = trim(s);
    if (s.len >= 2 && s.ptr[0] == '"') {
        size_t close = 1;
        while (close < s.len && s.ptr[close] != '"') close++;
        return (u8str_t){ .ptr = s.ptr + 1, .len = close - 1 };
    }
    return s;
}

/*
 * `mm:ss:ff`, where the third field is *frames*: 75 to a second, because
 * the format was written for audio CDs. Reading it as hundredths — which
 * is the obvious thing to do and what most naive parsers do — puts every
 * track boundary in the wrong place.
 */
static bool parse_cue_time(u8str_t s, double *out) {
    s = trim(s);
    long parts[3] = {0, 0, 0};
    size_t part = 0;
    long value = 0;
    bool any = false;

    for (size_t i = 0; i <= s.len; ++i) {
        char c = i < s.len ? s.ptr[i] : ':';
        if (c >= '0' && c <= '9') { value = value * 10 + (c - '0'); any = true; continue; }
        if (c != ':') return false;
        if (!any || part >= 3) return false;
        parts[part++] = value;
        value = 0;
        any = false;
    }

    if (part != 3) return false;
    *out = (double)parts[0] * 60.0 + (double)parts[1] + (double)parts[2] / 75.0;
    return true;
}

rubraview_cue_sheet_t rubraview_cue_parse(proven_arena_t *arena, u8str_t text, double total_seconds) {
    rubraview_cue_sheet_t sheet = {0};
    if (!arena || text.len == 0) return sheet;

    /* Two passes: count the tracks, then fill them. A cue sheet is small
       and this avoids a growing buffer for no benefit. */
    size_t track_count = 0;
    {
        line_reader_t r = { .ptr = text.ptr, .len = text.len, .pos = 0 };
        while (r.pos < r.len) {
            u8str_t line = trim(read_line(&r));
            if (rubraview_u8_starts_with_ci(line, "track ")) track_count++;
        }
    }
    if (track_count == 0) return sheet;

    proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, track_count, sizeof(rubraview_cue_track_t));
    if (!proven_is_ok(res.err)) return sheet;
    rubraview_cue_track_t *tracks = (rubraview_cue_track_t*)(void*)res.value.ptr;
    memset(tracks, 0, track_count * sizeof(*tracks));

    line_reader_t r = { .ptr = text.ptr, .len = text.len, .pos = 0 };
    size_t current = 0;
    bool in_track = false;

    while (r.pos < r.len) {
        u8str_t line = trim(read_line(&r));
        if (line.len == 0) continue;

        if (rubraview_u8_starts_with_ci(line, "file ")) {
            sheet.audio_file = quoted_or_rest((u8str_t){ .ptr = line.ptr + 5, .len = line.len - 5 });
            continue;
        }

        if (rubraview_u8_starts_with_ci(line, "track ")) {
            in_track = true;
            if (current >= track_count) break;

            u8str_t rest = trim((u8str_t){ .ptr = line.ptr + 6, .len = line.len - 6 });
            long number = 0;
            for (size_t i = 0; i < rest.len && rest.ptr[i] >= '0' && rest.ptr[i] <= '9'; ++i) {
                number = number * 10 + (rest.ptr[i] - '0');
            }
            tracks[current].number = (int32_t)number;
            tracks[current].start_seconds = -1.0;
            current++;
            continue;
        }

        if (rubraview_u8_starts_with_ci(line, "title ")) {
            u8str_t value = quoted_or_rest((u8str_t){ .ptr = line.ptr + 6, .len = line.len - 6 });
            /* Before the first TRACK, TITLE names the album. */
            if (in_track && current > 0) tracks[current - 1].title = value;
            else sheet.album_title = value;
            continue;
        }

        if (rubraview_u8_starts_with_ci(line, "performer ")) {
            u8str_t value = quoted_or_rest((u8str_t){ .ptr = line.ptr + 10, .len = line.len - 10 });
            if (in_track && current > 0) tracks[current - 1].performer = value;
            else sheet.album_performer = value;
            continue;
        }

        if (rubraview_u8_starts_with_ci(line, "index ") && in_track && current > 0) {
            u8str_t rest = trim((u8str_t){ .ptr = line.ptr + 6, .len = line.len - 6 });

            long index_number = 0;
            size_t i = 0;
            while (i < rest.len && rest.ptr[i] >= '0' && rest.ptr[i] <= '9') {
                index_number = index_number * 10 + (rest.ptr[i++] - '0');
            }

            /* INDEX 00 is the pre-gap — the silence before the song.
               INDEX 01 is where the music starts, and that is where a
               listener asking for track 5 expects to land. */
            if (index_number != 1) continue;

            double when = 0.0;
            if (parse_cue_time((u8str_t){ .ptr = rest.ptr + i, .len = rest.len - i }, &when)) {
                tracks[current - 1].start_seconds = when;
            }
            continue;
        }
    }

    /* A track ends where the next one starts. */
    for (size_t i = 0; i < current; ++i) {
        if (tracks[i].start_seconds < 0.0) tracks[i].start_seconds = 0.0;
        tracks[i].end_seconds = (i + 1 < current) ? tracks[i + 1].start_seconds : total_seconds;
        if (tracks[i].end_seconds <= tracks[i].start_seconds && total_seconds > 0.0) {
            tracks[i].end_seconds = total_seconds;
        }
    }

    sheet.tracks = tracks;
    sheet.count = current;
    return sheet;
}

int32_t rubraview_cue_track_at(const rubraview_cue_sheet_t *sheet, double time_seconds) {
    if (!sheet || sheet->count == 0) return -1;
    if (time_seconds < sheet->tracks[0].start_seconds) return -1;

    for (size_t i = 0; i < sheet->count; ++i) {
        double end = sheet->tracks[i].end_seconds;
        bool open_ended = end <= sheet->tracks[i].start_seconds;
        if (time_seconds >= sheet->tracks[i].start_seconds && (open_ended || time_seconds < end)) {
            return (int32_t)i;
        }
    }
    return -1;
}
