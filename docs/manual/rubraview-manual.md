# Rubraview — user manual

A viewer for images, comic archives and (later) video, for Windows.

## Where the files are

A build puts everything under `dist/`:

```
dist/rubraview-v0.0.17.exe              the viewer
dist/rubraview-mfprobe-v0.0.17.exe      a tool that reports which video
                                       formats your Windows can play
dist/rubraview-v0.0.17/                 the release bundle — zip this
```

The bundle holds the viewer, this manual, the licences of the three
libraries it borrows, and a list of the system DLLs it calls.

## Getting it running

The program is built with its version in its name —
`rubraview-v0.0.17.exe` — so that a copy sitting in a downloads folder
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

`PageDown` / `PageUp`, `Space` / `Backspace` turn the pages; `B` shows two
pages side by side, `M` reads right to left. Every key is in **Keys** at the
end of this manual.

Reaching the last page of `Vol 01.cbz` and pressing `PageDown` opens
`Vol 02.cbz`. Close the viewer partway through a book and it offers your
place when you open it again — press `Enter` to take it.

Folders and archives are the same thing to the viewer: `.cbz`, `.zip`,
`.cb7` and `.7z` all open, and nothing is ever unpacked to your disk.

## Animated images

While a GIF, WebP or APNG is on screen, `Space` pauses it, `.` and `,`
step a frame, and `Ctrl+]` / `Ctrl+[` change its speed.

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

`Delete` sends the file to the recycle bin (`Shift+Delete` deletes for good,
after asking), `F2` renames it keeping the extension, `Ctrl+Z` undoes a move,
a copy or a rename, and `1`–`9` send it to a folder you chose.

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

`Ctrl+B` does the same thing from the viewer, over the folder you are
reading: choose a size, a format, grayscale or privacy clean, then **Run on
this folder**. The results go into a `rubraview-out` folder beside those
pictures, so a run never overwrites an original.

The run gets a window of its own and is a separate program: you can go on
reading, close the viewer, or have it hang, and the conversion carries on.
Its window prints how many files were written, skipped and failed, and
waits for a key before it closes.

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

## Video and music

A video or a music file in the folder plays when it is the page on screen.
While it does:

- `Space` plays and pauses, `←` / `→` jump 5 seconds, `↑` / `↓` change the
  volume, `Shift+M` mutes.
