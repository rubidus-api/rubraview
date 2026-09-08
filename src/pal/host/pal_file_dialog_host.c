#include "rubraview/pal/pal_file_dialog.h"

rubraview_file_dialog_result_t rubraview_pal_file_dialog_open(proven_arena_t *arena, const rubraview_file_dialog_opts_t *opts) {
    (void)arena;
    (void)opts;
    /* Headless host mock: returns empty non-accepted result */
    return (rubraview_file_dialog_result_t){
        .paths = NULL,
        .count = 0,
        .accepted = false,
    };
}

rubraview_file_dialog_result_t rubraview_pal_file_dialog_save(proven_arena_t *arena, const rubraview_file_dialog_opts_t *opts) {
    (void)arena;
    (void)opts;
    return (rubraview_file_dialog_result_t){
        .paths = NULL,
        .count = 0,
        .accepted = false,
    };
}

rubraview_file_dialog_result_t rubraview_pal_file_dialog_pick_folder(proven_arena_t *arena, const rubraview_file_dialog_opts_t *opts) {
    (void)arena;
    (void)opts;
    return (rubraview_file_dialog_result_t){
        .paths = NULL,
        .count = 0,
        .accepted = false,
    };
}
