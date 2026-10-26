#ifdef _WIN32
#define _CRT_RAND_S
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <dxgi.h>
#include <stdatomic.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rubraview/pal/pal_media_host.h"
#include "rubraview/pal/pal_media.h"
#include "rubraview/lang.h"
#include "rubraview/stream.h"
#include "rubraview/pal/pal_audio.h"

/*
 * RV-176 (D-208, owner 2026-10-26: "FFmpeg를 별도 프로세스로", every use at once, on as it comes, Windows'
 * decoder after a stop). FFmpeg runs in `rubraview.exe --media-host`, a child in a job object that ends with the
 * viewer, sharing one file mapping (rubraview_mhost_block_t) and two events, as the plug-in host does (D-186).
 * A film, a song or a station has a child of its own; the one-shot uses share one long-lived service child, so
 * a bad picture cannot end a film. A child that ends or stops answering is ended; the viewer goes on.
 *
 * Everything a child writes is read as hostile: copied out of the mapping and checked by media_host.c, a
 * mapping it names opened only when the name is one of ours and only as large as was said.
 */

#define OPEN_WAIT_MS     22000u   /* FFmpeg's own open waits 15 s, a station 8 s a try */
#define PROBE_WAIT_MS    15000u
#define JOB_WAIT_MS      120000u  /* a picture written or a filter run on a large picture */
#define SMALL_WAIT_MS    10000u
#define CLOSE_WAIT_MS    3000u
#define HUNG_SECONDS     5.0      /* a playback child whose count stands still this long is ended */
#define REPLY_MAX        ((uint64_t)1 << 31)

/* Our own copies, as the renderer keeps them (MinGW's libraries lack some). */
static const GUID MH_IID_IDXGIDevice = { 0x54ec77fa, 0x1377, 0x44e6, { 0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c } };
static const GUID MH_IID_IDXGIFactory1 = { 0x770aae78, 0xf26f, 0x4dba, { 0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87 } };
static const GUID MH_IID_ID3D11Texture2D = { 0x6f15aaf2, 0xd208, 0x4e89, { 0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c } };
static const GUID MH_IID_ID3D11VideoDevice = { 0x10ec4d5b, 0x975a, 0x4689, { 0xb9, 0xe4, 0xd0, 0xaa, 0xc3, 0x0f, 0xe3, 0x33 } };
static const GUID MH_IID_ID3D11Multithread = { 0x9b7e4e00, 0x342c, 0x4106, { 0xa1, 0x9f, 0x4f, 0x27, 0x04, 0xf6, 0x89, 0xf0 } };

static _Atomic bool g_isolated = true;
static bool g_in_child;
static _Atomic int32_t g_fault_kind, g_fault_after;

void rubraview_pal_ffmpeg_set_isolated(bool on) { atomic_store(&g_isolated, on); }
bool rubraview_pal_ffmpeg_remote(void) { return !g_in_child && atomic_load(&g_isolated); }
void rubraview_pal_media_host_fault(int32_t kind, int32_t after) {
    atomic_store(&g_fault_kind, kind);
    atomic_store(&g_fault_after, after);
}

static _Thread_local char g_why[256];
static char g_service_why[256];   /* the service's last words, kept past the calling thread (a save's job) */
static char g_open_why[512];   /* the last remote open's refusal (the UI thread's) */

const char *rubraview_pal_media_host_why(void) { return g_open_why; }


/* ---- the folders FFmpeg is looked for in, handed to each child ---- */

static SRWLOCK g_folders_lock = SRWLOCK_INIT;
static char g_imported[RUBRAVIEW_MHOST_PATH], g_chosen[RUBRAVIEW_MHOST_PATH];
static _Atomic uint32_t g_folders_serial = 1;

static void copy_view(char *dst, size_t cap, proven_u8str_view_t v) {
    size_t n = v.size < cap - 1 ? v.size : cap - 1;
    if (n) memcpy(dst, v.ptr, n);
    dst[n] = '\0';
}

void rubraview_mhost_remote_folders(proven_u8str_view_t imported, proven_u8str_view_t chosen) {
    if (g_in_child) return;
    char a[RUBRAVIEW_MHOST_PATH], b[RUBRAVIEW_MHOST_PATH];
    copy_view(a, sizeof a, imported);
    copy_view(b, sizeof b, chosen);
    AcquireSRWLockExclusive(&g_folders_lock);
    if (strcmp(a, g_imported) != 0 || strcmp(b, g_chosen) != 0) {
        memcpy(g_imported, a, sizeof a);
        memcpy(g_chosen, b, sizeof b);
        atomic_fetch_add(&g_folders_serial, 1);   /* the service child is made again at its next use */
    }
    ReleaseSRWLockExclusive(&g_folders_lock);
}

/* ---- the viewer's sound settings, and the films' children that are told them ---- */

static SRWLOCK g_audio_lock = SRWLOCK_INIT;
static struct {
    uint32_t speed_permille, volume_permille, muted, latency_ms;
    rubraview_audio_chain_config_t chain;
} g_audio = { 1000, 1000, 0, 40, { .eq_preset = RUBRAVIEW_EQ_FLAT, .gain = 1.0 } };

#define PLAYS_MAX 16
typedef struct mh_play mh_play_t;
static mh_play_t *g_plays[PLAYS_MAX];   /* open films' children, newest last; under g_audio_lock */
static size_t g_play_count;

static void audio_write(rubraview_mhost_audio_t *a) {   /* under g_audio_lock */
    a->speed_permille = g_audio.speed_permille;
    a->volume_permille = g_audio.volume_permille;
    a->muted = g_audio.muted;
    a->latency_ms = g_audio.latency_ms;
    a->eq_preset = (int32_t)g_audio.chain.eq_preset;
    a->eq_custom = g_audio.chain.eq_custom;
    a->night_mode = g_audio.chain.night_mode;
    memcpy(a->eq_gains_db, g_audio.chain.eq_gains_db, sizeof a->eq_gains_db);
    a->gain = g_audio.chain.gain;
    atomic_fetch_add_explicit(&a->serial, 1, memory_order_release);
}

static void audio_tell_all(void);

void rubraview_mhost_note_speed(double speed) {
    if (g_in_child) return;
    AcquireSRWLockExclusive(&g_audio_lock);
    g_audio.speed_permille = (uint32_t)lround((speed < 0.25 ? 0.25 : speed > 4.0 ? 4.0 : speed) * 1000.0);
    ReleaseSRWLockExclusive(&g_audio_lock);
    audio_tell_all();
}

void rubraview_mhost_note_volume(double volume, bool muted) {
    if (g_in_child) return;
    AcquireSRWLockExclusive(&g_audio_lock);
    g_audio.volume_permille = (uint32_t)lround((volume < 0.0 ? 0.0 : volume > 1.0 ? 1.0 : volume) * 1000.0);
    g_audio.muted = muted;
    ReleaseSRWLockExclusive(&g_audio_lock);
    audio_tell_all();
}

void rubraview_mhost_note_latency(uint32_t milliseconds) {
    if (g_in_child) return;
    AcquireSRWLockExclusive(&g_audio_lock);
    g_audio.latency_ms = milliseconds;
    ReleaseSRWLockExclusive(&g_audio_lock);
    audio_tell_all();
}

void rubraview_mhost_note_chain(const rubraview_audio_chain_config_t *config) {
    if (g_in_child || !config) return;
    AcquireSRWLockExclusive(&g_audio_lock);
    g_audio.chain = *config;
    ReleaseSRWLockExclusive(&g_audio_lock);
    audio_tell_all();
}

/* ---- mappings ---- */

static void fresh_name(char out[RUBRAVIEW_MHOST_NAME]) {
    uint8_t r[16];
    for (int i = 0; i < 16; i += 4) {
        unsigned int v = 0;
        if (rand_s(&v) != 0) { LARGE_INTEGER t; QueryPerformanceCounter(&t); v = (unsigned int)t.QuadPart ^ (unsigned int)(uintptr_t)&v; }
        memcpy(r + i, &v, 4);
    }
    rubraview_mhost_map_name(out, GetCurrentProcessId(), r);
}

typedef struct mh_map { HANDLE handle; uint8_t *view; uint64_t bytes; } mh_map_t;

static void map_close(mh_map_t *m) {
    if (m->view) UnmapViewOfFile(m->view);
    if (m->handle) CloseHandle(m->handle);
    *m = (mh_map_t){ 0 };
}

/* A mapping of our own, under a fresh name, for the other side to open. */
static bool map_make(mh_map_t *m, uint64_t bytes, char name[RUBRAVIEW_MHOST_NAME]) {
    *m = (mh_map_t){ 0 };
    if (bytes == 0 || bytes > RUBRAVIEW_MHOST_MAP_MAX) return false;
    fresh_name(name);
    m->handle = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, (DWORD)(bytes >> 32), (DWORD)bytes, name);
    if (!m->handle || GetLastError() == ERROR_ALREADY_EXISTS) { map_close(m); return false; }
    m->view = (uint8_t *)MapViewOfFile(m->handle, FILE_MAP_ALL_ACCESS, 0, 0, (SIZE_T)bytes);
    if (!m->view) { map_close(m); return false; }
    m->bytes = bytes;
    return true;
}

/* A mapping the other side named: only a name of our form, only as large as it said and no more is used. */
static bool map_open(mh_map_t *m, const char *name, uint64_t bytes, bool write) {
    *m = (mh_map_t){ 0 };
    char copy[RUBRAVIEW_MHOST_NAME];
    memcpy(copy, name, sizeof copy);
    if (!rubraview_mhost_map_name_ok(copy, sizeof copy) || bytes == 0 || bytes > RUBRAVIEW_MHOST_MAP_MAX) return false;
    DWORD access = write ? FILE_MAP_ALL_ACCESS : FILE_MAP_READ;
    m->handle = OpenFileMappingA(access, FALSE, copy);
    if (!m->handle) return false;
    m->view = (uint8_t *)MapViewOfFile(m->handle, access, 0, 0, (SIZE_T)bytes);
    MEMORY_BASIC_INFORMATION mbi;
    if (!m->view || VirtualQuery(m->view, &mbi, sizeof mbi) != sizeof mbi || mbi.RegionSize < bytes) { map_close(m); return false; }
    m->bytes = bytes;
    return true;
}

