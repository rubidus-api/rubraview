/* lhadec_shim.h - FultaArc: the build environment of the lhadec sources (src/lha/lhadec/src). Force-included by the
 * Makefile when, and only when, those files are compiled. It renames lhadec's link symbols (lhadec_rename.h) and
 * nothing else: since D-038 lhadec allocates through the proven allocator lha_open is given (edited in place, the
 * room's byte pin retired), so libc's allocator is no longer mapped here. */
#ifndef FULTA_LHADEC_SHIM_H
#define FULTA_LHADEC_SHIM_H
#include "lhadec_rename.h"
#endif
