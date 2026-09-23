# T079 — Picking several files, and renaming the whole name

Covers: D-23, `docs/plans/active/2026-09-23-picker-selection.md`

## Steps and expected

1. Open the picker (`Ctrl+O`) in a folder with several files. `..` is the
   first tile wherever there is a folder above; tapping it goes up.
2. **Individual**: the button lights; tapping files turns them on and off,
   one at a time, and the bottom line counts them.
3. **Range**: tap two files — everything between them turns over, the two
   included. What was already on goes off.
4. **Same type**: every file with the focused file's extension is picked.
5. **Change ext**: a box opens holding the focused file's extension. Type
   another and press `Enter`: every picked file is renamed. `Ctrl+Z` puts
   each one back.
6. **Clear**: nothing is picked any more; the mode stays as it was.
7. `F2` on a file: the box holds the whole name, extension included, with
   a caret that blinks. Changing the extension is not asked about.

## Measured on the Windows 11 VM, 2026-09-23

A folder of `pic1..6.png` and `photo.jpg`, by screenshot:

- `..` first, and the five buttons along the bottom;
- Individual: tapping `pic1` and `pic3` gave "selected 2 (7166 bytes)";
- Range: tapping `pic4` then `pic6` left 5 picked — the range turned over
  `pic4`, `pic5`, `pic6` and left the other two alone;
- Same type: with `pic1` focused, all six `.png` were picked (6, 21498
  bytes), `photo.jpg` was not;
- Change ext: the box held `.png` with the caret; `jpg` and `Enter`
  renamed all six — the folder then read `photo.jpg pic1.jpg … pic6.jpg`;
- `F2`: the box read `pic1.png`, caret blinking (two screenshots a little
  apart, one with it, one without).

Found while measuring: the first tap on `Same type` picked nothing,
because the focus starts on `..`; the focus now starts on the first file.
And the extension change did nothing at first: the "has anything
changed?" test compared lengths, so `.png` to `.jpg` looked like no
change. Both fixed and re-measured.

## The rest of the buttons, measured 2026-09-23

Same folder, `pic1..4.png`, with `Folder 1` on the settings window's Files
page set to another folder:

- **Playlist**: with six `.png` picked, `Playlist0001.m3u8` appeared in the
  folder — `#EXTM3U` and the six names, relative — and the viewer opened
  exactly those six as one sequence (`pic1.png (1/6)`);
- **Recycle**: the first press asked, the second binned the two picked
  files, and the folder lost exactly those two;
- **Copy to** then `1`: the originals stayed and the copies appeared in
  the bound folder;
- **Move to** then `1`: the picked file left the folder and appeared in
  the bound one.

Two faults found here, both fixed:

- the numbered folders had never worked in any build: `settings.ini` was
  read by name alone, so it was looked for in whatever folder the viewer
  was started in. Portable mode was decided the same way. Both now use the
  executable's own folder;
- with a folder bound, a number key was taken by §3.18.3 (send the page to
  that folder) even while the picker was open, so "Copy to" moved the page
  behind the picker instead. The picker now answers first.

A third thing to know for testing: `keys.ps1` sends keys through the guest
and they can go nowhere if the viewer is not in front — `right` did
nothing for several runs. Keys over RDP (`xdotool key`) always arrive.