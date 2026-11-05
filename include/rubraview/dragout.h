#ifndef RUBRAVIEW_DRAGOUT_H
#define RUBRAVIEW_DRAGOUT_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RV-223 (D-278, owner 2026-11-06: "압축파일 내부를 브라우징할 때, 드래그 앤 드롭으로 윈도 탐색기 창이나 다른 곳에 선택된
 * 파일들만을 좌클릭 드래그로 복사하거나 우클릭 드래그로 컨텍스트 메뉴를 불러 ... Ctrl+C나 Ctrl+X로 복사 혹은 잘라내기"):
 * what is picked in the open-file screen dragged to Explorer or another program, or put on the clipboard for it.
 * Picked inside an archive, it is written out to a folder of the viewer's under the system's temporary folder when it
 * is let go (a drag) or when Ctrl+C / Ctrl+X is pressed (the clipboard: Windows reads it at once), and those files are
 * handed over; a move or a cut that arrived takes them out of the archive.
 */

/* Drop effects, as Windows' DROPEFFECT values. */
#define RUBRAVIEW_DROP_COPY 1u
#define RUBRAVIEW_DROP_MOVE 2u
#define RUBRAVIEW_DROP_LINK 4u

/** What a drag offers. A left drag copies (Explorer moves on the same drive whenever a move is allowed); a right drag
    offers copy, move and link for files on disk, and copy and, where the archive can be written again (`movable`),
    move out of an archive. */
uint32_t rubraview_dragout_effects(bool from_archive, bool movable, bool right_button);

/** Whether what is picked in this archive can be taken out of it after a move: a ZIP or a 7z (`.cbz`, `.cb7` too)
    that is no split set, its files not locked. */
bool rubraview_dragout_archive_movable(proven_u8str_view_t archive_path, bool any_encrypted);

/** The path a picked tile has in the folder it is written out to: `dest\name`, the name made one Windows takes as
    extracting makes it. Into `out` with its NUL; its length, 0 when it does not fit or a part is empty. */
size_t rubraview_dragout_path(proven_u8str_view_t dest, proven_u8str_view_t name, char *out, size_t cap);

/** What happened: what the target said ("Performed DropEffect"), else what the drag returned. */
uint32_t rubraview_dragout_performed(uint32_t returned, uint32_t said);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_DRAGOUT_H */
