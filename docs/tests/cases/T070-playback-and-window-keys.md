# T070 — Playback keys, volume, and the window from the keyboard

Covers: R148, RFC-0003 §5.2, D-15, D-16

## Preparation

A video (`two_audio.mp4`), no `settings.ini` or `keymap.ini`, the viewer
opened on the video.

## Steps and expected

| # | Key | Expected |
|---|---|---|
| 1 | `Space` | the title says `paused` |
| 2 | `Right` | the time jumps 5 s (stops at the end) |
| 3 | `Left` | the time goes back 5 s |
| 4 | `Down` | OSD `volume 95%` |
| 5 | `Shift+M` | OSD `muted (95%)` |
| 6 | `Ctrl+Up`, `Ctrl+Left` | the window 40 px shorter, then narrower (at 96 DPI) |
| 7 | `Alt+Right`, `Alt+Down` | the window moves 40 px right, then down |
| 8 | `Alt+Up`, `Alt+Left`, `Ctrl+Down`, `Ctrl+Right` | back where it was, and larger again |
| 9 | `Esc` | the viewer quits; `settings.ini` has `[audio] volume = 95`, `mute = true` |

A window already as large as the screen does not grow and does not move
past its edge — the frame is pulled back onto the screen.

## Measured on the Windows 11 VM, 2026-09-15 (v0.0.3 + D-16)

1 `00:06.292 paused`; 2 `00:10.000` (the end); 3 `00:05.000`; 5 OSD
`muted (95%)`; 6 `1280x752 → 1280x712 → 1200x712`; 7 `0,0 → 40,0 → 40,40`;
8 `0,0 1200x712 → 0,0 1240x752`; 9 the file held `volume = 95`,
`mute = true`. The VM has no audio device: the level is what the session is
told and what the OSD and the file say, not what was heard (that is T058's).
