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

void rubraview_pal_time_sleep_ms(uint32_t milliseconds) {
    Sleep(milliseconds);
}

#endif /* _WIN32 */
