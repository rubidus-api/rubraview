#ifndef RUBRAVIEW_PAL_PLUGIN_H
#define RUBRAVIEW_PAL_PLUGIN_H

#include "rubraview/plugin.h"
#include <wchar.h>

/*
 * RFC-0007 (RV-138, D-186): plug-in filters found and run, always in a child of this program
 * (`rubraview.exe --filter-host`), never in the viewer.
 */

/** The folder used when none is chosen: %LOCALAPPDATA%\Rubraview\plugins (made when `make`). */
bool rubraview_pal_plugins_default_folder(char *out, size_t capacity, bool make);

/** The folder's `*.rvf.dll`, each looked at (64-bit, its hash) and asked what it is; those refused are
    listed too, with why. Returns how many. A plug-in whose file has not changed is not asked again. */
size_t rubraview_pal_plugins_find(proven_u8str_view_t folder, rubraview_plugin_t *out, size_t capacity);

/** One run over BGRA rows (`width` * 4 apart) into `out` (the same size). `tick` is called about every 50 ms
    with the progress; true from it cancels. False and `*why` when the plug-in failed, crashed, was cancelled
    or did not stop (ended 5 s after Cancel). */
bool rubraview_pal_plugin_run(const rubraview_plugin_t *plugin, const double *params, const uint8_t *bgra,
                              int32_t width, int32_t height, uint8_t *out,
                              bool (*tick)(void *context, double progress), void *context, const char **why);

/** The child's whole life: `--filter-host <mapping> <dll>`; the process's exit code. */
int rubraview_pal_plugin_host_main(const wchar_t *mapping, const wchar_t *dll);

#endif