/* ---- a child ---- */

typedef struct mh_child {
    HANDLE process, job, mapping, request, reply;
    rubraview_mhost_block_t *block;
    bool dead;
    uint64_t beat;               /* the heartbeat last seen, and when it moved */
    double beat_at;
} mh_child_t;

static double now_seconds(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

static void child_kill(mh_child_t *c, const char *why) {
    if (c->job) TerminateJobObject(c->job, 5);
    else if (c->process) TerminateProcess(c->process, 5);
    if (!c->dead && why) snprintf(g_why, sizeof g_why, "%s", why);
    c->dead = true;
}

static void child_free(mh_child_t *c) {
    if (c->process && !c->dead) child_kill(c, NULL);
    if (c->process) { WaitForSingleObject(c->process, 2000); CloseHandle(c->process); }
    if (c->job) CloseHandle(c->job);
    if (c->block) UnmapViewOfFile(c->block);
    if (c->mapping) CloseHandle(c->mapping);
    if (c->request) CloseHandle(c->request);
    if (c->reply) CloseHandle(c->reply);
    *c = (mh_child_t){ 0 };
}

static uint64_t device_luid(void *device) {
    if (!device) return 0;
    IDXGIDevice *dxgi = NULL;
    IDXGIAdapter *adapter = NULL;
    uint64_t luid = 0;
    if (SUCCEEDED(ID3D11Device_QueryInterface((ID3D11Device *)device, &MH_IID_IDXGIDevice, (void **)&dxgi)) && dxgi) {
        if (SUCCEEDED(IDXGIDevice_GetAdapter(dxgi, &adapter)) && adapter) {
            DXGI_ADAPTER_DESC desc;
            if (SUCCEEDED(IDXGIAdapter_GetDesc(adapter, &desc)))
                luid = (uint64_t)(uint32_t)desc.AdapterLuid.LowPart | ((uint64_t)(uint32_t)desc.AdapterLuid.HighPart << 32);
            IDXGIAdapter_Release(adapter);
        }
        IDXGIDevice_Release(dxgi);
    }
    return luid;
}

static bool child_start(mh_child_t *c, rubraview_mhost_mode_t mode, const rubraview_media_gpu_t *gpu) {
    *c = (mh_child_t){ 0 };
    SECURITY_ATTRIBUTES sa = { .nLength = sizeof(sa), .bInheritHandle = TRUE };
    c->mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, (DWORD)sizeof(rubraview_mhost_block_t), NULL);
    c->block = c->mapping ? (rubraview_mhost_block_t *)MapViewOfFile(c->mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(rubraview_mhost_block_t)) : NULL;
    c->request = CreateEventW(&sa, FALSE, FALSE, NULL);
    c->reply = CreateEventW(&sa, FALSE, FALSE, NULL);
    if (!c->block || !c->request || !c->reply) { child_free(c); return false; }
    rubraview_mhost_block_t *b = c->block;
    rubraview_mhost_init(b, mode);
    AcquireSRWLockShared(&g_folders_lock);
    memcpy(b->ffmpeg_imported, g_imported, sizeof b->ffmpeg_imported);
    memcpy(b->ffmpeg_chosen, g_chosen, sizeof b->ffmpeg_chosen);
    ReleaseSRWLockShared(&g_folders_lock);
    if (gpu) {
        b->gpu_mode = gpu->mode;
        b->tone_mapping = gpu->tone_mapping;
        b->adapter_luid = device_luid(gpu->device ? gpu->device : gpu->shader_device);
    }
    if (mode == RUBRAVIEW_MHOST_PLAYBACK) {
        AcquireSRWLockShared(&g_audio_lock);
        audio_write(&b->audio);   /* the sound as the viewer's own would be */
        ReleaseSRWLockShared(&g_audio_lock);
        b->fault_kind = atomic_exchange(&g_fault_kind, 0);   /* the next child only */
        b->fault_after = atomic_load(&g_fault_after);
        /* the VM's checks: RUBRAVIEW_MEDIA_FAULT=crash:N or hang:N, after N pictures */
        char fault[32];
        DWORD n = GetEnvironmentVariableA("RUBRAVIEW_MEDIA_FAULT", fault, sizeof fault);
        if (n > 0 && n < sizeof fault) {
            if (strncmp(fault, "crash:", 6) == 0) { b->fault_kind = 1; b->fault_after = atoi(fault + 6); }
            else if (strncmp(fault, "hang:", 5) == 0) { b->fault_kind = 2; b->fault_after = atoi(fault + 5); }
        }
    }

    WCHAR exe[MAX_PATH], line[MAX_PATH + 128];
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) { child_free(c); return false; }
    _snwprintf(line, sizeof line / sizeof line[0], L"\"%ls\" --media-host %llu %llu %llu", exe,
               (unsigned long long)(uintptr_t)c->mapping, (unsigned long long)(uintptr_t)c->request,
               (unsigned long long)(uintptr_t)c->reply);
    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(attr_size);
    if (!attrs || !InitializeProcThreadAttributeList(attrs, 1, 0, &attr_size)) { free(attrs); child_free(c); return false; }
    HANDLE inherit[3] = { c->mapping, c->request, c->reply };   /* these and nothing else of ours */
    UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof inherit, NULL, NULL);
    STARTUPINFOEXW si = { .StartupInfo = { .cb = sizeof(STARTUPINFOEXW) }, .lpAttributeList = attrs };
    PROCESS_INFORMATION pi = { 0 };
    c->job = CreateJobObjectW(NULL, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = { 0 };
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
    if (c->job) SetInformationJobObject(c->job, JobObjectExtendedLimitInformation, &limits, sizeof limits);
    BOOL started = CreateProcessW(exe, line, NULL, NULL, TRUE, CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW,
                                  NULL, NULL, &si.StartupInfo, &pi);
    DeleteProcThreadAttributeList(attrs);
    free(attrs);
    if (!started) { child_free(c); return false; }
    if (c->job) AssignProcessToJobObject(c->job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    c->process = pi.hProcess;
    c->beat_at = now_seconds();
    return true;
}

static bool child_alive(mh_child_t *c) {
    if (!c->process || c->dead) return false;
    if (WaitForSingleObject(c->process, 0) == WAIT_OBJECT_0) { c->dead = true; snprintf(g_why, sizeof g_why, "%s", N_("FFmpeg stopped: it crashed")); return false; }
    return true;
}

/* A request, waited for: the reply's status, or FAILED when the child ended, did not answer, or answered nonsense. */
static int32_t child_call(mh_child_t *c, uint32_t kind, DWORD wait_ms) {
    if (!child_alive(c)) return RUBRAVIEW_MHOST_FAILED;
    rubraview_mhost_block_t *b = c->block;
    b->request_kind = kind;
    uint32_t serial = atomic_fetch_add_explicit(&b->request_serial, 1, memory_order_release) + 1;
    SetEvent(c->request);
    ULONGLONG begun = GetTickCount64();
    for (;;) {
        ULONGLONG spent = GetTickCount64() - begun;
        if (spent >= wait_ms) { child_kill(c, N_("FFmpeg did not answer in time; it was ended")); return RUBRAVIEW_MHOST_FAILED; }
        HANDLE both[2] = { c->reply, c->process };
        DWORD r = WaitForMultipleObjects(2, both, FALSE, (DWORD)(wait_ms - spent));
        if (r == WAIT_OBJECT_0 + 1) { c->dead = true; snprintf(g_why, sizeof g_why, "%s", N_("FFmpeg stopped: it crashed")); return RUBRAVIEW_MHOST_FAILED; }
        if (r != WAIT_OBJECT_0) continue;
        if (atomic_load_explicit(&b->reply_serial, memory_order_acquire) != serial) continue;   /* an older one's */
        int32_t status = b->reply_status;
        if (status != RUBRAVIEW_MHOST_OK && status != RUBRAVIEW_MHOST_NEED_PICTURE) {
            char said[256];
            memcpy(said, b->reply_why, sizeof said);
            rubraview_mhost_text_take(g_why, sizeof g_why, said, sizeof said);
            return RUBRAVIEW_MHOST_FAILED;
        }
        return status;
    }
}

static void child_end(mh_child_t *c) {
    if (c->block && child_alive(c)) {
        atomic_store_explicit(&c->block->wanted.quit, 1, memory_order_release);
        (void)child_call(c, RUBRAVIEW_MHOST_REQ_CLOSE, CLOSE_WAIT_MS);
    }
    child_free(c);
}

/* The reply's mapping, opened when its name and size are in order. */
static bool reply_map(mh_child_t *c, uint64_t limit, mh_map_t *m) {
    char name[RUBRAVIEW_MHOST_NAME];
    memcpy(name, c->block->reply_map, sizeof name);
    uint64_t bytes = c->block->reply_map_bytes;
    return rubraview_mhost_size_ok(bytes, limit) && map_open(m, name, bytes, false);
}

/* ======================================================================================== */
/* ---- playback through a child ---- */

typedef struct mh_play {
    mh_child_t child;
    rubraview_mhost_opened_t opened;
    mh_map_t frames;
    size_t tail;
    uint64_t consumer_generation;
    ID3D11Device *device;                 /* the viewer's decode device, where the child's textures are opened */
    struct { uint64_t handle; ID3D11Texture2D *texture; } textures[RUBRAVIEW_MHOST_SLOTS];
    bool stopped;
    uint32_t audio_serial;
} mh_play_t;

static void audio_tell_all(void) {
    AcquireSRWLockShared(&g_audio_lock);
    for (size_t i = 0; i < g_play_count; ++i) if (!g_plays[i]->stopped) audio_write(&g_plays[i]->child.block->audio);
    ReleaseSRWLockShared(&g_audio_lock);
}

static void plays_add(mh_play_t *p) {
    AcquireSRWLockExclusive(&g_audio_lock);
    if (g_play_count < PLAYS_MAX) g_plays[g_play_count++] = p;
    ReleaseSRWLockExclusive(&g_audio_lock);
}

static void plays_remove(mh_play_t *p) {
    AcquireSRWLockExclusive(&g_audio_lock);
    for (size_t i = 0; i < g_play_count; ++i) {
        if (g_plays[i] != p) continue;
        memmove(&g_plays[i], &g_plays[i + 1], (g_play_count - i - 1) * sizeof g_plays[0]);
        g_play_count--;
        break;
    }
    ReleaseSRWLockExclusive(&g_audio_lock);
}

bool rubraview_mhost_remote_tap(float *out, size_t count, size_t *out_count) {
    if (g_in_child) return false;
    bool found = false;
    AcquireSRWLockShared(&g_audio_lock);
    for (size_t i = g_play_count; i-- > 0 && !found;) {   /* the newest film's child that plays sound */
        mh_play_t *p = g_plays[i];
        if (p->stopped || !p->opened.info.audio_output) continue;
        *out_count = rubraview_mhost_tap_take(&p->child.block->tap, out, count);
        found = true;
    }
    ReleaseSRWLockShared(&g_audio_lock);
    return found;
}

static void play_stop(mh_play_t *p, const char *why) {
    if (!p->stopped) child_kill(&p->child, why);
    p->stopped = true;
}

/* The child still lives and its count moves; else it is ended and the film is said to have stopped. */
static bool play_alive(mh_play_t *p) {
    if (p->stopped) return false;
    if (!child_alive(&p->child)) { p->stopped = true; return false; }
    uint64_t beat = atomic_load_explicit(&p->child.block->heartbeat, memory_order_acquire);
    double now = now_seconds();
    if (beat != p->child.beat) { p->child.beat = beat; p->child.beat_at = now; }
    else if (now - p->child.beat_at > HUNG_SECONDS) { play_stop(p, N_("FFmpeg did not answer in time; it was ended")); return false; }
    return true;
}

static void remote_close(void *handle) {
    mh_play_t *p = (mh_play_t *)handle;
    if (!p) return;
    plays_remove(p);
    for (int i = 0; i < RUBRAVIEW_MHOST_SLOTS; ++i) if (p->textures[i].texture) ID3D11Texture2D_Release(p->textures[i].texture);
    map_close(&p->frames);
    if (p->stopped) child_free(&p->child); else child_end(&p->child);
    if (p->device) ID3D11Device_Release(p->device);
    free(p);
}

static bool remote_available(void) { return rubraview_mhost_remote_available(); }

/* The refusal, with the child's last words when it left any. */
static void open_refused(mh_child_t *c, const char *what) {
    char log[200] = { 0 };
    if (c->block) {
        uint32_t used = atomic_load_explicit(&c->block->log_used, memory_order_acquire);
        if (used > RUBRAVIEW_MHOST_LOG) used = RUBRAVIEW_MHOST_LOG;
        size_t from = used > sizeof log - 1 ? used - (sizeof log - 1) : 0;
        char raw[sizeof log];
        memcpy(raw, c->block->log + from, used - from);
        raw[used - from] = 0;
        for (size_t i = 0; raw[i]; ++i) if (raw[i] == '\n') raw[i] = ' ';
        rubraview_mhost_text_take(log, sizeof log, raw, sizeof raw);
    }
    snprintf(g_open_why, sizeof g_open_why, "%s%s%s%s%s", what, g_why[0] ? " (" : "", g_why, g_why[0] ? ")" : "", log[0] ? " — " : "");
    if (log[0]) strncat(g_open_why, log, sizeof g_open_why - strlen(g_open_why) - 1);
}

static void *remote_open(proven_u8str_view_t path, rubraview_media_failure_t *out_failure, rubraview_media_info_t *out_info,
                         const rubraview_media_gpu_t *gpu) {
    *out_failure = RUBRAVIEW_MEDIA_FAIL_FILE;
    g_open_why[0] = g_why[0] = '\0';
    if (path.size == 0 || path.size >= RUBRAVIEW_MHOST_INLINE - 1) { snprintf(g_open_why, sizeof g_open_why, "its name is too long"); return NULL; }
    if (!rubraview_mhost_remote_available()) { snprintf(g_open_why, sizeof g_open_why, "the service process found no FFmpeg"); return NULL; }
    mh_play_t *p = (mh_play_t *)calloc(1, sizeof(*p));
    if (!p) return NULL;
    if (!child_start(&p->child, RUBRAVIEW_MHOST_PLAYBACK, gpu)) { free(p); snprintf(g_open_why, sizeof g_open_why, "the process could not be started"); return NULL; }
    rubraview_mhost_block_t *b = p->child.block;
    memcpy(b->inline_data, path.ptr, path.size);
    b->inline_data[path.size] = 0;
    b->request_inline_bytes = path.size;
    int32_t status = child_call(&p->child, RUBRAVIEW_MHOST_REQ_OPEN, OPEN_WAIT_MS);
    bool ok = status == RUBRAVIEW_MHOST_OK && rubraview_mhost_opened_take(b, &p->opened);
    if (!ok) {
        open_refused(&p->child, status != RUBRAVIEW_MHOST_OK ? "the process did not answer the open"
                              : p->opened.why ? p->opened.why : "refused");
        int32_t said = b->open_failure;
        if (status == RUBRAVIEW_MHOST_OK && said > RUBRAVIEW_MEDIA_OPENED && said <= RUBRAVIEW_MEDIA_FAIL_STATION_NEEDS_FFMPEG)
            *out_failure = (rubraview_media_failure_t)said;
        remote_close(p);
        return NULL;
    }
    if (p->opened.info.has_video && !map_open(&p->frames, p->opened.frame_map, p->opened.frame_bytes * RUBRAVIEW_MHOST_SLOTS, false)) {
        snprintf(g_open_why, sizeof g_open_why, "its pictures' memory could not be opened (%lu)", (unsigned long)GetLastError());
        remote_close(p);
        return NULL;
    }
    void *device = gpu ? (gpu->device ? gpu->device : gpu->shader_device) : NULL;
    if (device) { p->device = (ID3D11Device *)device; ID3D11Device_AddRef(p->device); }
    p->audio_serial = 0;
    plays_add(p);
    *out_failure = RUBRAVIEW_MEDIA_OPENED;
    *out_info = p->opened.info;
    return p;
}

/* A slot texture of the child's, opened on the viewer's decode device once; checked against the film's size. */
static const void *remote_texture(mh_play_t *p, size_t slot, uint64_t handle) {
    if (!p->device || slot >= RUBRAVIEW_MHOST_SLOTS) return NULL;
    if (p->textures[slot].texture && p->textures[slot].handle == handle) return p->textures[slot].texture;
    if (p->textures[slot].texture) { ID3D11Texture2D_Release(p->textures[slot].texture); p->textures[slot].texture = NULL; }
    ID3D11Texture2D *t = NULL;
    if (FAILED(ID3D11Device_OpenSharedResource(p->device, (HANDLE)(uintptr_t)handle, &MH_IID_ID3D11Texture2D, (void **)&t)) || !t) return NULL;
    D3D11_TEXTURE2D_DESC d;
    ID3D11Texture2D_GetDesc(t, &d);
    if (d.Width < (UINT)p->opened.info.width || d.Height < (UINT)p->opened.info.height || d.Format != DXGI_FORMAT_B8G8R8A8_UNORM ||
        d.ArraySize != 1) {
        ID3D11Texture2D_Release(t);
        return NULL;
    }
    p->textures[slot].handle = handle;
    p->textures[slot].texture = t;
    return t;
}

static int32_t remote_waiting(mh_play_t *p) {
    size_t head = atomic_load_explicit(&p->child.block->ring.head, memory_order_acquire);
    int32_t n = rubraview_mhost_ring_waiting(head, p->tail);
    if (n < 0) play_stop(p, N_("FFmpeg stopped: it said something impossible"));
    return n < 0 ? 0 : n;
}

static void remote_release(mh_play_t *p) {
    p->tail++;
    atomic_store_explicit(&p->child.block->ring.tail, p->tail, memory_order_release);
}

static bool remote_peek_frame(void *handle, rubraview_video_frame_t *out_frame) {
    mh_play_t *p = (mh_play_t *)handle;
    if (!p || !out_frame || p->stopped || !p->opened.info.has_video) return false;
    while (remote_waiting(p) > 0) {
        size_t slot = p->tail % RUBRAVIEW_MHOST_SLOTS;
        rubraview_mhost_slot_t s;
        if (!rubraview_mhost_slot_take(&p->child.block->slots[slot], &s)) { play_stop(p, N_("FFmpeg stopped: it said something impossible")); return false; }
        if (s.generation != p->consumer_generation) { remote_release(p); continue; }   /* decoded before the latest seek */
        const void *texture = NULL;
        if (s.on_gpu && !(texture = remote_texture(p, slot, s.shared_handle))) { remote_release(p); continue; }   /* not one to show */
        *out_frame = (rubraview_video_frame_t){
            .pixels = s.on_gpu ? NULL : p->frames.view + slot * p->opened.frame_bytes,
            .width = p->opened.info.width, .height = p->opened.info.height, .stride = p->opened.info.width * 4,
            .pts = s.pts, .duration = s.duration, .gpu_texture = texture, .gpu_subresource = 0,
        };
        return true;
    }
    return false;
}

static void remote_pop_frame(void *handle) {
    mh_play_t *p = (mh_play_t *)handle;
    if (p && !p->stopped && remote_waiting(p) > 0) remote_release(p);
}

static size_t remote_frames_ready(void *handle) {
    mh_play_t *p = (mh_play_t *)handle;
    return p && !p->stopped ? (size_t)remote_waiting(p) : 0;
}

static void remote_seek(void *handle, double seconds) {
    mh_play_t *p = (mh_play_t *)handle;
    if (!p || p->stopped || p->opened.info.live) return;
    if (!(seconds > 0.0)) seconds = 0.0;
    rubraview_mhost_wanted_t *w = &p->child.block->wanted;
    atomic_store_explicit(&w->seek_100ns, (int64_t)llround(seconds * 1e7), memory_order_relaxed);
    p->consumer_generation++;
    atomic_store_explicit(&w->seek_generation, p->consumer_generation, memory_order_release);
}

static bool remote_seen(mh_play_t *p, rubraview_mhost_seen_t *seen) {
    if (!play_alive(p)) return false;
    if (!rubraview_mhost_observed_take(&p->child.block->observed, seen)) {
        /* being written, or nonsense: once is a race, nonsense stays nonsense */
        Sleep(0);
        if (!rubraview_mhost_observed_take(&p->child.block->observed, seen)) return false;
    }
    return true;
}

static bool remote_finished(void *handle) {
    mh_play_t *p = (mh_play_t *)handle;
    if (!p || p->stopped || !play_alive(p)) return true;
    rubraview_video_frame_t frame;
    if (remote_peek_frame(p, &frame)) return false;
    rubraview_mhost_seen_t seen;
    if (!remote_seen(p, &seen)) return p->stopped;
    return seen.end_generation == p->consumer_generation && seen.drained;
}

static bool remote_sound_over(void *handle) {
    mh_play_t *p = (mh_play_t *)handle;
    rubraview_mhost_seen_t seen;
    return p && p->opened.info.audio_output && remote_seen(p, &seen) && seen.drained;
}

static void remote_set_paused(void *handle, bool paused) {
    mh_play_t *p = (mh_play_t *)handle;
    if (p && !p->stopped) atomic_store_explicit(&p->child.block->wanted.paused, paused ? 1 : 0, memory_order_release);
}

static bool remote_audio_position(void *handle, double *out_position, double *out_wall) {
    mh_play_t *p = (mh_play_t *)handle;
    rubraview_mhost_seen_t seen;
    if (!p || !remote_seen(p, &seen) || !seen.position_known) return false;
    *out_position = seen.position;
    *out_wall = seen.wall;
    return true;
}

static bool remote_audio_format(void *handle, uint32_t *out_rate, uint32_t *out_channels) {
    mh_play_t *p = (mh_play_t *)handle;
    rubraview_mhost_seen_t seen;
    if (!p || !remote_seen(p, &seen)) return false;
    *out_rate = seen.played_rate;
    *out_channels = seen.played_channels;
    return seen.played_rate > 0 && seen.played_channels > 0;
}

static bool remote_hdr_peak(void *handle, double *out_nits) {
    mh_play_t *p = (mh_play_t *)handle;
    rubraview_mhost_seen_t seen;
    if (!p || !p->opened.info.hdr_tone_mapped || !remote_seen(p, &seen)) return false;
    *out_nits = seen.tone_peak;
    return true;
}

static bool remote_stream_title(void *handle, char *out, size_t cap) {
    mh_play_t *p = (mh_play_t *)handle;
    rubraview_mhost_seen_t seen;
    if (!p || !p->opened.info.live || !out || cap == 0 || !remote_seen(p, &seen) || !seen.icy_title[0]) return false;
    snprintf(out, cap, "%s", seen.icy_title);
    return true;
}

static bool remote_tracks(void *handle, rubraview_track_set_t *out_set) {
    mh_play_t *p = (mh_play_t *)handle;
    if (!p || !out_set) return false;
    *out_set = p->opened.tracks;
    return p->opened.tracks.count > 0;
}

static const rubraview_details_t *remote_details(void *handle) {
    mh_play_t *p = (mh_play_t *)handle;
    return p ? &p->opened.details : NULL;
}

static bool remote_select_audio_track(void *handle, int32_t stream_index) {
    mh_play_t *p = (mh_play_t *)handle;
    if (!p || p->stopped || stream_index < 0) return false;
    bool known = false;
    for (size_t i = 0; i < p->opened.tracks.count; ++i)
        if (p->opened.tracks.tracks[i].kind == RUBRAVIEW_TRACK_AUDIO && p->opened.tracks.tracks[i].stream_index == stream_index) known = true;
    if (!known || !p->opened.info.audio_output) return false;
    rubraview_mhost_wanted_t *w = &p->child.block->wanted;
    atomic_store_explicit(&w->audio_stream, stream_index, memory_order_relaxed);
    atomic_store_explicit(&w->audio_serial, ++p->audio_serial, memory_order_release);
    return true;
}

static void remote_set_gain(void *handle, double gain) {
    mh_play_t *p = (mh_play_t *)handle;
    if (!p || p->stopped || !(gain >= 0.0)) return;
    if (gain > 4.0) gain = 4.0;
    atomic_store_explicit(&p->child.block->wanted.gain_millionths, (int32_t)llround(gain * 1e6), memory_order_release);
}

static proven_u8str_view_t remote_read_subtitle_stream(void *handle, proven_arena_t *arena, int32_t stream_index) {
    mh_play_t *p = (mh_play_t *)handle;
    proven_u8str_view_t empty = { 0 };
    if (!p || !arena || stream_index < 0 || !play_alive(p)) return empty;
    p->child.block->request_args[0] = stream_index;
    if (child_call(&p->child, RUBRAVIEW_MHOST_REQ_SUBTITLE, SMALL_WAIT_MS * 3) != RUBRAVIEW_MHOST_OK) {
        if (p->child.dead) p->stopped = true;
        return empty;
    }
    mh_map_t m;
    if (!reply_map(&p->child, RUBRAVIEW_MHOST_SUBTITLE_MAX, &m)) return empty;
    proven_result_mem_mut_t res = proven_arena_alloc(arena, (size_t)m.bytes + 1);
    proven_u8str_view_t text = empty;
    if (proven_is_ok(res.err)) {
        memcpy(res.value.ptr, m.view, (size_t)m.bytes);
        res.value.ptr[m.bytes] = 0;
        text = (proven_u8str_view_t){ .ptr = res.value.ptr, .size = (size_t)m.bytes };
    }
    map_close(&m);
    return text;
}

static bool remote_stopped(void *handle) {
    mh_play_t *p = (mh_play_t *)handle;
    return p && (p->stopped || !play_alive(p));
}

static const rubraview_media_backend_api_t REMOTE_API = {
    .available = remote_available,
    .open = remote_open,
    .close = remote_close,
    .peek_frame = remote_peek_frame,
    .pop_frame = remote_pop_frame,
    .frames_ready = remote_frames_ready,
    .seek = remote_seek,
    .finished = remote_finished,
    .sound_over = remote_sound_over,
    .set_paused = remote_set_paused,
    .audio_position = remote_audio_position,
    .audio_format = remote_audio_format,
    .hdr_peak = remote_hdr_peak,
    .stream_title = remote_stream_title,
    .tracks = remote_tracks,
    .details = remote_details,
    .select_audio_track = remote_select_audio_track,
    .set_gain = remote_set_gain,
    .read_subtitle_stream = remote_read_subtitle_stream,
    .stopped = remote_stopped,
};

const rubraview_media_backend_api_t *rubraview_media_backend_remote(void) { return &REMOTE_API; }

/* ======================================================================================== */
/* ---- the service child: the one-shot uses ---- */

static SRWLOCK g_service_lock = SRWLOCK_INIT;
static mh_child_t g_service;
static uint32_t g_service_folders;

typedef struct mh_probe {
    bool done;
    uint32_t folders;
    bool available;
    int32_t refused, place, jxl, ffilter;
    uint32_t encoders;
    char names[256];
} mh_probe_t;
static mh_probe_t g_probe;

/* Under the lock: a live service child, made again when it died or the folders changed. */
static bool service_ready(void) {
    uint32_t folders = atomic_load(&g_folders_serial);
    if (g_service.process && (!child_alive(&g_service) || g_service_folders != folders)) child_free(&g_service);
    if (!g_service.process) {
        if (!child_start(&g_service, RUBRAVIEW_MHOST_SERVICE, NULL)) { snprintf(g_why, sizeof g_why, "%s", N_("FFmpeg could not be started")); return false; }
        g_service_folders = folders;
    }
    return true;
}

static SRWLOCK g_probe_lock = SRWLOCK_INIT;   /* the answer, read without waiting on a job the service runs */

static mh_probe_t probe(void) {
    uint32_t folders = atomic_load(&g_folders_serial);
    AcquireSRWLockShared(&g_probe_lock);
    mh_probe_t cached = g_probe;
    ReleaseSRWLockShared(&g_probe_lock);
    if (cached.done && cached.folders == folders) return cached;
    AcquireSRWLockExclusive(&g_service_lock);
    if (!g_probe.done || g_probe.folders != folders) {
        mh_probe_t got = { .done = true, .folders = folders };
        if (service_ready() && child_call(&g_service, RUBRAVIEW_MHOST_REQ_PROBE, PROBE_WAIT_MS) == RUBRAVIEW_MHOST_OK) {
            const int64_t *a = g_service.block->reply_args;
            int64_t v[6];
            memcpy(v, a, sizeof v);
            if (v[0] >= 0 && v[0] <= 1 && v[1] >= RUBRAVIEW_FFMPEG_NOT_REFUSED && v[1] <= RUBRAVIEW_FFMPEG_NOT_WHOLE &&
                v[2] >= 0 && v[2] <= 3 && v[3] >= 0 && v[3] <= 2 && v[4] >= 0 && v[4] <= 15 && v[5] >= 0 && v[5] <= RUBRAVIEW_FFILTER_READY) {
                got.available = v[0] == 1;
                got.refused = (int32_t)v[1];
                got.place = (int32_t)v[2];
                got.jxl = (int32_t)v[3];
                got.encoders = (uint32_t)v[4];
                got.ffilter = (int32_t)v[5];
                char said[256];
                memcpy(said, g_service.block->inline_data, sizeof said - 1);
                said[sizeof said - 1] = 0;
                rubraview_mhost_text_take(got.names, sizeof got.names, said, sizeof said);
            }
        }
        AcquireSRWLockExclusive(&g_probe_lock);
        g_probe = got;   /* a child that could not say is no FFmpeg, until the folders change */
        ReleaseSRWLockExclusive(&g_probe_lock);
    }
    mh_probe_t out = g_probe;
    ReleaseSRWLockExclusive(&g_service_lock);
    return out;
}

bool rubraview_mhost_remote_available(void) { return probe().available; }
int rubraview_mhost_remote_place(void) { return probe().place; }
rubraview_ffmpeg_refusal_t rubraview_mhost_remote_refused(void) { return (rubraview_ffmpeg_refusal_t)probe().refused; }
int rubraview_mhost_remote_jxl_state(void) { return probe().jxl; }
rubraview_ffilter_state_t rubraview_mhost_remote_filter_state(void) { return (rubraview_ffilter_state_t)probe().ffilter; }

uint32_t rubraview_mhost_remote_image_encoders(char *names, size_t size) {
    mh_probe_t p = probe();
    if (names && size) snprintf(names, size, "%s", p.names);
    return p.encoders;
}

/* A request whose payload goes in a mapping of ours. */
static bool service_payload(mh_map_t *m, const void *data, size_t size) {
    char name[RUBRAVIEW_MHOST_NAME];
    if (!map_make(m, size, name)) return false;
    if (data) memcpy(m->view, data, size);
    memcpy(g_service.block->request_map, name, sizeof name);
    g_service.block->request_map_bytes = size;
    return true;
}

static void service_no_payload(void) {
    memset(g_service.block->request_map, 0, sizeof g_service.block->request_map);
    g_service.block->request_map_bytes = 0;
}

int64_t rubraview_mhost_remote_fetch(proven_u8str_view_t address, uint8_t *out, size_t cap) {
    if (!out || cap == 0 || address.size == 0 || address.size >= RUBRAVIEW_MHOST_INLINE - 1) return -1;
    int64_t got = -1;
    AcquireSRWLockExclusive(&g_service_lock);
    if (service_ready()) {
        rubraview_mhost_block_t *b = g_service.block;
        memcpy(b->inline_data, address.ptr, address.size);
        b->inline_data[address.size] = 0;
        b->request_inline_bytes = address.size;
        b->request_args[0] = (int64_t)cap;
        service_no_payload();
        mh_map_t m;
        if (child_call(&g_service, RUBRAVIEW_MHOST_REQ_FETCH, SMALL_WAIT_MS * 2) == RUBRAVIEW_MHOST_OK && reply_map(&g_service, cap, &m)) {
            memcpy(out, m.view, (size_t)m.bytes);
            got = (int64_t)m.bytes;
            map_close(&m);
        }
    }
    ReleaseSRWLockExclusive(&g_service_lock);
    return got;
}

bool rubraview_mhost_remote_jxl_decode(const uint8_t *data, size_t size, int32_t max_side,
                                       uint8_t *(*room)(void *context, int32_t width, int32_t height),
                                       void *context, int32_t *out_width, int32_t *out_height) {
    if (!data || size == 0 || !room || size > (size_t)1 << 30) return false;
    bool ok = false;
    AcquireSRWLockExclusive(&g_service_lock);
    mh_map_t in = { 0 };
    if (service_ready() && service_payload(&in, data, size)) {
        rubraview_mhost_block_t *b = g_service.block;
        b->request_args[0] = max_side;
        mh_map_t m;
        if (child_call(&g_service, RUBRAVIEW_MHOST_REQ_JXL_DECODE, JOB_WAIT_MS / 2) == RUBRAVIEW_MHOST_OK) {
            int64_t w = b->reply_args[0], h = b->reply_args[1];
            int32_t side = max_side > 0 && max_side <= RUBRAVIEW_MHOST_MAX_SIDE ? max_side : RUBRAVIEW_MHOST_MAX_SIDE;
            if (w > 0 && h > 0 && w <= side && h <= side && reply_map(&g_service, REPLY_MAX, &m)) {
                if (m.bytes == (uint64_t)w * (uint64_t)h * 4u) {
                    uint8_t *dst = room(context, (int32_t)w, (int32_t)h);
                    if (dst) {
                        memcpy(dst, m.view, (size_t)m.bytes);
                        if (out_width) *out_width = (int32_t)w;
                        if (out_height) *out_height = (int32_t)h;
                        ok = true;
                    }
                }
                map_close(&m);
            }
        }
    }
    map_close(&in);
    ReleaseSRWLockExclusive(&g_service_lock);
    return ok;
}

bool rubraview_mhost_remote_jxl_frames(const uint8_t *data, size_t size, int32_t max_side,
                                       uint8_t *(*room)(void *context, int32_t width, int32_t height, double seconds),
                                       void *context) {
    if (!data || size == 0 || !room || size > (size_t)1 << 30) return false;
    uint32_t given = 0;
    AcquireSRWLockExclusive(&g_service_lock);
    mh_map_t in = { 0 };
    if (service_ready() && service_payload(&in, data, size)) {
        g_service.block->request_args[0] = max_side;
        mh_map_t m;
        if (child_call(&g_service, RUBRAVIEW_MHOST_REQ_JXL_FRAMES, JOB_WAIT_MS) == RUBRAVIEW_MHOST_OK && reply_map(&g_service, REPLY_MAX, &m)) {
            uint32_t count = rubraview_mhost_frame_count(m.view, (size_t)m.bytes);
            for (uint32_t i = 0; i < count; ++i) {
                rubraview_mhost_frame_ref_t f;
                if (!rubraview_mhost_frame_at(m.view, (size_t)m.bytes, i, max_side, &f)) break;
                uint8_t *dst = room(context, f.width, f.height, f.seconds);
                if (!dst) break;
                memcpy(dst, m.view + f.offset, (size_t)f.width * (size_t)f.height * 4u);
                given++;
            }
            map_close(&m);
        }
    }
    map_close(&in);
    ReleaseSRWLockExclusive(&g_service_lock);
    return given > 0;
}

static void still_args(int64_t *a, const rubraview_ffmpeg_still_t *how) {
    a[3] = how && how->avif;
    a[4] = how ? how->quality : 90;
    a[5] = how && how->chroma444;
    a[6] = how ? how->bits : 8;
    a[7] = how && how->moving;
}

/* The written file's bytes out of the reply, as malloc'd memory the caller frees. */
static uint8_t *service_bytes(size_t *out_size) {
    mh_map_t m;
    uint8_t *bytes = NULL;
    if (reply_map(&g_service, REPLY_MAX, &m)) {
        bytes = (uint8_t *)malloc((size_t)m.bytes);
        if (bytes) { memcpy(bytes, m.view, (size_t)m.bytes); *out_size = (size_t)m.bytes; }
        map_close(&m);
    }
    return bytes;
}

uint8_t *rubraview_mhost_remote_image_encode(const rubraview_pixbuf_t *px, const rubraview_ffmpeg_still_t *how,
                                             size_t *out_size, const char **why) {
    const char *dummy;
    if (!why) why = &dummy;
    *why = N_("no picture to write");
    if (out_size) *out_size = 0;
    if (!px || !px->pixels || px->width <= 0 || px->height <= 0 || !out_size ||
        (px->format != RUBRAVIEW_PIXFMT_BGRA8 && px->format != RUBRAVIEW_PIXFMT_RGBA8)) return NULL;
    size_t row = (size_t)px->width * 4u, bytes = row * (size_t)px->height;
    uint8_t *result = NULL;
    AcquireSRWLockExclusive(&g_service_lock);
    mh_map_t in = { 0 };
    if (service_ready() && service_payload(&in, NULL, bytes)) {
        for (int32_t y = 0; y < px->height; ++y) memcpy(in.view + (size_t)y * row, px->pixels + (size_t)y * (size_t)px->stride, row);
        int64_t *a = g_service.block->request_args;
        a[0] = px->width; a[1] = px->height; a[2] = px->format == RUBRAVIEW_PIXFMT_RGBA8;
        still_args(a, how);
        if (child_call(&g_service, RUBRAVIEW_MHOST_REQ_IMAGE_ENCODE, JOB_WAIT_MS) == RUBRAVIEW_MHOST_OK) result = service_bytes(out_size);
        if (result) *why = NULL; else { snprintf(g_service_why, sizeof g_service_why, "%s", g_why); *why = g_service_why; }
    } else {
        snprintf(g_service_why, sizeof g_service_why, "%s", g_why);
        *why = g_service_why;
    }
    map_close(&in);
    ReleaseSRWLockExclusive(&g_service_lock);
    return result;
}

uint8_t *rubraview_mhost_remote_pictures_encode(int32_t width, int32_t height, size_t count,
                                                bool (*picture)(void *context, size_t index, uint8_t *bgra, double *seconds),
                                                void *context, const rubraview_ffmpeg_still_t *how,
                                                size_t *out_size, const char **why) {
    const char *dummy;
    if (!why) why = &dummy;
    *why = N_("no picture to write");
    if (out_size) *out_size = 0;
    if (!picture || !out_size || width <= 0 || height <= 0 || width > RUBRAVIEW_MHOST_MAX_SIDE || height > RUBRAVIEW_MHOST_MAX_SIDE ||
        count == 0 || count > 100000) return NULL;
    size_t bytes = (size_t)width * (size_t)height * 4u;
    uint8_t *result = NULL;
    AcquireSRWLockExclusive(&g_service_lock);
    mh_map_t frame = { 0 };
    if (service_ready() && service_payload(&frame, NULL, bytes)) {
        rubraview_mhost_block_t *b = g_service.block;
        int64_t *a = b->request_args;
        a[0] = width; a[1] = height; a[2] = (int64_t)count;
        still_args(a, how);
        int32_t status = child_call(&g_service, RUBRAVIEW_MHOST_REQ_PICTURES_ENCODE, JOB_WAIT_MS);
        while (status == RUBRAVIEW_MHOST_NEED_PICTURE) {   /* the child asks for each picture in turn */
            int64_t index = b->reply_args[0];
            double seconds = 0.1;
            bool given = index >= 0 && (uint64_t)index < count && picture(context, (size_t)index, frame.view, &seconds);
            b->request_args[0] = given ? 1 : 0;
            b->request_real[0] = isfinite(seconds) && seconds >= 0.0 ? seconds : 0.1;
            status = child_call(&g_service, RUBRAVIEW_MHOST_REQ_PICTURE_GIVEN, JOB_WAIT_MS);
        }
        if (status == RUBRAVIEW_MHOST_OK) result = service_bytes(out_size);
        if (result) *why = NULL; else { snprintf(g_service_why, sizeof g_service_why, "%s", g_why); *why = g_service_why; }
    } else {
        snprintf(g_service_why, sizeof g_service_why, "%s", g_why);
        *why = g_service_why;
    }
    map_close(&frame);
    ReleaseSRWLockExclusive(&g_service_lock);
    return result;
}

bool rubraview_mhost_remote_filter_list(proven_arena_t *arena, rubraview_ffilter_catalog_t *out) {
    if (!arena || !out) return false;
    bool ok = false;
    AcquireSRWLockExclusive(&g_service_lock);
    if (service_ready()) {
        service_no_payload();
        mh_map_t m;
        if (child_call(&g_service, RUBRAVIEW_MHOST_REQ_FILTER_LIST, SMALL_WAIT_MS * 3) == RUBRAVIEW_MHOST_OK &&
            reply_map(&g_service, (uint64_t)64 << 20, &m)) {
            ok = rubraview_mhost_catalog_unpack(arena, m.view, (size_t)m.bytes, out);
            map_close(&m);
        }
    }
    ReleaseSRWLockExclusive(&g_service_lock);
    return ok;
}

bool rubraview_mhost_remote_filter_run(const char *graph, const uint8_t *bgra, int32_t width, int32_t height,
                                       uint8_t **out, int32_t *out_width, int32_t *out_height, char *said, size_t said_size) {
    *out = NULL;
    *out_width = *out_height = 0;
    if (said && said_size) said[0] = '\0';
    size_t glen = graph ? strlen(graph) : 0;
    if (!graph || glen == 0 || glen >= RUBRAVIEW_MHOST_INLINE - 1 || !bgra || width <= 0 || height <= 0 ||
        width > RUBRAVIEW_MHOST_MAX_SIDE || height > RUBRAVIEW_MHOST_MAX_SIDE) return false;
    size_t bytes = (size_t)width * (size_t)height * 4u;
    bool ok = false;
    AcquireSRWLockExclusive(&g_service_lock);
    mh_map_t in = { 0 };
    if (service_ready() && service_payload(&in, bgra, bytes)) {
        rubraview_mhost_block_t *b = g_service.block;
        memcpy(b->inline_data, graph, glen + 1);
        b->request_inline_bytes = glen;
        b->request_args[0] = width;
        b->request_args[1] = height;
        mh_map_t m;
        if (child_call(&g_service, RUBRAVIEW_MHOST_REQ_FILTER_RUN, JOB_WAIT_MS) == RUBRAVIEW_MHOST_OK) {
            int64_t w = b->reply_args[0], h = b->reply_args[1];
            if (w > 0 && h > 0 && w <= RUBRAVIEW_MHOST_MAX_SIDE && h <= RUBRAVIEW_MHOST_MAX_SIDE && reply_map(&g_service, REPLY_MAX, &m)) {
                if (m.bytes == (uint64_t)w * (uint64_t)h * 4u && (*out = (uint8_t *)malloc((size_t)m.bytes)) != NULL) {
                    memcpy(*out, m.view, (size_t)m.bytes);
                    *out_width = (int32_t)w;
                    *out_height = (int32_t)h;
                    ok = true;
                }
                map_close(&m);
            }
        }
        if (!ok && said && said_size) snprintf(said, said_size, "%s", g_why);
    } else if (said && said_size) {
        snprintf(said, said_size, "%s", g_why);
    }
    map_close(&in);
    ReleaseSRWLockExclusive(&g_service_lock);
    return ok;
}

/* ======================================================================================== */
/* ---- the child ---- */

static rubraview_mhost_block_t *g_child_block;
static HANDLE g_child_request, g_child_reply;
static mh_map_t g_child_reply_map;     /* held until the next request, so the viewer can open it */
static mh_map_t g_child_frames;        /* the four pictures */

static LONG WINAPI child_crash(EXCEPTION_POINTERS *e) {
    if (g_child_block) {
        char line[96];
        snprintf(line, sizeof line, "crashed: exception 0x%08lx", e && e->ExceptionRecord ? (unsigned long)e->ExceptionRecord->ExceptionCode : 0ul);
        rubraview_mhost_log(g_child_block, line);
    }
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_EXECUTE_HANDLER;
}

static void child_say(const char *why) {
    rubraview_mhost_text_put(g_child_block->reply_why, sizeof g_child_block->reply_why, why ? why : "");
}

/* The reply's payload, in a mapping the viewer opens by its name. */
static uint8_t *child_reply_room(uint64_t bytes) {
    map_close(&g_child_reply_map);
    char name[RUBRAVIEW_MHOST_NAME];
    if (!map_make(&g_child_reply_map, bytes, name)) return NULL;
    memcpy(g_child_block->reply_map, name, sizeof name);
    g_child_block->reply_map_bytes = bytes;
    return g_child_reply_map.view;
}

static uint8_t *child_frames_room(void *context, size_t bytes_each) {
    (void)context;
    char name[RUBRAVIEW_MHOST_NAME];
    if (!map_make(&g_child_frames, (uint64_t)bytes_each * RUBRAVIEW_MHOST_SLOTS, name)) return NULL;
    memcpy(g_child_block->frame_map, name, sizeof name);
    g_child_block->frame_bytes = bytes_each;
    return g_child_frames.view;
}

/* The viewer's card, by its LUID, for decoding and tone mapping here; NULL: in memory. */
static ID3D11Device *child_device(uint64_t luid, uint32_t *profiles) {
    *profiles = 0;
    if (!luid) return NULL;
    IDXGIFactory1 *factory = NULL;
    if (FAILED(CreateDXGIFactory1(&MH_IID_IDXGIFactory1, (void **)&factory)) || !factory) return NULL;
    ID3D11Device *device = NULL;
    IDXGIAdapter1 *adapter = NULL;
    for (UINT i = 0; !device && IDXGIFactory1_EnumAdapters1(factory, i, &adapter) == S_OK; ++i) {
        DXGI_ADAPTER_DESC1 d;
        if (SUCCEEDED(IDXGIAdapter1_GetDesc1(adapter, &d)) &&
            ((uint64_t)(uint32_t)d.AdapterLuid.LowPart | ((uint64_t)(uint32_t)d.AdapterLuid.HighPart << 32)) == luid) {
            const UINT tries[2] = { D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT, D3D11_CREATE_DEVICE_BGRA_SUPPORT };
            for (int k = 0; k < 2 && !device; ++k)
                if (FAILED(D3D11CreateDevice((IDXGIAdapter *)adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, tries[k], NULL, 0, D3D11_SDK_VERSION, &device, NULL, NULL)))
                    device = NULL;
        }
        IDXGIAdapter1_Release(adapter);
    }
    IDXGIFactory1_Release(factory);
    if (!device) return NULL;
    ID3D11DeviceContext *context = NULL;
    ID3D11Device_GetImmediateContext(device, &context);
    if (context) {   /* used from the decode thread and FFmpeg's */
        ID3D11Multithread *mt = NULL;
        if (SUCCEEDED(ID3D11DeviceContext_QueryInterface(context, &MH_IID_ID3D11Multithread, (void **)&mt)) && mt) {
            ID3D11Multithread_SetMultithreadProtected(mt, TRUE);
            ID3D11Multithread_Release(mt);
        }
        ID3D11DeviceContext_Release(context);
    }
    ID3D11VideoDevice *video = NULL;
    if (SUCCEEDED(ID3D11Device_QueryInterface(device, &MH_IID_ID3D11VideoDevice, (void **)&video)) && video) {
        *profiles = ID3D11VideoDevice_GetVideoDecoderProfileCount(video);
        ID3D11VideoDevice_Release(video);
    }
    return device;
}

typedef struct child_play {
    void *media;
    ID3D11Device *device;
    rubraview_ffmpeg_applied_t applied;
} child_play_t;

static int32_t child_open(child_play_t *cp) {
    rubraview_mhost_block_t *b = g_child_block;
    size_t n = b->request_inline_bytes < RUBRAVIEW_MHOST_INLINE - 1 ? (size_t)b->request_inline_bytes : RUBRAVIEW_MHOST_INLINE - 1;
    proven_u8str_view_t path = { .ptr = b->inline_data, .size = n };
    rubraview_media_gpu_t gpu = { .mode = b->gpu_mode, .tone_mapping = b->tone_mapping };
    if (b->gpu_mode > 0 || b->tone_mapping > 0) cp->device = child_device(b->adapter_luid, &gpu.decoder_profiles);
    if (cp->device) {
        if (b->gpu_mode > 0) gpu.device = cp->device;
        if (b->tone_mapping > 0) gpu.shader_device = cp->device;
    }
    rubraview_ffmpeg_hosting_t hosting = { .ring = &b->ring, .slots = b->slots, .pixels = child_frames_room };
    rubraview_media_failure_t failure = RUBRAVIEW_MEDIA_FAIL_FILE;
    rubraview_media_info_t info = { 0 };
    cp->media = rubraview_ffmpeg_host_open(path, &gpu, &hosting, &failure, &info);
    b->open_failure = cp->media ? RUBRAVIEW_MEDIA_OPENED : (int32_t)failure;
    if (!cp->media) return RUBRAVIEW_MHOST_OK;   /* said, in open_failure */
    rubraview_mhost_info_put(&b->info, &info);
    const rubraview_media_backend_api_t *api = rubraview_media_backend_ffmpeg();
    rubraview_track_set_t tracks = rubraview_tracks_create();
    (void)api->tracks(cp->media, &tracks);
    rubraview_mhost_tracks_put(b, &tracks);
    rubraview_mhost_details_put(b, api->details(cp->media));
    cp->applied = (rubraview_ffmpeg_applied_t){ .gain_millionths = 1000000 };
    return RUBRAVIEW_MHOST_OK;
}

static int32_t child_subtitle(child_play_t *cp) {
    if (!cp->media) return RUBRAVIEW_MHOST_FAILED;
    size_t cap = RUBRAVIEW_MHOST_SUBTITLE_MAX + (1u << 20);
    void *mem = malloc(cap);
    if (!mem) return RUBRAVIEW_MHOST_FAILED;
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t *)mem, .size = cap });
    proven_u8str_view_t text = rubraview_media_backend_ffmpeg()->read_subtitle_stream(cp->media, &arena, (int32_t)g_child_block->request_args[0]);
    int32_t status = RUBRAVIEW_MHOST_FAILED;
    if (text.size > 0 && text.size <= RUBRAVIEW_MHOST_SUBTITLE_MAX) {
        uint8_t *dst = child_reply_room(text.size);
        if (dst) { memcpy(dst, text.ptr, text.size); status = RUBRAVIEW_MHOST_OK; }
    }
    free(mem);
    return status;
}

