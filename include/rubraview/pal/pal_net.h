#ifndef RUBRAVIEW_PAL_NET_H
#define RUBRAVIEW_PAL_NET_H

#include "rubraview/core.h"
#include "rubraview/pal/pal_fs.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RV-326 (R375, D-413): network (SMB) places typed as "\\server" or "\\server\share\...". A server alone lists its
 * shared folders; a place that asks for a login is logged into through Windows' own sign-in (the connection is
 * the logon session's, as Explorer's, and "Remember my credentials" keeps it in Windows' Credential Manager).
 * Blocking: a server not there can take Windows' own time-out.
 */

/* A server's shared folders ("\\server\name", folders), the hidden ("name$") and printers left out. Empty when
   `server_path` is not a server alone or it cannot be listed. */
rubraview_fs_listing_t rubraview_pal_net_shares(proven_arena_t *arena, proven_u8str_view_t server_path);

typedef enum rubraview_net_reach {
    RUBRAVIEW_NET_NOT_UNC = 0,   /* not a network path: nothing done */
    RUBRAVIEW_NET_OPEN,          /* it opens as it is */
    RUBRAVIEW_NET_LOGGED_IN,     /* the sign-in was asked and it opens now */
    RUBRAVIEW_NET_CANCELLED,     /* the sign-in was closed */
    RUBRAVIEW_NET_NOT_FOUND,     /* no such server or share */
    RUBRAVIEW_NET_FAILED,        /* any other refusal; `why` says it in Windows' words */
} rubraview_net_reach_t;

/* Whether the server or share of `path` opens, and when it refuses for want of a login, Windows' sign-in over the
   window `owner` (an HWND; NULL for none). */
rubraview_net_reach_t rubraview_pal_net_reach(void *owner, proven_u8str_view_t path, char *why, size_t why_cap);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_NET_H */
