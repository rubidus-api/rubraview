#ifndef _WIN32
#include "rubraview/pal/pal_process.h"

/* The host build exists to run the core's tests; it starts no second
   process of its own. Both calls fail honestly rather than pretending. */

bool rubraview_pal_process_ui_language(char *buf, size_t cap) {
    (void)buf; (void)cap;
    return false;
}

u8str_t rubraview_pal_process_executable(proven_arena_t *arena) {
    (void)arena;
    return (u8str_t){0};
}

bool rubraview_pal_process_start_console(u8str_t command_line) {
    (void)command_line;
    return false;
}

bool rubraview_pal_process_start(u8str_t command_line) {
    (void)command_line;
    return false;
}
#endif
