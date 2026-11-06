/*
 * RV-223 (D-278): files of ours handed to another program — a drag out of the window (OLE's DoDragDrop) and the
 * clipboard as a data object of ours, so the target's report of what it did comes back.
 *
 * Seen on the VM (2026-11-06, tests/win/dragout_probe.c): Explorer asks for CF_HDROP while the drag is still held, and
 * the clipboard's data at once and at every right click, so the files are named from the start and written when the
 * button is let go (`ready`) — or, for the clipboard, before it is set. Explorer then says what it did with
 * "Performed DropEffect" (an optimised move returns nothing from DoDragDrop and says move); "Paste Succeeded" did not
 * come. Every other format a target sets on the object is kept and given back, as a shell data object does.
 *
 * RV-226 (D-283, T357): the object also answers IDataObjectAsyncCapability. Explorer then takes the drop on a thread of
 * its own — StartOperation, and DoDragDrop returns at once — and sets "Performed DropEffect" when it has finished (it
 * never called EndOperation in 90 s): that, or EndOperation from another target, is the end the window learns by
 * rubraview_pal_drag_out_finished. One such drop at a time; a drag while one is still taken goes the old way.
 */
#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shldisp.h>
#include <stddef.h>
#include "rubraview/pal/pal_window.h"
#include "rubraview/pal/pal_wide.h"
#include "rubraview/dragout.h"

/* ---- the data object ---- */

enum { KEPT_MAX = 48 };
typedef struct { FORMATETC fe; HGLOBAL data; } kept_t;

typedef struct out_data {
    IDataObject iface;
    IDataObjectAsyncCapability async;   /* RV-226 */
    LONG refs;
    HGLOBAL hdrop;           /* what is handed over, built once */
    DWORD preferred;
    DWORD said;              /* "Performed DropEffect" as the target set it, 0 before */
    BOOL async_mode;         /* offered (a drag), as the target left it */
    bool started, finished, ended, ended_ok;   /* StartOperation; then Performed DropEffect set, or EndOperation */
    DWORD ended_effect;
    kept_t kept[KEPT_MAX];
    int nkept;
} out_data_t;

static CLIPFORMAT g_cf_preferred, g_cf_performed;
static out_data_t *g_clip;           /* ours on the clipboard, while it is (one reference held here) */
static volatile LONG g_clip_said;    /* the effect its last paste reported, until it is read */
static bool g_dragging;
static out_data_t *g_async;          /* RV-226: a drop the target still takes (one reference held here) */

static void formats_register(void) {
    if (!g_cf_preferred) g_cf_preferred = (CLIPFORMAT)RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    if (!g_cf_performed) g_cf_performed = (CLIPFORMAT)RegisterClipboardFormatW(CFSTR_PERFORMEDDROPEFFECT);
}

static HGLOBAL global_copy(HGLOBAL src) {
    SIZE_T n = GlobalSize(src);
    HGLOBAL g = n ? GlobalAlloc(GMEM_MOVEABLE, n) : NULL;
    void *to = g ? GlobalLock(g) : NULL, *from = to ? GlobalLock(src) : NULL;
    if (!from) {
        if (to) GlobalUnlock(g);
        if (g) GlobalFree(g);
        return NULL;
    }
    proven_sys_mem_copy(to, from, n);
    GlobalUnlock(src);
    GlobalUnlock(g);
    return g;
}

static HGLOBAL global_dword(DWORD v) {
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    DWORD *p = g ? (DWORD *)GlobalLock(g) : NULL;
    if (!p) { if (g) GlobalFree(g); return NULL; }
    *p = v;
    GlobalUnlock(g);
    return g;
}

/* A DROPFILES of the paths in UTF-16, '/' made '\', each NUL-ended and the last twice. */
static HGLOBAL hdrop_make(const proven_u8str_view_t *paths, size_t count) {
    if (!paths || count == 0 || count > 100000) return NULL;
    size_t units = 1;
    for (size_t i = 0; i < count; ++i) {
        proven_result_size_t n = proven_utf8_to_utf16_size(paths[i]);
        if (n.err != PROVEN_OK || n.value == 0) return NULL;
        units += (size_t)n.value + 1;
    }
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + units * sizeof(WCHAR));
    DROPFILES *head = g ? (DROPFILES *)GlobalLock(g) : NULL;
    if (!head) { if (g) GlobalFree(g); return NULL; }
    head->pFiles = sizeof(DROPFILES);
    head->fWide = TRUE;
    WCHAR *at = (WCHAR *)((BYTE *)head + sizeof(DROPFILES)), *end = at + units;
    bool ok = true;
    for (size_t i = 0; ok && i < count; ++i) {
        proven_size_t n = 0;
        ok = proven_utf8_to_utf16(paths[i], (proven_u16 *)at, (proven_size_t)(end - at - 1), &n) == PROVEN_OK;
        for (proven_size_t k = 0; ok && k < n; ++k) if (at[k] == L'/') at[k] = L'\\';
        at += n + 1;   /* zeroed: the NUL is there */
    }
    GlobalUnlock(g);
    if (!ok) { GlobalFree(g); return NULL; }
    return g;
}

