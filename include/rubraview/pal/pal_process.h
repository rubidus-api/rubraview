#ifndef RUBRAVIEW_PAL_PROCESS_H
#define RUBRAVIEW_PAL_PROCESS_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Starting another copy of this program to do a long job (§3.11 batch,
 * owner 2026-09-20: "배치 작업은 별개 프로세스로 새 창에서").
 *
 * A run started this way has its own process and its own console window,
 * so it keeps going when the viewer is closed, and a viewer that hangs
 * does not take the conversion with it.
 */

/** This program's own executable, as a full path. Empty when it cannot be had. */
u8str_t rubraview_pal_process_executable(proven_arena_t *arena);

/**
 * Start `command_line` as a separate process with a console window of its
 * own, not waited for. The first token must be the executable, quoted if
 * it holds spaces. False when the process could not be started.
 */
bool rubraview_pal_process_start_console(u8str_t command_line);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_PROCESS_H */
