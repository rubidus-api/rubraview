#ifdef _WIN32
#include <windows.h>
#include "rubraview/pal/pal_time.h"

double rubraview_pal_time_now_seconds(void) {
    static LARGE_INTEGER frequency = {0};
    if (frequency.QuadPart == 0) {
        if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart == 0) {
            return (double)GetTickCount64() / 1000.0; /* fallback; never zero-divide */
        }
    }

    LARGE_INTEGER counter;
    if (!QueryPerformanceCounter(&counter)) {
        return (double)GetTickCount64() / 1000.0;
    }
    return (double)counter.QuadPart / (double)frequency.QuadPart;
}

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

/* RUBRAVIEW_TIMER_DEFAULT: to compare with Windows' tick left as it is, no fine wait of any kind. */
static bool timer_default(void) {
    static int known;   /* 0 not read, 1 no, 2 yes */
    if (!known) {
        WCHAR keep[4];
        known = GetEnvironmentVariableW(L"RUBRAVIEW_TIMER_DEFAULT", keep, 4) > 0 ? 2 : 1;
    }
    return known == 2;
}

bool rubraview_pal_time_high_resolution(void) {
    static int known;   /* 0 not tried, 1 no, 2 yes */
    if (!known) {
        WCHAR old[4];
        HANDLE t = timer_default() || GetEnvironmentVariableW(L"RUBRAVIEW_TIMER_PERIOD", old, 4) > 0 ? NULL
                 : CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        known = t ? 2 : 1;   /* refused before Windows 10 1803; RUBRAVIEW_TIMER_PERIOD: the 1 ms timer as before */
        if (t) CloseHandle(t);
    }
    return known == 2;
}

rubraview_timer_kind_t rubraview_pal_time_fine(bool fine) {
    static bool on, tried;
    static UINT (WINAPI *begin)(UINT), (WINAPI *end)(UINT);
    if (timer_default()) return RUBRAVIEW_TIMER_TICK;
    if (rubraview_pal_time_high_resolution()) return fine ? RUBRAVIEW_TIMER_HIGH : RUBRAVIEW_TIMER_TICK;   /* RV-273 */
    if (fine == on) return on ? RUBRAVIEW_TIMER_PERIOD : RUBRAVIEW_TIMER_TICK;
    if (!tried) {   /* winmm loaded when first wanted: nothing else of it is used */
        tried = true;
        HMODULE winmm = LoadLibraryW(L"winmm.dll");
        if (winmm) {
            begin = (UINT (WINAPI *)(UINT))(void (*)(void))GetProcAddress(winmm, "timeBeginPeriod");
            end = (UINT (WINAPI *)(UINT))(void (*)(void))GetProcAddress(winmm, "timeEndPeriod");
        }
    }
    if (!begin || !end) return RUBRAVIEW_TIMER_TICK;
    if (fine) begin(1); else end(1);
    on = fine;
    return on ? RUBRAVIEW_TIMER_PERIOD : RUBRAVIEW_TIMER_TICK;
}

void rubraview_pal_time_sleep_ms(uint32_t milliseconds) {
    /* RV-273: a short sleep on the high-resolution timer, now that the system's is left at its tick */
    static thread_local HANDLE timer;
    static thread_local bool tried;
    if (milliseconds > 0 && milliseconds < 16 && rubraview_pal_time_high_resolution()) {
        if (!tried) {
            tried = true;
            timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        }
        LARGE_INTEGER due = { .QuadPart = -(LONGLONG)milliseconds * 10000 };
        if (timer && SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) {
            WaitForSingleObject(timer, milliseconds + 50u);
            return;
        }
    }
    Sleep(milliseconds);
}

#endif /* _WIN32 */
