/*
 * RV-326 (R375, D-413): SMB places: a server's shares (NetShareEnum) and Windows' sign-in when one refuses
 * (WNetAddConnection3 with CONNECT_INTERACTIVE | CONNECT_PROMPT, no drive letter: the logon session's connection).
 */
#include "rubraview/pal/pal_net.h"
#include "rubraview/path.h"
#include <windows.h>
#include <winnetwk.h>
#include <lm.h>

static proven_u8str_view_t utf8_of(proven_arena_t *arena, const WCHAR *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return PROVEN_LIT("");
    proven_result_mem_mut_t r = proven_arena_alloc(arena, (size_t)n);
    if (!proven_is_ok(r.err)) return PROVEN_LIT("");
    WideCharToMultiByte(CP_UTF8, 0, w, -1, (char *)r.value.ptr, n, NULL, NULL);
    return (proven_u8str_view_t){ .ptr = r.value.ptr, .size = (size_t)n - 1 };
}

/* "\\server" + rest, rest UTF-8 ("" or "\share"), as UTF-16 */
static bool remote_name(proven_u8str_view_t server, const char *rest, size_t rest_n, WCHAR *out, size_t cap) {
    char buf[1024];
    if (server.size + rest_n + 3 > sizeof buf) return false;
    size_t k = 0;
    buf[k++] = '\\'; buf[k++] = '\\';
    for (size_t i = 0; i < server.size; ++i) buf[k++] = (char)server.ptr[i];
    for (size_t i = 0; i < rest_n; ++i) buf[k++] = rest[i] == '/' ? '\\' : rest[i];
    int w = MultiByteToWideChar(CP_UTF8, 0, buf, (int)k, out, (int)cap - 1);
    if (w <= 0) return false;
    out[w] = 0;
    return true;
}

rubraview_fs_listing_t rubraview_pal_net_shares(proven_arena_t *arena, proven_u8str_view_t server_path) {
    rubraview_fs_listing_t listing = {0};
    proven_u8str_view_t server;
    WCHAR remote[1100];
    if (!arena || rubraview_path_unc(server_path, &server, NULL) != RUBRAVIEW_UNC_SERVER ||
        !remote_name(server, "", 0, remote, sizeof remote / sizeof remote[0])) return listing;
    SHARE_INFO_1 *info = NULL;
    DWORD read = 0, total = 0, resume = 0;
    NET_API_STATUS st = NetShareEnum(remote, 1, (LPBYTE *)&info, MAX_PREFERRED_LENGTH, &read, &total, &resume);
    if ((st != NERR_Success && st != ERROR_MORE_DATA) || !info) { if (info) NetApiBufferFree(info); SetLastError(st); return listing; }
    proven_result_mem_mut_t r = rubraview_arena_alloc_array(arena, read ? read : 1, sizeof(rubraview_fs_entry_t));
    if (proven_is_ok(r.err)) {
        rubraview_fs_entry_t *e = (rubraview_fs_entry_t *)(void *)r.value.ptr;
        size_t n = 0;
        for (DWORD i = 0; i < read; ++i) {
            if ((info[i].shi1_type & 0xFFu) != STYPE_DISKTREE || (info[i].shi1_type & STYPE_SPECIAL)) continue;   /* a folder, not "C$" */
            proven_u8str_view_t name = utf8_of(arena, info[i].shi1_netname);
            if (name.size == 0 || name.ptr[name.size - 1] == '$') continue;
            size_t len = 2 + server.size + 1 + name.size;
            proven_result_mem_mut_t p = proven_arena_alloc(arena, len + 1);
            if (!proven_is_ok(p.err)) break;
            char *s = (char *)p.value.ptr;
            size_t k = 0;
            s[k++] = '\\'; s[k++] = '\\';
            proven_sys_mem_copy(s + k, server.ptr, server.size); k += server.size;
            s[k++] = '\\';
            proven_sys_mem_copy(s + k, name.ptr, name.size); k += name.size;
            s[k] = 0;
            e[n++] = (rubraview_fs_entry_t){ .name = name, .path = { .ptr = p.value.ptr, .size = k }, .is_directory = true };
        }
        listing.entries = e;
        listing.count = n;
    }
    NetApiBufferFree(info);
    SetLastError(NERR_Success);
    return listing;
}

