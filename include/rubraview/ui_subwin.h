#ifndef RUBRAVIEW_UI_SUBWIN_H
#define RUBRAVIEW_UI_SUBWIN_H

#include "rubraview/core.h"
#include "rubraview/subtitle.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The subtitle window (owner, 2026-10-14, RV-115): a film's subtitles as a
 * list in a window of its own, one a row; a click goes to that moment, and
 * several rows can be chosen (to be moved in time and edited, in the next
 * stage). What is here is what can be said without a window: which cues
 * the list holds, which row belongs to a moment, what a click chooses, and
 * how a row is written. The rows' places are `ui_listwin`'s; the viewer
 * paints.
 */

/**
 * The cues the list shows, in order: those of the language on screen (a
 * SAMI file may hold several) that have text. `rows[i]` is a cue's index
 * in the track. Returns how many were written, `max` at most.
 */
size_t rubraview_subwin_rows(const rubraview_subtitle_track_t *track, uint32_t *rows, size_t max);

/**
 * The row that belongs to a moment of the film: the last one that has
 * begun (the track's offset counted), whether it is still on screen or
 * not, so the list keeps its place between two subtitles. -1 before the
 * first.
 */
int32_t rubraview_subwin_row_at(const rubraview_subtitle_track_t *track, const uint32_t *rows, size_t count,
                                double seconds);

/**
 * A click on a row, away from its check box. Plain: the film goes there
 * (true is returned) and what is chosen stays as it is — the boxes hold
 * the choice. With Ctrl the row's box is switched, as a click on the box
 * itself does (pass `ctrl` for that); with Shift the run from the row last
 * clicked to this one is chosen (with Ctrl as well, added to what was).
 * Those two only choose (false). `anchor` is the row last clicked without
 * Shift.
 */
bool rubraview_subwin_click(bool *chosen, size_t count, size_t *anchor, size_t index, bool ctrl, bool shift);

/** Every row chosen, or none. */
void rubraview_subwin_choose_all(bool *chosen, size_t count, bool on);

size_t rubraview_subwin_chosen_count(const bool *chosen, size_t count);

/**
 * Move the chosen rows' cues `seconds` later (negative: earlier), start
 * and end together; a cue is not moved before the film's beginning. The
 * track is kept in order of start time, so a cue moved past its
 * neighbours changes place; `rows` and `chosen` are written again to
 * follow. `marks` is room for one flag a cue of the track. Returns how
 * many cues moved.
 */
size_t rubraview_subwin_shift(rubraview_subtitle_track_t *track, uint32_t *rows, bool *chosen, size_t count,
                              double seconds, bool *marks);

/**
 * The amount typed in the field above the list, in seconds: `0.1`, `1.5`,
 * `2`, a comma for the point as well. False when it is not a number, not
 * above nothing, or more than an hour.
 */
bool rubraview_subwin_amount(proven_u8str_view_t text, double *out_seconds);

/** A time as the list writes it: `1:02:03.45`, hours always, hundredths. */
proven_u8str_view_t rubraview_subwin_time(char *buffer, size_t size, double seconds);

/** A subtitle on one row: its line breaks written as " / ". Cut at `size`, on a character. */
proven_u8str_view_t rubraview_subwin_line(char *buffer, size_t size, proven_u8str_view_t text);

#ifdef __cplusplus
}
#endif

#endif
