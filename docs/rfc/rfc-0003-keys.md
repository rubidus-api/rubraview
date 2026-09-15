# RFC-0003: One Source for the Keys

- Status: Accepted (2026-09-15, D-16: arrows seek 5 s and change volume while playing · Space plays/pauses, stop has no key · Ctrl+arrows resize and Alt+arrows move the window · `[media]` · generated table · 30 s seek later)
- Date: 2026-09-15
- Amends: RFC-0001 §3.7.1, §3.7.2, §3.7.5
- Related: RFC-0002 (the boxes name actions; this RFC gives them keys), D-14 (keys changed in the settings window)

---

## 1. Why

There are three lists of what a key does, and they disagree:

1. **RFC-0001 §3.7.2**, the hotkey table — written first, as a wish list.
2. **`src/core/default_keymap.c`** — what the viewer actually binds (66 bindings in 7 contexts).
3. **`handle_action` in `src/app/main.c`** — what the viewer actually does (66 action ids).

A mechanical comparison (2026-09-15) found:

- **Keys that do nothing.** `pan_left`, `pan_right`, `pan_up`, `pan_down`
  are bound to `Alt+Arrow` and nothing handles them.
- **Actions with no key.** `layout_single`, `layout_dual`, `layout_book`
  are reachable only from the menu; `resume_accept` only from its prompt.
- **Table rows that were never built** — volume, mute, stop, ±30 s and ±1 s
  seek, A-B repeat, playback speed, frame capture — and rows the owner has
  since changed (arrows and J/K/A/D stopped turning pages, 2026-09-09).
- **Keys not in the table** — rename, delete, undo, settings, subtitle
  timing and track switching, export.

Nothing keeps them together: D-14 made keys editable and gave the keymap a
real conflict check, but the table in the RFC can drift again tomorrow.

## 2. Principles

1. **`default_keymap.c` is the source.** The table in the documents is
   generated from it, never typed.
2. **Every action is reachable, and every binding does something.** A gate
   enforces both, the way `check-settings.py` does for settings.
3. **A context is named for what is on screen**, and a key in an overlay
   context (media, slide show, sub-page) keeps its ordinary meaning
   everywhere else — the rule D-14 already follows.
4. **Keys and tiles share action ids** (RFC-0002). A tile never names a key.

## 3. Where the table and the keymap part (2026-09-15)

