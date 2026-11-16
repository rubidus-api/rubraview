/* RV-294: the Compress window, in the compression's own process (packwin.c). */
#ifndef RUBRAVIEW_PACKWIN_H
#define RUBRAVIEW_PACKWIN_H

#include "rubraview/archive_job.h"
#include "rubraview/editctl.h"

enum { PACKWIN_NAME = 0, PACKWIN_SPLIT, PACKWIN_PW, PACKWIN_AGAIN, PACKWIN_FIELDS };
enum { PACKWIN_CHIPS = 48 };

typedef struct packwin {
    rubraview_window_t   *window;
    rubraview_renderer_t *renderer;
    bool   open, dirty, bar_drag;
    double px, py, scroll, content, bar_most;
    char   what[160];                   /* what is compressed, for the title ("3 files", "the folder Photos") */
    /* the choices */
    int    level;
    rubraview_pack_method_t method;
    bool   solid_off, names_hidden;
    uint64_t split;                     /* read from its field by Compress */
    /* the fields: the name, the split size, the password twice */
    rubraview_editctl_t field[PACKWIN_FIELDS];
    editbox_view_t      view[PACKWIN_FIELDS];
    char   buf[PACKWIN_FIELDS][1024];
    double field_y[PACKWIN_FIELDS];
    int    focus, follow;
    char   composing[64];
    char   note[200];
    /* what was drawn, for the pointer */
    rubraview_pal_rect_t chip[PACKWIN_CHIPS], button[2], bar;
    int    chip_kind[PACKWIN_CHIPS];
    bool   chip_off[PACKWIN_CHIPS];
    int    chips;
} packwin_t;

void packwin_init(packwin_t *p, proven_u8str_view_t name, int level, rubraview_pack_method_t method, bool solid_off, uint64_t split, bool names_hidden);
void packwin_free(packwin_t *p);
bool packwin_show(packwin_t *p, proven_arena_t *arena, proven_u8str_view_t ui_font);
void packwin_close(packwin_t *p);
/** Its messages: 1 Compress (all read), -1 Cancel or closed, 0 neither yet. */
int packwin_pump(packwin_t *p);
void packwin_draw(packwin_t *p);
bool packwin_7z(const packwin_t *p);
rubraview_pack_method_t packwin_method(const packwin_t *p);

#endif
