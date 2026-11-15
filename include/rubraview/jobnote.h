#ifndef RUBRAVIEW_JOBNOTE_H
#define RUBRAVIEW_JOBNOTE_H

#include "rubraview/core.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RV-292 (owner, 2026-11-15: "프로세스 시작할 때 어떤 로그를 남기고 끝날 때 그걸 지우도록 해서 만약 제대로 끝나지 않았다면
 * 메인 로그에서 그걸 알 수 있게"): the note a job's process leaves in `<settings folder>\jobs` while it runs — what
 * it is, where, how far — deleted when it ends as it should. The viewer says a note whose process is not running.
 * The text is `key=value` lines (UTF-8); a value never holds a line break (one is written as a space). Host-tested.
 */

typedef struct rubraview_jobnote {
    char     kind[16];        /* copy, move, extract, archive */
    uint32_t pid;             /* the job's process */
    uint64_t started;         /* that process's start time as Windows keeps it (a pid reused is another start) */
    int64_t  when;            /* seconds since 1970, when it began */
    uint32_t viewer;          /* the viewer that started it */
    char     where[1024];     /* the folder it goes into, or the archive */
    char     first[1024];     /* its first source */
    uint64_t count, done;     /* items in all, and done when the note was last written */
} rubraview_jobnote_t;

/** The note as text into `out`; its length, or 0 when it does not fit. */
size_t rubraview_jobnote_format(const rubraview_jobnote_t *note, char *out, size_t cap);
/** Read back; false when it is not a note (no kind or no pid). Keys not known are passed over. */
bool rubraview_jobnote_parse(proven_u8str_view_t text, rubraview_jobnote_t *note);
/** The note's file name, "<pid>-<started>.job". */
size_t rubraview_jobnote_name(const rubraview_jobnote_t *note, char *out, size_t cap);
/**
 * Whether the note was left behind: its process is not running (`running` false), or the id is now another
 * process's (its start time not the note's). A note of the process still running is not.
 */
bool rubraview_jobnote_stale(const rubraview_jobnote_t *note, bool running, uint64_t running_started);

#ifdef __cplusplus
}
#endif

#endif
