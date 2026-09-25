#include "rubraview/textedit.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool is(u8str_t s, const char *lit) { return s.len == strlen(lit) && memcmp(s.ptr, lit, s.len) == 0; }

int main(void) {
    printf("[test_textedit] Starting one-line text field tests...\n");
    char buf[32];
    rubraview_textedit_t te = rubraview_textedit_make(buf, sizeof(buf));

    rubraview_textedit_set(&te, U8("photo.jpg"));
    assert(is(rubraview_textedit_text(&te), "photo.jpg") && te.caret == 9 && !rubraview_textedit_has_selection(&te));
    assert(buf[te.len] == '\0');
    printf("  [PASS] Set puts the caret at the end with nothing selected\n");

    /* Caret moves and insertion in the middle. */
    for (int i = 0; i < 4; ++i) rubraview_textedit_move(&te, RUBRAVIEW_TEXTEDIT_LEFT, false);
    rubraview_textedit_insert(&te, U8("_1"));
    assert(is(rubraview_textedit_text(&te), "photo_1.jpg") && te.caret == 7);
    rubraview_textedit_move(&te, RUBRAVIEW_TEXTEDIT_HOME, false);
    rubraview_textedit_delete(&te);
    assert(is(rubraview_textedit_text(&te), "hoto_1.jpg") && te.caret == 0);
    rubraview_textedit_move(&te, RUBRAVIEW_TEXTEDIT_END, false);
    rubraview_textedit_backspace(&te);
    assert(is(rubraview_textedit_text(&te), "hoto_1.jp"));
    printf("  [PASS] The caret moves, and typing, Delete and Backspace work at it\n");

    /* Hangul: three bytes a syllable, never split. */
    rubraview_textedit_set(&te, U8("가나다.png"));
    rubraview_textedit_move(&te, RUBRAVIEW_TEXTEDIT_HOME, false);
    rubraview_textedit_move(&te, RUBRAVIEW_TEXTEDIT_RIGHT, false);
    assert(te.caret == 3);
    rubraview_textedit_move(&te, RUBRAVIEW_TEXTEDIT_RIGHT, true);
    assert(is(rubraview_textedit_selection(&te), "나"));
    rubraview_textedit_delete(&te);
    assert(is(rubraview_textedit_text(&te), "가다.png") && te.caret == 3);
    rubraview_textedit_backspace(&te);
    assert(is(rubraview_textedit_text(&te), "다.png") && te.caret == 0);
    printf("  [PASS] The caret steps over whole Hangul syllables\n");

    /* Selection with Shift, collapse without, typing replaces it. */
    rubraview_textedit_set(&te, U8("holiday.jpeg"));
    rubraview_textedit_move(&te, RUBRAVIEW_TEXTEDIT_HOME, false);
    rubraview_textedit_move(&te, RUBRAVIEW_TEXTEDIT_END, true);
    for (int i = 0; i < 5; ++i) rubraview_textedit_move(&te, RUBRAVIEW_TEXTEDIT_LEFT, true);
    assert(is(rubraview_textedit_selection(&te), "holiday"));
    rubraview_textedit_insert(&te, U8("summer"));
    assert(is(rubraview_textedit_text(&te), "summer.jpeg") && !rubraview_textedit_has_selection(&te));
    rubraview_textedit_select_all(&te);
    assert(is(rubraview_textedit_selection(&te), "summer.jpeg"));
    rubraview_textedit_move(&te, RUBRAVIEW_TEXTEDIT_LEFT, false);
    assert(te.caret == 0 && !rubraview_textedit_has_selection(&te));
    rubraview_textedit_select_all(&te);
    rubraview_textedit_move(&te, RUBRAVIEW_TEXTEDIT_RIGHT, false);
    assert(te.caret == te.len);
    rubraview_textedit_select_all(&te);
    rubraview_textedit_backspace(&te);
    assert(te.len == 0 && buf[0] == '\0');
    printf("  [PASS] Shift selects, a bare move collapses, typing and Backspace replace the selection\n");

    /* Paste: line breaks become spaces; what does not fit is cut at a character. */
    rubraview_textedit_set(&te, U8(""));
    rubraview_textedit_insert(&te, U8("a\r\nb"));
    assert(is(rubraview_textedit_text(&te), "a  b"));
    char small[8];
    rubraview_textedit_t s = rubraview_textedit_make(small, sizeof(small));
    size_t put = rubraview_textedit_insert(&s, U8("가나다"));   /* 9 bytes, room for 7 */
    assert(put == 6 && is(rubraview_textedit_text(&s), "가나"));
    assert(rubraview_textedit_insert(&s, U8("x")) == 1 && rubraview_textedit_insert(&s, U8("y")) == 0);
    printf("  [PASS] Pasted line breaks become spaces; overflow is cut on a character\n");

    printf("[test_textedit] All tests passed successfully!\n");
    return 0;
}
