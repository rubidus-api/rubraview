#include "rubraview/playback.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* ---- §5.5 A-B looping ---- */

rubraview_ab_loop_t rubraview_ab_create(void) {
    return (rubraview_ab_loop_t){ .point_a = -1.0, .point_b = -1.0, .active = false };
}

/* Both points set and in the right order is the only state that loops. */
static void refresh(rubraview_ab_loop_t *loop) {
    loop->active = loop->point_a >= 0.0 && loop->point_b > loop->point_a;
}

void rubraview_ab_set_a(rubraview_ab_loop_t *loop, double seconds) {
    if (!loop || seconds < 0.0) return;
    loop->point_a = seconds;

    /* Setting A past B means the two were set the other way round.
       Swapping is kinder than refusing: the region between them is
       plainly what was meant. */
    if (loop->point_b >= 0.0 && loop->point_b < loop->point_a) {
        double swap = loop->point_a;
        loop->point_a = loop->point_b;
        loop->point_b = swap;
    }
    refresh(loop);
}

void rubraview_ab_set_b(rubraview_ab_loop_t *loop, double seconds) {
    if (!loop || seconds < 0.0) return;
    loop->point_b = seconds;
    if (loop->point_a >= 0.0 && loop->point_b < loop->point_a) {
        double swap = loop->point_a;
        loop->point_a = loop->point_b;
        loop->point_b = swap;
    }
    refresh(loop);
}

void rubraview_ab_clear(rubraview_ab_loop_t *loop) {
    if (!loop) return;
    *loop = rubraview_ab_create();
}

double rubraview_ab_wrap(const rubraview_ab_loop_t *loop, double position_seconds) {
    if (!loop || !loop->active) return -1.0;

    /* Past the end of the loop, or before its start — which happens when
       the reader seeks away and then the loop should pull them back. */
    if (position_seconds >= loop->point_b) return loop->point_a;
    if (position_seconds < loop->point_a) return loop->point_a;
    return -1.0;
}

bool rubraview_ab_bar_region(const rubraview_ab_loop_t *loop, double duration_seconds,
                             double *out_start_fraction, double *out_end_fraction) {
    if (!loop || !loop->active || duration_seconds <= 0.0) return false;
    if (out_start_fraction) *out_start_fraction = clampd(loop->point_a / duration_seconds, 0.0, 1.0);
    if (out_end_fraction) *out_end_fraction = clampd(loop->point_b / duration_seconds, 0.0, 1.0);
    return true;
}

/* ---- §5.3 seeking ---- */

double rubraview_seek_target(double position_seconds, double duration_seconds,
                             const rubraview_ab_loop_t *loop,
                             rubraview_seek_kind_t kind, double amount,
                             double frame_rate) {
    double target = position_seconds;

    switch (kind) {
        case RUBRAVIEW_SEEK_ABSOLUTE:
            target = amount;
            break;
        case RUBRAVIEW_SEEK_FRAME: {
            /* A file that does not state its rate still steps: a
               twenty-fifth of a second is close enough to be useful and
               never zero, which is what would freeze the step key. */
            double rate = frame_rate > 0.0 ? frame_rate : 25.0;
            target = position_seconds + amount / rate;
            break;
        }
        case RUBRAVIEW_SEEK_RELATIVE:
        default:
            target = position_seconds + amount;
            break;
    }

    if (duration_seconds > 0.0) target = clampd(target, 0.0, duration_seconds);
    else if (target < 0.0) target = 0.0;

    /* With a loop set, seeking stays inside it. Letting a seek leave the
       loop would cancel a thing the reader deliberately set up, without
       ever saying so. */
    if (loop && loop->active) target = clampd(target, loop->point_a, loop->point_b);

    return target;
}

double rubraview_seekbar_time(double bar_x, double bar_width, double click_x, double duration_seconds) {
    if (bar_width <= 0.0 || duration_seconds <= 0.0) return 0.0;
    double fraction = clampd((click_x - bar_x) / bar_width, 0.0, 1.0);
    return fraction * duration_seconds;
}

double rubraview_seekbar_fraction(double position_seconds, double duration_seconds) {
    if (duration_seconds <= 0.0) return 0.0;
    return clampd(position_seconds / duration_seconds, 0.0, 1.0);
}

