#include "rubraview/filemanage.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static u8str_t lit(const char *s) { return (u8str_t){ .ptr = s, .len = strlen(s) }; }
static bool is(u8str_t s, const char *l) { return s.len == strlen(l) && memcmp(s.ptr, l, s.len) == 0; }

int main(void) {
    printf("[test_filemanage] Starting file management model tests...\n");

    size_t mem_size = 1024 * 1024;
    void *raw = malloc(mem_size);
    assert(raw != NULL);
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw, .size = mem_size });

    /* Test 1: §3.18.2's forbidden characters. */
    {
        assert(rubraview_rename_validate(lit("holiday.jpg")) == RUBRAVIEW_RENAME_OK);
        assert(rubraview_rename_validate(lit("2026 여름 사진.jpg")) == RUBRAVIEW_RENAME_OK);

        const char *bad[] = { "a/b.jpg", "a\\b.jpg", "a:b.jpg", "a*b.jpg",
                              "a?b.jpg", "a\"b.jpg", "a<b.jpg", "a>b.jpg", "a|b.jpg" };
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
            assert(rubraview_rename_validate(lit(bad[i])) == RUBRAVIEW_RENAME_ERR_ILLEGAL_CHAR);
        }
        assert(rubraview_rename_validate(lit("")) == RUBRAVIEW_RENAME_ERR_EMPTY);
    }
    printf("  [PASS] The characters Windows forbids are refused\n");

    /* Test 2: the reserved device names — the ones people forget, where
       the rename fails after the old name has already gone. */
    {
        assert(rubraview_rename_validate(lit("NUL.jpg")) == RUBRAVIEW_RENAME_ERR_RESERVED_NAME);
        assert(rubraview_rename_validate(lit("con.png")) == RUBRAVIEW_RENAME_ERR_RESERVED_NAME);
        assert(rubraview_rename_validate(lit("Com1.txt")) == RUBRAVIEW_RENAME_ERR_RESERVED_NAME);
        assert(rubraview_rename_validate(lit("LPT9")) == RUBRAVIEW_RENAME_ERR_RESERVED_NAME);

        /* Names that merely start the same way are fine. */
        assert(rubraview_rename_validate(lit("console.jpg")) == RUBRAVIEW_RENAME_OK);
        assert(rubraview_rename_validate(lit("COM10.jpg")) == RUBRAVIEW_RENAME_OK);
    }
    printf("  [PASS] The DOS device names are refused, and lookalikes are not\n");

    /* Test 3: Windows silently strips a trailing dot or space, so the
       file would end up with a name the reader did not choose. */
    {
        assert(rubraview_rename_validate(lit("photo.")) == RUBRAVIEW_RENAME_ERR_TRAILING);
        assert(rubraview_rename_validate(lit("photo ")) == RUBRAVIEW_RENAME_ERR_TRAILING);
        assert(rubraview_rename_error_text(RUBRAVIEW_RENAME_ERR_TRAILING).len > 0);
    }
    printf("  [PASS] A trailing dot or space is refused rather than silently dropped\n");

    /* Test 4: §3.18.2 selects the stem only, so typing replaces the
       title without losing the format. */
    {
        assert(rubraview_rename_stem_length(lit("holiday.jpg")) == 7);
        assert(rubraview_rename_stem_length(lit("archive.tar.gz")) == 11);  /* only the last extension */
        assert(rubraview_rename_stem_length(lit("README")) == 6);

        u8str_t renamed = rubraview_rename_compose(&arena, lit("holiday.jpg"), lit("여름"));
        assert(is(renamed, "여름.jpg"));

        u8str_t no_ext = rubraview_rename_compose(&arena, lit("README"), lit("NOTES"));
        assert(is(no_ext, "NOTES"));
    }
    printf("  [PASS] Renaming replaces the stem and keeps the extension\n");

    /* Test 5: the undo stack returns actions newest first. */
    {
        rubraview_undo_stack_t stack = {0};
        rubraview_file_action_t out = {0};
        assert(rubraview_undo_peek(&stack, &out) == RUBRAVIEW_UNDO_NOTHING);

        rubraview_undo_push(&stack, (rubraview_file_action_t){
            .op = RUBRAVIEW_FILE_OP_RECYCLE, .source_path = lit("a.jpg"), .playlist_index = 3 });
        rubraview_undo_push(&stack, (rubraview_file_action_t){
            .op = RUBRAVIEW_FILE_OP_MOVE, .source_path = lit("b.jpg"),
            .target_path = lit("D:/Keep/b.jpg"), .playlist_index = 4 });

        assert(rubraview_undo_peek(&stack, &out) == RUBRAVIEW_UNDO_AVAILABLE);
        assert(is(out.source_path, "b.jpg") && out.playlist_index == 4);

        rubraview_undo_commit(&stack);
        assert(rubraview_undo_peek(&stack, &out) == RUBRAVIEW_UNDO_AVAILABLE);
        assert(is(out.source_path, "a.jpg"));

        rubraview_undo_commit(&stack);
        assert(rubraview_undo_peek(&stack, &out) == RUBRAVIEW_UNDO_NOTHING);
    }
    printf("  [PASS] Undo returns actions newest first, with the position to restore\n");

    /* Test 6: a permanent delete is on the stack so Ctrl+Z can say it
       cannot be undone — rather than undoing the action before it, which
       would restore the wrong file and look like it worked. */
    {
        rubraview_undo_stack_t stack = {0};
        rubraview_undo_push(&stack, (rubraview_file_action_t){
            .op = RUBRAVIEW_FILE_OP_MOVE, .source_path = lit("keep-me.jpg") });
        rubraview_undo_push(&stack, (rubraview_file_action_t){
            .op = RUBRAVIEW_FILE_OP_PURGE, .source_path = lit("gone.jpg") });

        rubraview_file_action_t out = {0};
        assert(rubraview_undo_peek(&stack, &out) == RUBRAVIEW_UNDO_IRREVERSIBLE);
        assert(is(out.source_path, "gone.jpg"));
        assert(!rubraview_file_op_is_undoable(RUBRAVIEW_FILE_OP_PURGE));
        assert(rubraview_file_op_is_undoable(RUBRAVIEW_FILE_OP_RECYCLE));

        /* And the move underneath is still there, untouched. */
        rubraview_undo_commit(&stack);
        assert(rubraview_undo_peek(&stack, &out) == RUBRAVIEW_UNDO_AVAILABLE);
        assert(is(out.source_path, "keep-me.jpg"));
    }
    printf("  [PASS] A permanent delete blocks undo instead of letting it hit the wrong file\n");

    /* Test 7: a long session does not grow the stack without bound, and
       the newest actions are the ones kept. */
    {
        rubraview_undo_stack_t stack = {0};
        for (int i = 0; i < RUBRAVIEW_UNDO_CAPACITY + 10; ++i) {
            rubraview_undo_push(&stack, (rubraview_file_action_t){
                .op = RUBRAVIEW_FILE_OP_RECYCLE, .playlist_index = (size_t)i });
        }
        assert(stack.count == RUBRAVIEW_UNDO_CAPACITY);

        rubraview_file_action_t out = {0};
        assert(rubraview_undo_peek(&stack, &out) == RUBRAVIEW_UNDO_AVAILABLE);
        assert(out.playlist_index == RUBRAVIEW_UNDO_CAPACITY + 9);
    }
    printf("  [PASS] The stack is bounded, and it is the oldest actions that fall off\n");

    /* Test 8: §3.18.3's settings, and the rule that decides whether the
       viewer moves on afterwards. */
    {
        u8str_t ini = lit("[curation]\n"
                          "dir_1 = D:/Curation/Keep\n"
                          "dir_2 = D:/Curation/Best\n"
                          "dir_9 = D:/Curation/Review\n"
                          "curation_mode = copy\n");
        rubraview_curation_t c = rubraview_curation_parse(&arena, ini);

        assert(is(rubraview_curation_target(&c, 1), "D:/Curation/Keep"));
        assert(is(rubraview_curation_target(&c, 2), "D:/Curation/Best"));
        assert(is(rubraview_curation_target(&c, 9), "D:/Curation/Review"));
        assert(rubraview_curation_target(&c, 5).len == 0);   /* unbound */
        assert(rubraview_curation_target(&c, 0).len == 0);   /* out of range */
        assert(rubraview_curation_target(&c, 10).len == 0);

        /* Copy mode leaves the file in the sequence, so the viewer stays. */
        assert(!rubraview_curation_advances(&c));

        rubraview_curation_t moving = rubraview_curation_parse(&arena, lit("[curation]\ndir_1 = D:/K\n"));
        assert(moving.mode == RUBRAVIEW_CURATION_MOVE);   /* the default */
        assert(rubraview_curation_advances(&moving));
    }
    printf("  [PASS] Curation folders are read, and copy stays while move advances\n");

    /* Test 9: §3.19.2's drop routing. */
    {
        rubraview_drop_item_t one_file[] = { { .path = lit("a.jpg"), .is_directory = false } };
        rubraview_drop_item_t one_dir[] = { { .path = lit("D:/Photos"), .is_directory = true } };
        rubraview_drop_item_t several[] = {
            { .path = lit("a.jpg"), .is_directory = false },
            { .path = lit("b.jpg"), .is_directory = false },
        };
        rubraview_drop_item_t mixed[] = {
            { .path = lit("D:/Photos"), .is_directory = true },
            { .path = lit("a.jpg"), .is_directory = false },
        };

        assert(rubraview_drop_classify(one_file, 1) == RUBRAVIEW_DROP_OPEN_FILE);
        assert(rubraview_drop_classify(one_dir, 1) == RUBRAVIEW_DROP_OPEN_FOLDER);
        assert(rubraview_drop_classify(several, 2) == RUBRAVIEW_DROP_PLAYLIST);
        /* A mixture is still a playlist: any other reading throws part
           of the drop away. */
        assert(rubraview_drop_classify(mixed, 2) == RUBRAVIEW_DROP_PLAYLIST);
        assert(rubraview_drop_classify(NULL, 0) == RUBRAVIEW_DROP_NOTHING);
    }
    printf("  [PASS] A drop is routed by what was dropped, and a mixture becomes a playlist\n");

    /* Test 10: §3.19.1's hand-over rule. */
    {
        assert(rubraview_instance_decide(true, true) == RUBRAVIEW_INSTANCE_HAND_OVER);
        assert(rubraview_instance_decide(true, false) == RUBRAVIEW_INSTANCE_RUN);
        /* Turned off, a second copy runs on its own even when one is up. */
        assert(rubraview_instance_decide(false, true) == RUBRAVIEW_INSTANCE_RUN);
        assert(rubraview_instance_decide(false, false) == RUBRAVIEW_INSTANCE_RUN);
    }
    printf("  [PASS] A second launch hands over only when single-instance is on\n");

    /* Test 11: §3.19.3's ProgIDs. Registering and unregistering must
       agree about the name, or the registry keeps leftovers. */
    {
        assert(is(rubraview_shell_progid(&arena, lit("jpg")), "Rubraview.jpg"));
        assert(is(rubraview_shell_progid(&arena, lit(".JPG")), "Rubraview.jpg"));
        assert(rubraview_shell_progid(&arena, lit("")).len == 0);
        assert(rubraview_shell_progid(&arena, lit(".")).len == 0);

        u8str_t list = rubraview_shell_extensions();
        assert(list.len > 0);
        assert(memchr(list.ptr, ';', list.len) != NULL);
    }
    printf("  [PASS] A ProgID is derived one way, so registering and unregistering agree\n");

    free(raw);
    printf("[test_filemanage] All tests passed successfully!\n");
    return 0;
}