/* The request's payload, opened by the name the viewer gave. */
static bool child_payload(mh_map_t *m, bool write) {
    char name[RUBRAVIEW_MHOST_NAME];
    memcpy(name, g_child_block->request_map, sizeof name);
    return map_open(m, name, g_child_block->request_map_bytes, write);
}

static uint8_t *child_jxl_room(void *context, int32_t width, int32_t height) {
    (void)context;
    g_child_block->reply_args[0] = width;
    g_child_block->reply_args[1] = height;
    return child_reply_room((uint64_t)width * (uint64_t)height * 4u);
}

typedef struct child_frames { uint8_t *data; size_t used, cap; uint32_t count; rubraview_mhost_frame_ref_t refs[RUBRAVIEW_MHOST_FRAMES_MAX]; } child_frames_t;

static uint8_t *child_frames_add(void *context, int32_t width, int32_t height, double seconds) {
    child_frames_t *f = (child_frames_t *)context;
    if (f->count >= RUBRAVIEW_MHOST_FRAMES_MAX || width <= 0 || height <= 0) return NULL;
    size_t bytes = (size_t)width * (size_t)height * 4u;
    if (bytes > ((size_t)768 << 20) - f->used) return NULL;   /* the reader's own limit */
    if (f->used + bytes > f->cap) {
        size_t cap = f->cap ? f->cap : bytes * 4;
        while (cap < f->used + bytes) cap *= 2;
        uint8_t *grown = (uint8_t *)realloc(f->data, cap);
        if (!grown) return NULL;
        f->data = grown;
        f->cap = cap;
    }
    f->refs[f->count] = (rubraview_mhost_frame_ref_t){ width, height, seconds, f->used };
    f->count++;
    uint8_t *at = f->data + f->used;
    f->used += bytes;
    return at;
}

