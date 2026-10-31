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

bool rubraview_pal_time_fine(bool fine) {
    static bool on, tried;
    static UINT (WINAPI *begin)(UINT), (WINAPI *end)(UINT);
    if (fine == on) return on;
    if (!tried) {   /* winmm loaded when first wanted: nothing else of it is used */
        tried = true;
        WCHAR keep[4];
        if (GetEnvironmentVariableW(L"RUBRAVIEW_TIMER_DEFAULT", keep, 4) > 0) return false;   /* to compare with Windows' tick left as it is */
        HMODULE winmm = LoadLibraryW(L"winmm.dll");
        if (winmm) {
            begin = (UINT (WINAPI *)(UINT))(void (*)(void))GetProcAddress(winmm, "timeBeginPeriod");
            end = (UINT (WINAPI *)(UINT))(void (*)(void))GetProcAddress(winmm, "timeEndPeriod");
        }
    }
    if (!begin || !end) return false;
    if (fine) begin(1); else end(1);
    on = fine;
    return on;
}

void rubraview_pal_time_sleep_ms(uint32_t milliseconds) {
    Sleep(milliseconds);
}

#endif /* _WIN32 */
