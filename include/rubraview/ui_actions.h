#ifndef RUBRAVIEW_UI_ACTIONS_H
#define RUBRAVIEW_UI_ACTIONS_H

#include "rubraview/core.h"
#include "rubraview/layout.h"
#include "rubraview/viewport.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What a menu item or a toolbox tile tells the reader about its action
 * before it is tapped: whether it can do anything on the page on screen,
 * whether a toggle is on, and which of several choices is the one in
 * use. The same rules serve both boxes, so a tile and a menu item for
 * the same action never disagree.
 */

typedef enum rubraview_tile_mark {
    RUBRAVIEW_MARK_NONE = 0,
    RUBRAVIEW_MARK_ON,        /* a toggle that is on: "Crisp: on" */
    RUBRAVIEW_MARK_OFF,       /* a toggle that is off */
    RUBRAVIEW_MARK_CURRENT,   /* the choice in use among several (a layout, a fit) */
} rubraview_tile_mark_t;

/* What the viewer knows about the page on screen, as plain facts. */
typedef struct rubraview_action_facts {
    bool has_page;            /* something is shown */
    bool archive_series;      /* reading an archive: there are archives to step through */
    bool media;               /* a film or a sound is open */
    bool video;               /* ... and it has pictures */
    bool frames;              /* an animation or a multi-page file */
    bool other_audio_track;   /* `A` would change the sound track */
    bool other_subtitle;      /* `C` would change the subtitles */
    bool subtitle_shown;      /* subtitles are being drawn */
    /* toggles */
    bool slideshow, filmstrip, osd, toolbox_pinned, toolbox_detached, fullscreen;
    bool nearest, pixel_grid, spread_detect, fit_lock, always_on_top, muted;
    bool rtl;                 /* pages read right to left */
    bool playing;             /* a film, music or animation is running (not paused) */
    /* choices */
    rubraview_page_layout_t layout;
    rubraview_fit_mode_t fit;
} rubraview_action_facts_t;

typedef struct rubraview_action_state {
    bool enabled;                 /* false: the tile is dimmed and a tap does nothing */
    rubraview_tile_mark_t mark;
    const char *value;            /* a setting the caption names, "L>R"; NULL for none */
} rubraview_action_state_t;

/** An action nobody lists here is enabled and unmarked. */
rubraview_action_state_t rubraview_action_state(u8str_t action, const rubraview_action_facts_t *facts);

/**
 * The caption a tile shows: "File >" for a submenu, "Order: R>L" when
 * `value` names a setting, "Crisp: on" or "Crisp: off" for a toggle, the
 * label itself otherwise. Written into `buffer`; when it does not fit,
 * the plain label is returned instead.
 */
u8str_t rubraview_tile_caption(u8str_t label, bool submenu, rubraview_tile_mark_t mark, const char *value,
                               char *buffer, size_t buffer_size);

/**
 * The icon a toolbox button draws (owner, 2026-09-22): a code point in the
 * Segoe MDL2 Assets font, chosen by what a tap will do now (the pause sign
 * while playing, the play sign while paused). 0 for an action whose short
 * caption says it better ("1x", "1:1", "A-B"); the caller draws that.
 */
uint32_t rubraview_action_icon(u8str_t action, const rubraview_action_facts_t *facts);
/** The pin at a box's top-left: an outline when off, filled when on. */
uint32_t rubraview_pin_icon(bool on);

/*
 * A destructive menu item asks first (owner, 2026-09-21): the first tap
 * arms it and its caption asks ("Delete?"); a second tap on the same item
 * within RUBRAVIEW_CONFIRM_SECONDS acts. Tapping anything else, or
 * waiting, disarms it.
 */
#define RUBRAVIEW_CONFIRM_SECONDS 5.0
typedef struct rubraview_confirm {
    int64_t key;       /* which item is armed; meaningful while `until` > now */
    double until;
} rubraview_confirm_t;

/** True when this press should act; otherwise it arms `key`. */
bool rubraview_confirm_press(rubraview_confirm_t *confirm, int64_t key, double now);
bool rubraview_confirm_armed(const rubraview_confirm_t *confirm, int64_t key, double now);
void rubraview_confirm_clear(rubraview_confirm_t *confirm);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_UI_ACTIONS_H */
