#include "rubraview/ui_subwin.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static bool cue_listed(const rubraview_subtitle_track_t *track, const rubraview_subtitle_cue_t *cue) {
    if (cue->text.len == 0) return false;
    if (track->shown_language.len == 0) return true;
    return cue->language.len == track->shown_language.len &&
           memcmp(cue->language.ptr, track->shown_language.ptr, cue->language.len) == 0;
}

size_t rubraview_subwin_rows(const rubraview_subtitle_track_t *track, uint32_t *rows, size_t max) {
    if (!track || !rows) return 0;
    size_t n = 0;
    for (size_t i = 0; i < track->count && n < max && i <= UINT32_MAX; ++i) {
        if (cue_listed(track, &track->cues[i])) rows[n++] = (uint32_t)i;
    }
    return n;
}

int32_t rubraview_subwin_row_at(const rubraview_subtitle_track_t *track, const uint32_t *rows, size_t count,
                                double seconds) {
    if (!track || !rows || count == 0) return -1;
    double t = seconds - track->offset_seconds;
    /* The cues are in order of their start, so the rows are too. */
    size_t low = 0, high = count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (rows[mid] < track->count && track->cues[rows[mid]].start_seconds <= t) low = mid + 1;
        else high = mid;
    }
    if (low == 0) return -1;
    return low - 1 > (size_t)INT32_MAX ? INT32_MAX : (int32_t)(low - 1);
}

bool rubraview_subwin_click(bool *chosen, size_t count, size_t *anchor, size_t index, bool ctrl, bool shift) {
    if (!chosen || !anchor || index >= count) return false;
    if (shift) {
        size_t from = *anchor < count ? *anchor : index;
        size_t low = from < index ? from : index, high = from < index ? index : from;
        if (!ctrl) memset(chosen, 0, count * sizeof(chosen[0]));
        for (size_t i = low; i <= high; ++i) chosen[i] = true;
        return false;
    }
    *anchor = index;
    if (ctrl) {
        chosen[index] = !chosen[index];
        return false;
    }
    return true;
}

size_t rubraview_subwin_shift(rubraview_subtitle_track_t *track, uint32_t *rows, bool *chosen, size_t count,
                              double seconds, bool *marks) {
    if (!track || !rows || !chosen || !marks || count == 0 || seconds == 0.0 || !(seconds == seconds)) return 0;
    memset(marks, 0, track->count * sizeof(marks[0]));
    size_t moved = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!chosen[i] || rows[i] >= track->count) continue;
        rubraview_subtitle_cue_t *cue = &track->cues[rows[i]];
        double by = seconds;
        if (cue->start_seconds + by < 0.0) by = -cue->start_seconds;   /* not before the beginning */
        marks[rows[i]] = true;
        if (by == 0.0) continue;
        cue->start_seconds += by;
        cue->end_seconds += by;
        cue->time_changed = true;
        moved++;
    }
    /* In order of start again. Nearly in order already, so each cue is
       walked back to its place; equal starts keep the order they had. */
    for (size_t i = 1; i < track->count; ++i) {
        rubraview_subtitle_cue_t cue = track->cues[i];
        bool mark = marks[i];
        size_t at = i;
        while (at > 0 && track->cues[at - 1].start_seconds > cue.start_seconds) {
            track->cues[at] = track->cues[at - 1];
            marks[at] = marks[at - 1];
            at--;
        }
        track->cues[at] = cue;
        marks[at] = mark;
    }
    size_t again = rubraview_subwin_rows(track, rows, count);
    for (size_t i = 0; i < count; ++i) chosen[i] = i < again && marks[rows[i]];
    return moved;
}

bool rubraview_subwin_amount(u8str_t text, double *out_seconds) {
    double whole = 0.0, part = 0.0, scale = 1.0;
    size_t digits = 0, i = 0;
    while (i < text.len && text.ptr[i] == ' ') i++;
    for (; i < text.len && text.ptr[i] >= '0' && text.ptr[i] <= '9'; ++i, ++digits) {
        whole = whole * 10.0 + (double)(text.ptr[i] - '0');
        if (whole > 3600.0) return false;
    }
    if (i < text.len && (text.ptr[i] == '.' || text.ptr[i] == ',')) {
        ++i;
        for (; i < text.len && text.ptr[i] >= '0' && text.ptr[i] <= '9'; ++i, ++digits) {
            if (scale < 1e6) { scale *= 10.0; part += (double)(text.ptr[i] - '0') / scale; }
        }
    }
    while (i < text.len && text.ptr[i] == ' ') i++;
    if (i != text.len || digits == 0) return false;
    double seconds = whole + part;
    if (!(seconds > 0.0) || seconds > 3600.0) return false;
    if (out_seconds) *out_seconds = seconds;
    return true;
}

void rubraview_subwin_choose_all(bool *chosen, size_t count, bool on) {
    if (!chosen) return;
    for (size_t i = 0; i < count; ++i) chosen[i] = on;
}

size_t rubraview_subwin_chosen_count(const bool *chosen, size_t count) {
    size_t n = 0;
    if (!chosen) return 0;
    for (size_t i = 0; i < count; ++i) n += chosen[i] ? 1u : 0u;
    return n;
}

u8str_t rubraview_subwin_time(char *buffer, size_t size, double seconds) {
    u8str_t none = { .ptr = "", .len = 0 };
    if (!buffer || size == 0) return none;
    if (!(seconds > 0.0)) seconds = 0.0;
    if (seconds > 359999.0) seconds = 359999.0;       /* 99:59:59 */
    long long hundredths = llround(seconds * 100.0);
    long long whole = hundredths / 100;
    int n = snprintf(buffer, size, "%lld:%02lld:%02lld.%02lld", whole / 3600, whole / 60 % 60, whole % 60, hundredths % 100);
    if (n <= 0) return none;
    return (u8str_t){ .ptr = buffer, .len = (size_t)n < size ? (size_t)n : size - 1 };
}

u8str_t rubraview_subwin_line(char *buffer, size_t size, u8str_t text) {
    size_t n = 0;
    if (!buffer || size == 0) return (u8str_t){ .ptr = "", .len = 0 };
    for (size_t i = 0; i < text.len; ++i) {
        unsigned char c = (unsigned char)text.ptr[i];
        if (c == '\r') continue;
        if (c == '\n') {
            if (i + 1 >= text.len) break;             /* a break at the end says nothing */
            if (n + 3 > size) break;
            memcpy(buffer + n, " / ", 3);
            n += 3;
            continue;
        }
        /* a whole character or none */
        size_t len = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        if (i + len > text.len || n + len > size) break;
        memcpy(buffer + n, text.ptr + i, len);
        n += len;
        i += len - 1;
    }
    return (u8str_t){ .ptr = buffer, .len = n };
}