static int32_t child_jxl_frames(const uint8_t *data, size_t size, int32_t max_side) {
    child_frames_t *f = (child_frames_t *)calloc(1, sizeof(*f));
    if (!f) return RUBRAVIEW_MHOST_FAILED;
    int32_t status = RUBRAVIEW_MHOST_FAILED;
    if (rubraview_pal_ffmpeg_jxl_frames(data, size, max_side, child_frames_add, f) && f->count) {
        size_t head = rubraview_mhost_frames_head(f->count);
        uint8_t *dst = child_reply_room(head + f->used);
        if (dst) {
            memcpy(dst, &f->count, 4);
            for (uint32_t i = 0; i < f->count; ++i) {
                uint8_t *e = dst + 4 + (size_t)i * 24u;
                uint64_t at = head + f->refs[i].offset;
                memcpy(e, &f->refs[i].width, 4); memcpy(e + 4, &f->refs[i].height, 4);
                memcpy(e + 8, &f->refs[i].seconds, 8); memcpy(e + 16, &at, 8);
            }
            memcpy(dst + head, f->data, f->used);
            status = RUBRAVIEW_MHOST_OK;
        }
    }
    free(f->data);
    free(f);
    return status;
}

static void still_from(rubraview_ffmpeg_still_t *how, const int64_t *a) {
    *how = (rubraview_ffmpeg_still_t){ .avif = a[3] != 0, .quality = (int32_t)a[4], .chroma444 = a[5] != 0, .bits = (int32_t)a[6], .moving = a[7] != 0 };
}

