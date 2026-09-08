#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <shobjidl.h>
#include <stdbool.h>
#include "rubraview/pal/pal_file_dialog.h"

static u8str_t win32_wstr_to_u8str(proven_arena_t *arena, const WCHAR *wstr) {
    if (!wstr || !arena) return (u8str_t){0};
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (utf8_len <= 0) return (u8str_t){0};

    proven_result_mem_mut_t res = proven_arena_alloc(arena, (proven_size_t)utf8_len);
    if (!proven_is_ok(res.err)) return (u8str_t){0};

    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, (char*)res.value.ptr, utf8_len, NULL, NULL);
    return (u8str_t){
        .ptr = (const char*)res.value.ptr,
        .len = (proven_size_t)(utf8_len - 1), /* Exclude null terminator from length */
    };
}

rubraview_file_dialog_result_t rubraview_pal_file_dialog_open(proven_arena_t *arena, const rubraview_file_dialog_opts_t *opts) {
    if (!arena) return (rubraview_file_dialog_result_t){0};

    IFileOpenDialog *pfd = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IFileOpenDialog, (void**)&pfd);
    if (FAILED(hr)) return (rubraview_file_dialog_result_t){0};

    FILEOPENDIALOGOPTIONS fos = 0;
    IFileDialog_GetOptions(pfd, &fos);
    fos |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST;
    if (opts && opts->allow_multi) {
        fos |= FOS_ALLOWMULTISELECT;
    }
    if (opts && opts->folder_mode) {
        fos |= FOS_PICKFOLDERS;
    }
    IFileDialog_SetOptions(pfd, fos);

    if (opts && opts->title) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, opts->title, -1, NULL, 0);
        if (wlen > 0) {
            WCHAR wtitle[256];
            MultiByteToWideChar(CP_UTF8, 0, opts->title, -1, wtitle, 256);
            IFileDialog_SetTitle(pfd, wtitle);
        }
    }

    HWND parent = opts ? (HWND)opts->parent_window_handle : NULL;
    hr = IFileDialog_Show(pfd, parent);
    if (FAILED(hr)) {
        IFileDialog_Release(pfd);
        return (rubraview_file_dialog_result_t){ .accepted = false };
    }

    rubraview_file_dialog_result_t result = { .accepted = true };

    if (opts && opts->allow_multi) {
        IShellItemArray *pItems = NULL;
        hr = IFileOpenDialog_GetResults(pfd, &pItems);
        if (SUCCEEDED(hr)) {
            DWORD count = 0;
            IShellItemArray_GetCount(pItems, &count);
            if (count > 0) {
                proven_result_mem_mut_t mres = proven_arena_alloc(arena, count * sizeof(u8str_t));
                if (proven_is_ok(mres.err)) {
                    u8str_t *path_arr = (u8str_t*)mres.value.ptr;
                    for (DWORD i = 0; i < count; ++i) {
                        IShellItem *pItem = NULL;
                        if (SUCCEEDED(IShellItemArray_GetItemAt(pItems, i, &pItem))) {
                            LPWSTR pszFilePath = NULL;
                            if (SUCCEEDED(IShellItem_GetDisplayName(pItem, SIGDN_FILESYSPATH, &pszFilePath))) {
                                path_arr[i] = win32_wstr_to_u8str(arena, pszFilePath);
                                CoTaskMemFree(pszFilePath);
                            }
                            IShellItem_Release(pItem);
                        }
                    }
                    result.paths = path_arr;
                    result.count = count;
                }
            }
            IShellItemArray_Release(pItems);
        }
    } else {
        IShellItem *pItem = NULL;
        hr = IFileDialog_GetResult(pfd, &pItem);
        if (SUCCEEDED(hr)) {
            LPWSTR pszFilePath = NULL;
            if (SUCCEEDED(IShellItem_GetDisplayName(pItem, SIGDN_FILESYSPATH, &pszFilePath))) {
                proven_result_mem_mut_t mres = proven_arena_alloc(arena, sizeof(u8str_t));
                if (proven_is_ok(mres.err)) {
                    u8str_t *path_arr = (u8str_t*)mres.value.ptr;
                    path_arr[0] = win32_wstr_to_u8str(arena, pszFilePath);
                    result.paths = path_arr;
                    result.count = 1;
                }
                CoTaskMemFree(pszFilePath);
            }
            IShellItem_Release(pItem);
        }
    }

    IFileDialog_Release(pfd);
    return result;
}

rubraview_file_dialog_result_t rubraview_pal_file_dialog_save(proven_arena_t *arena, const rubraview_file_dialog_opts_t *opts) {
    if (!arena) return (rubraview_file_dialog_result_t){0};

    IFileSaveDialog *pfd = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IFileSaveDialog, (void**)&pfd);
    if (FAILED(hr)) return (rubraview_file_dialog_result_t){0};

    FILEOPENDIALOGOPTIONS fos = 0;
    IFileDialog_GetOptions(pfd, &fos);
    fos |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_OVERWRITEPROMPT;
    IFileDialog_SetOptions(pfd, fos);

    HWND parent = opts ? (HWND)opts->parent_window_handle : NULL;
    hr = IFileDialog_Show(pfd, parent);
    if (FAILED(hr)) {
        IFileDialog_Release(pfd);
        return (rubraview_file_dialog_result_t){ .accepted = false };
    }

    rubraview_file_dialog_result_t result = { .accepted = true };
    IShellItem *pItem = NULL;
    hr = IFileDialog_GetResult(pfd, &pItem);
    if (SUCCEEDED(hr)) {
        LPWSTR pszFilePath = NULL;
        if (SUCCEEDED(IShellItem_GetDisplayName(pItem, SIGDN_FILESYSPATH, &pszFilePath))) {
            proven_result_mem_mut_t mres = proven_arena_alloc(arena, sizeof(u8str_t));
            if (proven_is_ok(mres.err)) {
                u8str_t *path_arr = (u8str_t*)mres.value.ptr;
                path_arr[0] = win32_wstr_to_u8str(arena, pszFilePath);
                result.paths = path_arr;
                result.count = 1;
            }
            CoTaskMemFree(pszFilePath);
        }
        IShellItem_Release(pItem);
    }

    IFileDialog_Release(pfd);
    return result;
}

rubraview_file_dialog_result_t rubraview_pal_file_dialog_pick_folder(proven_arena_t *arena, const rubraview_file_dialog_opts_t *opts) {
    rubraview_file_dialog_opts_t folder_opts;
    if (opts) {
        folder_opts = *opts;
    } else {
        folder_opts = (rubraview_file_dialog_opts_t){0};
    }
    folder_opts.folder_mode = true;
    folder_opts.allow_multi = false;
    return rubraview_pal_file_dialog_open(arena, &folder_opts);
}
#endif /* _WIN32 */
