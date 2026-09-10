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
| 1 | Press `F10` (and `Ctrl+,`) | The settings panel appears with eight tabs across the top. |
| 2 | Click each tab | Each shows its own settings; none is empty. |
| 3 | Change a slider on **Viewer**, press `Apply` | A line says the settings were saved, and the window stays open. |
| 4 | Open `settings.ini` in a text editor | The value you set is there, under the right `[section]`. |
| 5 | Add a line of your own — `made_up = 1` — then change something and `Apply` again | Your line is **still there**. A future version's key must survive being opened by this one. |
| 6 | Change something and press `Cancel`, then reopen | The change is gone. |
| 7 | Press `Reset to defaults`, then `OK` | Everything returns to its default and the panel closes. |
| 8 | Look at the greyed-out rows | They are settings nothing reads yet. They are listed rather than hidden on purpose — check that they cannot be dragged. |
| 9 | On the **General** tab, press `Register file types`, then check Explorer's "Open with" | Rubraview is offered. `Unregister` removes it. |
| 10 | Set a curation folder in `settings.ini`, press `Apply` in the settings window, then press `1` on an image | The file goes to that folder **without restarting** — the settings window re-reads the folders when it saves. |
| 11 | With **no** `settings.ini` beside the executable, press `F10` and `Apply` | The file is written to `%APPDATA%\rubraview\settings.ini`. |
| 12 | Put an empty `settings.ini` beside the executable and repeat | It is written **there**, and nothing new appears under `%APPDATA%`. |
| 13 | Copy the whole `dist/rubraview-v<version>` folder to a machine that has never run this program, and run it | It starts. There is no runtime to install. |
| 14 | Compare the bundle's `imports.txt` against that machine | Every name in it is a Windows system DLL. If a new name appears in a later build that is not, the "no DLL beside it" promise has been broken and the packaging step is where to catch it. |
| 15 | Run it from a USB stick with a `settings.ini` beside it, then check the host machine | Nothing was written outside the stick — no `%APPDATA%\rubraview`, no registry keys unless you asked for the file associations. |

## Known limitations (not defects)

- **The settings window is drawn inside the viewer's own window**, not
  as a separate top-level window. §3.22.1 offers both; a second window
  would need its own message loop, DPI handling and renderer without
  changing what can be configured.
- **A folder setting cannot be typed in the window.** The nine curation
  folders are shown but disabled; set them in `settings.ini` with a text
  editor. The panel has no text field yet.
- **The keyboard tab does not rebind anything.** Conflict detection is
  implemented and tested (T048), and `keymap.ini` works; the in-place
  rebinding table is not built.
- **Most settings are greyed.** They are declared with their sections,
  ranges and defaults, and the gate makes sure the ones marked live
  really are — but the modules that would read them are M5 and M8, or
  are still reading their values from constants.
