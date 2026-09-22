# T077 — The toolbox strip, the boxes' pins and anchor halves

Covers: RFC-0002 §6.3, D-20

## Steps and expected

1. Open a film. Point at the toolbox anchor's right half: the toolbox opens
   as a strip — a pin and the seek bar on top, the file's name under them,
   then small icon buttons in rows of eight: previous and next file,
   −5 s, +5 s, play/pause, stop, volume down and up, mute, subtitles,
   sound track, A-B, speed, full screen. Near the window's edge the strip
   moves inward rather than being cut off.
2. Point at a button: it is edged in blue, and the name line says what it
   does (`-5 s`, `Mute`, ...).
3. Click the seek bar: the film goes to that point.
4. Move the pointer away: an unpinned box folds back to its anchor.
5. Click a box's pin (top-left): it turns blue and filled, and the box
   stays open when the pointer leaves. Click it again: outline, and the box
   folds when the pointer leaves.
6. Click an anchor's left half: the box opens pinned. Drag the left half:
   the box moves. Clicking the right half does nothing more than hovering.
7. `Ctrl+T`: the detached toolbox window shows the same strip under its
   `T` / `Dock` bar; its seek bar and buttons work there too.
8. Settings (F10) › Video, at the bottom: "Adding FFmpeg (optional)" says
   DLLs not the exe, the five file names, beside rubraview.exe, version 8.1,
   and where to get it.

## Measured on the Windows 11 VM, 2026-09-22

All eight by screenshot (two_audio.mkv / clip_h264.mp4, WARP):

- 1: the strip opened left of the anchor at 980..1280 x 455..585, 14
  buttons in two rows, icons from Segoe MDL2 Assets;
- 2: over `+5 s` the name line read `+5 s` and the button was edged;
- 3: a click 18 px into the 260 px bar of a 10 s film went to 00:00.667;
- 5: pinned, the toolbox was still open 1.2 s after the pointer left;
  the menu box, unpinned, folded;
- 6: a click on `M` opened the menu with its pin lit;
- 7 and 8 as expected.

A first try clicked 16 px too high (screen and client rows differ by less
than assumed) and landed on the canvas, turning the page: the test's
coordinates, not the viewer.
