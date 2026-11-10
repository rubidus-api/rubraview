#ifndef RUBRAVIEW_UI_CARD_H
#define RUBRAVIEW_UI_CARD_H

#include "rubraview/core.h"
#include "rubraview/ui_box.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The input card (RV-265, R296; owner, 2026-11-10: "그 위젯은 윈도 내부에서 이동과 크기조절, 확인과 취소가
 * 가능한 그런 공통 루틴으로 돌아가게 해 줘요. 물론 모달레스여야 해요"): every typed value is asked in one card
 * that the reader moves by its title and sizes by its edges and corners, that stays wholly inside the window it is
 * in (the host), and that has OK and Cancel. It is modeless: the window under it keeps working; the card has the
 * keyboard while it has the focus (a press on it), the window when the window is pressed.
 *
 * What is here is what can be said without a window: the card's rectangle in the host, its size floor, the part of
 * it under a point, a drag that moves or sizes it, the clamp that keeps it inside, the focus, and its place kept as
 * shares of the host. Its title, fields and buttons are the host's to lay out inside the rectangle and to paint.
 */

/** What a point is over. The edges are bits, so a corner is two of them. */
enum {
    RUBRAVIEW_CARD_OUTSIDE = 0,
    RUBRAVIEW_CARD_BODY    = 1,   /* inside, away from the title and the edges */
    RUBRAVIEW_CARD_TITLE   = 2,   /* the title band: a drag moves the card */
    RUBRAVIEW_CARD_LEFT    = 4,
    RUBRAVIEW_CARD_RIGHT   = 8,
    RUBRAVIEW_CARD_TOP     = 16,
    RUBRAVIEW_CARD_BOTTOM  = 32,
};
#define RUBRAVIEW_CARD_EDGES (RUBRAVIEW_CARD_LEFT | RUBRAVIEW_CARD_RIGHT | RUBRAVIEW_CARD_TOP | RUBRAVIEW_CARD_BOTTOM)

typedef struct rubraview_card {
    rubraview_rect_t rect;     /* in the host's pixels */
    double min_width, min_height;
    double margin;             /* kept free between the card and the host's edges */
    double title_height;       /* the band a drag moves it by */
    double edge;               /* how far in from its border an edge can be taken */
    bool open;
    bool focused;              /* the keyboard is the card's */
    bool sized;                /* the reader sized it: its size is kept, not the one asked */
    int32_t drag;              /* 0: none; RUBRAVIEW_CARD_TITLE: moving; else the edge bits being dragged */
    double grab_x, grab_y;     /* where the drag began */
    rubraview_rect_t grab_rect;
} rubraview_card_t;

/** A card's place kept between asks, as shares of its host (layout.ini's [cards]). */
typedef struct rubraview_card_share {
    double left, top, width, height;   /* 0..1 of the host */
    bool known;
    bool sized;
} rubraview_card_share_t;

/**
 * Opens the card in a host of `host_w` x `host_h`: `want_w` x `want_h` centred across and at `rise` (0 top, 1
 * bottom) of the room down, or where `kept` says when it is known (its size too when the reader had sized it, never
 * under what is asked now). Clamped inside. The card has the focus.
 */
void rubraview_card_open(rubraview_card_t *card, double host_w, double host_h, double want_w, double want_h, double rise,
                         const rubraview_card_share_t *kept);

/** Closes it; any drag ends. */
void rubraview_card_close(rubraview_card_t *card);

/**
 * The floor the content needs now (it may grow: a row more): the card is made at least that, then kept inside the
 * host. Call each time it is drawn, so a host made smaller pulls it in. A host smaller than the floor gets a card of
 * the host's size less the margins.
 */
void rubraview_card_fit(rubraview_card_t *card, double host_w, double host_h, double min_w, double min_h);

/** The part under a point (RUBRAVIEW_CARD_OUTSIDE when closed). */
int32_t rubraview_card_hit(const rubraview_card_t *card, double x, double y);

/**
 * A press. Inside, the card takes the focus and a press on the title or an edge begins a drag (true). Outside, the
 * focus goes to the window under it (false): the press is the window's.
 */
bool rubraview_card_press(rubraview_card_t *card, double x, double y);

/** The pointer moved: a drag in hand moves or sizes the card, inside the host. True when it changed. */
bool rubraview_card_motion(rubraview_card_t *card, double x, double y, double host_w, double host_h);

/** The button was let go: the drag ends. True when one was in hand. */
bool rubraview_card_release(rubraview_card_t *card);

/** Its place as shares of the host, to be kept. */
rubraview_card_share_t rubraview_card_share_of(const rubraview_card_t *card, double host_w, double host_h);

/** A kept share read back; false (and nothing kept) when out of range. */
bool rubraview_card_share_valid(const rubraview_card_share_t *share);

#ifdef __cplusplus
}
#endif

#endif
