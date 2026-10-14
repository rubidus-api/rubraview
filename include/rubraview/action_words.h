#ifndef RUBRAVIEW_ACTION_WORDS_H
#define RUBRAVIEW_ACTION_WORDS_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * What an action does, in a short English sentence ("Zoom in"), as the key
 * tables of the manual and the README say it; NULL for an action that has
 * none. `src/core/action_words.c` is generated from scripts/check-actions.py
 * (owner, 2026-10-14: the keys page says it when the pointer rests on a row).
 */
const char *rubraview_action_words(u8str_t action);

#ifdef __cplusplus
}
#endif

#endif
