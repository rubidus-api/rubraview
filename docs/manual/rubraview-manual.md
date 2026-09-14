# Rubraview — user manual

A viewer for images, comic archives and (later) video, for Windows.

## Where the files are

A build puts everything under `dist/`:

```
dist/rubraview-v0.0.3.exe              the viewer
dist/rubraview-mfprobe-v0.0.3.exe      a tool that reports which video
                                       formats your Windows can play
dist/rubraview-v0.0.3/                 the release bundle — zip this
```

The bundle holds the viewer, this manual, the licences of the three
libraries it borrows, and a list of the system DLLs it calls.

## Getting it running

The program is built with its version in its name —
`rubraview-v0.0.3.exe` — so that a copy sitting in a downloads folder
still says which build it is. Rename it to `rubraview.exe` if you prefer;
nothing depends on the name. The examples below use the short form.

`rubraview.exe --version` prints the version, and it is in the window
title too, so a screenshot identifies the build.

`rubraview.exe` needs nothing installed beside it. Double-click it, or
give it a file or a folder:

```
rubraview.exe "D:\Comics\Vol 01.cbz"
rubraview.exe "D:\Photos"
```

Launching it again while it is already open does not open a second
window — the running one comes forward and shows the new file. Pass
`--new-instance` when you do want a second window.

To make Windows offer Rubraview in "Open with", run
`rubraview.exe --register-shell` once. `--unregister-shell` removes it
again and leaves nothing behind in the registry.

## Reading

| Key | What it does |
|---|---|
| `→` `←`, `PageDown` `PageUp`, `J` `K`, `D` `A` | Next / previous page |
| `Space` | Next page (or play/pause while an animation is on screen) |
| `Home` `End` | First / last page |
| `Ctrl+]` `Ctrl+[` | Next / previous archive in the folder |
| `B` | Single page / two pages |
| `M` | Left-to-right / right-to-left (manga) |
| `1`–`5` | Fit to window, width, height, actual size, smart fit |
| `Ctrl+Wheel` | Zoom at the pointer |
| `N` | Crisp scaling for pixel art |
| `G` | Pixel grid, once zoomed past 400% |
| `R`, `Shift+R`, `H`, `V` | Rotate and flip the view |
| `F`, `F11` | Full screen |
| `S`, `F5` | Slide show; `[` and `]` change the interval |
| `T`, `Tab`, `F4` | Toolbox, menu, filmstrip |
| `O` | Open something |
| `I` | Show the file's details |

Reaching the last page of `Vol 01.cbz` and pressing `→` opens
`Vol 02.cbz`. Close the viewer partway through a book and it offers your
place when you open it again — press `Enter` to take it.

Folders and archives are the same thing to the viewer: `.cbz`, `.zip`,
`.cb7` and `.7z` all open, and nothing is ever unpacked to your disk.

## Animated images

While a GIF, WebP or APNG is on screen:

| Key | What it does |
|---|---|
| `Space` | Pause and resume |
| `.` `,` | One frame forward / back |
| `Ctrl+]` `Ctrl+[` | Faster / slower, 0.25x to 2x |

A multi-page TIFF or a multi-size `.ico` uses `.` and `,` to step
through its pages, and opens an `.ico` at its largest layer.

## Adjusting a picture

`E` opens the adjust panel. Drag a slider, and the picture follows.
Nothing is written until you press **Save a copy**, which writes
`<name>_edit.<ext>` beside the original and leaves the original alone.

`Ctrl+E` opens Export: pick a format and a quality, tick **Privacy
clean** to strip GPS and camera details, and press Export.

Privacy clean on a JPEG that is otherwise unchanged does not re-encode
the picture — the metadata is cut out and every pixel is left exactly as
it was.

## Sorting your files

| Key | What it does |
|---|---|
| `Delete` | To the recycle bin |
| `Shift+Delete` | Delete for good — it asks first |
| `Ctrl+Z` | Undo a move, a copy or a rename |
| `F2` | Rename, keeping the extension |
| `1`–`9` | Send the file to a folder you chose |

To use the number keys for sorting, put this in `settings.ini`:

```ini
[curation]
dir_1 = D:\Sorted\Keep
dir_2 = D:\Sorted\Best
curation_mode = move    ; or copy
```

A number key sorts only when you have given it a folder; the ones you
have not keep their usual meaning.

**`Ctrl+Z` cannot bring a file back from the recycle bin.** Windows keeps
that undo for File Explorer. Restore it from the recycle bin instead.
Moves, copies and renames do undo properly.

## Converting a lot of files at once

```
rubraview.exe --batch --resize=50% --format=webp "D:\Photos"
```

No window opens. It prints how many files it converted, skipped and
failed.

| Option | What it does |
|---|---|
| `--resize=50%` `--resize=1920x1080` `--resize=w800` `--resize=h600` | How to resize |
| `--filter=lanczos3\|bicubic\|bilinear\|nearest` | How to resample |
| `--format=jpg\|png\|webp\|gif\|bmp\|tif\|ico` | What to write |
| `--quality=1..100` | For JPEG and WebP |
| `--rotate=90\|180\|270\|exif`, `--flip=h\|v` | Turn them |
| `--grayscale`, `--exposure=`, `--contrast=`, `--sharpen=amount[,radius]` | Adjust them |
| `--privacy-clean` | Strip GPS and camera details |
| `--include=*.jpg;*.png`, `--exclude=*_thumb.*` | Which files |
| `--min-size=`, `--max-size=` | By size, in bytes |
| `--name={name}_thumb.{ext}` | What to call the results |
| `--out=DIR` | Where to put them |
| `--recursive` | Include subfolders |

A misspelled option stops the run before anything is converted.

Rotating a JPEG by 90, 180 or 270 degrees, with nothing else asked for,
does not decode the picture at all — the result is bit-for-bit the same
image, just turned. Rotating one four times gives you back the file you
started with.

## Settings

`F10` or `Ctrl+,` opens the settings window: eight tabs, `Apply` to
save without closing, `Reset to defaults` to start over.

Settings live in `settings.ini`. If there is one **beside
`rubraview.exe`**, that is the one used and nothing is written anywhere
else on the machine — this is portable mode, for a USB stick. Otherwise
they live in `%APPDATA%\rubraview\`.

Settings shown greyed are ones the program does not read yet. They are
listed rather than hidden so you can see what is coming.

## What is not there yet

- **Video and audio.** The format list mentions them; the playback
  engine is not built.
- **Reading a `.cbr` (RAR) archive.**
- **Typing a Korean or Japanese filename in the rename box.** You can
  rename such a file, but not type one — the box takes plain characters
  for now.
- **Dropping several files at once** opens the first of them.
