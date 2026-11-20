#ifndef RUBRAVIEW_CHOOSE_H
#define RUBRAVIEW_CHOOSE_H

#include "rubraview/core.h"
#include "rubraview/jobwire.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RV-314 (owner, 2026-11-20: "폴더 위치 지정하는거도 외부 윈api 보다는 내부 파일관리창으로 ... 파일 선택도 내부 api로. 파일 내부
 * 관리와 공통 루틴으로"): a folder, a file, files or a name to save as, chosen in Rubraview's own open-file screen run as
 * a process of its own (`rubraview.exe --choose <request in hex>`). The request goes on the command line, the answer
 * comes back to the asking window (WM_COPYDATA). Both are jobwire messages. Host-tested.
 */

typedef enum rubraview_choose_mode {
    RUBRAVIEW_CHOOSE_FOLDER = 1,   /* a folder: the one shown, or the one picked */
    RUBRAVIEW_CHOOSE_FILE   = 2,   /* one file */
    RUBRAVIEW_CHOOSE_FILES  = 3,   /* one or more files */
    RUBRAVIEW_CHOOSE_SAVE   = 4,   /* a name to save as, in the folder shown */
} rubraview_choose_mode_t;

#define RUBRAVIEW_CHOOSE_FILTERS 16
#define RUBRAVIEW_JOBWIRE_CHOOSE        32   /* the request */
#define RUBRAVIEW_JOBWIRE_CHOOSE_ANSWER 33   /* the answer: id, accepted, filter, count, then the paths */

typedef struct rubraview_choose_request {
    uint8_t  mode;                  /* a rubraview_choose_mode_t */
    uint32_t id;                    /* the asker's, given back in the answer */
    uint64_t reply_to;              /* the window the answer goes to (an HWND) */
    uint64_t over;                  /* the window it opens over (the one that asked; 0: reply_to's) */
    proven_u8str_view_t title;
    proven_u8str_view_t start_dir;  /* "" where the screen was left */
    proven_u8str_view_t name;       /* SAVE: the name offered */
    uint32_t filter_count, filter;  /* the kinds of file offered, and the one chosen at first */
    proven_u8str_view_t filter_name[RUBRAVIEW_CHOOSE_FILTERS];
    proven_u8str_view_t filter_pattern[RUBRAVIEW_CHOOSE_FILTERS];   /* "*.jpg;*.png"; "" or "*.*" every file */
} rubraview_choose_request_t;

void rubraview_choose_put_request(rubraview_jobwire_out_t *out, const rubraview_choose_request_t *req);
/** The request in `data` (its strings pointing into it); false when it is none. */
bool rubraview_choose_get_request(const uint8_t *data, size_t len, rubraview_choose_request_t *req);

typedef struct rubraview_choose_answer {
    uint32_t id;
    bool     accepted;
    uint32_t filter;                /* the kind chosen at the end (SAVE) */
    uint32_t count;                 /* the paths that follow */
} rubraview_choose_answer_t;

void rubraview_choose_put_answer(rubraview_jobwire_out_t *out, const rubraview_choose_answer_t *answer,
                                 const proven_u8str_view_t *paths, size_t count);
/** The answer's head; `msg` is then at its paths (rubraview_jobwire_get_str, `count` times). False when it is none. */
bool rubraview_choose_get_answer(const uint8_t *data, size_t len, rubraview_choose_answer_t *answer, rubraview_jobwire_msg_t *msg);

/** `n` bytes as lowercase hex in `out` (NUL ended); false when it does not fit. */
bool rubraview_choose_hex(const uint8_t *data, size_t n, char *out, size_t cap);
/** Hex back to bytes; false on an odd length, a letter not hex, or no room. */
bool rubraview_choose_unhex(proven_u8str_view_t text, uint8_t *out, size_t cap, size_t *n);

/** Whether the screen lists an entry: a folder always; a file not for FOLDER, else when the kind `filter` takes it. */
bool rubraview_choose_shows(const rubraview_choose_request_t *req, uint32_t filter, proven_u8str_view_t name, bool is_directory);

/**
 * SAVE: the name typed, trimmed, with the chosen kind's extension added when it has none of the kind's (the kind's
 * first `*.ext`); "" (0) when nothing usable was typed (empty, only dots, or a character Windows refuses in a name).
 */
size_t rubraview_choose_save_name(proven_u8str_view_t typed, proven_u8str_view_t pattern, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_CHOOSE_H */
