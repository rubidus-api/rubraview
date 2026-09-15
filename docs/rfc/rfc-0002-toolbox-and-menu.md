# RFC-0002: What the Toolbox and the Menu Box Hold

- Status: Proposed
- Date: 2026-09-15
- Amends: RFC-0001 §3.6.1–§3.6.4 (the two floating boxes)
- Related: RFC-0003 (keys), D-13 (settings document), D-14 (keys changed in place)

---

## 1. Why

The viewer opens images, comic archives, videos and music well. What the
reader can *do* with them from the two floating boxes has not kept up:

- The toolbox is one fixed row of eight tiles — Prev, Next, Zoom−, Zoom+,
  1:1, Rotate, Slides, Full — whatever is on screen. A video gets a Rotate
  tile and no Play.
- **There is no play, pause, stop or volume control on screen.** Pause
  exists only as `Space` (measured on the Windows 11 VM, 2026-09-15: `Space`
  pauses and resumes `e_clip.mp4`). Stop, volume and mute do not exist
  anywhere — not as an action, not as a key, not in the audio layer
  (`pal_audio.h` has no volume call). What reads as "playback controls do
  not work" is mostly "playback controls were never built".
- The menu box has no File category. Opening a file is `Show › Files`;
  opening a folder, the reading history, rename, delete, settings and quit
  are not in it at all.
- The collapsed anchors read `=` and `<>`, which say nothing about which box
  is which.
- Both boxes are drawn at one fixed opacity (fill `#1A1A1AE0`, about 88%).

The owner's request (2026-09-15): toolbox tiles that change with what is on
screen — video, music, image, comic archive; anchors marked `M` and `T`;
Alt + wheel over a box changes its opacity, never down to invisible; File
Open and the like in the menu; and a plan for what both boxes should hold.

## 2. What is there today

### 2.1 The menu tree (`src/app/main.c`, `MENU_ITEMS`)

```
Menu
├── Layout ── Single · Dual · Book
├── Fit ───── Window · Width · Height · 1:1 · Smart
├── View ──── Rotate · Flip H · Crisp · Grid
├── Show ──── Slides · Strip · Files (open_picker)
└── Media ─── Sound (next track) · Subs (next subtitle) · Sub − · Sub +
```

Five roots, nineteen leaves, two levels. Every leaf names an action the
keymap can also reach. A level shows at most twelve tiles (`MENU_MAX_TILES`),
one of them `Back` below the root.

### 2.2 The toolbox

Eight tiles, the same for every file (`TOOLBOX_ACTIONS`):
`prev_page next_page zoom_out zoom_in actual_size rotate_cw toggle_slideshow toggle_fullscreen`.

### 2.3 What the viewer can already tell apart

The key dispatcher already decides what kind of page is on screen
(`dispatch_key`): a running slide show, a video or sound file
(`app->media`, with `media_info.has_video`), an animated image, a
multi-page TIFF/ICO (`subpage`), and otherwise a still image. Whether the
page came from an archive is `source.archive_path`. The toolbox profile in
§4 uses exactly these tests, so a tile and a key can never disagree about
what is on screen.

## 3. Principles

1. **One list.** As D-13 did for the settings window, the boxes are
   described in a small internal document and drawn by one interpreter —
   not one C table per file kind. (Alternative in §8, Q1.)
2. **Tiles name actions, never keys.** A tile is an action id from the
   keymap's vocabulary; RFC-0003 decides which key reaches it.
3. **A tile does one thing, and says what it will do.** Toggles show their
   state (`Pause` while playing, `Play` while paused; `Mute` / `Sound`).
4. **The boxes never become invisible**, and never hide the only way back
   (the anchor stays grabbable at the lowest opacity).
5. **No new file.** Opacity and volume are settings in `settings.ini`.

## 4. The toolbox, by what is on screen

A profile is chosen each time the page changes, in this order (first match):

