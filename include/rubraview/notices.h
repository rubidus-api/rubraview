#ifndef RUBRAVIEW_NOTICES_H
#define RUBRAVIEW_NOTICES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The licence notices of what is linked in, a line each (UTF-8), from
   scripts/gen-notices.py. Returns how many. */
size_t rubraview_notice_lines(const char *const **out);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_NOTICES_H */
