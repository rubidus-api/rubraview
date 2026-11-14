#ifndef RUBRAVIEW_SHELLVERBS_H
#define RUBRAVIEW_SHELLVERBS_H

#include "rubraview/core.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RV-279 (owner, 2026-11-14: "레지스트리 쪽은 우클릭 메뉴에서 바로 보이고"): a file's verbs as Explorer reads them from
 * the registry, for Rubraview's own right-click list. Nothing is loaded and nothing runs here: the registry is read
 * through `rubraview_shellreg_t`, which the PAL fills from Windows' and the test from a table. Shell extensions
 * (IContextMenu handlers), Send to and Open with are not in the registry's verbs: they are in Explorer's own menus,
 * which a child process shows (`--shell-menu-host`).
 */

#define RUBRAVIEW_SHELLVERBS_MAX 12        /* in one right-click list, beside Rubraview's own rows */
#define RUBRAVIEW_SHELLVERB_DOCUMENT_MAX 15 /* Explorer offers a Document verb on 15 files at most */

typedef enum rubraview_shellreg_root {
    RUBRAVIEW_SHELLREG_CLASSES = 0,   /* HKEY_CLASSES_ROOT: the user's classes over the machine's, as Windows merges them */
    RUBRAVIEW_SHELLREG_USER,          /* HKEY_CURRENT_USER */
    RUBRAVIEW_SHELLREG_MACHINE        /* HKEY_LOCAL_MACHINE */
} rubraview_shellreg_root_t;

typedef struct rubraview_shellreg {
    void *ctx;
    /* Value `name` ("" the key's default) of `key` under `root`, as UTF-8 text (a number as its decimal digits,
       REG_EXPAND_SZ with the environment expanded). False when the key or the value is not there. */
    bool (*value)(void *ctx, rubraview_shellreg_root_t root, const char *key, const char *name, char *out, size_t cap);
    /* The `index`th subkey's name of `key`; false past the last (or when the key is not there). */
    bool (*subkey)(void *ctx, rubraview_shellreg_root_t root, const char *key, size_t index, char *out, size_t cap);
    /* "@file.dll,-12" text read (SHLoadIndirectString); may be NULL. False when it cannot be read. */
    bool (*indirect)(void *ctx, const char *text, char *out, size_t cap);
} rubraview_shellreg_t;

typedef enum rubraview_shellverb_multi {
    RUBRAVIEW_SHELLVERB_MULTI_DOCUMENT = 0,   /* the default: one run a file, 15 files at most */
    RUBRAVIEW_SHELLVERB_MULTI_SINGLE,         /* one file only */
    RUBRAVIEW_SHELLVERB_MULTI_PLAYER          /* any number of files */
} rubraview_shellverb_multi_t;

typedef struct rubraview_shellverb {
    char    name[64];         /* the verb's key ("open", "edit"); a cascade's child: "parent\\child" */
    char    caption[192];     /* the words to show, without '&' */
    char    command[1024];    /* the command line with %1 and the others still in it; "" when `by_shell` */
    int32_t multi;            /* rubraview_shellverb_multi_t */
    bool    standard;         /* `caption` is one of Explorer's own words (N_ text): to be translated */
    bool    by_shell;         /* Windows runs it (DelegateExecute, ExplorerCommandHandler, DropTarget, no command,
                                 or a code not filled here): the child, through ShellExecuteEx with `name` */
    bool    is_default;       /* the type's default verb (what a double click does) */
} rubraview_shellverb_t;

/**
 * The verbs of `path` (UTF-8; a folder when `is_folder`) in Explorer's order: the default first, `Position` Top and
 * Bottom honoured, then as the keys are read. Where they are looked for, in order:
 *   the extension's ProgID (the user's choice in FileExts\.ext\UserChoice, else HKCR\.ext's default; its `CurVer`)
 *   `SystemFileAssociations\.ext`, `SystemFileAssociations\<PerceivedType>`, `*`, `AllFilesystemObjects`;
 *   a folder: `Directory`, `Folder`, `AllFilesystemObjects`.
 * The first verb of a name wins. Left out: `ProgrammaticAccessOnly`, `LegacyDisable`, `OnlyInBrowserWindow`,
 * `HideBasedOnVelocityId`, `CommandStateHandler` (Explorer asks a COM object; the child's menus have them),
 * `printto`; `Extended` unless `shift`; an `AppliesTo` that is not one of the simple forms (System.FileName,
 * System.ItemName, System.FileExtension, joined by OR) or does not match; one a `MultiSelectModel` keeps from
 * `files` files; Rubraview's own (rubraview_shell_entry_is_ours). A cascade (`SubCommands`, `ExtendedSubCommandsKey`)
 * gives its children one level down, "Parent › Child", those with a command line only. Returns how many went into
 * `out` (at most `cap`).
 */
size_t rubraview_shellverbs_list(const rubraview_shellreg_t *reg, const char *path, bool is_folder, bool shift,
                                 size_t files, rubraview_shellverb_t *out, size_t cap);

/**
 * The command line for one file: %1 %L %V %D %0 the path, %W its folder, %* and %2..%9 nothing (Explorer passes no
 * parameters), %% a %. False when it does not fit, when it names a code not filled here (%I, %S, %H...), when an
 * environment variable is still in it, or when it has no place for the file (Windows then decides): such a verb is
 * `by_shell`.
 */
bool rubraview_shellverb_command(const char *command, const char *path, char *out, size_t cap);

/** The program a command line starts (its first token, without quotes), into `out`. False when it has none. */
bool rubraview_shellverb_program(const char *command, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_SHELLVERBS_H */
