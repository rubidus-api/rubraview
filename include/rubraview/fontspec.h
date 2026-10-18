#ifndef RUBRAVIEW_FONTSPEC_H
#define RUBRAVIEW_FONTSPEC_H

#include "rubraview/core.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * RV-136: a font as a setting holds it — "Malgun Gothic", "Segoe UI, Bold",
 * "Georgia, Light, Italic": the family, then a weight's English name and
 * "Italic" when they are not the plain ones. Read from the end, so a family
 * whose name has a comma in it is kept whole.
 */
typedef struct rubraview_font_spec {
    u8str_t family;   /* points into the text read; empty for the program's face */
    int32_t weight;   /* 100..900; 400 when not said */
    bool    italic;
} rubraview_font_spec_t;

rubraview_font_spec_t rubraview_font_spec_parse(u8str_t text);

/** The text for a setting; `buf` holds it. A weight not among the names is the nearest named one. */
u8str_t rubraview_font_spec_format(char *buf, size_t cap, u8str_t family, int32_t weight, bool italic);

/** The named weights, light to heavy, and their English names ("Regular" for 400). */
#define RUBRAVIEW_FONT_WEIGHTS 9
int32_t rubraview_font_weight_at(int32_t index);
const char *rubraview_font_weight_name(int32_t weight);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_FONTSPEC_H */
