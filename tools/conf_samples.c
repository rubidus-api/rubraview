/*
 * conf_samples — writes sample configuration files through the viewer's
 * real writer, one file per argument directory entry, for
 * scripts/check-conf-format.py to read back with two unrelated parsers
 * (D-13). It is a host tool: nothing here runs in the viewer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rubraview/ini.h"

static u8str_t lit(const char *s) { return (u8str_t){ .ptr = s, .len = strlen(s) }; }

static int write_file(const char *dir, const char *name, u8str_t text) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return 1; }
    fwrite(text.ptr, 1, text.len, f);
    fclose(f);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: conf_samples <dir>\n"); return 2; }
    size_t size = 1u << 20;
    void *raw = malloc(size);
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw, .size = size });
    int failures = 0;

    /* Every kind, and the strings most likely to break one reader or the other. */
    rubraview_ini_doc_t kinds = {0};
    rubraview_ini_set_bool(&arena, &kinds, lit("kinds"), lit("yes"), true);
    rubraview_ini_set_bool(&arena, &kinds, lit("kinds"), lit("no"), false);
    rubraview_ini_set_int(&arena, &kinds, lit("kinds"), lit("zero"), 0);
    rubraview_ini_set_int(&arena, &kinds, lit("kinds"), lit("negative"), -42);
    rubraview_ini_set_int(&arena, &kinds, lit("kinds"), lit("big"), 9007199254740991LL);
    rubraview_ini_set_float(&arena, &kinds, lit("kinds"), lit("half"), 0.5);
    rubraview_ini_set_float(&arena, &kinds, lit("kinds"), lit("whole"), 2.0);
    rubraview_ini_set_float(&arena, &kinds, lit("kinds"), lit("negative_float"), -1.25);
    rubraview_ini_set_string(&arena, &kinds, lit("text"), lit("plain"), lit("ffmpeg"));
    rubraview_ini_set_string(&arena, &kinds, lit("text"), lit("empty"), lit(""));
    rubraview_ini_set_string(&arena, &kinds, lit("text"), lit("looks_like_number"), lit("2024"));
    rubraview_ini_set_string(&arena, &kinds, lit("text"), lit("looks_like_bool"), lit("true"));
    rubraview_ini_set_string(&arena, &kinds, lit("text"), lit("windows_path"), lit("C:\\Users\\Public\\Films"));
    rubraview_ini_set_string(&arena, &kinds, lit("text"), lit("quotes"), lit("say \"hi\""));
    rubraview_ini_set_string(&arena, &kinds, lit("text"), lit("percent"), lit("100% done"));
    rubraview_ini_set_string(&arena, &kinds, lit("text"), lit("hash_and_semicolon"), lit("a # b ; c = d"));
    rubraview_ini_set_string(&arena, &kinds, lit("text"), lit("korean"), lit("한글 폴더 이름"));
    rubraview_ini_set_string(&arena, &kinds, lit("text"), lit("edge_spaces"), lit("  padded  "));
    rubraview_ini_set_string(&arena, &kinds, lit("text"), lit("keys"), lit("F, F11, Alt+Enter"));
    failures += write_file(argv[1], "kinds.ini", rubraview_ini_serialize(&arena, &kinds));

    /* An old hand-edited file, read by the tolerant reader and written back. */
    const char *old =
        "\xEF\xBB\xBF; written by hand\n"
        "[video]\n"
        "decoder = ffmpeg\n"
        "subtitle_size = 48\n"
        "subtitle_size = 30\n"
        "zero_led = 010\n"
        "folder = D:\\Manga\\One Piece\n";
    rubraview_ini_doc_t rewritten = rubraview_ini_parse(&arena, lit(old));
    failures += write_file(argv[1], "rewritten-old.ini", rubraview_ini_serialize(&arena, &rewritten));

    free(raw);
    return failures ? 1 : 0;
}
