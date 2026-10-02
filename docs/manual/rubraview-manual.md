# Rubraview — user manual

A viewer for images, comic archives and (later) video, for Windows.

## Where the files are

A build puts everything under `dist/`:

```
dist/rubraview-v0.0.29.exe              the viewer, as built
dist/rubraview-v0.0.29/                 the release bundle
dist/rubraview-v0.0.29.zip              that bundle, zipped — the download
dist/rubraview.exe                      the viewer on its own — the other download
```

The bundle holds `rubraview.exe`, this manual, the changelog and the
licences of the libraries it borrows. The same licences are also inside
the program, at the end of the F1 help window, so the executable can be
downloaded on its own.

## Getting it running

Inside the bundle the program is plain `rubraview.exe`: the zip and its
folder carry the version, so a shortcut to the program survives an update.
`rubraview.exe --version` prints the version, and it is in the window
title too, so a screenshot identifies the build.

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
pages side by side, `M` reads right to left. Two pages are shown at the
same height, so a small scan beside a large one is not drawn small (at
actual size, `4`, each keeps its own pixels).

A picture too large for the graphics card — tens of thousands of pixels a
side, or over 128 megapixels — opens reduced; the status line says
"shown reduced" and still gives its real size. Zoom in, and the part on
screen is read again from the file at full detail: it sharpens a moment
after, tile by tile, and so does whatever you move to. Every key is in **Keys** at the
end of this manual.

Reaching the last page of `Vol 01.cbz` and pressing `PageDown` opens
`Vol 02.cbz`. Close the viewer partway through a book and it offers your
place when you open it again — press `Enter` to take it.

Folders and archives are the same thing to the viewer: `.cbz`, `.zip`,
`.cb7`, `.7z`, `.cbr`, `.rar` and `.alz` all open, and nothing is ever unpacked to
your disk — for now a CBR / RAR only when its pages are stored uncompressed
(see "What is not there yet"). A solid archive (7z or RAR) reads a far page by unpacking the
pages before it first, with its progress on screen. A password-protected
RAR asks for its password (it is shown as dots, and forgotten when the
viewer closes; one that worked is tried first on the next locked book). A
RAR split into volumes (`x.part1.rar`, `x.part2.rar`, ... or `x.rar`,
`x.r00`, ...) opens as one book from whichever volume you open. An ALZ
asks for its password the same way, and a split one (`x.alz`, `x.a00`,
`x.a01`, ...) opens as one book from its `.alz`.

When the names inside an archive come out garbled — made on a Japanese,
Chinese or Taiwanese computer — press `Shift+N` to read them again in the
next code page (Japanese Shift-JIS, Chinese GBK and Big5, Korean, UTF-8,
Western), or pick one from File › Names in archive. The page you are on
stays on screen, and the choice is for that archive only: the next one
opens with Settings › Files › Archive filenames again.

Drop files on the window to open them — several at once become one
sequence. Pictures dragged out of a browser or a mail program work too:
they are copied to a temporary folder of the viewer's, which is removed
when it closes.

## Animated images

While a GIF, WebP or APNG is on screen, `Space` pauses it, `.` and `,`
step a frame, and `Ctrl+]` / `Ctrl+[` change its speed.

A multi-page TIFF or a multi-size `.ico` uses `.` and `,` to step
through its pages, and opens an `.ico` at its largest layer.

## Adjusting a picture

`E` opens the adjust panel. Drag a slider, and the picture follows.
The curve box shows the picture's histogram behind the curve; its
corner button (**RGB >**) picks which curve you edit — click for the
next of RGB, Red, Green, Blue and Luma, right-click for the one before.
Nothing is written until you press **Save a copy**, which writes
`<name>_edit.<ext>` beside the original and leaves the original alone;
a line at the top says whether it was saved.

`Ctrl+E` opens Export: pick a format and a quality, tick **Privacy
clean** to strip GPS and camera details, and press Export.

Privacy clean on a JPEG that is otherwise unchanged does not re-encode
the picture — the metadata is cut out and every pixel is left exactly as
it was.

## Sorting your files

`Delete` sends the file to the recycle bin (`Shift+Delete` deletes for good,
after asking), `F2` renames it (the box edits like any text field: arrows
and `Shift` select, `Ctrl+A`, `Ctrl+C` / `Ctrl+X` / `Ctrl+V`), `Ctrl+Z` undoes a move,
a copy or a rename, and `1`–`9` send it to a folder you chose.