- `[` marks where a repeat starts and `]` where it ends; `\` turns it off.
  `Ctrl+]` / `Ctrl+[` play faster or slower (0.25x to 4x — the sound's pitch
  follows), `Ctrl+\` goes back to normal.
- A strip above the information bar shows the time; click or drag on it to
  go somewhere else.
- Subtitles come from a file beside the film (`.srt`, `.smi`, `.vtt`,
  `.ass`, and a DVD's pictures as `.idx` with its `.sub`) or from inside
  the film itself.
- `C` and `A` switch subtitles and sound tracks; `Z` / `X` move the subtitles
  half a second earlier or later.

## The floating boxes

Two small anchors float over the picture: **M** opens the menu, **T** the
toolbox. The toolbox changes with what is on screen — playing controls for a
video or music, frame steps for an animation, layout and the next or previous
archive for a comic archive. Hold `Alt` and turn the wheel over a box to make
it more or less see-through (never below 30 %).

Each anchor has two halves. Point at the right one and the box opens; move
away and it folds again. Click the left one and the box opens and stays;
drag the left one to move the box. A box never opens past the window's
edge. While a box is open, a pin appears beside its anchor: it says
whether the box stays open when the pointer leaves — click it to switch.

The toolbox is a strip: the seek bar (click to go there) with the file's
name under it, then small icon buttons — previous and next file, back and
forward 5 s, play or pause, stop, volume, and so on. Pointing at a button
puts what it does where the name was.

The menu reads the same way everywhere: a tile ending in `>` opens a submenu,
a switch says whether it is on (`Crisp: off`), the layout and fit in use
have a blue edge, and a grey tile cannot do anything with what is on screen
(`Next archive` in a plain folder, `Track` on a film with one sound track).
The menu always opens at its top level. `Delete` in the menu asks first:
its tile turns into `Delete?`, and a second tap within five seconds moves
the file to the recycle bin. The reading-order tile says which way pages
run (`Order: L>R` or `Order: R>L`). Open folder lists folders and the files
the viewer can open, and says how many others it left out.

`Shift+T` pins the toolbox open; `Ctrl+T` gives it a small window of its own
that stays on top, and `Dock` puts it back. The menu's first tile, the `Pin`
button on the title bar (point at the top edge) and `Ctrl+Shift+T` keep the
viewer itself on top of other windows. `Ctrl` + arrows size the window,
`Alt` + arrows move it.

## Settings

In the file picker, the bottom row picks several files without holding a
key: **Individual** makes every tap turn one file on or off, **Range**
takes two taps and turns over everything between them, **Same type**
takes every file with the same extension as the one in focus, **Change
ext** gives all the picked files one extension you type, **Playlist**
writes them as a playlist file beside themselves and opens them as one
sequence, **Recycle** bins them (press it twice), **Move to** and **Copy
to** send them to one of the numbered folders — press the number after
the button — and **Clear** lets them all go. `Ctrl+Z` undoes any of it,
one file at a time. A mode stays on until you press its button again, and
`..` at the start of the list goes up a folder.

`F2` renames the file, the extension included — correcting `.jgp` to
`.jpg` is a rename like any other, and nothing is asked.

`F1` opens the help in a window of its own. It lists every key that is
bound, grouped by where it works, and it stays open while you use the
viewer, so you can try a key with the list in front of you. The wheel or
`PageUp` / `PageDown` scrolls it, `F1` or `Esc` closes it. The keys it
shows are the ones in force, so a key you change appears changed.

`F10` or `Ctrl+,` opens the settings window, a window of its own. A change
takes effect at once; **Revert** goes back to what was there when it
opened, **Defaults** to the defaults, and the file is written when the
window closes. The **Keys** page lists every key: select one and press
`Enter`, then the key to add; `Delete` takes the last one off. A key another
action already uses is refused, and the message says which. **Export keys
to a file** saves them to share or keep; **Import keys from a file** puts a
saved set on the page, and Revert takes it back off.

Settings live in `settings.ini`. If there is one **beside
`rubraview.exe`**, that is the one used and nothing is written anywhere
else on the machine — this is portable mode, for a USB stick. Otherwise
they live in `%APPDATA%\rubraview\`.

Settings shown greyed are ones the program does not read yet. They are
listed rather than hidden so you can see what is coming.

## What is not there yet

- **Reading a `.cbr` (RAR) archive.**
- **Selecting or pasting in the rename box.** It takes typing — Korean
  and other IMEs included — and Backspace, but no selection, arrow keys or
  clipboard yet. (The keys themselves work with the Korean IME in Hangul
  mode: `F` is still full screen.)

## Keys

Built from the keymap itself, so it is always what the viewer does. A key
listed under a later heading only means that while that is on screen.

<!-- keys:begin — generated from src/core/default_keymap.c by scripts/check-actions.py --write; do not edit -->

**Everywhere**

| Keys | What it does |
|---|---|
| `F`, `F11`, `Alt+Enter` | Full screen |
| `Tab` | Open or close the menu box |
| `T` | Open or close the toolbox |
| `Shift+T` | Pin the toolbox open |
| `Ctrl+T` | Give the toolbox a window of its own, or dock it |
| `Ctrl+Shift+T` | Always on top of other windows |
| `O`, `Ctrl+O` | Open a file |
| `Ctrl+Shift+O` | Open a folder |
| `F4` | Filmstrip |
| `I` | Information bar |
| `E` | Adjust the picture |
| `Ctrl+E` | Export |
| `Ctrl+Shift+S` | Save a copy as |
| `Ctrl+B` | Convert many files |
| `F10`, `Ctrl+,` | Settings |
| `F1` | This help, in a window of its own |
| `G` | Pixel grid past 400% |
| `Esc` | Quit |
| `Delete` | To the recycle bin |
| `Shift+Delete` | Delete for good (asks first) |
| `Ctrl+Z` | Undo a move, copy or rename |
| `F2` | Rename, keeping the extension |
| `Ctrl+Left` | Window narrower |
| `Ctrl+Right` | Window wider |
| `Ctrl+Up` | Window shorter |
| `Ctrl+Down` | Window taller |
| `Alt+Left` | Move the window left |
| `Alt+Right` | Move the window right |
| `Alt+Up` | Move the window up |
| `Alt+Down` | Move the window down |

**Moving between pages**

| Keys | What it does |
|---|---|
| `PageDown`, `Space`, `Enter` | Next page |
| `PageUp`, `Backspace`, `Shift+Space` | Previous page |
| `Home`, `Ctrl+Home` | First page |
| `End`, `Ctrl+End` | Last page |
| `Shift+Right`, `Ctrl+PageDown` | Ten pages on |
| `Shift+Left`, `Ctrl+PageUp` | Ten pages back |
| `Ctrl+Backspace` | Up to the folder |
| `B` | Single page / two pages / book |
| `M` | Left-to-right / right-to-left (manga) |
| `Shift+B` | Detect two-page spreads |
| `Ctrl+]` | Next archive in the folder |
| `Ctrl+[` | Previous archive in the folder |

**The view**

| Keys | What it does |
|---|---|
| `1` | Fit to the window |
| `2` | Fit to the width |
| `3` | Fit to the height |
| `4`, `0`, `Ctrl+0` | Actual size (1:1) |
| `5` | Smart fit (shrink only) |
| `Ctrl+1` | Stretch to fill |
| `L` | Keep the fit for the next files |
| `+` | Zoom in |
| `-` | Zoom out |
| `R` | Rotate clockwise |
| `Shift+R` | Rotate anticlockwise |
| `H` | Flip left-right |
| `V` | Flip upside down |
| `N` | Crisp scaling for pixel art |

**While a slide show runs**

| Keys | What it does |
|---|---|
| `S`, `F5` | Start or stop the slide show |
| `]` | Slower slides (0.5 s) |
| `[` | Faster slides (0.5 s) |
| `Shift+]` | Slower slides (0.1 s) |
| `Shift+[` | Faster slides (0.1 s) |

**While a video, music or animated picture is on screen**

| Keys | What it does |
|---|---|
| `Space` | Play / pause |
| `.` | One frame forward |
| `,` | One frame back |
| `Ctrl+]` | Faster (0.25x a step) |
| `Ctrl+[` | Slower (0.25x a step) |
| `Right` | 5 seconds on |
| `Left` | 5 seconds back |
| `Up` | Volume up 5% |
| `Down` | Volume down 5% |
| `Shift+M` | Mute / sound |
| `[` | Repeat from here (A) |
| `]` | Repeat to here (B) |
| `\` | Repeat off |
| `Ctrl+\` | Normal speed |
| `Z` | Subtitles half a second earlier |
| `X` | Subtitles half a second later |
| `A` | Next sound track |
| `C` | Next subtitles |

**A multi-page TIFF or ICO**

| Keys | What it does |
|---|---|
| `.` | Next page of the file |
| `,` | Previous page of the file |

<!-- keys:end -->
