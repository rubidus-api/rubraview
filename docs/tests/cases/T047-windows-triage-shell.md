# T047: Windows triage, lifecycle and shell check (M7)

M7 gave the viewer the power to delete, rename and move a reader's
files, plus single-instance hand-over, drag-and-drop and file
associations. The decisions are covered on the host by T046 — what a
legal filename is, what an undo would put back, which folder a number
key means, how a drop is routed. What is not covered is whether the
Windows calls behind them do what they say.

**This case writes to and deletes real files. Use a copied folder.**

## Prerequisites

- `dist/rubraview.exe` from `make win64`.
- A throwaway folder of 20-30 images, copied from somewhere else.
- A `settings.ini` beside the executable containing:
  ```ini
  single_instance = true

  [curation]
  dir_1 = D:\Triage\Keep
  dir_2 = D:\Triage\Best
  curation_mode = move
  ```
  with `D:\Triage` somewhere you do not mind writing to.

## Steps and expected results

| # | Action | Expected |
|---|---|---|
| 1 | Open the folder, press `Delete` | The file goes to the recycle bin — check it is there — and the viewer moves to the next image without a blank frame. |
| 2 | Press `Delete` on the last image | The viewer falls back to the previous image rather than showing nothing. |
| 3 | Press `Ctrl+Z` after a `Delete` | A line appears saying Windows keeps the recycle bin's undo to itself, and pointing at the bin. It does **not** claim to have restored anything, and it does **not** undo some earlier action instead. |
| 4 | Press `Shift+Delete` | A confirmation appears. Press `N` (or anything but `Y`/`Enter`): nothing happens. Press `Shift+Delete` again, then `Y`: the file is gone from the disk and **not** in the recycle bin. |
| 5 | Press `Ctrl+Z` right after that purge | It says the file cannot be brought back. It must **not** undo the action before the purge — that would restore the wrong file and look like it worked. |
| 6 | Press `1` | The file moves to `D:\Triage\Keep`, a line says so, and the viewer advances. The folder is created if it did not exist. |
| 7 | Press `Ctrl+Z` | The file comes back to where it was, and the viewer returns to it. |
| 8 | Set `curation_mode = copy`, restart, press `2` | The file is **copied** to `Best` and the viewer **stays** on it. |
| 9 | Press `2` twice on the same file | The second copy fails rather than overwriting the first, and says so. |
| 10 | Press `F2`, type a new name, `Enter` | The file is renamed, the extension is unchanged, and the viewer stays on it. |
| 11 | Press `F2` and type `NUL`, then `Enter` | It refuses and explains — Windows keeps that name for a device. Try `a/b` and a name ending in a space: both refused. |
| 12 | Press `F2` and `Esc` | Nothing is renamed. |
| 13 | Press `1` on a page inside a `.cbz` | Nothing happens. A page in an archive is not a file, and none of §3.18 applies to it. |
| 14 | Set `dir_1` to nothing (or remove the `[curation]` section), restart, press `1` | `1` goes back to meaning "fit to window" (§3.7.2). The two meanings of the number keys are separated by whether a folder is bound. |
| 15 | With the viewer open, run `rubraview.exe <another file>` from a prompt | The running window comes forward and shows that file. **No second window opens**, and the command returns immediately. |
| 16 | Minimise the viewer, then double-click a file association | The window is restored and brought to the front. |
| 17 | `rubraview.exe --new-instance <file>` | A second, independent window opens. |
| 18 | Set `single_instance = false`, restart, launch twice | Two windows. |
| 19 | Drag one image from Explorer onto the window | It opens. |
| 20 | Drag a folder onto the window | It opens as a gallery. |
| 21 | Drag three files onto the window | The first opens, with a line saying so. (See the limitation below.) |
| 22 | `rubraview.exe --register-shell` | It prints a line and exits. In Explorer, the "Open with" list now offers Rubraview for `.jpg`, `.png`, `.cbz` and the rest. |
| 23 | `rubraview.exe --unregister-shell`, then look in `regedit` under `HKCU\Software\Classes` | **No `Rubraview.*` key remains.** If you had set another program as the default for an extension in between, that choice is still there — unregistering only removes what still points at Rubraview. |

## Known limitations (not defects)

- **`Ctrl+Z` cannot restore a recycled file.** Windows exposes the
  recycle bin's undo through the shell's own undo stack, which belongs
  to Explorer, not to this process. Rather than pretend, the viewer says
  so and points at the bin. Moves, copies and renames **are** undone
  properly.
- **The rename box is not the native `EDIT` control** §3.18.2 names, so
  it has no IME, no selection and no clipboard: it takes plain
  characters and Backspace. A Korean or Japanese name cannot be typed
  into it yet, though one can be renamed *from*.
- **A multi-file drop opens the first file** rather than building a
  temporary playlist. The routing decision is implemented and tested
  (T046); what is missing is the temporary-playlist plumbing from §3.12.