u8str_t rubraview_format_timecode(char *buffer, size_t buffer_size, double seconds, bool with_milliseconds) {
    u8str_t empty = { .ptr = "", .len = 0 };
    if (!buffer || buffer_size < 16) return empty;

    if (seconds < 0.0) seconds = 0.0;

    long long total_ms = (long long)(seconds * 1000.0 + 0.5);
    long long ms = total_ms % 1000;
    long long total = total_ms / 1000;
    long long s = total % 60;
    long long m = (total / 60) % 60;
    long long h = total / 3600;

    int written;
    if (h > 0) {
        written = with_milliseconds
            ? snprintf(buffer, buffer_size, "%lld:%02lld:%02lld.%03lld", h, m, s, ms)
            : snprintf(buffer, buffer_size, "%lld:%02lld:%02lld", h, m, s);
    } else {
        /* Under an hour the hours field is noise: `01:14.200` reads
           faster than `0:01:14.200`. */
        written = with_milliseconds
            ? snprintf(buffer, buffer_size, "%02lld:%02lld.%03lld", m, s, ms)
            : snprintf(buffer, buffer_size, "%02lld:%02lld", m, s);
    }

    if (written <= 0) return empty;
    return (u8str_t){ .ptr = buffer, .len = (size_t)written };
}

bool rubraview_parse_timecode(u8str_t text, double *out_seconds) {
    if (!out_seconds || text.len == 0) return false;

    /* Up to three parts divided by colons — seconds, or minutes and
       seconds, or hours, minutes and seconds — and the last of them may
       carry a fraction. */
    double part[3] = { 0.0, 0.0, 0.0 };
    size_t count = 0;
    size_t i = 0;
    while (i < text.len && (text.ptr[i] == ' ' || text.ptr[i] == '\t')) i++;

    bool any_digit = false;
    while (i <= text.len) {
        double whole = 0.0;
        bool digits = false;
        while (i < text.len && text.ptr[i] >= '0' && text.ptr[i] <= '9') {
            whole = whole * 10.0 + (double)(text.ptr[i] - '0');
            digits = true;
            any_digit = true;
            i++;
        }
        if (i < text.len && (text.ptr[i] == '.' || text.ptr[i] == ',')) {
            i++;
            double scale = 0.1;
            while (i < text.len && text.ptr[i] >= '0' && text.ptr[i] <= '9') {
                whole += (double)(text.ptr[i] - '0') * scale;
                scale *= 0.1;
                digits = true;
                any_digit = true;
                i++;
            }
        }
        if (!digits) return false;
        if (count >= 3) return false;
        part[count++] = whole;
        if (i < text.len && text.ptr[i] == ':') { i++; continue; }
        break;
    }
    while (i < text.len && (text.ptr[i] == ' ' || text.ptr[i] == '\t')) i++;
    if (i != text.len || !any_digit) return false;   /* something else is in there */

    double seconds = 0.0;
    for (size_t k = 0; k < count; ++k) {
        seconds = seconds * 60.0 + part[k];
    }
    if (seconds < 0.0) seconds = 0.0;
    *out_seconds = seconds;
    return true;
}

/* ---- §3.16.2 tracks ---- */

rubraview_track_set_t rubraview_tracks_create(void) {
    rubraview_track_set_t set = {0};
    set.current_video = -1;
    set.current_audio = -1;
    set.current_subtitle = -1;
    return set;
}

bool rubraview_tracks_add(rubraview_track_set_t *set, rubraview_track_t track) {
    if (!set || set->count >= RUBRAVIEW_MAX_TRACKS) return false;
    set->tracks[set->count++] = track;
    return true;
}