| # | Profile | When | Tiles (left to right) |
|---|---|---|---|
| 1 | **Video** | `app->media` and `has_video` | Play/Pause · Stop · −5 s · +5 s · Vol − · Vol + · Mute · Subtitles · Sound track · Full |
| 2 | **Music** | `app->media` and not `has_video` | Prev file · Play/Pause · Stop · −5 s · +5 s · Next file · Vol − · Vol + · Mute |
| 3 | **Animated image** | GIF / APNG / animated WebP | Prev · Play/Pause · Frame ◀ · Frame ▶ · Next · Zoom − · Zoom + · Full |
| 4 | **Multi-page image** | TIFF / ICO with sub-pages | Prev file · Page ◀ · Page ▶ · Next file · Zoom − · Zoom + · 1:1 · Full |
| 5 | **Comic archive** | the page came from CBZ / ZIP / CB7 | Prev · Next · Layout (Single→Dual→Book) · Reading order · Prev volume · Next volume · Fit · Full |
| 6 | **Image** | anything else | Prev · Next · Zoom − · Zoom + · 1:1 · Rotate · Slides · Full |

- A running slide show adds a leading **Stop slides** tile to whatever the
  profile is, so a slide show can always be stopped with one tap.
- Ten tiles at most; the grid wraps at the box's column count, which
  `ui_box` already derives from the tile count.
- A tile whose action cannot apply (Subtitles on a file with none) is drawn
  dimmed and does nothing, rather than disappearing — tiles do not move
  under the reader's finger.
- Profile 6 is today's toolbox unchanged.

### 4.1 Playback actions the profiles need

| Action id | Does | Exists today |
|---|---|---|
| `media_play_pause` | play ↔ pause; at the end, play from the start | yes, as `anim_toggle_pause` (renamed with an alias, RFC-0003) |
| `media_stop` | pause and return to 0:00, first frame shown | **no** |
| `media_seek_back` / `media_seek_forward` | ±5 s | yes (Ctrl+Left / Ctrl+Right) |
| `media_volume_down` / `media_volume_up` | ±5 %, 0–100 %, shown on the OSD | **no** |
| `media_mute` | mute ↔ sound, volume remembered | **no** |
| `next_subtitle_track`, `next_audio_track` | as today | yes |
| `anim_step_back` / `anim_step_forward` | frame step (video and animation) | yes |

Volume and mute persist as `[audio] volume` (0–100) and `[audio] mute`
in `settings.ini`, declared in the settings document so the Audio page
shows them.

### 4.2 On-screen playback strip

A tile row cannot show where in the film the reader is. While a video or
music page is on screen, the bottom information bar grows a **timeline**:
elapsed · a bar to click or drag to seek · total, and the volume as a
number beside it (`🔊 70%`, `muted`). It fades with the OSD and returns on
pointer movement. This replaces reading the time from the window title.

## 5. The menu box

Proposed tree. Roots in bold; `▸` marks a submenu; items that exist today
are plain, new ones marked `+`.

```
Menu
├── File
│   ├── + Open file…            open_picker
│   ├── + Open folder…          open_folder
│   ├── + Recent ▸              the reading history, newest first (up to 10)
│   ├── + Next archive / Prev archive
│   ├── + Rename                rename_file
│   ├── + Delete                delete_file
│   ├── + Export…               quick_export
│   ├── + Batch…                open_batch
│   ├── + Settings…             open_settings
│   └── + Quit                  quit
├── View
│   ├── Layout ▸                Single · Dual · Book · + Reading order · + Spread detect
│   ├── Fit ▸                   Window · Width · Height · 1:1 · Smart · + Stretch · + Fit lock
│   ├── Rotate · + Rotate back · Flip H · + Flip V
│   └── Crisp · Grid
├── Playback                    (shown while a video or music page is on screen)
│   ├── + Play/Pause · + Stop · + −5 s · + +5 s · + Frame ◀ · + Frame ▶
│   ├── + Volume ▸              − · + · Mute
│   ├── Sound track (was Media › Sound)
│   └── Subtitles ▸             Next (was Subs) · Earlier (Sub −) · Later (Sub +)
├── Show
│   ├── Slides · Strip · + Info (OSD) · + Toolbox · + Fullscreen
│   └── + Box opacity ▸         100 % · 80 % · 60 % · 40 %
└── Help
    ├── + Keys…                 the settings window's Keys page
    └── + About                 version, FFmpeg found or not
```

