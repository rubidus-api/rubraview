#include "rubraview/lyrics.h"
#include "rubraview/number.h"
#include <string.h>
#include <stdlib.h>

typedef struct line_reader {
    const char *ptr;
    size_t len, pos;
} line_reader_t;

static proven_u8str_view_t read_line(line_reader_t *r) {
    size_t start = r->pos;
    while (r->pos < r->len && r->ptr[r->pos] != '\n') r->pos++;

    size_t end = r->pos;
    if (end > start && r->ptr[end - 1] == '\r') end--;
    if (r->pos < r->len) r->pos++;

    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)r->ptr + start, .size = end - start };
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
static bool parse_lrc_stamp(proven_u8str_view_t s, double *out) {
    int64_t minutes = 0, seconds = 0;
    double fraction = 0.0;
    size_t i = 0;

    while (i < s.size && s.ptr[i] >= '0' && s.ptr[i] <= '9') {
        minutes = rubraview_digit_append(minutes, s.ptr[i++]);
    }
    if (i == 0 || i >= s.size || s.ptr[i] != ':') return false;
    i++;

    size_t seconds_start = i;
    while (i < s.size && s.ptr[i] >= '0' && s.ptr[i] <= '9') {
        seconds = rubraview_digit_append(seconds, s.ptr[i++]);
    }
    if (i == seconds_start) return false;

    if (i < s.size && (s.ptr[i] == '.' || s.ptr[i] == ':')) {
        i++;
        double scale = 1.0;
        double value = 0.0;
        while (i < s.size && s.ptr[i] >= '0' && s.ptr[i] <= '9') {
            value = value * 10.0 + (s.ptr[i++] - '0');
            scale *= 10.0;
        }
        if (scale > 1.0) fraction = value / scale;
    }
    if (i != s.size) return false;

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

rubraview_lyrics_t rubraview_lyrics_parse(proven_arena_t *arena, proven_u8str_view_t text) {
    rubraview_lyrics_t lyrics = {0};
    if (!arena || text.size == 0) return lyrics;

    line_reader_t r = { .ptr = (const char *)text.ptr, .len = text.size, .pos = 0 };
    lyric_buf_t lines = {0};

    while (r.pos < r.len) {
        proven_u8str_view_t line = proven_u8str_view_trim(read_line(&r));
        if (line.size == 0) continue;

        /* A line may carry several timestamps: a chorus is written once
           and pointed at from each place it occurs. */
        double stamps[16];
        size_t stamp_count = 0;
        size_t i = 0;

        while (i < line.size && line.ptr[i] == '[' && stamp_count < 16) {
            size_t close = i + 1;
            while (close < line.size && line.ptr[close] != ']') close++;
            if (close >= line.size) break;

            proven_u8str_view_t inside = { .ptr = line.ptr + i + 1, .size = close - i - 1 };
            double when = 0.0;

            if (parse_lrc_stamp(inside, &when)) {
                stamps[stamp_count++] = when;
            } else {
                /* Not a time: one of the metadata tags. */
                if (proven_u8str_view_starts_with_ascii_ci(inside, PROVEN_LIT("ti:"))) {
                    lyrics.title = proven_u8str_view_trim((proven_u8str_view_t){ .ptr = inside.ptr + 3, .size = inside.size - 3 });
                } else if (proven_u8str_view_starts_with_ascii_ci(inside, PROVEN_LIT("ar:"))) {
                    lyrics.artist = proven_u8str_view_trim((proven_u8str_view_t){ .ptr = inside.ptr + 3, .size = inside.size - 3 });
                } else if (proven_u8str_view_starts_with_ascii_ci(inside, PROVEN_LIT("al:"))) {
                    lyrics.album = proven_u8str_view_trim((proven_u8str_view_t){ .ptr = inside.ptr + 3, .size = inside.size - 3 });
                } else if (proven_u8str_view_starts_with_ascii_ci(inside, PROVEN_LIT("offset:"))) {
                    /* The tag is in milliseconds, and positive means
                       the words come *earlier*. */
                    double ms = 0.0;
                    proven_u8str_view_t value = { .ptr = inside.ptr + 7, .size = inside.size - 7 };
                    if (rubraview_parse_double_prefix(value, &ms, NULL)) lyrics.offset_seconds = -ms / 1000.0;
                }
            }
            i = close + 1;
        }

        if (stamp_count == 0) continue;

        proven_u8str_view_t body = proven_u8str_view_trim((proven_u8str_view_t){ .ptr = line.ptr + i, .size = line.size - i });
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

rubraview_lyrics_t rubraview_lyrics_from_text(proven_arena_t *arena, proven_u8str_view_t text) {
    rubraview_lyrics_t lyrics = rubraview_lyrics_parse(arena, text);
    if (lyrics.count > 0 || !arena) return lyrics;
    lyrics = (rubraview_lyrics_t){ .untimed = true };
    line_reader_t r = { .ptr = (const char *)text.ptr, .len = text.size, .pos = 0 };
    lyric_buf_t lines = {0};
    while (r.pos < r.len) {
        proven_u8str_view_t line = proven_u8str_view_trim(read_line(&r));
        if (line.size == 0) continue;
        if (!lyric_push(arena, &lines, (rubraview_lyric_line_t){ .time_seconds = -1.0, .text = line })) break;
    }
    lyrics.lines = lines.data;
    lyrics.count = lines.count;
    return lyrics;
}

rubraview_lyrics_t rubraview_lyrics_from_lines(proven_arena_t *arena, const rubraview_lyric_line_t *lines, size_t count) {
    rubraview_lyrics_t lyrics = {0};
    if (!arena || !lines || count == 0) return lyrics;
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, count, sizeof(rubraview_lyric_line_t));
    if (!proven_is_ok(res.err)) return lyrics;
    lyrics.lines = (rubraview_lyric_line_t*)(void*)res.value.ptr;
    memcpy(lyrics.lines, lines, count * sizeof(rubraview_lyric_line_t));
    lyrics.count = count;
    sort_lyrics(lyrics.lines, count);
    return lyrics;
}

int32_t rubraview_lyrics_untimed_index(const rubraview_lyrics_t *lyrics, double time_seconds, double duration_seconds) {
    if (!lyrics || lyrics->count == 0 || !(duration_seconds > 0.0) || time_seconds < 0.0) return -1;
    double at = time_seconds / duration_seconds;
    if (at >= 1.0) return (int32_t)lyrics->count - 1;
    return (int32_t)(at * (double)lyrics->count);
}

int32_t rubraview_lyrics_index_at(const rubraview_lyrics_t *lyrics, double time_seconds) {
    if (!lyrics || lyrics->count == 0 || lyrics->untimed) return -1;

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
    if (!lyrics || lyrics->untimed || index < 0 || (size_t)index >= lyrics->count) return -1.0;
    return lyrics->lines[index].time_seconds + lyrics->offset_seconds;
}

/* ---- .cue ---- */

/* A quoted field, or the rest of the line when it is not quoted. */
static proven_u8str_view_t quoted_or_rest(proven_u8str_view_t s) {
    s = proven_u8str_view_trim(s);
    if (s.size >= 2 && s.ptr[0] == '"') {
        size_t close = 1;
        while (close < s.size && s.ptr[close] != '"') close++;
        return (proven_u8str_view_t){ .ptr = s.ptr + 1, .size = close - 1 };
    }
    return s;
}

/*
 * `mm:ss:ff`, where the third field is *frames*: 75 to a second, because
 * the format was written for audio CDs. Reading it as hundredths — which
 * is the obvious thing to do and what most naive parsers do — puts every
 * track boundary in the wrong place.
 */
static bool parse_cue_time(proven_u8str_view_t s, double *out) {
    s = proven_u8str_view_trim(s);
    int64_t parts[3] = {0, 0, 0};
    size_t part = 0;
    int64_t value = 0;
    bool any = false;

    for (size_t i = 0; i <= s.size; ++i) {
        char c = i < s.size ? s.ptr[i] : ':';
        if (c >= '0' && c <= '9') { value = rubraview_digit_append(value, c); any = true; continue; }
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

rubraview_cue_sheet_t rubraview_cue_parse(proven_arena_t *arena, proven_u8str_view_t text, double total_seconds) {
    rubraview_cue_sheet_t sheet = {0};
    if (!arena || text.size == 0) return sheet;

    /* Two passes: count the tracks, then fill them. A cue sheet is small
       and this avoids a growing buffer for no benefit. */
    size_t track_count = 0;
    {
        line_reader_t r = { .ptr = (const char *)text.ptr, .len = text.size, .pos = 0 };
        while (r.pos < r.len) {
            proven_u8str_view_t line = proven_u8str_view_trim(read_line(&r));
            if (proven_u8str_view_starts_with_ascii_ci(line, PROVEN_LIT("track "))) track_count++;
        }
    }
    if (track_count == 0) return sheet;

    proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, track_count, sizeof(rubraview_cue_track_t));
    if (!proven_is_ok(res.err)) return sheet;
    rubraview_cue_track_t *tracks = (rubraview_cue_track_t*)(void*)res.value.ptr;
    memset(tracks, 0, track_count * sizeof(*tracks));

    line_reader_t r = { .ptr = (const char *)text.ptr, .len = text.size, .pos = 0 };
    size_t current = 0;
    bool in_track = false;

    while (r.pos < r.len) {
        proven_u8str_view_t line = proven_u8str_view_trim(read_line(&r));
        if (line.size == 0) continue;

        if (proven_u8str_view_starts_with_ascii_ci(line, PROVEN_LIT("file "))) {
            proven_u8str_view_t rest = proven_u8str_view_trim((proven_u8str_view_t){ .ptr = line.ptr + 5, .size = line.size - 5 });
            sheet.audio_file = quoted_or_rest(rest);
            /* Unquoted, the file's type (WAVE, MP3, ...) follows the name. */
            if (rest.size > 0 && rest.ptr[0] != '"') {
                size_t cut = sheet.audio_file.size;
                while (cut > 0 && sheet.audio_file.ptr[cut - 1] != ' ') cut--;
                if (cut > 1) sheet.audio_file.size = cut - 1;
            }
            continue;
        }

        if (proven_u8str_view_starts_with_ascii_ci(line, PROVEN_LIT("track "))) {
            in_track = true;
            if (current >= track_count) break;

            proven_u8str_view_t rest = proven_u8str_view_trim((proven_u8str_view_t){ .ptr = line.ptr + 6, .size = line.size - 6 });
            int32_t number = 0;
            for (size_t i = 0; i < rest.size && rest.ptr[i] >= '0' && rest.ptr[i] <= '9'; ++i) {
                number = rubraview_digit_append_i32(number, rest.ptr[i]);
            }
            tracks[current].number = number;
            tracks[current].start_seconds = -1.0;
            current++;
            continue;
        }

        if (proven_u8str_view_starts_with_ascii_ci(line, PROVEN_LIT("title "))) {
            proven_u8str_view_t value = quoted_or_rest((proven_u8str_view_t){ .ptr = line.ptr + 6, .size = line.size - 6 });
            /* Before the first TRACK, TITLE names the album. */
            if (in_track && current > 0) tracks[current - 1].title = value;
            else sheet.album_title = value;
            continue;
        }

        if (proven_u8str_view_starts_with_ascii_ci(line, PROVEN_LIT("performer "))) {
            proven_u8str_view_t value = quoted_or_rest((proven_u8str_view_t){ .ptr = line.ptr + 10, .size = line.size - 10 });
            if (in_track && current > 0) tracks[current - 1].performer = value;
            else sheet.album_performer = value;
            continue;
        }

        if (proven_u8str_view_starts_with_ascii_ci(line, PROVEN_LIT("index ")) && in_track && current > 0) {
            proven_u8str_view_t rest = proven_u8str_view_trim((proven_u8str_view_t){ .ptr = line.ptr + 6, .size = line.size - 6 });

            int64_t index_number = 0;
            size_t i = 0;
            while (i < rest.size && rest.ptr[i] >= '0' && rest.ptr[i] <= '9') {
                index_number = rubraview_digit_append(index_number, rest.ptr[i++]);
            }

            /* INDEX 00 is the pre-gap — the silence before the song.
               INDEX 01 is where the music starts, and that is where a
               listener asking for track 5 expects to land. */
            if (index_number != 1) continue;

            double when = 0.0;
            if (parse_cue_time((proven_u8str_view_t){ .ptr = rest.ptr + i, .size = rest.size - i }, &when)) {
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

/* The name after the last folder separator, either kind. */
static proven_u8str_view_t cue_basename(proven_u8str_view_t s) {
    for (size_t i = s.size; i > 0; --i) {
        if (s.ptr[i - 1] == '/' || s.ptr[i - 1] == '\\') return (proven_u8str_view_t){ .ptr = s.ptr + i, .size = s.size - i };
    }
    return s;
}

static size_t cue_stem_len(proven_u8str_view_t s) {
    for (size_t i = s.size; i > 0; --i) if (s.ptr[i - 1] == '.') return i - 1;
    return s.size;
}

static bool cue_same_ci(const char *a, const char *b, size_t n) {
    return proven_u8str_view_eq_ascii_ci(proven_u8str_view_from_bytes(a, n), proven_u8str_view_from_bytes(b, n));
}

bool rubraview_cue_names_audio(const rubraview_cue_sheet_t *sheet, proven_u8str_view_t audio_name) {
    if (!sheet || sheet->audio_file.size == 0 || audio_name.size == 0) return false;
    proven_u8str_view_t named = cue_basename(sheet->audio_file), have = cue_basename(audio_name);
    if (named.size == have.size && cue_same_ci((const char *)named.ptr, (const char *)have.ptr, have.size)) return true;
    size_t a = cue_stem_len(named), b = cue_stem_len(have);
    return a > 0 && a == b && cue_same_ci((const char *)named.ptr, (const char *)have.ptr, a);
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