static void child_reply(uint32_t serial, int32_t status) {
    g_child_block->reply_status = status;
    atomic_store_explicit(&g_child_block->reply_serial, serial, memory_order_release);
    SetEvent(g_child_reply);
}

/* PICTURES_ENCODE: each picture asked of the viewer in turn, the request's serial kept. */
typedef struct child_pictures { mh_map_t frame; size_t bytes; uint32_t serial; bool broken; } child_pictures_t;

static bool child_picture(void *context, size_t index, uint8_t *bgra, double *seconds) {
    child_pictures_t *cp = (child_pictures_t *)context;
    if (cp->broken) return false;
    rubraview_mhost_block_t *b = g_child_block;
    b->reply_args[0] = (int64_t)index;
    child_reply(cp->serial, RUBRAVIEW_MHOST_NEED_PICTURE);
    for (;;) {
        if (WaitForSingleObject(g_child_request, (DWORD)JOB_WAIT_MS) != WAIT_OBJECT_0) { cp->broken = true; return false; }
        uint32_t serial = atomic_load_explicit(&b->request_serial, memory_order_acquire);
        if (serial == cp->serial) continue;
        cp->serial = serial;
        if (b->request_kind != RUBRAVIEW_MHOST_REQ_PICTURE_GIVEN || b->request_args[0] != 1) { cp->broken = true; return false; }
        memcpy(bgra, cp->frame.view, cp->bytes);
        *seconds = b->request_real[0];
        atomic_store_explicit(&b->heartbeat, atomic_load(&b->heartbeat) + 1, memory_order_release);
        return true;
    }
}