static bool not_there(DWORD e) {
    return e == ERROR_BAD_NETPATH || e == ERROR_BAD_NET_NAME || e == ERROR_NETWORK_UNREACHABLE || e == ERROR_HOST_UNREACHABLE ||
           e == ERROR_REM_NOT_LIST || e == ERROR_NO_NET_OR_BAD_PATH || e == ERROR_NO_NETWORK || e == ERROR_NETNAME_DELETED ||
           e == ERROR_BAD_NETPATH;
}

static void say_why(DWORD e, char *why, size_t cap) {
    if (!why || cap == 0) return;
    why[0] = 0;
    WCHAR text[512];
    DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e, 0, text, 512, NULL);
    while (n > 0 && (text[n - 1] == L'\r' || text[n - 1] == L'\n' || text[n - 1] == L' ' || text[n - 1] == L'.')) text[--n] = 0;
    if (n == 0) { wsprintfW(text, L"error %lu", (unsigned long)e); }
    int k = WideCharToMultiByte(CP_UTF8, 0, text, -1, why, (int)cap, NULL, NULL);
    if (k <= 0) why[0] = 0;
}

rubraview_net_reach_t rubraview_pal_net_reach(void *owner, proven_u8str_view_t path, char *why, size_t why_cap) {
    if (why && why_cap) why[0] = 0;
    proven_u8str_view_t server, share;
    rubraview_unc_kind_t kind = rubraview_path_unc(path, &server, &share);
    if (kind == RUBRAVIEW_UNC_NONE) return RUBRAVIEW_NET_NOT_UNC;
    char rest[600];
    size_t rn = 0;
    if (kind == RUBRAVIEW_UNC_SHARE) {
        if (share.size + 2 > sizeof rest) return RUBRAVIEW_NET_FAILED;
        rest[rn++] = '\\';
        proven_sys_mem_copy(rest + rn, share.ptr, share.size); rn += share.size;
    } else {
        proven_sys_mem_copy(rest, "\\IPC$", 5); rn = 5;   /* the server's own: signed into, its shares listed */
    }
    WCHAR remote[1100], server_w[1100];
    if (!remote_name(server, rest, rn, remote, 1100) || !remote_name(server, "", 0, server_w, 1100)) return RUBRAVIEW_NET_FAILED;
    DWORD err;
    if (kind == RUBRAVIEW_UNC_SHARE) {
        if (GetFileAttributesW(remote) != INVALID_FILE_ATTRIBUTES) return RUBRAVIEW_NET_OPEN;
        err = GetLastError();
    } else {
        SHARE_INFO_1 *info = NULL;
        DWORD read = 0, total = 0, resume = 0;
        err = NetShareEnum(server_w, 1, (LPBYTE *)&info, MAX_PREFERRED_LENGTH, &read, &total, &resume);
        if (info) NetApiBufferFree(info);
        if (err == NERR_Success || err == ERROR_MORE_DATA) return RUBRAVIEW_NET_OPEN;
    }
    if (not_there(err)) { say_why(err, why, why_cap); return RUBRAVIEW_NET_NOT_FOUND; }
    NETRESOURCEW nr = { .dwType = kind == RUBRAVIEW_UNC_SHARE ? RESOURCETYPE_DISK : RESOURCETYPE_ANY, .lpRemoteName = remote };
    DWORD r = WNetAddConnection3W((HWND)owner, &nr, NULL, NULL, CONNECT_INTERACTIVE | CONNECT_PROMPT);
    if (r == NO_ERROR) return RUBRAVIEW_NET_LOGGED_IN;
    if (r == ERROR_CANCELLED) return RUBRAVIEW_NET_CANCELLED;
    if (not_there(r)) { say_why(r, why, why_cap); return RUBRAVIEW_NET_NOT_FOUND; }
    say_why(r, why, why_cap);
    return RUBRAVIEW_NET_FAILED;
}
