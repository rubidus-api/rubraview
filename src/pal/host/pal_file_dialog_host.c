#include "rubraview/pal/pal_file_dialog.h"

rv_file_dialog_result_t rv_pal_file_dialog_open(proven_arena_t *arena, const rv_file_dialog_opts_t *opts) {
    (void)arena;
    (void)opts;
    /* Headless host mock: returns empty non-accepted result */
    return (rv_file_dialog_result_t){
        .paths = NULL,
        .count = 0,
        .accepted = false,
    };
}

rv_file_dialog_result_t rv_pal_file_dialog_save(proven_arena_t *arena, const rv_file_dialog_opts_t *opts) {
    (void)arena;
    (void)opts;
    return (rv_file_dialog_result_t){
        .paths = NULL,
        .count = 0,
        .accepted = false,
    };
}

rv_file_dialog_result_t rv_pal_file_dialog_pick_folder(proven_arena_t *arena, const rv_file_dialog_opts_t *opts) {
    (void)arena;
    (void)opts;
    return (rv_file_dialog_result_t){
        .paths = NULL,
        .count = 0,
        .accepted = false,
    };
}
