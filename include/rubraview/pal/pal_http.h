#ifndef RUBRAVIEW_PAL_HTTP_H
#define RUBRAVIEW_PAL_HTTP_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RV-323 (R369, R370, D-411): one HTTPS request through Windows' own WinHTTP (its TLS, its proxy settings), the
 * only network the viewer makes besides the radio stations FFmpeg plays. Blocking: call it from a worker. The
 * client is named as LRCLIB asks ("Rubraview <version> (https://github.com/rubidus-api/rubraview)").
 */

typedef struct rubraview_http_reply {
    int      status;      /* the HTTP status; 0 when no answer came */
    uint8_t *body;        /* malloc'd, NUL-ended (not counted); NULL when none */
    size_t   size;
    char     why[128];    /* what failed, in words, when no answer came */
} rubraview_http_reply_t;

/* `method` "GET" or "POST"; `body` sent as `content_type` when given; `header` one more "Name: value" line or
   NULL. At most `max_bytes` of the answer are kept. True when an answer came (any status). */
bool rubraview_pal_http(const char *method, const char *url, const char *content_type, const uint8_t *body, size_t body_size,
                        const char *header, uint32_t timeout_ms, size_t max_bytes, rubraview_http_reply_t *out);
void rubraview_pal_http_free(rubraview_http_reply_t *reply);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_HTTP_H */