static bool language_matches(u8str_t a, u8str_t b) {
    if (a.len == 0 || b.len == 0) return false;

    /* Containers write "kor", "ko", "kor-KR" for the same thing, so the
       shorter tag is compared as a prefix of the longer. */
    size_t n = a.len < b.len ? a.len : b.len;
    for (size_t i = 0; i < n; ++i) {
        char x = a.ptr[i], y = b.ptr[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

int32_t rubraview_tracks_choose(const rubraview_track_set_t *set, rubraview_track_kind_t kind,
                                u8str_t preferred_language) {
    if (!set) return -1;

    int32_t by_language = -1, by_default = -1, first = -1;

    for (size_t i = 0; i < set->count; ++i) {
        const rubraview_track_t *track = &set->tracks[i];
        if (track->kind != kind) continue;

        /* A forced subtitle track captions the signs in a film being
           watched in its own language. It is not the track someone
           asking for subtitles wants. */
        if (kind == RUBRAVIEW_TRACK_SUBTITLE && track->is_forced) continue;

        if (first < 0) first = (int32_t)i;
        if (by_default < 0 && track->is_default) by_default = (int32_t)i;
        if (by_language < 0 && language_matches(track->language, preferred_language)) {
            by_language = (int32_t)i;
        }
    }

    if (by_language >= 0) return by_language;
    if (by_default >= 0) return by_default;
    return first;
}

size_t rubraview_tracks_count(const rubraview_track_set_t *set, rubraview_track_kind_t kind) {
    if (!set) return 0;
    size_t count = 0;
    for (size_t i = 0; i < set->count; ++i) {
        if (set->tracks[i].kind == kind) count++;
    }
    return count;
}

int32_t rubraview_tracks_next(const rubraview_track_set_t *set, rubraview_track_kind_t kind,
                              int32_t current) {
    if (!set || set->count == 0) return -1;

    /* Walk forward, past the end of the list, to the *next* track of
       this kind. Starting from -1 finds the first one. */
    int32_t found = -1;
    for (int32_t i = current + 1; i < (int32_t)set->count; ++i) {
        if (set->tracks[i].kind == kind) { found = i; break; }
    }

    if (found >= 0) return found;

    /* Past the last one. Subtitles turn off here — that is what a reader
       usually reaches for next, and a cycle that cannot do it sends them
       hunting for a menu. Audio wraps to the first instead: silence is
       not one of its options. */
    if (kind == RUBRAVIEW_TRACK_SUBTITLE) return -1;

    for (int32_t i = 0; i < (int32_t)set->count; ++i) {
        if (set->tracks[i].kind == kind) return i;
    }
    return current;
}

u8str_t rubraview_track_label(char *buffer, size_t buffer_size,
                              const rubraview_track_set_t *set, int32_t index) {
    u8str_t empty = { .ptr = "", .len = 0 };
    if (!buffer || buffer_size < 8) return empty;

    if (!set || index < 0 || (size_t)index >= set->count) {
        int written = snprintf(buffer, buffer_size, "Off");
        return written > 0 ? (u8str_t){ .ptr = buffer, .len = (size_t)written } : empty;
    }

    const rubraview_track_t *track = &set->tracks[index];

    /* Which number the reader sees is its position among tracks of its
       own kind, not its stream index: "#2 of the audio tracks" is what a
       menu means, and a container's stream numbering counts video too. */
    int32_t ordinal = 0;
    for (size_t i = 0; i <= (size_t)index && i < set->count; ++i) {
        if (set->tracks[i].kind == track->kind) ordinal++;
    }

    char language[32];
    size_t n = track->language.len < sizeof(language) - 1 ? track->language.len : sizeof(language) - 1;
    if (n > 0) memcpy(language, track->language.ptr, n);
    language[n] = '\0';

    char codec[32];
    size_t c = track->codec.len < sizeof(codec) - 1 ? track->codec.len : sizeof(codec) - 1;
    if (c > 0) memcpy(codec, track->codec.ptr, c);
    codec[c] = '\0';

    int written;
    if (track->kind == RUBRAVIEW_TRACK_AUDIO && track->channels > 0) {
        written = snprintf(buffer, buffer_size, "#%d %s (%s %d.%d)", ordinal,
                           n > 0 ? language : "unlabelled", c > 0 ? codec : "?",
                           track->channels > 2 ? track->channels - 1 : track->channels,
                           track->channels > 2 ? 1 : 0);
    } else {
        written = snprintf(buffer, buffer_size, "#%d %s (%s)", ordinal,
                           n > 0 ? language : "unlabelled", c > 0 ? codec : "?");
    }

    if (written <= 0) return empty;
    if ((size_t)written >= buffer_size) written = (int)buffer_size - 1;
    return (u8str_t){ .ptr = buffer, .len = (size_t)written };
}