- `Media` becomes `Playback` and appears only when it can do something —
  the same rule as the toolbox profile.
- `Files` moves from `Show` to `File › Open file…`.
- Nothing that exists today disappears; everything moves at most one level.
- Every `+` item is an action the keymap already has, except those §4.1
  adds and `Recent` (a menu built from `history.ini` at the moment it opens).

## 6. The anchors and opacity

### 6.1 `M` and `T`

The collapsed anchor's left half — the one that opens on click — reads
**`M`** for the menu box and **`T`** for the toolbox (today `=` and `<>`).
The right half, which opens on hover, keeps `v`. This is a label: pressing
`M` on the keyboard still toggles reading order (RFC-0003 §5.3).

### 6.2 Alt + wheel over a box

- With the pointer over a box — anchor or expanded — and `Alt` held, each
  wheel notch changes that box's opacity by 5 %.
- Range **30 %–100 %** (default 88 %, today's look). 30 % is proposed as
  the floor: the tiles' text stays readable over a bright page. The number
  is the owner's (Q3).
- Opacity scales the fill, border and tile colours; text stays opaque.
- Stored as `[ui] menubox_opacity` and `[ui] toolbox_opacity` (or one
  `box_opacity`, Q2), applied at once and written with the other settings.
- Alt + wheel anywhere else does what the wheel does today.
- A plain `Alt` press and release must not open the window's system menu
  (Windows does that on `WM_SYSKEYUP`); the window swallows `SC_KEYMENU`
  when no other key came with it.

## 7. Delivery in slices

| Slice | Content | Verified by |
|---|---|---|
| S1 | Anchors `M`/`T`; Alt + wheel opacity with the floor; settings keys | host test of the clamp and step; VM screenshot at 30 % and 100 % |
| S2 | Box document + interpreter; today's toolbox and menu expressed in it, no visible change | host test that the document yields today's 8 tiles and 19 leaves; a gate like `check-settings.py` that every action is handled |
| S3 | Toolbox profiles (§4) and the `Stop slides` tile | host test of profile selection for each page kind; VM screenshots of video, music, image, archive |
| S4 | `media_stop`, `media_mute`, volume (PAL + setting + OSD) | host test of the volume/mute state; on the VM the state and OSD only — the VM has no audio device, so hearing it waits for T058's machine |
| S5 | Menu tree (§5) including File and Recent | host test of the tree; VM walk through File › Open file |
| S6 | Timeline strip (§4.2) | host test of seek-from-click arithmetic; VM drag on a video |

Each slice is its own commit, with RFC-0001 §3.6 amended as it lands.

## 8. Questions for the owner

- **Q1. One document or C tables?** ⓐ a document read by one interpreter,
  as D-13 (recommended: a new profile is a text edit, and a gate can check
  every tile's action); ⓑ a C table per profile (less code now, six tables
  to keep in step with the keymap).
- **Q2. Opacity per box or shared?** ⓐ each box its own (recommended: the
  toolbox sits over the picture more often than the menu); ⓑ one value.
- **Q3. The floor.** 30 % proposed. Lower lets more picture through;
  below about 25 % white tile text over a white page is hard to find.
- **Q4. Volume mechanism.** ⓐ Windows session volume (`ISimpleAudioVolume`)
  — takes effect at once and follows the Windows mixer (recommended);
  ⓑ scaling the samples before they reach the device — testable on the
  host, but a change is heard only after the buffered audio plays out
  (about 0.1–0.2 s).
- **Q5. The timeline (§4.2)** now with the playback tiles, or after them.
- **Q6. Out of scope unless asked:** the detached toolbox window
  (RFC-0001 §3.6.1 — the `DETACHED` state exists, nothing implements it),
  pinning, A-B repeat, playback speed.
