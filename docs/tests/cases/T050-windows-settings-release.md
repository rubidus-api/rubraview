# T050: Windows settings and release check (M9)

M9 gave the viewer a settings window and a release layout. The schema
behind the window is covered on the host by T048, and §11.2's own
completion criterion — that every setting the code reads is reachable
from a tab — is checked mechanically by T049 rather than by looking.

What is left for a real machine: that the window works, that the file
lands in the right place, and that the packaged build runs somewhere it
was not built.

## Prerequisites

- `dist/rubraview-v<version>/` from `make package`.
- A second Windows machine, or a fresh user account, that has never run
  this program.

## Steps and expected results

| # | Action | Expected |
|---|---|---|
| 1 | Press `F10` (and `Ctrl+,`) | A separate window titled **Rubraview settings** opens above the viewer, with eight pages listed on its left (D-13; the steps in detail are T067). |
| 2 | Pick each page | Each shows its own settings; none is empty. |
| 3 | Change a value on **Viewer**, close the window with `Esc` | The viewer says the settings were saved. |
| 4 | Open `settings.ini` in a text editor | The value you set is there, under the right `[section]`, in the INI and TOML subset (T068). |
| 5 | Add a line of your own — `made_up = 1` — under any section, then change something and close again | Your line is **still there**. A future version's key must survive being opened by this one. |
| 6 | Change something, press `[ Revert ]`, close | The file is unchanged: Revert went back to what it held. |
| 7 | Press `[ Defaults ]`, then close | Everything returns to its default, and that is what is written. |
| 8 | Look at the grey rows | They are settings nothing reads yet — listed rather than hidden on purpose. |
| 9 | On **General**, press `< Register file types >`, then check Explorer's "Open with" | Rubraview is offered. `< Remove file types >` removes it. |
| 10 | On **Files**, pick a folder for **Folder 1** (`Enter`), close the window, then press `1` on an image | The file goes to that folder **without restarting** — closing the window re-reads the folders. `Delete` on the row empties it. |
| 11 | With **no** `settings.ini` beside the executable, open and close the settings window after a change | The file is written to `%APPDATA%\rubraview\settings.ini`. |
| 12 | Put an empty `settings.ini` beside the executable and repeat | It is written **there**, and nothing new appears under `%APPDATA%`. |
| 13 | Copy the whole `dist/rubraview-v<version>` folder to a machine that has never run this program, and run it | It starts. There is no runtime to install. |
| 14 | Compare the bundle's `imports.txt` against that machine | Every name in it is a Windows system DLL. If a new name appears in a later build that is not, the "no DLL beside it" promise has been broken and the packaging step is where to catch it. |
| 15 | Run it from a USB stick with a `settings.ini` beside it, then check the host machine | Nothing was written outside the stick — no `%APPDATA%\rubraview`, no registry keys unless you asked for the file associations. |

## Known limitations (not defects)

- **A folder setting is picked, not typed** (D-13): `Enter` opens the folder
  dialog and `Delete` empties the row. There is no text field in the window.
- **The keyboard tab does not rebind anything.** Conflict detection is
  implemented and tested (T048), and `keymap.ini` works; the in-place
  rebinding table is not built.
- **Most settings are greyed.** They are declared with their sections,
  ranges and defaults, and the gate makes sure the ones marked live
  really are — but the modules that would read them are M5 and M8, or
  are still reading their values from constants.
