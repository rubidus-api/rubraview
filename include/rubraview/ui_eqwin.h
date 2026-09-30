#ifndef RUBRAVIEW_UI_EQWIN_H
#define RUBRAVIEW_UI_EQWIN_H

#include "rubraview/core.h"
#include "rubraview/ui_box.h"
#include "rubraview/audio_dsp.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The equaliser window (owner, 2026-10-01): a see-through panel floating
 * over the picture, like the playlist window. A title row that drags it and
 * closes it; a row of presets (the eight named ones and Custom); ten
 * upright sliders, one a band, from -12 to +12 dB in half-decibel steps.
 * Geometry and state only: the viewer paints and keeps the values.
 */

#define RUBRAVIEW_EQWIN_PRESETS (RUBRAVIEW_EQ_PRESET_COUNT + 1)   /* the last is Custom */
#define RUBRAVIEW_EQWIN_CUSTOM  RUBRAVIEW_EQ_PRESET_COUNT
#define RUBRAVIEW_EQWIN_MAX_DB  12.0

typedef enum rubraview_eqwin_part {
    RUBRAVIEW_EQWIN_NONE = 0,
    RUBRAVIEW_EQWIN_TITLE,     /* drags the window */
    RUBRAVIEW_EQWIN_CLOSE,
    RUBRAVIEW_EQWIN_PRESET,    /* `index` is the preset */
    RUBRAVIEW_EQWIN_SLIDER,    /* `index` is the band */
    RUBRAVIEW_EQWIN_BODY,
} rubraview_eqwin_part_t;

typedef struct rubraview_eqwin {
    bool   open;
    bool   placed;
    double x, y, width, height;
    double title_height, preset_height, label_height;
    bool   dragging;           /* the title, held */
    double grab_dx, grab_dy;
    int    sliding;            /* the band held, -1 for none */
} rubraview_eqwin_t;

rubraview_eqwin_t rubraview_eqwin_create(double dpi_scale);

/** Keep it inside the view; the first time, centred low in it. */
void rubraview_eqwin_place(rubraview_eqwin_t *w, double view_w, double view_h);

rubraview_rect_t rubraview_eqwin_rect(const rubraview_eqwin_t *w);
rubraview_rect_t rubraview_eqwin_close_rect(const rubraview_eqwin_t *w);
rubraview_rect_t rubraview_eqwin_preset_rect(const rubraview_eqwin_t *w, size_t preset);

/** A band's column (value above, track, frequency below) and its track. */
rubraview_rect_t rubraview_eqwin_band_rect(const rubraview_eqwin_t *w, size_t band);
rubraview_rect_t rubraview_eqwin_track_rect(const rubraview_eqwin_t *w, size_t band);

/** What is under the point; `out_index` gets the preset or the band. */
rubraview_eqwin_part_t rubraview_eqwin_hit(const rubraview_eqwin_t *w, double px, double py, size_t *out_index);

/** The gain a height on a band's track means, in half decibels, within ±12. */
double rubraview_eqwin_db_at(const rubraview_eqwin_t *w, size_t band, double py);

/** Where a gain sits on a band's track (the knob's centre). */
double rubraview_eqwin_y_of(const rubraview_eqwin_t *w, size_t band, double db);

/** A gain moved by `steps` half decibels (the wheel), within ±12. */
double rubraview_eqwin_step(double db, int steps);

/** Dragging by the title: begin at the pointer, move with it, kept inside the view. */
void rubraview_eqwin_drag_begin(rubraview_eqwin_t *w, double px, double py);
void rubraview_eqwin_drag_to(rubraview_eqwin_t *w, double px, double py, double view_w, double view_h);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_UI_EQWIN_H */
