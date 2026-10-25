/*
 * The command line, read before any window: what it asks is done here and ends the run (`--batch`, `--diag`, the
 * probes, the shell's registration, a launch handed to the window already open), or the viewer starts (RV-163, D-198:
 * cut from wWinMain, its lines as they were; each early end says its code and returns true, and wWinMain lets the
 * memory and COM go once).
 */
#include "app.h"

bool run_command_line(proven_arena_t *arena, int *exit_code) {
    int argc = 0;
    LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (wargv) {
        /* Every argument: Explorer's menu may name many files (D-86). */
        int usable = 0;
        const char **argv_utf8 = NULL;
        proven_result_mem_mut_t list = rubraview_arena_alloc_array(arena, (size_t)(argc > 0 ? argc : 1), sizeof(char*));
        if (proven_is_ok(list.err)) {
            argv_utf8 = (const char**)(void*)list.value.ptr;
            for (int i = 0; i < argc; ++i) {
                argv_utf8[i] = "";
                int need = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, NULL, 0, NULL, NULL);
                proven_result_mem_mut_t one = need > 0 ? proven_arena_alloc(arena, (size_t)need)
                                                       : (proven_result_mem_mut_t){ .err = PROVEN_ERR_OVERFLOW };
                if (proven_is_ok(one.err) &&
                    WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, (char*)(void*)one.value.ptr, need, NULL, NULL) > 0) {
                    argv_utf8[i] = (const char*)(void*)one.value.ptr;
                }
            }
            usable = argc;
        }
        LocalFree(wargv);

        rubraview_cli_result_t cli;
        rubraview_cli_parse(&cli, arena, usable, argv_utf8);

        if (cli.err != RUBRAVIEW_CLI_OK) {
            char message[512];
            size_t pos = 0;
            message[0] = '\0';
            append_text(message, sizeof(message), &pos, "rubraview: ");
            proven_u8str_view_t reason = rubraview_cli_error_text(cli.err);
            for (size_t i = 0; i < reason.size && pos + 1 < sizeof(message); ++i) {
                message[pos++] = reason.ptr[i];
            }
            message[pos] = '\0';
            if (cli.offending.size > 0) {
                append_text(message, sizeof(message), &pos, ": ");
                for (size_t i = 0; i < cli.offending.size && pos + 1 < sizeof(message); ++i) {
                    message[pos++] = cli.offending.ptr[i];
                }
                message[pos] = '\0';
            }
            console_line(message);
            *exit_code = 2;
            return true;
        }

        if (cli.show_version) {
            console_line("rubraview " RUBRAVIEW_VERSION_STRING);
            *exit_code = 0;
            return true;
        }

        if (cli.probe_media) {
            int code = probe_media_file(arena, cli.input);
            *exit_code = code;
            return true;
        }

        if (cli.probe_gpu) {
            probe_gpu_resize();   /* D-38 first: it needs no file */
            int code = probe_gpu_file(arena, cli.input);
            *exit_code = code;
            return true;
        }

        if (cli.diagnostics) {
            int code = run_diagnostics(arena, cli.input);
            *exit_code = code;
            return true;
        }

        launch_paths_gather(arena, &cli);
        g_launch_verb = cli.shell_verb;
        g_launch_page = cli.start_page;
        /* RV-106: the order and the depth of this run's folders. */
        if (cli.sort_given) g_cli_sort = (int)cli.sort_mode;
        if (cli.descending_given) g_cli_descending = cli.descending ? 1 : 0;
        g_cli_subfolders = cli.recursive && !cli.batch_mode;
        g_launch_whole = cli.shell_whole;

        if (cli.batch_mode) {
            int code = run_batch(arena, &cli);
            *exit_code = code;
            return true;
        }

        /* §3.19.3, D-86: registering the file types and the menu is a job,
           not a launch. Without --types every extension is meant. */
        if (cli.wipe_shell) {
            /* D-110: this user's entries; with --all-users every user's, and the packages. */
            int removed = shell_wipe(cli.shell_all_users);
            console_line(removed < 0 ? "rubraview: could not remove the entries (every user's need an administrator)"
                                     : "rubraview: every Rubraview entry is out of Explorer's menu");
            *exit_code = removed < 0 ? SHELL_FAILED : SHELL_DONE;
            return true;
        }
        if (cli.register_shell || cli.unregister_shell) {
            int code = SHELL_FAILED;
            if (cli.register_shell) {
                proven_u8str_view_t selection = cli.shell_types.size > 0 ? cli.shell_types : rubraview_shell_extensions_all();
                shell_part_t types = cli.shell_no_types ? SHELL_PART_LEAVE : cli.shell_remove_types ? SHELL_PART_REMOVE : SHELL_PART_APPLY;
                shell_part_t menu = cli.shell_no_menu ? SHELL_PART_LEAVE : cli.shell_remove_menu ? SHELL_PART_REMOVE : SHELL_PART_APPLY;
                code = shell_apply(arena, selection, cli.shell_menu_types, types, menu, cli.shell_all_users);
            } else {
                code = shell_remove(cli.shell_all_users);
            }
            console_line(code == SHELL_FAILED ? "rubraview: could not update the file types and the menu"
                       : code == SHELL_DONE_NO_PARTS ? "rubraview: updated; Windows 11's menu needs rubraview_menu.dll and rubraview_menu.msix beside the program"
                       : code == SHELL_DONE_REFUSED ? "rubraview: updated; Windows did not take the Windows 11 menu's package"
                       : code == SHELL_DONE_STORE ? "rubraview: updated; Windows 11's menu is left to the Microsoft Store's copy"
                       : "rubraview: file types and menu updated");
            if (cli.shell_setup) code = rubraview_shell_setup_exit(code, cli.unregister_shell && !cli.register_shell);
            *exit_code = code;
            return true;
        }

        /* §3.19.1: hand the file to the window that is already open,
           rather than opening a second one. */
        {
            /* The file the viewer itself reads (settings_read_file): beside the
               executable in portable mode, under AppData otherwise. It was looked
               for in the working folder, so an installed viewer never saw it. */
            proven_u8str_view_t exe = rubraview_pal_process_executable(arena);
            proven_u8str_view_t beside = exe.size > 0 ? rubraview_path_dirname(exe) : PROVEN_LIT(".");
            if (beside.size == 0) beside = PROVEN_LIT(".");
            proven_u8str_view_t portable_ini = rubraview_path_join(arena, beside, PROVEN_LIT("settings.ini"));
            proven_u8str_view_t appdata = PROVEN_LIT(".");
            char appdata_utf8[1024];
            DWORD appdata_len = GetEnvironmentVariableA("APPDATA", appdata_utf8, (DWORD)sizeof(appdata_utf8));
            if (appdata_len > 0 && appdata_len < sizeof(appdata_utf8)) appdata = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)appdata_utf8, .size = appdata_len };
            proven_u8str_view_t settings_file = rubraview_config_path(
                arena, rubraview_config_mode_for(portable_ini.size > 0 && rubraview_pal_fs_exists(portable_ini)),
                beside, appdata, PROVEN_LIT("settings.ini"));
            proven_u8str_view_t settings = rubraview_pal_fs_read_file(arena, settings_file, 256u * 1024u);
            /* Through the settings loader, which knows where the key
               lives now ([general]) and where older files put it — and
               what it is when no file says (D-99: a window each). */
            rubraview_settings_t loaded = rubraview_settings_load(arena, settings);
            bool single_instance = rubraview_settings_get(&loaded, PROVEN_LIT("general"), PROVEN_LIT("single_instance")) > 0.5;
            /* What the menu asks is for the window that is open (the
               classic menu starts this once a selected file). */
            if (cli.shell_verb != RUBRAVIEW_SHELL_VERB_NONE) single_instance = true;
            /* D-100: but "Open in a new window" is a window of its own, always. */
            if (cli.new_instance || cli.shell_verb == RUBRAVIEW_SHELL_VERB_NEW_WINDOW) single_instance = false;

            bool alone = rubraview_pal_instance_claim();
            if (rubraview_instance_decide(single_instance, !alone) == RUBRAVIEW_INSTANCE_HAND_OVER) {
                size_t need = rubraview_shellreq_pack(cli.shell_verb, cli.shell_whole, g_launch_paths, g_launch_count, NULL, 0);
                char *blob = (char*)malloc(need);
                bool sent = false;
                if (blob) {
                    (void)rubraview_shellreq_pack(cli.shell_verb, cli.shell_whole, g_launch_paths, g_launch_count, blob, need);
                    /* The other launch may still be making its window
                       (several files opened at once): it is waited for. */
                    sent = rubraview_pal_instance_send_request(blob, need, 8000);
                    free(blob);
                }
                if (sent) {
                    *exit_code = 0;
                    return true;
                }
                /* The mutex said someone was there but no window
                   answered — a crashed instance, most likely. Start
                   normally rather than refusing to run. */
            }
        }
    }
    return false;
}
