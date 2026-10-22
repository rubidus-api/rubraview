#include "rubraview/aspect.h"
#include "rubraview/lang.h"
#include <math.h>
#include <string.h>

static const struct { const char *action, *label; double ratio; } ASPECTS[RUBRAVIEW_ASPECT_COUNT] = {
    [RUBRAVIEW_ASPECT_ORIGINAL]     = { "aspect_original", "original", 0.0 },
    [RUBRAVIEW_ASPECT_4_3]          = { "aspect_4_3",      "4:3",      4.0 / 3.0 },
    [RUBRAVIEW_ASPECT_5_4]          = { "aspect_5_4",      "5:4",      5.0 / 4.0 },
    [RUBRAVIEW_ASPECT_3_2]          = { "aspect_3_2",      "3:2",      3.0 / 2.0 },
    [RUBRAVIEW_ASPECT_16_10]        = { "aspect_16_10",    "16:10",    16.0 / 10.0 },
    [RUBRAVIEW_ASPECT_16_9]         = { "aspect_16_9",     "16:9",     16.0 / 9.0 },
    [RUBRAVIEW_ASPECT_CINEMA_FLAT]  = { "aspect_185",      "1.85:1",   1.85 },
    [RUBRAVIEW_ASPECT_21_9]         = { "aspect_21_9",     "21:9",     64.0 / 27.0 },
    [RUBRAVIEW_ASPECT_CINEMA_SCOPE] = { "aspect_239",      "2.39:1",   2.39 },
    [RUBRAVIEW_ASPECT_1_1]          = { "aspect_1_1",      "1:1",      1.0 },
    [RUBRAVIEW_ASPECT_PAPER]        = { "aspect_paper",    "A4 / B4",  0.70710678118654752 },
    [RUBRAVIEW_ASPECT_LETTER]       = { "aspect_letter",   N_("Letter"),   8.5 / 11.0 },
    [RUBRAVIEW_ASPECT_CUSTOM]       = { "aspect_custom",   "custom",   0.0 },
};

static bool known(rubraview_aspect_t aspect) {
    return (int)aspect >= 0 && aspect < RUBRAVIEW_ASPECT_COUNT;
}

const char *rubraview_aspect_action(rubraview_aspect_t aspect) {
    return known(aspect) ? ASPECTS[aspect].action : "";
}

const char *rubraview_aspect_label(rubraview_aspect_t aspect) {
    return known(aspect) ? rubraview_tr(ASPECTS[aspect].label) : "";
}

rubraview_aspect_t rubraview_aspect_from_action(proven_u8str_view_t action) {
    for (int i = 0; i < RUBRAVIEW_ASPECT_COUNT; ++i) {
        size_t len = strlen(ASPECTS[i].action);
        if (action.size == len && action.ptr && memcmp(action.ptr, ASPECTS[i].action, len) == 0) return (rubraview_aspect_t)i;
    }
    return RUBRAVIEW_ASPECT_COUNT;
}

rubraview_aspect_t rubraview_aspect_next(rubraview_aspect_t aspect) {
    if (!known(aspect)) return RUBRAVIEW_ASPECT_ORIGINAL;
    return (rubraview_aspect_t)(((int)aspect + 1) % RUBRAVIEW_ASPECT_COUNT);
}

double rubraview_aspect_ratio(rubraview_aspect_t aspect, double custom_width, double custom_height, bool on_its_side) {
    if (!known(aspect) || aspect == RUBRAVIEW_ASPECT_ORIGINAL) return 0.0;
    double ratio = ASPECTS[aspect].ratio;
    if (aspect == RUBRAVIEW_ASPECT_CUSTOM) {
        if (!(custom_width > 0.0) || !(custom_height > 0.0)) return 0.0;
        ratio = custom_width / custom_height;
    }
    if (on_its_side) ratio = 1.0 / ratio;
    /* Past a hundred to one there is nothing left to look at. */
    return ratio < 0.01 ? 0.01 : ratio > 100.0 ? 100.0 : ratio;
}

void rubraview_aspect_apply(double ratio, bool turned, int32_t width, int32_t height, int32_t *out_width, int32_t *out_height) {
    if (out_width) *out_width = width;
    if (out_height) *out_height = height;
    if (!(ratio > 0.0) || width <= 0 || height <= 0) return;
    /* As seen, the height stays and the width becomes height x ratio. */
    double seen_height = turned ? (double)width : (double)height;
    double seen_width = floor(seen_height * ratio + 0.5);
    if (seen_width < 1.0) seen_width = 1.0;
    if (seen_width > 1.0e9) seen_width = 1.0e9;
    if (turned) { if (out_height) *out_height = (int32_t)seen_width; }
    else if (out_width) *out_width = (int32_t)seen_width;
}
