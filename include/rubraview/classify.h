#ifndef RUBRAVIEW_CLASSIFY_H
#define RUBRAVIEW_CLASSIFY_H

/*
 * RV-133 (owner, 2026-10-17: "미리 설정된 폴더 목록 창을 띄우고, 폴더를
 * 선택하여 누르면 이미지가 그곳으로 이동하도록 예약됩니다. 그리고 분류가
 * 끝났을 때 목록창의 복사/옮기기를 누르면 한꺼번에 옮겨집니다."): which
 * file is set aside for which of the nine curation folders, nothing moved
 * yet. A file is in one folder at most; choosing another moves its mark.
 * The paths are the caller's: kept alive by it as long as they are here.
 */

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RUBRAVIEW_CLASSIFY_MAX 8192u

typedef struct rubraview_classify {
    proven_u8str_view_t paths[RUBRAVIEW_CLASSIFY_MAX];
    uint8_t folders[RUBRAVIEW_CLASSIFY_MAX];   /* 1 .. 9 */
    size_t  count;
} rubraview_classify_t;

/** The folder (1-9) `path` is set aside for; 0 for none. */
int32_t rubraview_classify_folder_of(const rubraview_classify_t *c, proven_u8str_view_t path);
/** Set `path` aside for `folder` (1-9), or take its mark off (0). False when full or the folder is out of range. */
bool rubraview_classify_set(rubraview_classify_t *c, proven_u8str_view_t path, int32_t folder);
/** How many are set aside for `folder`; 0 counts them all. */
size_t rubraview_classify_count(const rubraview_classify_t *c, int32_t folder);
/** Take the `index`th mark off, the order of the rest kept (after its file was put in place). */
void rubraview_classify_remove_at(rubraview_classify_t *c, size_t index);
void rubraview_classify_clear(rubraview_classify_t *c);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_CLASSIFY_H */