| Action | RFC-0001 §3.7.2 | `default_keymap.c` | State |
|---|---|---|---|
| Next page | Right, PageDown, Space, J, D | PageDown, Space, Enter | changed by the owner 2026-09-09 |
| Previous page | Left, PageUp, Shift+Space, K, A | PageUp, Backspace, Shift+Space | changed by the owner 2026-09-09 |
| Up to folder | Backspace, Alt+Up | Ctrl+Up | moved: both keys spoken for |
| Next / prev archive | Ctrl+] / Ctrl+[, Ctrl+PageDown/Up | Ctrl+] / Ctrl+[ | PageDown/Up kept by skipping |
| 1:1 | 4, 0, Ctrl+0, Middle click | 4, 0, Ctrl+0 | middle click is the pointer's (§3.7.3) |
| Pan | Alt+Arrows, W A S D | Alt+Arrows | **bound, not handled** |
| Play / pause | Space, P | Space | `P` missing |
| Frame step | Period / Comma, Ctrl+Right/Left | Period / Comma | Ctrl+Right/Left became 5 s seek |
| Seek ±5 s | Right / Left | Ctrl+Right / Ctrl+Left | moved |
| Seek ±30 s | Ctrl+Right / Left | — | not built |
| Seek ±1 s | Shift+Right / Left | — | not built |
| A-B repeat, clear | [ , ] , \ | — | not built |
| Volume ±5 % | Up / Down | — | **not built (no volume in the audio layer)** |
| Mute | Shift+M | — | **not built** |
| Stop | — (toolbox only) | — | **not built** |
| Playback speed | } / { | Ctrl+] / Ctrl+[ (animations only) | video: not built |
| Capture frame | Ctrl+C, Ctrl+S | — | not built |
| Toolbox | T, F2 | T | F2 is rename (owner 2026-09-15) |
| Rename, delete, undo, purge | §3.18, not in the table | F2, Delete, Ctrl+Z, Shift+Delete | table incomplete |
| Settings, export, save as | not in the table | F10 / Ctrl+Comma, Ctrl+E, Ctrl+Shift+S | table incomplete |
| Subtitle −/+, next subtitle, next sound | §3.16, not in the table | Z / X, C, A | table incomplete |
| Crisp (nearest) | not in the table | N | table incomplete |

## 4. The gate: `check-actions.py`

Reads the keymap (built-in text), the box document (RFC-0002 S2, or the C
tables until then) and `handle_action`, and fails when:

1. a binding, tile or menu item names an action `handle_action` does not
   know — today: the four `pan_*`;
2. an action `handle_action` knows is reachable by no key, tile or menu
   item, unless it is listed as internal with a reason (`resume_accept`:
   answered in its own prompt);
3. the table in RFC-0001 §3.7.2 (and the manual's key list) differs from
   the table generated from the keymap.

It is proven to fail both ways by injection, as the other gates were.

## 5. Proposed changes

### 5.1 Contexts

| Today | Proposed | When it is asked first |
|---|---|---|
| `[ui]` | `[ui]` | always, after `navigation` |
| `[navigation]` | `[navigation]` | always, first |
| `[view]` | `[view]` | always, after `ui` |
| `[slideshow]` | `[slideshow]` | while a slide show runs |
| `[animation]` | **`[media]`** — video, music and animated images | while one is on screen |
| `[subpage]` | `[subpage]` | while a multi-page TIFF/ICO is on screen |

`[animation]` stays readable as an alias, so a `keymap.ini` saved under
D-14 keeps working; the settings window writes `[media]` from then on.
RFC-0001 §3.7.5's own example already says `[media]`.

### 5.2 Playback keys (context `media`)

| Action | Key | Why this key |
|---|---|---|
| `media_play_pause` | Space, **P** | Space as today; P as §3.7.2 |
| `media_stop` | **Ctrl+Space** | free; see Q2 |
| `media_seek_back` / `forward` ±5 s | **Left / Right** | the arrows are free since 2026-09-09 and are what players use |
| ±30 s | **Ctrl+Left / Ctrl+Right** | today these seek 5 s — this changes them |
| Frame step | Comma / Period | as today |
| `media_volume_down` / `up` ±5 % | **Down / Up** | free; Ctrl+Up stays "up to folder" |
| `media_mute` | **Shift+M** | as §3.7.2; plain M stays reading order |
| Speed −/+ | Ctrl+[ / Ctrl+] | extended from animations to video and music |
| Subtitle −/+, next subtitle, next sound | Z / X, C, A | as today |

Because `media` is an overlay, Left/Right still do nothing in a folder of
pictures, and Shift+Left/Right still skip ten pages there.

### 5.3 The rest

- **Pan.** ⓐ implement `pan_*` (Alt+Arrows move a zoomed picture by a step)
  — recommended, the keys were promised; ⓑ remove the four bindings.
- **Layout keys.** `layout_single/dual/book` stay menu-only, listed as such
  in the generated table; `B` already cycles them.
- **`M`.** Stays reading order; RFC-0002's `M` on the anchor is a label.
- **Frame capture, A-B repeat, ±1 s seek.** Not proposed now; they leave the
  generated table and are listed under "not built" in RFC-0001 until an RFC
  brings them back.

### 5.4 Keyboard input methods (to measure first)

The viewer window does nothing about the input method. With the Korean IME
in Hangul mode, Windows can deliver letter keys as `VK_PROCESSKEY`, which
the key-name table does not know — so `T`, `I`, `E`, `S`, `B` and the other
single-letter shortcuts may silently do nothing. **Not measured yet.** If it
reproduces on the VM: turn the IME off for the viewer window
(`ImmAssociateContextEx`) and turn it on only while the rename box is open,
which is the one place text is typed.

### 5.5 Alt

`Alt` alone must not open the window's system menu (RFC-0002 §6.2 uses
Alt + wheel). `Alt+Enter` and `Alt+Arrows` keep working.

## 6. Delivery in slices

| Slice | Content | Verified by |
|---|---|---|
| K1 | `check-actions.py` with today's exceptions written down; `pan_*` per Q3 | the gate fails on injected drift both ways |
| K2 | Generated key table; RFC-0001 §3.7.2 and the manual replaced by it | gate rule 3 |
| K3 | `[media]` context with the `[animation]` alias | host test: an old keymap.ini loads the same |
| K4 | Playback keys (§5.2), landing with RFC-0002 S4 | host tests of dispatch per context; VM title and OSD |
| K5 | IME measurement, then the fix if it reproduces | VM with the Korean IME in Hangul mode |

## 7. Questions for the owner

- **Q1. Arrows while a video or music plays** — Left/Right seek ±5 s and
  Up/Down change volume (recommended; what most players do), or keep the
  arrows unbound.
- **Q2. Stop's key** — Ctrl+Space (recommended), or no key (tile only).
- **Q3. Pan** — implement Alt+Arrows (recommended) or remove the bindings.
- **Q4. Rename `[animation]` to `[media]`** with the old name still read.
- **Q5. Generated table** — RFC-0001 §3.7.2 becomes generated and the
  unbuilt rows move to a "not built" list (recommended), or the table stays
  hand-written with only a check against the keymap.
- **Q6. Ctrl+Left / Ctrl+Right** go from 5 s to 30 s once plain arrows seek 5 s.
