/*
 * RV-281: the properties window's view, shared by propswin.c (the window, General, Details) and propsec.c (phase 2:
 * Digital Signatures, Security).
 */
#ifndef RUBRAVIEW_APP_PROPSVIEW_H
#define RUBRAVIEW_APP_PROPSVIEW_H

#include "app.h"
#include "rubraview/pal/pal_fileprops.h"

#define PROPS_FILES_MAX 256
#define PROPS_ROWS_MAX  256
#define PROPS_ASK_SECONDS 20.0      /* a child that has not answered by then is given up */
#define PROPS_ROW_CHANGED (1u << 16)

enum { PB_WINDOWS = 0, PB_OK, PB_CANCEL, PB_APPLY, PB_CHANGE, PB_RENAME,
       /* phase 2 (propsec.c) */
       PB_CERT, PB_ADVANCED, PB_ADD, PB_REMOVE, PB_OWNER, PB_INH_COPY, PB_INH_DROP, PB_INH_ON, PB_ELEVATE,
       PB_AUDIT, PB_AUDIT_ADD, PB_AUDIT_REMOVE, PB_AUDIT_APPLY, PB_COUNT };
#define EDIT_NAME (-2)              /* v->editing: the Security tab's name field (Add..., Change... of the owner, an audit entry) */
enum { NAME_NONE = 0, NAME_ADD, NAME_OWNER, NAME_AUDIT };
#define PROPS_SIGS_MAX  8
#define PROPS_ACES_MAX  256
#define PROPS_PEOPLE_MAX 64
enum { ASK_NONE = 0, ASK_WAITING, ASK_DONE, ASK_FAILED };

typedef struct props_view {
    char                       **paths;
    size_t                       count;
    int32_t                      origin;        /* PROPS_FROM_* */
    size_t                       origin_index;  /* the tile or the row */
    rubraview_fileprops_file_t  *files;
    rubraview_fileprops_file_t   sum;
    rubraview_tristate_t         want[6];       /* the boxes as the user left them */
    uint32_t                     last_on;
    bool                         unblock;
    int32_t                      tab;
    /* Details */
    rubraview_props_row_t       *rows;
    size_t                       row_count;
    int32_t                      details;       /* ASK_* */
    uint32_t                     read_ticket, write_ticket;
    double                       asked_at, write_at;
    bool                         writing, close_after_write;
    int32_t                      scroll;
    int32_t                      editing;       /* the row being typed in, -1 */
    char                         edit_buf[512];
    rubraview_textedit_t         edit;
    char                         composing[64];
    /* where things are drawn, for the pointer */
    rubraview_pal_rect_t         tab_rect[RUBRAVIEW_PROPS_TAB_COUNT];
    rubraview_pal_rect_t         button[PB_COUNT];
    rubraview_pal_rect_t         box[8];        /* 0..5 the attributes, 6 Unblock */
    rubraview_pal_rect_t         row_rect[PROPS_ROWS_MAX];
    /* phase 2: Digital Signatures */
    rubraview_props_sig_t        sigs[PROPS_SIGS_MAX];
    size_t                       sig_count;
    int32_t                      sig_sel;
    rubraview_pal_rect_t         sig_rect[PROPS_SIGS_MAX];
    /* phase 2: Security */
    rubraview_props_security_t   sec;
    rubraview_props_ace_t        aces[PROPS_ACES_MAX];   /* as read */
    size_t                       ace_count;
    rubraview_props_principal_t  people[PROPS_PEOPLE_MAX];
    size_t                       people_count;
    int32_t                      person_sel;
    bool                         people_changed;         /* a box clicked, one added or removed */
    int32_t                      protect_want;           /* -1 unchanged, 0 inheritance on, 1 off */
    char                         owner_sid_want[192], owner_want[256];
    bool                         advanced;
    int32_t                      name_for;               /* NAME_* while the name field is up */
    bool                         sec_failed;             /* Windows refused: Apply as administrator offered */
    bool                         sec_asking;             /* the elevated child at work */
    uint32_t                     sec_ticket;
    double                       sec_at;
    rubraview_props_ace_t        audits[PROPS_PEOPLE_MAX];
    size_t                       audit_count;
    int32_t                      audit_sel;
    int32_t                      audit_state;            /* ASK_*: not read, waiting, read, failed */
    bool                         audit_protect, audit_changed, audit_writing;
    uint32_t                     audit_ticket;
    double                       audit_at;
    rubraview_pal_rect_t         person_rect[PROPS_PEOPLE_MAX], audit_rect[PROPS_PEOPLE_MAX];
    rubraview_pal_rect_t         right_box[2][RUBRAVIEW_RIGHT_COUNT];   /* [allow, deny][right]; an audit entry's: [0] */
    rubraview_pal_rect_t         audit_kind_box[2];      /* Success, Failure */
    rubraview_pal_rect_t         star_rect[PROPS_ROWS_MAX][5];
    size_t                       rows_drawn_from, rows_drawn;
    char                         message[300];
} props_view_t;


#define PW_BACK    0xFF1B1E23u
#define PW_PANEL   0xFF23272Du
#define PW_TEXT    0xFFE6E6E6u
#define PW_DIM     0xFF9AA0A8u
#define PW_LINE    0xFF3A4048u
#define PW_ACCENT  0xFF4A90D9u
#define PW_HEADING 0xFF7FB2E5u


props_view_t *V(app_state_t *app);
void pw_say(props_view_t *v, const char *text);
void pw_text_at(rubraview_renderer_t *r, const char *s, rubraview_pal_rect_t box, double size, uint32_t color);
void pw_button_at(app_state_t *app, int which, const char *words, rubraview_pal_rect_t b, bool accent, bool enabled);
void pw_check_at(app_state_t *app, int which, const char *words, rubraview_tristate_t state, double x, double y, bool enabled);
double pw_info_row(app_state_t *app, double y, const char *label, const char *value);
double pw_separator(app_state_t *app, double y);
void pw_name_begin(app_state_t *app, int32_t name_for);

/* propsec.c */
void propsec_read(app_state_t *app);                 /* the signatures and the permissions, read again */
bool propsec_sig_tab(const props_view_t *v);
void propsec_draw_signatures(app_state_t *app, double y, double bottom);
void propsec_draw_security(app_state_t *app, double y, double bottom);
bool propsec_press(app_state_t *app, double x, double y);
bool propsec_button(app_state_t *app, int which);
void propsec_poll(app_state_t *app);
bool propsec_changed(const props_view_t *v);
bool propsec_apply(app_state_t *app);               /* true when done now (or nothing to do) */
void propsec_free(props_view_t *v);
void propsec_name_done(app_state_t *app, const char *typed);

#endif
