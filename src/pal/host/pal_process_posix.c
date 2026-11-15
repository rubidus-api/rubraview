#ifndef _WIN32
#include "rubraview/pal/pal_process.h"

/* The host build exists to run the core's tests; it starts no second
   process of its own. Both calls fail honestly rather than pretending. */

bool rubraview_pal_process_ui_language(char *buf, size_t cap) {
    (void)buf; (void)cap;
    return false;
}

bool rubraview_pal_process_packaged(char *family, size_t cap) {
    if (family && cap > 0) family[0] = '\0';
    return false;
}

proven_u8str_view_t rubraview_pal_process_executable(proven_arena_t *arena) {
    (void)arena;
    return (proven_u8str_view_t){0};
}

bool rubraview_pal_process_start_console(proven_u8str_view_t command_line) {
    (void)command_line;
    return false;
}

bool rubraview_pal_process_start(proven_u8str_view_t command_line) {
    (void)command_line;
    return false;
}

bool rubraview_pal_process_start_apart(proven_u8str_view_t command_line) {
    (void)command_line;
    return false;
}
#endif

bool rubraview_pal_process_started(uint32_t pid, uint64_t *started) {
    (void)pid;
    if (started) *started = 0;
    return false;   /* the job notes are Windows' */
}

uint32_t rubraview_pal_process_self(uint64_t *started) {
    if (started) *started = 0;
    return 0;
}

bool rubraview_pal_process_read_bytes(uint64_t *bytes) {
    if (bytes) *bytes = 0;
    return false;   /* the viewer's live rows are Windows' */
}
