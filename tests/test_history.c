#include "rubraview/history.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static u8str_t lit(const char *s) {
    return (u8str_t){ .ptr = s, .len = strlen(s) };
}

static bool str_eq(u8str_t s, const char *l) {
    size_t n = strlen(l);
    return s.len == n && (n == 0 || memcmp(s.ptr, l, n) == 0);
}

int main(void) {
    printf("[test_history] Starting reading history and config hierarchy unit tests...\n");

    size_t mem_size = 256 * 1024;
    void *raw = malloc(mem_size);
    assert(raw != NULL);
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw, .size = mem_size });

    /* Test 1: The §3.17.1 file shape parses, including the exact line
       the RFC prints. */
    {
        const char *text =
            "[history]\n"
            "C:\\Comics\\OnePiece_Vol100.cbz = page:58, total:192, time:1725792000\n"
            "D:\\Manga\\Berserk_Vol01.cbz = page:3, total:210, time:1725000000\n";

        rubraview_history_t history = rubraview_history_parse(&arena, lit(text));
        assert(history.count == 2);

        const rubraview_history_entry_t *e =
            rubraview_history_find(&history, lit("C:\\Comics\\OnePiece_Vol100.cbz"));
        assert(e != NULL);
        assert(e->page == 58);
        assert(e->total == 192);
        assert(e->timestamp == 1725792000);
    }
    printf("  [PASS] The §3.17.1 history line parses into page, total and time\n");

    /* Test 2: An unknown path has no position, and keys outside the
       [history] section are not mistaken for entries. */
    {
        rubraview_history_t history = rubraview_history_parse(&arena, lit(
            "[settings]\n"
            "single_instance = true\n"
            "[history]\n"
            "A.cbz = page:5, total:10, time:100\n"));
        assert(history.count == 1);
        assert(rubraview_history_find(&history, lit("single_instance")) == NULL);
        assert(rubraview_history_find(&history, lit("never_opened.cbz")) == NULL);
    }
    printf("  [PASS] Only [history] keys become positions; unknown paths return nothing\n");

    /* Test 2b: a volume reached by paging on from the one before is keyed
       by the folder joined with '/'; the same file double-clicked in
       Explorer comes with backslashes. It is one book, with one place to
       resume from, and recording it again updates that place. */
    {
        rubraview_history_t history = rubraview_history_parse(&arena, lit(
            "[history]\n"
            "C:\\c/Vol 02.cbz = page:1, total:2, time:100\n"));
        const rubraview_history_entry_t *e = rubraview_history_find(&history, lit("C:\\c\\vol 02.CBZ"));
        assert(e != NULL && e->page == 1);

        rubraview_history_record(&arena, &history, lit("C:\\c\\Vol 02.cbz"), 0, 2, 200);
        assert(history.count == 1);
        assert(history.entries[0].timestamp == 200);
    }
    printf("  [PASS] One file spelt two ways is one history entry\n");

    /* Test 3: A malformed or partial line restores what it can instead
       of losing the whole file. */
    {
        rubraview_history_t history = rubraview_history_parse(&arena, lit(
            "[history]\n"
            "partial.cbz = page:7\n"
            "garbage.cbz = nonsense\n"));
        assert(history.count == 2);

        const rubraview_history_entry_t *partial = rubraview_history_find(&history, lit("partial.cbz"));
        assert(partial && partial->page == 7 && partial->total == 0);

        const rubraview_history_entry_t *garbage = rubraview_history_find(&history, lit("garbage.cbz"));
        assert(garbage && garbage->page == 0);
    }
    printf("  [PASS] Partial and malformed lines degrade rather than failing the file\n");

    /* Test 4: Recording the same archive twice updates in place, so
       reopening a volume does not grow the file. */
    {
        rubraview_history_t history = {0};
        rubraview_history_record(&arena, &history, lit("vol01.cbz"), 10, 200, 1000);
        rubraview_history_record(&arena, &history, lit("vol02.cbz"), 5, 180, 1001);
        assert(history.count == 2);

        rubraview_history_record(&arena, &history, lit("vol01.cbz"), 42, 200, 2000);
        assert(history.count == 2);

        const rubraview_history_entry_t *e = rubraview_history_find(&history, lit("vol01.cbz"));
        assert(e->page == 42 && e->timestamp == 2000);
    }
    printf("  [PASS] Re-recording an archive updates in place, not appends\n");

    /* Test 5: Serialize and re-parse round-trips every position. */
    {
        rubraview_history_t history = {0};
        rubraview_history_record(&arena, &history, lit("a.cbz"), 12, 100, 555);
        rubraview_history_record(&arena, &history, lit("b/c d.cbz"), 3, 40, 556);

        u8str_t text = rubraview_history_serialize(&arena, &history);
        assert(text.len > 0 && text.ptr[text.len] == '\0');
        /* D-13: one section per book, the path a quoted string — a path
           cannot be a key in a file that is also TOML. */
        const char *want =
            "[entry-1]\npath = \"a.cbz\"\npage = 12\ntotal = 100\ntime = 555\n"
            "[entry-2]\npath = \"b/c d.cbz\"\npage = 3\ntotal = 40\ntime = 556\n";
        assert(text.len == strlen(want) && memcmp(text.ptr, want, text.len) == 0);

        rubraview_history_t back = rubraview_history_parse(&arena, text);
        assert(back.count == 2);

        const rubraview_history_entry_t *a = rubraview_history_find(&back, lit("a.cbz"));
        assert(a && a->page == 12 && a->total == 100 && a->timestamp == 555);

        /* A path containing a space survives the round trip. */
        const rubraview_history_entry_t *b = rubraview_history_find(&back, lit("b/c d.cbz"));
        assert(b && b->page == 3);
    }
    printf("  [PASS] Serialize and re-parse round-trip, spaces in paths included\n");

    /* Test 6: Pruning keeps the newest entries and drops the oldest. */
    {
        /* The name buffers outlive every use below, so the recorded
           slices stay valid without copying. */
        char names[10][16];
        rubraview_history_t history = {0};
        for (int i = 0; i < 10; ++i) {
            snprintf(names[i], sizeof(names[i]), "v%02d.cbz", i);
            rubraview_history_record(&arena, &history, lit(names[i]), i, 100, 1000 + i);
        }
        assert(history.count == 10);

        rubraview_history_prune(&history, 4);
        assert(history.count == 4);
        /* The four newest timestamps are 1006..1009. */
        for (size_t i = 0; i < history.count; ++i) {
            assert(history.entries[i].timestamp >= 1006);
        }
    }
    printf("  [PASS] Pruning keeps the newest positions and drops the oldest\n");

    /* Test 7: §3.17.1 — a resume prompt is only worth showing partway
       through a volume. */
    {
        rubraview_history_entry_t middle = { .page = 58, .total = 192 };
        rubraview_history_entry_t start = { .page = 0, .total = 192 };
        rubraview_history_entry_t finished = { .page = 191, .total = 192 };

        assert(rubraview_history_should_offer_resume(&middle));
        assert(!rubraview_history_should_offer_resume(&start));
        assert(!rubraview_history_should_offer_resume(&finished));
        assert(!rubraview_history_should_offer_resume(NULL));
    }
    printf("  [PASS] Resume is offered partway through, not at the start or the end\n");

    /* Test 8: §3.17.2 — a settings.ini beside the executable means
       portable mode, and nothing goes to the host machine. */
    {
        assert(rubraview_config_mode_for(true) == RUBRAVIEW_CONFIG_PORTABLE);
        assert(rubraview_config_mode_for(false) == RUBRAVIEW_CONFIG_APPDATA);

        u8str_t portable = rubraview_config_path(&arena, RUBRAVIEW_CONFIG_PORTABLE,
                                                 lit("E:/Portable/Rubraview"), lit("X:/appdata"),
                                                 lit("history.ini"));
        assert(str_eq(portable, "E:/Portable/Rubraview/history.ini"));

        u8str_t roaming = rubraview_config_path(&arena, RUBRAVIEW_CONFIG_APPDATA,
                                                lit("E:/Portable/Rubraview"), lit("X:/appdata"),
                                                lit("history.ini"));
        assert(str_eq(roaming, "X:/appdata/rubraview/history.ini"));
    }
    printf("  [PASS] Portable mode writes beside the executable; otherwise under AppData\n");

    free(raw);
    printf("[test_history] All tests passed successfully!\n");
    return 0;
}