To use the number keys for sorting, give them folders in Settings › Files ›
Number keys send the file to: `Enter` on a folder opens a box with the
path in it, all of it chosen. Type or paste a path, or copy the one that is
there (`Ctrl+C`); `Ctrl+O` browses for one; `Enter` keeps it (only a folder
that exists), `Esc` leaves it as it was, and `Delete` on the row clears it.
The same can be written in `settings.ini`:

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
| (Settings › Display) | Bicubic and Lanczos resizes of large pictures run on the graphics card when there is one, with the same result |
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
- `Shift+\` opens the same two points as numbers, so a repeat can be set
  to the millisecond. Inside that box `[` and `]` put the moment you are
  at into the field you are in — so you can tap a point, correct the
  number, and tap it again, whichever way round suits you. `Tab` swaps
  fields, `Enter` keeps them, `Esc` leaves them as they were.
  `Ctrl+]` / `Ctrl+[` play faster or slower (0.25x to 4x — the sound's pitch
  follows), `Ctrl+\` goes back to normal.
- The toolbox's seek bar shows the time; click or drag on it to go
  somewhere else.
- What happens when a film or a song ends is set by the toolbox's end
  button, `Ctrl+R`, Playback › At the end, or Settings › Audio › When a
  file ends: **Once** stops there; **Next** plays the next film or song in
  the folder or playlist and stops after the last; **1 loop** plays the same
  one again; **Loop** goes round all of them; **Shuffle** plays them in a
  random order, each once before any comes again, and never the same one
  twice in a row. Pictures between them are passed over. `Next` is where it
  starts.
- Subtitles come from a file beside the film (`.srt`, `.smi`, `.vtt`,
  `.ass`, a DVD's pictures as `.idx` with its `.sub`, and a Blu-ray's as
  `.sup`) or from inside the film itself.
- The sound can be shaped (Settings › Audio › Sound, the toolbox's **EQ**,
  **Night** and **Viz** buttons, or Playback › Sound): an equaliser with
  eight presets and a window of ten sliders of its own (**EQ bands**:
  drag a band, turn the wheel over it, right-click for 0 dB), night mode,
  which brings loud passages down so quiet
  ones need not be turned up, and **Volume levelling**, which evens songs
  out by the ReplayGain they carry (by track or by album).
- A song's page shows an analyser above its words — 64 bars with falling
  peaks, or the wave itself (`Viz` switches, or turns it off).
- A `.lrc` beside a song, with the same name, shows its words in time:
  the line being sung, three before and three after; click a line to go
  there. Without one, the words stored in the song itself are shown —
  in time when they are timed, otherwise moving along with the song. A
  `.cue` beside it (of any name, as long as it names the song; or opened
  itself) cuts one long file into its record's tracks:
  the page names the track playing, and previous / next go track by track
  inside the file before they change file.
- `Shift+P` puts the music in a small window of its own, on top of
  whatever you are reading: the cover, the track, a strip to move along
  and buttons to play, pause and change track. Music keeps playing when
  you turn to a picture, and steps aside on its own while a film with
  sound is playing.
- `C` and `A` switch subtitles and sound tracks; `Z` / `X` move the subtitles
  half a second earlier or later.
- Text subtitles sit in a shaded box. Tap it and its edge thickens and four
  small buttons appear at its top-right corner: drag **M** to move the box,
  drag **R** to resize it (the height is the subtitle size), **S** opens the
  subtitle settings and **X** turns subtitles off. Tap anywhere else to let
  it go. The box stays where you left it. A DVD's or a Blu-ray's picture
  subtitles stay where the disc puts them.
- The toolbox's **CC** button turns subtitles off and on. Tap it twice
  quickly, or hold it for half a second, and a list of the film's subtitles
  opens — tap the one you want (or **Off**).

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

The toolbox is a strip: the seek bar (click or drag to go there) with the
file's name under it, then small icon buttons — previous and next file,
back and forward 5 s, play or pause, stop, volume, and so on. Pointing at a
button puts what it does where the name was. For a film or a song the line
under the bar ends with the time, the volume, the speed and the A-B repeat;
for a picture, with its place in the folder or archive (`4 / 6`), and the
bar is a page bar. The seek bar is only in the toolbox; the status line
(`Shift+I`) keeps the picture's size and the zoom.

The menu reads the same way everywhere: a tile ending in `>` opens a submenu,
a switch says whether it is on (`Crisp: off`), the layout and fit in use
have a blue edge, and a grey tile cannot do anything with what is on screen
(`Next archive` in a plain folder, `Track` on a film with one sound track).
The menu always opens at its top level. `Delete` in the menu asks first:
its tile turns into `Delete?`, and a second tap within five seconds moves
the file to the recycle bin. The reading-order tile says which way pages
run (`Order: L>R` or `Order: R>L`). Open folder lists folders and the files
the viewer can open, and says how many others it left out.

Most of what the viewer does has a toolbox button: turning pages one, ten
or all the way, zoom and fit, turning and flipping, the slide show, the
filmstrip, the playlist, the information, editing and exporting, opening,
always on top, the settings and full screen; for a film, 5 s and 30 s back
and on as well. While a box is open, a second square beside its pin, with
three dots, sizes it: drag it sideways for more or fewer buttons (or menu
tiles) a row, and away from the box or back for bigger or smaller ones; the
size is kept, and Settings › Display has the same four numbers.

At the middle of each side of the window, pointing near the edge shows
three buttons. For pictures the left ones go to the previous file, ten back
and the first, the right ones to the next, ten on and the last; for a film
or a song they go 5 s and 30 s back or on, and to the previous or next file
(`Shift` + `Left`/`Right` also go 30 s).

`P`, the playlist button or File › Playlist shows what is open as a list
floating over the picture: the playlist, or the folder's or the archive's
files when there is none. The file on screen is marked and kept in view; a
click goes to a file, the wheel scrolls, the title drags it, `X` or `Esc`
closes it.

`I` (or `Ctrl+I`, or File › Information) opens the file's information: its
size, dates, pixels and format, and for a photo what the camera wrote —
camera and lens, when it was taken (with the time zone), exposure, aperture,
ISO, focal length, program, metering, white balance, colour space, and where
it was: the place in degrees and minutes, the same in decimal degrees for a
map's search box, the altitude, the direction the camera faced and the GPS
time. A picture without any says so. `I` again or `Esc` closes it.

The title bar (point at the top edge) has, after the pin, a `Size` button:
press it and drag to size the window from its bottom-right corner. A double
click on the title bar maximises the window or gives it back its size.

`Shift+T` pins the toolbox open; `Ctrl+T` gives it a small window of its own
that stays on top, and `Dock` puts it back. The menu's first tile, the `Pin`
button on the title bar (point at the top edge) and `Ctrl+Shift+T` keep the
viewer itself on top of other windows. `Ctrl` + arrows size the window,
`Alt` + arrows move it.

## Settings

`F10` opens the settings. Every setting there now does what it says; most
take effect at once, a few the next time something is opened (the archive
filename code page, the memory cap). Some worth knowing:

- **General › On startup** — started with no file, the viewer shows
  nothing (`blank`), reopens what you read last (`last_file`), or opens the
  picker in its folder (`last_folder`, the default). **Frameless window**
  off gives the window Windows' own title bar and borders.
- **Viewer** — the fit and the layout a new session starts with, the gap
  between two pages, how much one zoom step is, whether a wide scan is cut
  in two, and whether a narrow window shows one page at a time.
- **Files** — **Remember the page** off keeps no reading history;
  **Offer to resume** off opens where you stopped without asking.
- **Display** — the size of the menu tiles and anchors (48, 64 or 96), the
  colour that marks the choice in use, and whether a picture's own colour
  profile is used.
- **Cache** — how much memory pages may take and how many are read ahead
  and kept behind; **Always strip metadata on export** starts Export with
  that box ticked.
- **Keys › Use keymap.ini** off uses the built-in keys whatever the file says.

In the file picker each tile shows its picture — softened and a little
dark, so the name drawn over it stays readable — and a folder shows the
first picture in it (or its first film). The tiles fill in as they come
into view; an archive keeps a plain tile.

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

Your favourite folders come first on the bar under the path (`Ctrl+D` or the
star adds or removes the folder on screen). Right-click one to give it a
name of your own (an empty name gives the folder's back); drag one sideways
to put it somewhere else on the bar. Both are kept in `favorites.ini`.

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

- **Compressed or password-protected `.cbr` (RAR) archives.** The program
  uses no UnRAR code; RAR decoding of the project's own is being written.
  Until then such a book says so, and a CBR with uncompressed pages opens.
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
| `Shift+I` | Information bar |
| `E` | Adjust the picture |
| `Ctrl+E` | Export |
| `Ctrl+Shift+S` | Save a copy as |
| `Ctrl+B` | Convert many files |
| `F10`, `Ctrl+,` | Settings |
| `F1` | This help, in a window of its own |
| `I`, `Ctrl+I` | The file's information (size, pixels, codec, EXIF), text you can mark and copy |
| `P` | The playlist, or this folder's files, floating over the picture |
| `Ctrl+R` | At the end of a film or a song: stop, the next file, this one again, all round, shuffle |
| `Shift+P` | Mini player |
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
| `Shift+N` | Read the open archive's file names in the next code page (Shift-JIS, GBK, Big5, ...), for that archive only |

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
| `Shift+Right` | 30 seconds on |
| `Shift+Left` | 30 seconds back |
| `Up` | Volume up 5% |
| `Down` | Volume down 5% |
| `Shift+M` | Mute / sound |
| `[` | Repeat from here (A) |
| `]` | Repeat to here (B) |
| `\` | Repeat off |
| `Shift+\` | Repeat by number |
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