static int32_t child_service(uint32_t kind, uint32_t *serial) {
    rubraview_mhost_block_t *b = g_child_block;
    int64_t *a = b->request_args;
    switch (kind) {
        case RUBRAVIEW_MHOST_REQ_PROBE: {
            char names[256] = { 0 };
            b->reply_args[0] = rubraview_ffmpeg_local_available() ? 1 : 0;
            b->reply_args[1] = rubraview_ffmpeg_local_refused();
            b->reply_args[2] = rubraview_ffmpeg_local_place();
            b->reply_args[3] = rubraview_ffmpeg_local_jxl_state();
            b->reply_args[4] = rubraview_ffmpeg_local_image_encoders(names, sizeof names);
            b->reply_args[5] = rubraview_ffilter_local_state();
            rubraview_mhost_text_put((char *)b->inline_data, sizeof names, names);
            return RUBRAVIEW_MHOST_OK;
        }
        case RUBRAVIEW_MHOST_REQ_FETCH: {
            size_t n = b->request_inline_bytes < RUBRAVIEW_MHOST_INLINE - 1 ? (size_t)b->request_inline_bytes : 0;
            size_t cap = a[0] > 0 && a[0] <= (64 << 20) ? (size_t)a[0] : 0;
            uint8_t *buf = cap ? (uint8_t *)malloc(cap) : NULL;
            int64_t got = buf ? rubraview_pal_ffmpeg_fetch((proven_u8str_view_t){ b->inline_data, n }, buf, cap) : -1;
            int32_t status = RUBRAVIEW_MHOST_FAILED;
            if (got > 0) { uint8_t *dst = child_reply_room((uint64_t)got); if (dst) { memcpy(dst, buf, (size_t)got); status = RUBRAVIEW_MHOST_OK; } }
            free(buf);
            return status;
        }
        case RUBRAVIEW_MHOST_REQ_JXL_DECODE:
        case RUBRAVIEW_MHOST_REQ_JXL_FRAMES: {
            mh_map_t in;
            if (!child_payload(&in, false)) return RUBRAVIEW_MHOST_FAILED;
            int32_t status;
            if (kind == RUBRAVIEW_MHOST_REQ_JXL_DECODE) {
                int32_t w = 0, h = 0;
                status = rubraview_pal_ffmpeg_jxl_decode(in.view, (size_t)in.bytes, (int32_t)a[0], child_jxl_room, NULL, &w, &h)
                       ? RUBRAVIEW_MHOST_OK : RUBRAVIEW_MHOST_FAILED;
            } else {
                status = child_jxl_frames(in.view, (size_t)in.bytes, (int32_t)a[0]);
            }
            map_close(&in);
            return status;
        }
        case RUBRAVIEW_MHOST_REQ_IMAGE_ENCODE: {
            mh_map_t in;
            if (!child_payload(&in, false) || a[0] <= 0 || a[1] <= 0 || (uint64_t)a[0] * (uint64_t)a[1] * 4u != in.bytes) {
                map_close(&in);
                return RUBRAVIEW_MHOST_FAILED;
            }
            rubraview_pixbuf_t px = { .pixels = in.view, .width = (int32_t)a[0], .height = (int32_t)a[1], .stride = (int32_t)a[0] * 4,
                                      .format = a[2] ? RUBRAVIEW_PIXFMT_RGBA8 : RUBRAVIEW_PIXFMT_BGRA8 };
            rubraview_ffmpeg_still_t how;
            still_from(&how, a);
            size_t size = 0;
            const char *why = NULL;
            uint8_t *bytes = rubraview_pal_ffmpeg_image_encode(&px, &how, &size, &why);
            map_close(&in);
            int32_t status = RUBRAVIEW_MHOST_FAILED;
            if (bytes) { uint8_t *dst = child_reply_room(size); if (dst) { memcpy(dst, bytes, size); status = RUBRAVIEW_MHOST_OK; } free(bytes); }
            else child_say(why);
            return status;
        }
        case RUBRAVIEW_MHOST_REQ_PICTURES_ENCODE: {
            child_pictures_t cp = { .serial = *serial };
            if (!child_payload(&cp.frame, false) || a[0] <= 0 || a[1] <= 0 || a[2] <= 0) { map_close(&cp.frame); return RUBRAVIEW_MHOST_FAILED; }
            cp.bytes = (size_t)a[0] * (size_t)a[1] * 4u;
            if (cp.bytes != cp.frame.bytes) { map_close(&cp.frame); return RUBRAVIEW_MHOST_FAILED; }
            rubraview_ffmpeg_still_t how;
            still_from(&how, a);
            size_t size = 0;
            const char *why = NULL;
            uint8_t *bytes = rubraview_pal_ffmpeg_pictures_encode((int32_t)a[0], (int32_t)a[1], (size_t)a[2], child_picture, &cp, &how, &size, &why);
            map_close(&cp.frame);
            *serial = cp.serial;   /* the answer goes to the last request */
            int32_t status = RUBRAVIEW_MHOST_FAILED;
            if (bytes) { uint8_t *dst = child_reply_room(size); if (dst) { memcpy(dst, bytes, size); status = RUBRAVIEW_MHOST_OK; } free(bytes); }
            else child_say(why);
            return status;
        }
        case RUBRAVIEW_MHOST_REQ_FILTER_LIST: {
            size_t cap = (size_t)48 << 20;
            void *mem = malloc(cap);
            if (!mem) return RUBRAVIEW_MHOST_FAILED;
            proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t *)mem, .size = cap });
            rubraview_ffilter_catalog_t catalog;
            int32_t status = RUBRAVIEW_MHOST_FAILED;
            if (rubraview_ffilter_local_list(&arena, &catalog)) {
                size_t need = rubraview_mhost_catalog_pack(&catalog, NULL, 0);
                uint8_t *dst = need ? child_reply_room(need) : NULL;
                if (dst && rubraview_mhost_catalog_pack(&catalog, dst, need) == need) status = RUBRAVIEW_MHOST_OK;
            }
            free(mem);
            return status;
        }
        case RUBRAVIEW_MHOST_REQ_FILTER_RUN: {
            mh_map_t in;
            if (!child_payload(&in, false) || a[0] <= 0 || a[1] <= 0 || (uint64_t)a[0] * (uint64_t)a[1] * 4u != in.bytes) {
                map_close(&in);
                return RUBRAVIEW_MHOST_FAILED;
            }
            char graph[RUBRAVIEW_MHOST_INLINE];
            size_t n = b->request_inline_bytes < sizeof graph - 1 ? (size_t)b->request_inline_bytes : sizeof graph - 1;
            memcpy(graph, b->inline_data, n);
            graph[n] = '\0';
            uint8_t *out = NULL;
            int32_t ow = 0, oh = 0;
            char said[256] = { 0 };
            bool ok = rubraview_ffilter_local_run(graph, in.view, (int32_t)a[0], (int32_t)a[1], &out, &ow, &oh, said, sizeof said);
            map_close(&in);
            int32_t status = RUBRAVIEW_MHOST_FAILED;
            if (ok && out) {
                uint8_t *dst = child_reply_room((uint64_t)ow * (uint64_t)oh * 4u);
                if (dst) { memcpy(dst, out, (size_t)ow * (size_t)oh * 4u); b->reply_args[0] = ow; b->reply_args[1] = oh; status = RUBRAVIEW_MHOST_OK; }
            } else {
                child_say(said);
            }
            free(out);
            return status;
        }
        default:
            return RUBRAVIEW_MHOST_FAILED;
    }
}

