#ifndef RUBRAVIEW_LOG_H
#define RUBRAVIEW_LOG_H

#include "rubraview/core.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RV-256 (owner, 2026-11-08: "내부 로그 창도 있으면 좋겠어요. 동작의 로그들을 차근차근 저장하고, 필요하면 창으로 보여주고,
 * 버튼 누르면 텍스트 파일로 저장할 수 있고. 이건 따로 저장하지는 않고 매 창(실행 인스턴스)마다 그때그때 저장만 합니다. 로그
 * 크기 상한은 기본 1MB로 하되 필요하면 설정에서 조절할 수 있도록 하고, 이건 다른 메모리랑 별개로 힙에서 추가 할당받아서 따로
 * 관리하게 합시다."): what a window did, one line each, in a ring of its own on the heap (never an arena). Full, the
 * oldest whole lines go first. Lines are "  12.345 kind  text" — the seconds since the window began, a kind word,
 * the text on one line. The main thread's alone: no lock.
 */
#define RUBRAVIEW_LOG_DEFAULT_BYTES ((size_t)1u << 20)
#define RUBRAVIEW_LOG_LINE_MAX 2048   /* a longer text is cut there, with "..." */

typedef struct rubraview_log {
    char  *ring;       /* `cap` bytes from rubraview_alloc */
    size_t cap, start, len;
    size_t lines;      /* whole lines held */
    size_t dropped;    /* lines the cap pushed out since it began */
} rubraview_log_t;

/* The ring made `cap` bytes (1 KB at least); false when the heap has not that much. */
bool rubraview_log_init(rubraview_log_t *log, size_t cap);
void rubraview_log_free(rubraview_log_t *log);

/* A line added: `seconds` since the window began, `kind` one word, `text` (line ends made spaces, past
   RUBRAVIEW_LOG_LINE_MAX cut). */
void rubraview_log_add(rubraview_log_t *log, double seconds, const char *kind, proven_u8str_view_t text);

/* The log as text, oldest first, into `out` (at most cap - 1 bytes, NUL-ended); its whole length, so a NULL `out`
   asks the size. */
size_t rubraview_log_copy(const rubraview_log_t *log, char *out, size_t cap);

/* The ring made `cap` bytes, its newest lines kept; false (unchanged) when the heap has not that much. */
bool rubraview_log_resize(rubraview_log_t *log, size_t cap);

void rubraview_log_clear(rubraview_log_t *log);

#ifdef __cplusplus
}
#endif

#endif
