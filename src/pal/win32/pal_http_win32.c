/* RV-323 (R369, R370, D-411): see pal_http.h. */
#include "rubraview/pal/pal_http.h"
#include "rubraview/version.h"
#include "rubraview/pal/pal_wide.h"
#include <windows.h>
#include <winhttp.h>

static void say(rubraview_http_reply_t *out, const char *what, DWORD err) {
    rubraview_fmt(out->why, sizeof out->why, "{} (error {})", PROVEN_ARG(what), PROVEN_ARG((unsigned long)err));
}

/* UTF-8 to a new wide string (rubraview_free it); NULL when it does not convert. */
static wchar_t *wide(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = (wchar_t *)rubraview_alloc((size_t)n * sizeof(wchar_t));
    if (w && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n) != n) { rubraview_free(w); return NULL; }
    return w;
}

bool rubraview_pal_http(const char *method, const char *url, const char *content_type, const uint8_t *body, size_t body_size,
                        const char *header, uint32_t timeout_ms, size_t max_bytes, rubraview_http_reply_t *out) {
    if (!out) return false;
    proven_sys_mem_set(out, 0, sizeof *out);
    if (!method || !url || max_bytes == 0 || body_size > 0x7FFFFFFF) { rubraview_fmt(out->why, sizeof out->why, "{}", PROVEN_ARG("bad request")); return false; }
    wchar_t *wurl = wide(url), *wmethod = wide(method);
    wchar_t *wheaders = NULL;
    char head[512];
    int hn = 0;
    if (content_type) hn += rubraview_fmt(head + hn, sizeof head - (size_t)hn, "Content-Type: {}\r\n", PROVEN_ARG(content_type));
    if (header && hn >= 0) hn += rubraview_fmt(head + hn, sizeof head - (size_t)hn, "{}\r\n", PROVEN_ARG(header));
    if (hn > 0) wheaders = wide(head);
    HINTERNET session = NULL, connect = NULL, request = NULL;
    bool ok = false;
    URL_COMPONENTS parts = { .dwStructSize = sizeof parts };
    wchar_t host[256] = { 0 }, path[2048] = { 0 };
    parts.lpszHostName = host; parts.dwHostNameLength = RUBRAVIEW_COUNT_OF(host);
    parts.lpszUrlPath = path;  parts.dwUrlPathLength = RUBRAVIEW_COUNT_OF(path);
    wchar_t extra[2048] = { 0 };
    parts.lpszExtraInfo = extra; parts.dwExtraInfoLength = RUBRAVIEW_COUNT_OF(extra);
    if (!wurl || !wmethod || !WinHttpCrackUrl(wurl, 0, 0, &parts)) { say(out, "address not understood", GetLastError()); goto done; }
    if (parts.nScheme != INTERNET_SCHEME_HTTPS) { rubraview_fmt(out->why, sizeof out->why, "{}", PROVEN_ARG("only https")); goto done; }
    {
        wchar_t agent[96];
        MultiByteToWideChar(CP_UTF8, 0, "Rubraview " RUBRAVIEW_VERSION_STRING " (https://github.com/rubidus-api/rubraview)", -1, agent, RUBRAVIEW_COUNT_OF(agent));
        session = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) session = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);   /* before Windows 8.1 */
    }
    if (!session) { say(out, "no network session", GetLastError()); goto done; }
    WinHttpSetTimeouts(session, (int)timeout_ms, (int)timeout_ms, (int)timeout_ms, (int)timeout_ms);
    connect = WinHttpConnect(session, host, parts.nPort, 0);
    if (!connect) { say(out, "could not reach the server", GetLastError()); goto done; }
    {
        wchar_t object[4096];
        object[0] = 0;
        size_t a = rubraview_wlen(path), b = rubraview_wlen(extra);
        if (a + b + 1 > RUBRAVIEW_COUNT_OF(object)) { rubraview_fmt(out->why, sizeof out->why, "{}", PROVEN_ARG("address too long")); goto done; }
        rubraview_wcopy(object, RUBRAVIEW_COUNT_OF(object), path);
        rubraview_wcat(object, RUBRAVIEW_COUNT_OF(object), extra);
        request = WinHttpOpenRequest(connect, wmethod, object, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    }
    if (!request) { say(out, "could not make the request", GetLastError()); goto done; }
    if (!WinHttpSendRequest(request, wheaders ? wheaders : WINHTTP_NO_ADDITIONAL_HEADERS, wheaders ? (DWORD)-1L : 0,
                            (LPVOID)body, (DWORD)body_size, (DWORD)body_size, 0) ||
        !WinHttpReceiveResponse(request, NULL)) {
        DWORD e = GetLastError();
        say(out, e == ERROR_WINHTTP_TIMEOUT ? "no answer in time" : e == ERROR_WINHTTP_NAME_NOT_RESOLVED ? "no network (the name was not found)"
                 : e == ERROR_WINHTTP_CANNOT_CONNECT ? "could not connect" : "the request failed", e);
        goto done;
    }
    {
        DWORD status = 0, len = sizeof status;
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
        out->status = (int)status;
    }
    {
        size_t cap = 64u * 1024u, n = 0;
        uint8_t *buf = (uint8_t *)rubraview_alloc(cap + 1);
        if (!buf) { rubraview_fmt(out->why, sizeof out->why, "{}", PROVEN_ARG("no memory")); goto done; }
        for (;;) {
            DWORD avail = 0, got = 0;
            if (!WinHttpQueryDataAvailable(request, &avail) || avail == 0) break;
            if (n + avail > max_bytes) avail = (DWORD)(max_bytes - n);
            if (avail == 0) break;
            if (n + avail > cap) {
                size_t grown = cap * 2 < n + avail ? n + avail : cap * 2;
                uint8_t *more = (uint8_t *)rubraview_realloc((proven_mem_mut_t){ .ptr = buf, .size = grown + 1 });
                if (!more) break;
                buf = more;
                cap = grown;
            }
            if (!WinHttpReadData(request, buf + n, avail, &got) || got == 0) break;
            n += got;
        }
        buf[n] = 0;
        out->body = buf;
        out->size = n;
    }
    ok = true;
done:
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);
    rubraview_free(wurl);
    rubraview_free(wmethod);
    rubraview_free(wheaders);
    return ok;
}

void rubraview_pal_http_free(rubraview_http_reply_t *reply) {
    if (!reply) return;
    rubraview_free(reply->body);
    reply->body = NULL;
    reply->size = 0;
}