int rubraview_pal_media_host_main(const wchar_t *mapping_text, const wchar_t *request_text, const wchar_t *reply_text) {
    g_in_child = true;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(child_crash);
    HANDLE mapping = (HANDLE)(uintptr_t)_wcstoui64(mapping_text, NULL, 10);
    g_child_request = (HANDLE)(uintptr_t)_wcstoui64(request_text, NULL, 10);
    g_child_reply = (HANDLE)(uintptr_t)_wcstoui64(reply_text, NULL, 10);
    rubraview_mhost_block_t *b = (rubraview_mhost_block_t *)MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(rubraview_mhost_block_t));
    if (!b || !rubraview_mhost_block_ok(b, sizeof(*b)) || !g_child_request || !g_child_reply) return 2;
    g_child_block = b;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);   /* WASAPI */
    char imported[RUBRAVIEW_MHOST_PATH], chosen[RUBRAVIEW_MHOST_PATH];
    rubraview_mhost_text_put(imported, sizeof imported, b->ffmpeg_imported);
    rubraview_mhost_text_put(chosen, sizeof chosen, b->ffmpeg_chosen);
    rubraview_pal_ffmpeg_set_folders((proven_u8str_view_t){ (const proven_byte_t *)imported, strlen(imported) },
                                     (proven_u8str_view_t){ (const proven_byte_t *)chosen, strlen(chosen) });
    bool playback = b->mode == RUBRAVIEW_MHOST_PLAYBACK;
    child_play_t cp = { 0 };
    uint32_t handled = 0, audio_applied = 0, tap_tick = 0;
    for (;;) {
        DWORD r = WaitForSingleObject(g_child_request, playback ? 4 : 1000);
        atomic_store_explicit(&b->heartbeat, atomic_load(&b->heartbeat) + 1, memory_order_release);
        if (playback) {   /* the viewer's sound settings, and the latest sound for its analyser */
            uint32_t serial = atomic_load_explicit(&b->audio.serial, memory_order_acquire);
            if (serial != audio_applied) {
                audio_applied = serial;
                rubraview_audio_chain_config_t chain;
                double speed, volume;
                bool muted;
                uint32_t latency;
                rubraview_mhost_audio_take(&b->audio, &chain, &speed, &volume, &muted, &latency);
                rubraview_pal_audio_set_speed(speed);
                rubraview_pal_audio_set_volume(volume, muted);
                rubraview_pal_audio_set_latency_ms(latency);
                rubraview_pal_audio_set_chain(&chain);
            }
            if (cp.media && ++tap_tick % 3 == 0) {
                static float samples[RUBRAVIEW_MHOST_TAP];
                size_t n = rubraview_pal_audio_tap_read(samples, RUBRAVIEW_MHOST_TAP);
                rubraview_mhost_tap_put(&b->tap, samples, n);
            }
        }
        if (cp.media) {
            rubraview_ffmpeg_host_apply(cp.media, &b->wanted, &cp.applied);
            rubraview_mhost_seen_t seen;
            rubraview_ffmpeg_host_observe(cp.media, &seen);
            rubraview_mhost_observed_put(&b->observed, &seen);
            if (b->fault_kind && rubraview_ffmpeg_host_pictures(cp.media) >= (uint64_t)(b->fault_after > 0 ? b->fault_after : 0)) {
                rubraview_mhost_log(b, b->fault_kind == 1 ? "fault: crash" : "fault: hang");
                if (b->fault_kind == 1) RaiseException(0xE0000176u, EXCEPTION_NONCONTINUABLE, 0, NULL);
                for (;;) Sleep(INFINITE);
            }
        }
        if (atomic_load_explicit(&b->wanted.quit, memory_order_acquire) && r != WAIT_OBJECT_0) break;
        uint32_t serial = atomic_load_explicit(&b->request_serial, memory_order_acquire);
        if (serial == handled) continue;
        handled = serial;
        uint32_t kind = b->request_kind;
        child_say("");
        int32_t status = RUBRAVIEW_MHOST_FAILED;
        bool close = false;
        if (kind == RUBRAVIEW_MHOST_REQ_OPEN && playback && !cp.media) status = child_open(&cp);
        else if (kind == RUBRAVIEW_MHOST_REQ_SUBTITLE && playback) status = child_subtitle(&cp);
        else if (kind == RUBRAVIEW_MHOST_REQ_CLOSE) { status = RUBRAVIEW_MHOST_OK; close = true; }
        else if (!playback) { map_close(&g_child_reply_map); status = child_service(kind, &serial); handled = serial; }
        if (close) {
            if (cp.media) rubraview_media_backend_ffmpeg()->close(cp.media);
            cp.media = NULL;
            child_reply(serial, status);
            break;
        }
        child_reply(serial, status);
    }
    if (cp.media) rubraview_media_backend_ffmpeg()->close(cp.media);
    if (cp.device) ID3D11Device_Release(cp.device);
    map_close(&g_child_frames);
    map_close(&g_child_reply_map);
    return 0;
}

#endif /* _WIN32 */