static kept_t *kept_find(out_data_t *d, CLIPFORMAT cf) {
    for (int i = 0; i < d->nkept; ++i)
        if (d->kept[i].fe.cfFormat == cf) return &d->kept[i];
    return NULL;
}

static HRESULT STDMETHODCALLTYPE od_query(IDataObject *p, REFIID riid, void **out) {
    if (!out) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDataObject)) {
        *out = p;
        IDataObject_AddRef(p);
        return S_OK;
    }
    if (IsEqualIID(riid, &IID_IDataObjectAsyncCapability)) {   /* RV-226 */
        *out = &((out_data_t *)p)->async;
        IDataObject_AddRef(p);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE od_addref(IDataObject *p) { return (ULONG)InterlockedIncrement(&((out_data_t *)p)->refs); }

static ULONG STDMETHODCALLTYPE od_release(IDataObject *p) {
    out_data_t *d = (out_data_t *)p;
    LONG n = InterlockedDecrement(&d->refs);
    if (n == 0) {
        for (int i = 0; i < d->nkept; ++i) GlobalFree(d->kept[i].data);
        if (d->hdrop) GlobalFree(d->hdrop);
        rubraview_free(d);
    }
    return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE od_getdata(IDataObject *p, FORMATETC *fe, STGMEDIUM *sm) {
    out_data_t *d = (out_data_t *)p;
    if (!fe || !sm) return E_INVALIDARG;
    if (!(fe->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;
    kept_t *k = kept_find(d, fe->cfFormat);
    HGLOBAL g = k ? global_copy(k->data) : fe->cfFormat == CF_HDROP ? global_copy(d->hdrop)
              : fe->cfFormat == g_cf_preferred ? global_dword(d->preferred) : NULL;
    if (!g) return DV_E_FORMATETC;
    sm->tymed = TYMED_HGLOBAL;
    sm->hGlobal = g;
    sm->pUnkForRelease = NULL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE od_getdatahere(IDataObject *p, FORMATETC *fe, STGMEDIUM *sm) { (void)p; (void)fe; (void)sm; return E_NOTIMPL; }

static HRESULT STDMETHODCALLTYPE od_querygetdata(IDataObject *p, FORMATETC *fe) {
    out_data_t *d = (out_data_t *)p;
    if (!fe) return E_INVALIDARG;
    bool have = fe->cfFormat == CF_HDROP || fe->cfFormat == g_cf_preferred || kept_find(d, fe->cfFormat) != NULL;
    return !have ? DV_E_FORMATETC : (fe->tymed & TYMED_HGLOBAL) ? S_OK : DV_E_TYMED;
}

static HRESULT STDMETHODCALLTYPE od_canonical(IDataObject *p, FORMATETC *in, FORMATETC *out) {
    (void)p; (void)in;
    if (out) out->ptd = NULL;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE od_setdata(IDataObject *p, FORMATETC *fe, STGMEDIUM *sm, BOOL release) {
    out_data_t *d = (out_data_t *)p;
    if (!fe || !sm) return E_INVALIDARG;
    if (sm->tymed != TYMED_HGLOBAL) return E_NOTIMPL;
    HGLOBAL copy = global_copy(sm->hGlobal);
    if (!copy) return E_OUTOFMEMORY;
    if (fe->cfFormat == g_cf_performed && GlobalSize(copy) >= sizeof(DWORD)) {
        DWORD *v = (DWORD *)GlobalLock(copy);
        if (v) {
            d->said = *v;
            if (d == g_clip) InterlockedExchange(&g_clip_said, (LONG)*v);
            if (d->started) d->finished = true;   /* RV-226: Explorer's end of a drop it took on its own thread */
            GlobalUnlock(copy);
        }
    }
    kept_t *k = kept_find(d, fe->cfFormat);
    if (!k && d->nkept == KEPT_MAX) { GlobalFree(copy); return E_OUTOFMEMORY; }
    if (!k) k = &d->kept[d->nkept++];
    else GlobalFree(k->data);
    k->fe = *fe;
    k->fe.ptd = NULL;
    k->data = copy;
    if (release) ReleaseStgMedium(sm);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE od_enum(IDataObject *p, DWORD dir, IEnumFORMATETC **out) {
    out_data_t *d = (out_data_t *)p;
    if (!out) return E_POINTER;
    *out = NULL;
    if (dir != DATADIR_GET) return E_NOTIMPL;
    FORMATETC all[KEPT_MAX + 2];
    UINT n = 0;
    all[n++] = (FORMATETC){ CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    all[n++] = (FORMATETC){ g_cf_preferred, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    for (int i = 0; i < d->nkept; ++i) all[n++] = d->kept[i].fe;
    return SHCreateStdEnumFmtEtc(n, all, out);
}

static HRESULT STDMETHODCALLTYPE od_advise(IDataObject *p, FORMATETC *fe, DWORD f, IAdviseSink *s, DWORD *c) {
    (void)p; (void)fe; (void)f; (void)s; (void)c;
    return OLE_E_ADVISENOTSUPPORTED;
}
static HRESULT STDMETHODCALLTYPE od_unadvise(IDataObject *p, DWORD c) { (void)p; (void)c; return OLE_E_ADVISENOTSUPPORTED; }
static HRESULT STDMETHODCALLTYPE od_enumadvise(IDataObject *p, IEnumSTATDATA **e) { (void)p; (void)e; return OLE_E_ADVISENOTSUPPORTED; }

/* ---- IDataObjectAsyncCapability (RV-226): the target may take the drop on a thread of its own ---- */

static out_data_t *of_async(IDataObjectAsyncCapability *a) { return (out_data_t *)(void *)((char *)a - offsetof(out_data_t, async)); }
static HRESULT STDMETHODCALLTYPE oa_query(IDataObjectAsyncCapability *a, REFIID riid, void **out) { return od_query(&of_async(a)->iface, riid, out); }
static ULONG STDMETHODCALLTYPE oa_addref(IDataObjectAsyncCapability *a) { return od_addref(&of_async(a)->iface); }
static ULONG STDMETHODCALLTYPE oa_release(IDataObjectAsyncCapability *a) { return od_release(&of_async(a)->iface); }
static HRESULT STDMETHODCALLTYPE oa_setmode(IDataObjectAsyncCapability *a, BOOL on) { of_async(a)->async_mode = on; return S_OK; }
static HRESULT STDMETHODCALLTYPE oa_getmode(IDataObjectAsyncCapability *a, BOOL *on) {
    if (!on) return E_POINTER;
    *on = of_async(a)->async_mode;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE oa_start(IDataObjectAsyncCapability *a, IBindCtx *ctx) {
    (void)ctx;
    of_async(a)->started = true;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE oa_inop(IDataObjectAsyncCapability *a, BOOL *in) {
    if (!in) return E_POINTER;
    out_data_t *d = of_async(a);
    *in = d->started && !d->finished;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE oa_end(IDataObjectAsyncCapability *a, HRESULT hr, IBindCtx *ctx, DWORD effects) {
    (void)ctx;
    out_data_t *d = of_async(a);
    d->ended = true;
    d->ended_ok = SUCCEEDED(hr);
    d->ended_effect = effects;
    d->finished = true;
    return S_OK;
}
static IDataObjectAsyncCapabilityVtbl g_oa_vtbl = { oa_query, oa_addref, oa_release, oa_setmode, oa_getmode, oa_start, oa_inop, oa_end };

static IDataObjectVtbl g_od_vtbl = { od_query, od_addref, od_release, od_getdata, od_getdatahere, od_querygetdata,
                                     od_canonical, od_setdata, od_enum, od_advise, od_unadvise, od_enumadvise };

static out_data_t *out_data_new(const proven_u8str_view_t *paths, size_t count, DWORD preferred) {
    formats_register();
    out_data_t *d = rubraview_alloc_zero(1, sizeof *d);
    if (!d) return NULL;
    d->iface.lpVtbl = &g_od_vtbl;
    d->async.lpVtbl = &g_oa_vtbl;
    d->refs = 1;
    d->preferred = preferred;
    d->hdrop = hdrop_make(paths, count);
    if (!d->hdrop) { rubraview_free(d); return NULL; }
    return d;
}

/* An Esc that gave a drag or its wait up is that only: it is let go, and taken off the queue before the window's own
   keys see it (they would close the open-file screen). */
static void swallow_escape(void) {
    for (int i = 0; i < 200 && (GetAsyncKeyState(VK_ESCAPE) & 0x8000); ++i) Sleep(10);
    MSG msg;
    while (PeekMessageW(&msg, NULL, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE)) {
        bool esc = msg.wParam == VK_ESCAPE && (msg.message == WM_KEYDOWN || msg.message == WM_KEYUP || msg.message == WM_CHAR);
        if (esc) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

/* ---- the drag ---- */

typedef struct out_source {
    IDropSource iface;
    LONG refs;
    DWORD button;            /* MK_LBUTTON or MK_RBUTTON: the drag ends when it is let go */
    DWORD shown;             /* the effect the target last offered (GiveFeedback) */
    bool (*ready)(void *ctx);
    void *ctx;
} out_source_t;

static HRESULT STDMETHODCALLTYPE os_query(IDropSource *p, REFIID riid, void **out) {
    if (!out) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDropSource)) {
        *out = p;
        IDropSource_AddRef(p);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE os_addref(IDropSource *p) { return (ULONG)InterlockedIncrement(&((out_source_t *)p)->refs); }
static ULONG STDMETHODCALLTYPE os_release(IDropSource *p) { return (ULONG)InterlockedDecrement(&((out_source_t *)p)->refs); }   /* on the stack */

static HRESULT STDMETHODCALLTYPE os_continue(IDropSource *p, BOOL escape, DWORD keys) {
    out_source_t *s = (out_source_t *)p;
    if (escape) return DRAGDROP_S_CANCEL;
    if (keys & s->button) return S_OK;
    /* Let go: over nothing that takes it, nothing is written; over a target, the files are written now, before it
       takes them (a right drag's menu comes after). */
    if ((s->shown & (DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK)) == 0) return DRAGDROP_S_CANCEL;
    if (s->ready && !s->ready(s->ctx)) return DRAGDROP_S_CANCEL;
    return DRAGDROP_S_DROP;
}

static HRESULT STDMETHODCALLTYPE os_feedback(IDropSource *p, DWORD effect) {
    ((out_source_t *)p)->shown = effect;
    return DRAGDROP_S_USEDEFAULTCURSORS;
}

static IDropSourceVtbl g_os_vtbl = { os_query, os_addref, os_release, os_continue, os_feedback };

bool rubraview_pal_dragging_out(void) { return g_dragging; }

uint32_t rubraview_pal_drag_out(rubraview_window_t *window, const proven_u8str_view_t *paths, size_t count, uint32_t allowed,
                                bool right, bool (*ready)(void *ctx), void *ctx) {
    (void)window;
    if (g_dragging) return 0;
    out_data_t *d = out_data_new(paths, count, DROPEFFECT_COPY);   /* a left drag copies, onto the same drive too */
    if (!d) return 0;
    out_source_t s = { .iface = { &g_os_vtbl }, .refs = 1, .button = right ? MK_RBUTTON : MK_LBUTTON, .ready = ready, .ctx = ctx };
    d->async_mode = g_async == NULL;   /* RV-226: one drop taken asynchronously at a time */
    DWORD effect = DROPEFFECT_NONE;
    g_dragging = true;
    HRESULT hr = DoDragDrop(&d->iface, &s.iface, allowed & (DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK), &effect);
    g_dragging = false;
    if (hr == DRAGDROP_S_CANCEL) swallow_escape();
    if (hr == DRAGDROP_S_DROP && d->started && !d->finished) {
        g_async = d;   /* the target works on: our reference kept until it says it has finished */
        return RUBRAVIEW_DROP_PENDING;
    }
    uint32_t done = hr != DRAGDROP_S_DROP ? 0u
                  : d->started ? rubraview_dragout_async_result((uint32_t)d->said, d->ended, d->ended_ok, (uint32_t)d->ended_effect)
                  : rubraview_dragout_performed((uint32_t)effect, (uint32_t)d->said);
    IDataObject_Release(&d->iface);
    return done;
}

bool rubraview_pal_drag_out_pending(void) { return g_async != NULL; }

bool rubraview_pal_drag_out_finished(uint32_t *done) {
    if (!g_async || !g_async->finished) return false;
    out_data_t *d = g_async;
    g_async = NULL;
    if (done) *done = rubraview_dragout_async_result((uint32_t)d->said, d->ended, d->ended_ok, (uint32_t)d->ended_effect);
    IDataObject_Release(&d->iface);
    return true;
}

void rubraview_pal_drag_out_forget(void) {
    if (!g_async) return;
    IDataObject_Release(&g_async->iface);   /* the target's own reference keeps it while it still needs it */
    g_async = NULL;
}

/* ---- the clipboard ---- */

void rubraview_pal_clipboard_let_go(void) {
    if (!g_clip) return;
    if (OleIsCurrentClipboard(&g_clip->iface) == S_OK) OleSetClipboard(NULL);   /* never OleFlushClipboard: it renders */
    IDataObject_Release(&g_clip->iface);
    g_clip = NULL;
}

bool rubraview_pal_clipboard_set_files_told(rubraview_window_t *window, const proven_u8str_view_t *paths, size_t count, bool moving) {
    (void)window;
    rubraview_pal_clipboard_let_go();
    out_data_t *d = out_data_new(paths, count, moving ? DROPEFFECT_MOVE : (DROPEFFECT_COPY | DROPEFFECT_LINK));
    if (!d) return false;
    InterlockedExchange(&g_clip_said, 0);
    if (FAILED(OleSetClipboard(&d->iface))) {
        IDataObject_Release(&d->iface);
        return false;
    }
    g_clip = d;   /* our reference, beside the clipboard's */
    return true;
}

uint32_t rubraview_pal_clipboard_said(void) {
    LONG v = InterlockedExchange(&g_clip_said, 0);
    return rubraview_dragout_performed(0, (uint32_t)v);
}

bool rubraview_pal_clipboard_is_ours(void) {
    return g_clip && OleIsCurrentClipboard(&g_clip->iface) == S_OK;
}

/* ---- waiting, with a word on screen ---- */

typedef struct wait_view { const WCHAR *text; double share; } wait_view_t;

static LRESULT CALLBACK wait_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        wait_view_t *v = (wait_view_t *)GetWindowLongPtrW(h, GWLP_USERDATA);
        RECT r;
        GetClientRect(h, &r);
        FillRect(dc, &r, (HBRUSH)(COLOR_WINDOW + 1));
        if (v) {
            RECT bar = { r.left + 12, r.bottom - 22, r.left + 12 + (LONG)((double)(r.right - r.left - 24) * (v->share < 0 ? 0 : v->share > 1 ? 1 : v->share)), r.bottom - 12 };
            FillRect(dc, &bar, (HBRUSH)(COLOR_HIGHLIGHT + 1));
            RECT t = { r.left + 12, r.top + 10, r.right - 12, r.bottom - 26 };
            SetBkMode(dc, TRANSPARENT);
            HGDIOBJ old = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
            DrawTextW(dc, v->text, -1, &t, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(dc, old);
        }
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

bool rubraview_pal_wait_with_progress(rubraview_window_t *window, bool (*done)(void *ctx, double *share), void *ctx, proven_u8str_view_t text) {
    static bool registered;
    HINSTANCE inst = GetModuleHandleW(NULL);
    if (!registered) {
        WNDCLASSW wc = { .lpfnWndProc = wait_proc, .hInstance = inst, .lpszClassName = L"RubraviewWait", .hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_WAIT) };
        registered = RegisterClassW(&wc) != 0;
    }
    WCHAR wide[512];
    proven_size_t n = 0;
    if (proven_utf8_to_utf16(text, (proven_u16 *)wide, RUBRAVIEW_COUNT_OF(wide) - 1, &n) != PROVEN_OK) n = 0;
    wide[n] = 0;
    wait_view_t view = { .text = wide, .share = 0.0 };
    HWND owner = window ? (HWND)rubraview_pal_window_native_handle(window) : NULL;
    HWND popup = NULL;
    ULONGLONG start = GetTickCount64();
    bool finished = false, given_up = false;
    while (!(finished = done(ctx, &view.share))) {
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) { given_up = true; break; }
        if (!popup && registered && GetTickCount64() - start > 500) {
            POINT at;
            GetCursorPos(&at);
            popup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"RubraviewWait", L"Rubraview", WS_POPUP | WS_BORDER | WS_VISIBLE,
                                    at.x + 16, at.y + 16, 360, 72, owner, NULL, inst, NULL);
            if (popup) SetWindowLongPtrW(popup, GWLP_USERDATA, (LONG_PTR)&view);
        }
        MSG msg;   /* the small window's own messages only: the viewer's wait for the drag to end */
        if (popup) {
            InvalidateRect(popup, NULL, FALSE);
            while (PeekMessageW(&msg, popup, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        }
        Sleep(30);   /* the viewer's messages wait in its queue: no wake for them here */
    }
    if (popup) DestroyWindow(popup);
    if (given_up) swallow_escape();
    return finished && !given_up;
}
