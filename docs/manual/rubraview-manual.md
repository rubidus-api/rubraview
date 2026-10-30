# Rubraview — user manual

A viewer for images, comic archives and (later) video, for Windows.

## Where the files are

A build puts everything under `dist/`:

```
dist/rubraview-v1.39.46.exe              the viewer, as built
dist/rubraview-v1.39.46/                 the release bundle
dist/rubraview-v1.39.46.zip              that bundle, zipped — the download
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

There is an installer too: `rubraview-v<version>.msi` puts the same folder
under Program Files, the program in the Start menu, and registers the file
types and Explorer's right-click menus for every user — Windows 11's own
menu among them, which the zip needs an administrator and a visit to
Settings for. It asks for an administrator once. A newer installer
replaces an older one; Installed apps removes it and its registration.
Settings and history stay in your profile either way. After an upgrade
every extension is registered again: if you had switched some off for
every user, do it again in Settings › Explorer.

`rubraview.exe` needs nothing installed beside it. Double-click it, or
give it a file or a folder:

```
rubraview.exe "D:\Comics\Vol 01.cbz"
rubraview.exe "D:\Photos"
```

The first file a window is given brings its folder: the other pictures
and films beside it are the list. A file opened from Explorer after that
goes to the Rubraview window in front: it is put at the end of that
window's list and shown. A book, a folder or a playlist is opened in
place of what was open.

**Settings › General › Opening a file from Explorer** changes each part:

| Setting | As it comes | What it does |
|---|---|---|
| Reuse the open window | on | off: every file opened is a window of its own |
| A file given to the open window | `add` | `folder`: opened in place of the list, with its folder; `only`: in place of it, alone |
| A file added to the list goes | `end` | `sorted`: the whole list is sorted again as Settings › Files says |
| Show the file that was added | on | off: it joins the list and the page being read stays |
| The first file brings its folder | on | off: the first file is alone, and later ones join it |

A playlist file (`.m3u8`, `.m3u`, or `.rvlist`: one path a line) opens
as the files it names, in its order; a path that is not whole is taken
from the playlist's folder, and a file that is not there is left out.

**Internet radio.** A playlist can also name radio stations by their
address, each with its own title, and then it is a list of stations to
choose from (`P` shows it). Write it as an Extended M3U, the format most
players read:

    #EXTM3U
    #EXTINF:-1,My station
    http://radio.example.com:8000/live
    #EXTINF:-1,An old Windows Media station
    mms://wm.example.com/live

`-1` means a live stream with no length. `.pls` (`File1=`, `Title1=`)
and Windows Media's `.asx` lists, as station sites hand them out, open
too. Stations are played by FFmpeg (Settings › External); `http`,
`https` and `mms` addresses are taken (MMS is tried over HTTP, then
TCP). A station cannot be wound, and one that does not answer is given
up after 8 seconds. **Save** in the list window writes the stations and
their titles as an `.m3u8`. Playing a station connects to it over the
network; Rubraview lets FFmpeg reach only network addresses for it,
never a file on your PC.

While a station plays, the title bar shows what it says is on (the song,
when the station sends it). A station whose address is itself a list —
`…/listen.pls`, `.m3u`, `.asx`, or an address that answers with a list —
is read and its stream played; a list that names yet another list is not
followed. In an `.asx`, the other addresses an entry gives are tried in
turn when the first does not answer (they are not written again by
**Save**). To add a station by hand, press **URL** in the list window and
type its address, then a space and a title if you like:

    http://radio.example.com:8000/live My station

On the command line, `--sort=natural|lexical|modified|created|size|random`
with `--descending` or `--ascending` orders this run's folders whatever
the settings say, and `--recursive` reads their subfolders too.

For a window of its own, use the right-click menu's **Open in a new
window**, or `--new-instance` / `--new-window` on the command line.
`--page=N` opens a book (an archive) on its page N; the restart
offered in Settings › Cache uses it to come back where you were.
Several windows share the settings, the reading history, the recent
files, the favourites and the boxes' places safely: a file is replaced
whole, one viewer at a time, and a viewer that saves keeps what another
changed meanwhile. The settings are one set for all of them: a setting
changed in one window — in the settings window, by a key, by the wheel on
the volume — is written a moment later and takes effect in every other
open window at once. Keys changed on the Keys page reach the others as
the settings window closes.

**Recent files...** (`Ctrl+Shift+R`, or the right click) opens the files, books,
folders and playlists you opened lately, in any window, newest first, in a
floating list like the playlist (`P`): each with when it was opened, its
size and its folder. A click opens one; its box and **Remove**, or its
`x`, take files off the list (not off the disk); **Clear all** empties it.

### In Explorer: double click and the right-click menu

**Settings (`F10`) › Explorer** decides what Windows is given. Two
switches say what — **Open by double click (file types)** and
**Right-click menu** — and under them every extension has two boxes of
its own, **Open with** (double click) and **Right-click menu**, by kind:
pictures, comics, video, music, and the plain archives (ZIP, 7z, RAR, ALZ,
EGG), which get the menu only and are never made Rubraview's to open by
double click. `←` and `→` go from one box to the other, `Space` ticks the
one marked `>`, and the mark stays in its column going up and down.

The Microsoft Store's copy says so at the top of the page (**This copy**):
Windows has its file types from the package, so the Register and Remove
rows do nothing there; choose Rubraview in **Open Windows' Default apps**.

**Register for this user** makes Windows match the switches: what is on is
written, and what Rubraview wrote earlier on an extension now switched off
is taken back. **Register for every user** does the same for the whole
computer — Windows asks for an administrator first. The two **Remove**
rows take everything back: only what Rubraview wrote goes, and a type that
was another program's before is that program's again. Windows keeps the
last word on a default it has been told by hand; **Open Windows' Default
apps** is where to change that.
Registered, Rubraview is also listed there by name, with the file types it
registered, so it can be made the default for each of them in one place;
removing the last of them takes it off the list.

Each kind of file has its own icon in Explorer, white on Rubraview's red:
a picture, a strip of film, notes for music, a written page for text and
an open book for comics. Windows shows a type's icon from the program it
opens with, so a type whose default is still another program (Media
Player for MP4 or MP3, say) keeps that program's icon until Rubraview is
made its default in **Open Windows' Default apps**. The Microsoft Store's
copy shows the same icons.

The menu is a **Rubraview** item with these under it:

| Item | On | With several files selected |
|---|---|---|
| Open | pictures, video, music, comics, archives | as a double click: added to the open window's list, the first of them shown |
| Open in a new window | pictures, video, music, comics, archives | a new window with them (a window each where the menu is the registry's alone: registered for this user, or Windows 10) |
| Open these only | pictures, video, music | the selected files are the list, nothing else from the folder |
| Add to the list | pictures, video, music | all are added to the end of what the open window is showing; it stays on its page |
| Browse the archive | comics, archives | offered for one file: the archive's entries in the picker |
| Convert... | pictures | the Batch window, to run on the selected files |
| Print... | pictures | the system's print dialog; each picture on a sheet of its own |

On Windows 11 the menu registered for this user is under **Show more
options**. Windows 11's own menu takes items only from a registered
package, which needs an administrator once: **Register for every user**
does it, when `rubraview_menu.dll` and `rubraview_menu.msix` are beside
`rubraview.exe` (they are in the zip; the single-file download has the
classic menu only). Windows 11 lists a package's items under **Show more
options** too, so with the package registered Rubraview's entry there is
the package's and the registry's is not written. After moving the
program's folder, register again.

"Add to the list" keeps the list's order setting (Settings › Files › Sort
by), so an added file takes its place among the others. Added to a folder
being read, the folder's files and the new ones become one list; a book
has no files to add to, so the files are opened as a list of their own. A
film that was playing goes on from where it was.

**Print** (`Ctrl+P`, or Print... in the right click) prints the picture on screen the
same way. A panel asks how first, and beside it a preview shows the sheet
as it will come out: the paper in its proportions, the part the printer
reaches (a grey line), the cells when several pictures go on a sheet, and
each picture where and how it is printed. It changes as the choices do.
**Print** sends it to the printer chosen, with no other dialog; **With
Windows' print dialog...** opens Windows' own instead.

| In the panel | What it sets |
|---|---|
| Printer | Any printer of this computer (Windows' default first chosen) |
| Paper | The printer's own papers, with their sizes |
| The sheet | As the printer has it, upright (portrait) or on its side (landscape) |
| Copies | 1 to 99 |
| Preview of | With several sheets, which one the preview shows |
| Turn | Auto (a quarter when the picture then fills more of the sheet), None, 90° right, 180°, 90° left |
| Size | Fit the sheet (all of the picture, as large as fits), Fill the sheet (the sheet covered, the overhang cut off), Stretch (both sides to the sheet's, proportions given up), Actual size (96 pixels an inch) |
| Scale % | 10 to 400, of the size chosen: 50 is half the fitted picture, 200 twice — what then passes the sheet is cut off |
| Place | Centre, a side or a corner: where the picture sits, and which part is kept when it is cut |
| Margin mm | 0 to 50, kept clear inside the printer's printable part |
| A sheet | 1, 2, 4, 6 or 9 pictures a sheet, each in a cell of its own |

A click on Turn, Size, Place or A sheet steps to the next choice; Scale and
Margin are dragged. The choices are kept (Settings › Files › Printing) and
offered again the next time. A click outside the panel, or `Ctrl+P` again,
closes it without printing. A page inside an archive is printed the same
way.

**Remove everything, older versions' too** on the same page is for a menu
that shows Rubraview more than once: it takes out every Rubraview entry
Explorer has — those of an older version, or of a copy in another folder,
too — along with the file types, and asks for an administrator to do the
same for every user. Other programs' entries stay. Press Register again
afterwards for what you want back. From a command line it is
`--wipe-shell` (`--wipe-shell --all-users` from an administrator's
prompt).

From a command line: `rubraview.exe --register-shell` and
`--unregister-shell`; `--types=jpg,png,comics,...` (extensions, the kind
words `pictures`, `comics`, `video`, `music`, `archives`, or `all`) for
some only, `--no-types` or `--no-menu` to leave one of the two as it is
(`--remove-types`, `--remove-menu` to take one back), and
`--all-users` (from an administrator's prompt) for every user (`--setup`
is what the installer adds: the run then ends with 0 when all but Windows
11's menu was registered). The menu's
items are `--open`, `--new-window`, `--open-only`, `--add`, `--browse`, `--convert` and
`--print`, each followed by the files.

## Reading

`PageDown` / `PageUp`, `Space` / `Backspace` turn the pages. `B`, the Toolbar's Layout button (its icon is the layout in
use) and the right click on a book choose how pages are laid out, and
Settings › Viewer › Layout shows and keeps the same choice:

- **Single** — one page at a time, a wide scan whole.
- **Dual** — two pages side by side; a wide scan stands alone.
- **Book** — the cover alone, then two pages side by side.
- **Webtoon** — every page one under the other at the same width, read as
  one long strip: the wheel, a drag, or `PageDown` / `PageUp` (a window's
  worth) move through it, and zooming widens or narrows the strip.
- **Comic** — one page at a time, and a wide scan as its two halves, one
  after the other in reading order.

Every layout but Single is for looking at pictures: a film, a song and a
picture that moves (an animated GIF, WebP or PNG) are left out there, and
the picture after them is shown in their place. One you pick by name — in
the file list, the playlist, on the page bar, or by opening it — is still
shown, alone, until you turn the page. In Single everything is shown in
its turn. A folder with nothing but films plays them in any layout.

Dual, Book and Comic each come a second time, read right to left
(`dual-rtl`, `book-rtl`, `comic-rtl` — a book read from the back, as a
Japanese comic is): the earlier page stands on the right, Comic shows a
wide scan's right half first, and a swipe turns the other way. A click
does not: in every layout a click on the right part of the window goes to
the next file and on the left part to the one before; the middle does
nothing.
`B` goes through all eight in that order: each of the three is followed
by its right-to-left form. There is no separate reading-order switch.

Two pages are shown at the same height, so a small scan beside a large one
is not drawn small (at actual size, `4`, each keeps its own pixels).

A picture smaller than the window is enlarged, and a moment after the view
settles the part on screen is drawn again more sharply — lines and letters
crisper, with no halo beside them. Settings › Viewer › **Enlarging a small
picture** set to `smooth` leaves it as first drawn. Zoomed past the
window, hold the left button and drag to move the picture. The wheel
zooms a picture (a film's and a song's wheel is the volume); a double
click puts the picture's middle back on the window's. Holding the button
still for a moment opens the right-click list, as a right click does.

Rotating (`R`, `Shift+R`) counts the pages as turned: laid on their side
they are shown one at a time, whole; standing tall, two at a time. Turned
upside down, each page is turned where it stands.

A picture too large for the graphics card — tens of thousands of pixels a
side, or over 128 megapixels — opens reduced; the information bar
(`Shift+I`) says "shown reduced" and still gives its real size. Zoom in, and the part on
screen is read again from the file at full detail: it sharpens a moment
after, tile by tile, and so does whatever you move to. Every key is in **Keys** at the
end of this manual.

What `PageDown` (or `.`) does past the last page, and `PageUp` (or `,`)
before the first, is **Settings › Files › At the first and last**: `say`
(as it comes) tells you it is the last or the first and stays; `next` opens
the next archive beside it — `Vol 01.cbz` runs into `Vol 02.cbz` — or the
next folder beside this one that has something to show, and going back
opens the previous one at its last page; `round` goes round to the other end
of what is open. In a multi-page file (a TIFF, an icon) `.` and `,` turn its
pages and then go on to the next or previous file. Close the viewer partway through a book and it offers your
place when you open it again — press `Enter` to take it.

Folders and archives are the same thing to the viewer: `.cbz`, `.zip`,
`.cb7`, `.7z`, `.cbr`, `.rar`, `.alz` and `.egg` all open, and nothing is
ever unpacked to your disk. A solid archive (7z or RAR) reads a far page by unpacking the
pages before it first, with its progress on screen. A password-protected
book — RAR, ZIP or 7z — asks for its password (it is shown as dots, and
forgotten when the viewer closes; one that worked is tried first on the
next locked book). A RAR split into volumes (`x.part1.rar`,
`x.part2.rar`, ... or `x.rar`, `x.r00`, ...) opens as one book from
whichever volume you open. A page whose data is damaged (its checksum
does not match) is not shown.

When the names inside an archive come out garbled — made on a Japanese,
Chinese or Taiwanese computer — press `Shift+N` to read them again in the
next code page (Japanese Shift-JIS, Chinese GBK and Big5, Korean, UTF-8,
Western). The page you are on
stays on screen, and the choice is for that archive only: the next one
opens with Settings › Files › Archive filenames again.

A page turn fades the old page into the new one for a quarter of a
second. While a slide show runs, the pointer goes away after a second and
a half of stillness and comes back when it moves. It runs in the window as
it is — full screen only if the window already was (the slide show's
settings have a button for it). The Toolbar then starts
with a stop button and the delay between slides ("3 s") where a film's
speed button would be: turn the wheel over it, or hold it and drag up or
down, and the delay moves half a second; a tap lists 1, 2, 3, 5 and
10 seconds to choose from. `Ctrl+]` shortens it (faster slides) and `Ctrl+[` lengthens it, half a
second at a time, a tenth with `Shift` as well — the same keys as a film's speed and a text's
auto-scroll. A right click on the picture while it runs opens **the slide
show's settings**: the delay — type it in the field (`Enter` keeps it) or
press **-** and **+** for half a second less or more — how one slide
changes to the next (**Cut**, **Fade**, **Slide**, **Zoom**, the same as
Settings › Viewer › Turning a page), whether it goes round again after the
last or stops there, and **Pause**/**Play**, **Stop**, **Full screen** and
**Close**. A slide's time counts from when it is drawn, so a large picture
still being read is not passed over; turning a page yourself, it goes on
from there.

A right click on the picture otherwise opens **a short list** where you
clicked, for what is on screen. For a picture: Copy picture, Adjust
picture; Fit to window, Real size, Rotate, Rotate back; Slide show,
Info; Rename, Copy to folder, Move to folder, Copy to another folder...,
Move to another folder..., Show in Explorer,
Delete (click it twice: it goes to the recycle bin); Open file, Recent
files..., Favourites..., Add to favourites, Batch, Print...; Help and
Settings. What the Toolbar on screen has a button for is left out of it
(Open file and Settings stay). Every list has Open file, Recent files...,
the favourites and Batch, a picture's, a book's and an animation's
Print... too. A book or an archive has the next and previous archive and
the layouts; a film has play and stop, the subtitles (taking them out of
the film too), the sound track, slower, faster and normal speed, copying
and saving the frame and full screen; a song has
the previous and next file, what the end does, the equaliser and the
mini player. A row that cannot act now is grey. The arrow keys and
`Enter` choose, `Esc` or a click elsewhere closes it. The mouse's back and
forward buttons turn the page, a middle click goes to real size and back,
the wheel turns the page on a picture that fits the window (away from
you the one before, towards you the next) and brings up the filmstrip
along the bottom for a moment — it slides to keep the page in the
middle, stays while the pointer is on it, and a click on a thumbnail
goes to that page (`F4` keeps it up) — `Shift` and the wheel go ten
pages; on a touch screen two fingers move
the picture and a pinch zooms it. (README has the whole table, under
Mouse and touch.)

Drop files on the window to open them — several at once become one
sequence. Pictures dragged out of a browser or a mail program work too:
they are copied to a temporary folder of the viewer's, which is removed
when it closes.

## Animated images

An animated GIF, WebP or PNG (APNG, named `.png` or `.apng`) is played as a film is. `Space` pauses it, `.`
and `,` step a frame, `Left` and `Right` seek five seconds, `Shift+[` and
`Shift+]` set the two ends of a repeat and `\` clears it, and `Ctrl+]` /
`Ctrl+[` change its speed from 0.25x to 4x. The Toolbar has the same on buttons,
with Stop and a seek bar to click; paused, the title says where it is.

At its end an animation goes round again. Settings › Viewer › When an
animation ends — or `Ctrl+R` while one is on screen — makes it stop
there, or go on to the next file. An animation may be as long as its
file is; of a very long one only some pictures are kept in memory, and
going back in it stays quick.

In the Toolbar, the back and forward buttons seek five seconds. Hold one
down and five steps open over it — 5 s, 10 s, 30 s, 1 min, 5 min; drag
onto one and let go. This is the same for a film and for music.

`Ctrl+Enter` opens an Explorer window with the file on screen picked
out — the archive itself, for a page inside one.

A multi-page TIFF or a multi-size `.ico` uses `.` and `,` to step
through its pages, and opens an `.ico` at its largest layer.

## Adjusting a picture

`E` opens Edit, a window of its own beside the viewer, as the settings
window is: everything that changes the picture is there, while the
picture, the selection drawn on it and the preview stay in the viewer.
Its tabs say what you are doing: **Adjust** (where, light, colour, levels
and the curve, blur and sharpness), **Effects** (effects, plug-ins, G'MIC,
FFmpeg filters), **Size** (the area, cropping, resizing) and **Edits**
(the list of edits, and doing them to many pictures). At the right of the
tabs **Undo**, **Redo**, **Save a copy** and **Save** (over the original;
it asks for a second press) are always there — on a narrow window as
icons. In a tab each part has its name over it; the parts follow the
width: one column that scrolls on a narrow window, two on a middle one,
three on a wide one (each column scrolls on its own).
Its size and place are kept while the program runs; its `X` or `Esc`
closes Edit, and the viewer's keys work from it. Drag a slider and the
picture follows; press a slider's name to type its value. The curve box
shows the histogram behind the curve; its corner button (**RGB >**) picks
which curve you edit.

**An area first.** At the top left of the Adjust and Effects tabs, and in Size, **Choose an area
on the picture**: the pointer becomes a cross over the picture, and a
drag there marks a part of it — a rectangle, or an ellipse (**Shape** in
the selection's box), with a soft edge if you give it a **Feather**. Drag
inside it to move it, drag one of its eight handles to size it, or type
X, Y, W and H in the selection's box. Press the button again, or `Esc`, to
stop choosing: a drag then moves the picture and the wheel zooms, as
without Edit. With an area, what you do next is done to it alone; without
one, to the whole picture. **Copy the area to the clipboard** puts that
part of the picture, with the edits in force, on the clipboard (an
ellipse's corners clear); with no area, the whole picture.

**The edits.** Under it, every edit is a row: what it moved ("Saturation
-100: whole") and where. Each has its own area, so several faces can be
blurred each by an edit of its own. A click on a row shows the picture as
it stood after that edit; `Ctrl+Z` and `Ctrl+Y` step back and on. Drag a
row by its name to another place (a line shows where), or use its arrows;
its `x` takes it out, and **Remove the ticked edits** every ticked one. An
edit after a crop or a resize keeps to the same part of the picture when
the order changes. **Copies of every picture in the list...** does the
edits to every picture of the folder or playlist and writes each as a
copy (the name typed with `*`, below); **Apply to every original in the
list...** writes them over the pictures themselves — it asks first and
needs a second press, and each old file goes to the Recycle Bin. Copies
named as the copies' pattern (`*_edit`) are left out of both, and a
picture inside an archive is never written over. After it the list starts
empty, as the pictures now hold the edits.

**Pixels or ratios.** On another picture each edit keeps its sizes as they
were given. An area drawn or moved on the picture, a resize by **Resize
%**, and any number switched to a ratio are that part, or that share, of
every picture: a quarter in from the left is a quarter in on a small
picture and a large one. A number typed in pixels, or a size from **or a
size**, stays those pixels — a 1920-wide resize is 1920 wide on every
picture — held inside a smaller picture. A soft edge stays in pixels.

**Save the steps (for Batch)...** writes the edits to a `.rvsteps` file and
**Load steps...** puts a file's edits on the picture on screen. Batch runs
them on many pictures (below). A file says when loaded how many of its
edits run G'MIC, a plug-in or FFmpeg: those run with your rights, so load
files you trust.

**Then a change.** The tones, **Colour balance** (red, green, blue),
**Automatic colour** (each channel stretched, a colour cast drawn in),
an **Effect** with its amount, chosen by kind — **Colour** (grey, sepia,
negative, black and white, black and white dithered, fewer colours (2 to
256, dithered or not), posterize, solarize), **Artistic** (mosaic, oil
paint, pencil sketch, emboss, edges, 3D button), **Blur and noise**,
**Distort** (swirl, wave, fragment, explosion) and **Fix and light** (red
eye, colour fringe, vignette) — **Crop to the
selection**, **Resize**. A change is done at once, to the selection or,
without one, to the whole picture, and is a step: moving more sliders
changes that step, until **Done** (`Enter`), another kind of change, or
another selection starts the next. `Ctrl+Z` undoes the last step,
`Ctrl+Y` does it again, and the undo list (its own movable box, closed by
its `X`) goes back to any of them. **Reset** goes back to the picture as
it was. **Show the original** compares.

**What works with Edit open.** Turning the pages (over pictures only: a
film, a sound or a text is passed by), the file list and the playlist,
Export, the view's size and fit, and the information; the Toolbar shows
only these, and the other keys say they wait until Edit is closed. Export
from Edit writes the picture with its edits, and Edit comes back after it
with them. A film is edited by its frame: pause it, and `E` saves the
frame beside the film as a picture (as Save frame does) and opens Edit on
it; turning on from there does not come back to the film.

**On the next picture.** Turn the page with Edit open and its steps are
shown done to the new picture too — a selection or a crop at the same
place, in proportion to its size. Only shown: no file is changed until you
save. **Edit to all**, on the floating list's bar, does the steps to every
picture of the folder or playlist and writes each as a copy beside it: you
type the copy's name with `*` for the picture's own (`*_edit` as it comes,
or `edited_*`); each copy is in its picture's format, or PNG when that
cannot be written.

**Resize.** **Resize %**, or **or a size**: half, double, fit the screen,
640x480 to 8K (turned to the picture's way), the long side, or megapixels.
**Size: ... px (... cm)...** types it: a number is pixels, and a unit
makes it a length — `21cm`, `210mm`, `8.5in`, `50%` — at **DPI for cm and
in**. Under each number its button (or `/`) switches it between pixels
and a **ratio** of the picture: `0.5` is half, `1.5` half as big again, as
fine as you type it (`0.1234`). The crop's box has the same for X, Y, W
and H (`0.25` is a quarter of the way in). **Keep the shape**, the **Filter**, **In linear light** (truer when
shrinking: fine light detail does not darken) and **Sharpen after**. A
resize is always of the whole picture.

**Saving.** **Save a copy** writes `<name>_edit.<ext>` beside the
original. **Save over the original** asks for a second press, then puts
the old file in the Recycle Bin. A page inside an archive is written back
into the archive.

`Ctrl+E` opens Export: pick a format, and the panel shows what that format
has: JPEG its quality and progressive; PNG its level and colours; WebP its
quality (not when lossless), lossless and effort; GIF, BMP and ICO
nothing more ("as the file" shows the file's own format's). **AVIF
(FFmpeg)** and **JPEG XL (FFmpeg)** are written by FFmpeg's encoders,
when the FFmpeg the viewer uses has them (BtbN's 9.0 build has libjxl,
libaom, SVT-AV1 and rav1e; Settings › External › FFmpeg names those it found):
their quality (100 being lossless JPEG XL) and, for AVIF, **AVIF colour**
(4:2:0, or 4:4:4 for the sharpest colour) and **AVIF depth** (8 or 10
bits). Transparency is kept in both (AVIF's needs libaom; another AV1
encoder lays it over white), so are the file's details (EXIF and XMP), and
an animation stays moving in both (JPEG XL's needs libjxl_anim; without
it the first picture is written). Tick **Privacy
clean** (JPEG, PNG, WebP, TIFF) to strip GPS and camera details, and press
Export. **PNG
colours** writes a PNG with or without transparency, in 256 colours or in
grey; **Progressive JPEG** writes one that a browser shows whole and
coarse first.

More, by format (1.16.0):

- JPEG: **Colour subsampling** (4:2:0 is smallest, 4:4:4 keeps colour
  edges sharpest), **Grey JPEG**, **Optimised tables** (a smaller file,
  the same picture) and **Restart markers** (one every N rows of blocks,
  so a damaged file loses less).
- PNG: **PNG filter** and **Interlaced (Adam7)**. With **PNG colours** at
  *a palette*, **Palette colours** (2 to 256, made from the picture; 16 or
  fewer write a smaller 4-, 2- or 1-bit PNG) and **Dither**.
- GIF: **Palette colours** and **Dither**.
- BMP: **BMP bits** (32 with transparency, 24, or 8 with a palette).
- TIFF: **TIFF compression** (none, LZW, ZIP).
- WebP: lossy, **Alpha quality** and **Sharp colour edges**; lossless,
  **Near-lossless** (100 is off; lower lets nearby values change for a
  smaller file); **WebP preset** (picture, photo, drawing, icon, text).
- JPEG and lossy WebP: **File size at most** (KB): the best quality, up to
  the one set, whose file fits.
- TIFF: **CMYK (for print)**.

**Camera details, comments** (also Settings › Cache): when a picture is
written again — exported to another format, edited, or by the batch — what
it said (camera and GPS details, XMP, IPTC captions and keywords,
comments) is **kept in the file** where its format has a place: JPEG, PNG
and WebP take the EXIF and XMP as they were (the orientation set to
upright), JPEG its IPTC and comments, PNG and GIF the rest written in
words in their comment. **In the file, or a .txt beside it** also writes
what the new file cannot hold (a BMP, TIFF or ICO; a WebP's comments and
IPTC) as a text file of the same name beside it (`photo.txt`, or
`photo-1.txt` when that is taken). **Dropped** writes the pixels only.
The text file starts with `# Rubraview <version> details` and is a small
TOML file (`file = "photo.bmp"` names the picture it belongs to). When a
picture has such a file beside it, Rubraview reads it back: the
information window (`I`) shows it under **Kept beside it in photo.txt**,
as if it were in the picture, and saving that picture again carries it
on (into the new file where it has a place). A text file that is not
Rubraview's, or names another picture, is left alone.
Privacy clean drops them all.

A JPEG exported as a JPEG with nothing changed but **Progressive JPEG**,
**Optimised tables** or **Restart markers** is rewritten without being
encoded again: the picture is exactly the same.

AVIF pictures (`.avif`) are read by Rubraview itself, on any Windows —
stills and animations, with their transparency, 10-bit ones shown as 8.
JPEG XL (`.jxl`) is read by Windows, when a JPEG XL codec is installed
in it, or else by FFmpeg, when its DLLs are found (Settings > External,
"FFmpeg") and were built with libjxl; without
either a `.jxl` says what is missing. The DLLs must be FFmpeg 9.0's
(`avcodec-63.dll` and its fellows): an older set is not loaded, and a
picture or a film that needed it says that the DLLs beside the program
are another version. All five are needed: with one missing, the notice
says the FFmpeg 9.0 DLLs could not be loaded. One that moves plays through
FFmpeg, and is its first picture where only Windows' codec reads it; a
very large one plays as many of its pictures as fit in memory and says
how many.
Neither kind is written. Both have their thumbnails — in a book too —
whether or not Windows has a codec for them.

A BigTIFF (the TIFF kind made for very large files) opens like any
TIFF, every page of it, as long as the file is under 4 GB; its camera
and GPS details are not shown.

A film whose picture neither Windows nor FFmpeg can decode says that a
codec is missing instead of playing its sound over a black window.

WebP is written by Rubraview itself, on any Windows: **Quality** decides
how good a lossy one is, **WebP lossless** keeps every visible pixel, and **WebP
effort** (0 to 6) trades time for a smaller file. An animated GIF, PNG or
WebP exported as WebP stays an animation, every frame at its time — as
long as it was not edited; an edited one is written as the one picture on
screen. The same holds for `--batch --format=webp` without a resize, a
turn or an adjustment.

While a film or an animation is on screen, `Ctrl+C` copies the frame you
see to the clipboard and `Ctrl+Shift+E` saves it as a PNG beside the file
(`film_00-23-792.png`: the minute, second and thousandth). On a still
picture `Ctrl+C` (or Copy picture in the right-click list) copies the picture itself, whole and as its file holds it — not
turned or adjusted as it is shown.

Privacy clean on a JPEG that is otherwise unchanged does not re-encode
the picture — the metadata is cut out and every pixel is left exactly as
it was.

### Plug-ins: filters from outside

Settings › External › **Plug-ins** (off until you turn it on) lets filters
written by others into Edit and the batch. A plug-in is a 64-bit DLL named
`*.rvf.dll` in the **Plug-in folder** (empty: `%LOCALAPPDATA%\Rubraview\plugins`,
made when plug-ins are turned on). The settings page lists what it found —
name, version, licence and file — and why one is not used (a 32-bit DLL,
another interface version). In Edit, under the effects, **Plug-in** chooses
one, its own parameters follow, and **Run the plug-in** makes it a step on
the whole picture or the selection; `Esc` cancels a long run.

Each plug-in runs in a process of its own, never in the viewer: one that
crashes is said to have crashed, one that does not stop when cancelled is
ended five seconds later, and the viewer goes on either way. A step
remembers the plug-in's file and fingerprint (SHA-256): a plug-in replaced
by another file is not run for an older step. A plug-in is somebody's
product — check its licence before you use it, in the batch especially.

Writing one: `docs/manual/plugins/` holds the interface
(`include/rubraview/plugin_filter.h`, two functions over BGRA rows and up to
eight parameters) and a complete sample, `posterise.c`.

### G'MIC: a thousand filters

[G'MIC](https://gmic.eu) is a free image processor with about a thousand
filters. Rubraview does not ship it; it runs G'MIC's own program, so you
choose which version you use and its licence is between you and G'MIC.

1. Get the **command-line** zip for Windows from gmic.eu/download.html:
   under *G'MIC for Windows - Other interfaces*, the row *Command-line
   interface (CLI)*, its *.zip archive* (`gmic_<version>_cli_win64.zip`;
   Settings › External › G'MIC has the link). The GIMP, Photoshop,
   Paint.NET and Qt stand-alone downloads hold no `gmic.exe`. Unpack it
   anywhere.
2. In Settings › External › G'MIC, name `gmic.exe` or its folder;
   **Open the folder** shows it in Explorer.
3. Press **Make the filter list**, once. G'MIC fetches its filter
   definitions from gmic.eu (step 1 of 2) and lists them (step 2 of 2); it
   takes a few minutes. The G'MIC line counts the time, and from the second
   time on says about how far it is in percent, by how long the last run
   took. Press it again after updating G'MIC.
4. The list made is shown under it, a row a category with how many filters
   it has; **Open the whole list** opens every filter, by category and with
   its command, in a window of its own, where `Ctrl+F` finds one.

In Edit, **G'MIC** chooses a category, **filter** one of its filters, and
the filter's own settings follow as rows (a text, a colour or a point is
typed in the box). **Run G'MIC** makes it a step — on the whole picture, or
on the selection — that the preview shows and Save writes; `Esc` stops a
long run. Some filters change the picture's size. **Type a G'MIC command...**
runs any G'MIC command line (`blur 3`, `fx_watercolor 1,1,0`); G'MIC's own
error is shown when it refuses one.

In the batch: `--gmic="<command>" --gmic-exe=<gmic.exe or its folder>`.

G'MIC runs as a program of its own with your rights, given only the
picture as a file in the temporary folder, and is ended with everything it
started if it does not finish in ten minutes. A command you type can do
whatever G'MIC can do, so type only commands you understand.

### FFmpeg's filters

FFmpeg's own video filters (about 160 in the LGPL build) are in Edit too,
once `avfilter-12.dll` is in the FFmpeg folder beside the other five (it is
in the same zip). Settings › External › FFmpeg › **Filters** says how many
were found, or why there are none.

In Edit, **FFmpeg filters** chooses a group by letter (`a – c`), **filter**
one of its filters (with what it does), and its options follow as rows; a
size, a colour or an expression is typed in the box, and an option left
alone keeps FFmpeg's default. **Run the FFmpeg filter** makes it a step, on
the whole picture or on the selection; some filters change the size.
**Type an FFmpeg filter graph...** runs any graph — `unsharp=5:5:1.0`,
`hflip,scale=iw/2:ih/2`, `colorlevels=rimin=0.1` — and FFmpeg's own words
are shown when it refuses one. (`eq` and some others are in FFmpeg's GPL
build only.)

In the batch: `--ffmpeg-filter="<graph>"`.

The filters run inside Rubraview, as FFmpeg's decoders do, so a slow one
cannot be stopped with `Esc`. Filters that would open files or addresses
(`movie`, `amovie`), listen on the network (`zmq`, `azmq`) or load other
code (`frei0r`, `ladspa`, `lv2`) are refused wherever they appear in a graph.

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

Two more keys do the same with a folder each, whatever the number keys are
set to: `F6` **copies** the file on screen to one folder and `F7` **moves**
it to another (Settings › Files › The file on screen, to a folder; or
`copy_dir` and `move_dir` under `[curation]`). They are in the Toolbar too
(Copy to, Move to) and in the right click. A page inside an archive can be
copied out with `F6` — it is written as a file of its own — but not moved
(to take it out of the archive as well, delete it there afterwards). Nothing in the folder is ever replaced: when
the name is taken the file goes in as `name-1.jpg`, then `name-2.jpg`, and
so on. `Ctrl+Z` takes a copy away again or brings a moved file back.

To send it somewhere else, `Shift+F6` (copy) and `Shift+F7` (move) — also
Copy to another folder... / Move to another folder... in the right click, and `F6` / `F7` themselves while no folder is set for them —
open a box in the middle of the window holding the last folder you used.
Type or paste a folder, or press `Ctrl+O` to browse to one, and press
`Enter`. A folder that is not there yet is not made by accident: the
first `Enter` says it is new, a second `Enter` makes it and puts the file
in it; `Esc` leaves everything as it was.

**Inside an archive** the same keys work on the page on screen, and the
archive is written again with the change in it:

- `Delete` (or `Shift+Delete`) takes the page out. It asks first — `Y`
  deletes, anything else keeps — because there is no Recycle Bin inside an
  archive. The last page of a book is not deleted: delete the book's file
  instead.
- `F2` renames the page, in the folder it has inside the archive. A name
  another entry already has is refused.
- In the adjust panel (`E`), **Save a copy** puts the edited picture into
  the archive beside the page as `name_edit.ext`, and **Save over the
  page** replaces the page with it.

A ZIP / CBZ, or a 7z / CB7 whose files are packed one by one, keeps its
format and its name, and its other entries are copied as they are. Any
other archive — a solid 7z, a RAR / CBR, an ALZ, an EGG, a split set —
cannot be changed as it is, so a panel asks once what it is to become:

| In the panel | What it sets |
|---|---|
| Becomes | ZIP (`name.cbz` for a comic, `name.zip` otherwise) or 7z (`name.cb7` / `name.7z`); the keys `Z` and `7` pick them too |
| Method | packed — Deflate in a ZIP, LZMA in a 7z — or stored as it is (Store / Copy) |
| Level | 1 to 9 when packed: 9 is the smallest and the slowest |

**Write** (or `Enter`) starts; `Esc`, a click outside the panel or doing
anything else leaves the book alone. The three choices are kept (Settings ›
Files › An archive written again); the method and level are also what a
page saved into a ZIP or a 7z in place is packed with. Pictures and films
hardly pack further, so a low level loses little. The book is written
again in that format with the change in it and the old file — every
volume of a split set — goes to the Recycle Bin. A file already holding
the new name is never replaced: the book becomes `name-1.cbz`. The answer
is kept for that book until the program closes. A 7z needs free space
beside the archive of about twice its unpacked size while it is written.

The new archive is written beside the old one, opened again and compared
with what it should hold before it takes the old one's place; `Esc` stops
the writing. Stopped, failed or not matching, the new file is removed and
the book is as it was. Nothing else can be opened or changed while it is
being written. An **encrypted archive is not changed**, with its password
or without: its pages can be read and copied out (`F6`), no more.

`Ctrl+Z` takes back the last change to a book that kept its format: the
file it was before is kept beside it as `name.cbz.rvold` until the next
change to an archive or until the program closes, and `Ctrl+Z` in that
book puts it back. (If the program is ended by force the `.rvold` file
stays; it is the book before the change and can be deleted.) A book that
was written again in another format is not taken back this way: its old
file is in the Recycle Bin.

**`Ctrl+Z` cannot bring a file back from the recycle bin.** Windows keeps
that undo for File Explorer. Restore it from the recycle bin instead.
Moves, copies and renames do undo properly.

### Classifying

`K` (or Classify... in the right click) opens a window of the nine
folders the number keys have, each with how many pictures are set aside
for it. Click a folder, or press its digit, and the picture on screen is
set aside for it — nothing moves yet — and the next one comes. Look
through them all, then press **Copy all set aside** or **Move all set
aside**: every one goes to its folder at once, a name already there never
replaced (`name-1.jpg`). `Ctrl+Z` undoes them one by one, the last 64.
**This one goes to ...** takes the picture on screen off the list;
**Forget what is set aside** empties it.

## Converting a lot of files at once

```
rubraview.exe --batch --resize=50% --format=webp "D:\Photos"
```

No window opens. It prints how many files it converted, skipped and
failed.

`Ctrl+B` does the same thing from the viewer, in a window of its own
beside it: the viewer goes on working while it is open, Edit can be open
next to it, and `Ctrl+B` again or `Esc` closes it. It reads from the top:

1. **The files**: the pictures of the list now open — a folder, a
   playlist, a list you filtered or sorted — in its order; films, music
   and text in it are left out, and the line says how many of each.
   Press it to see the list in its own window (`F3`). After Explorer's
   **Convert...** it is the files chosen there.
2. **Edit's steps, first**: every step in Edit's list is applied, in that
   order, to every picture before anything else. The line says how many
   there are; press it to open Edit (`E`) and make or change them. **Edit's
   steps** chooses none, **the steps in Edit now** (written to
   `batch-steps.rvsteps` beside the settings), or **a file of steps** saved
   from Edit. Crops, effects, plug-ins, G'MIC and FFmpeg filters are done
   there; choices saved before that still hold a crop or an effect keep
   them, and a row says so (press it to drop them). On the command line
   `--steps=FILE` does the same, with `--gmic-exe=` and `--plugin-folder=`
   for what the steps run.
3. **Then, on each picture**: size, sharpening, turning, colour, frame,
   watermark and text.
4. **Written as**: format and its options, and **Names**, which opens the
   names window (below).
5. **Go**: **Save these choices...** writes them to a `.rvbatch` file (an
   option a line) and **Load choices...** reads one back; the last ones
   used come back by themselves. **Run on the N pictures** starts it and
   closes the window. The results go into a `rubraview-out` folder beside
   those pictures unless you type another in **Into**; into the pictures'
   own folder a name already there is never replaced.

The names window builds each new name from parts you turn on — text
before, the folder's name, the file's own name, a number (from, going up
by, how many digits), the picture's size, the date it was taken (else the
file's date, written as 2024-05-01, 20240501, 2024-05-01_132207 or
240501), text after — joined by `_`, `-`, a space or nothing; then the
letters as they are, lower or upper case, spaces made `_`, any number of
"replace this with that", and what to do with a name already there. At
its right the first twelve files are listed with their new names as you
choose, and a name met twice or already in the output folder is marked.
**Pattern** shows the result and can be typed whole.

The run gets a window of its own and is a separate program: you can go on
reading, close the viewer, or have it hang, and the conversion carries on.
Its window prints how many files were written, skipped and failed, and
waits for a key before it closes.

| Option | What it does |
|---|---|
| `--resize=50%` `--resize=1920x1080` `--resize=w800` `--resize=h600` | How to resize |
| `--filter=lanczos3\|bicubic\|bilinear\|nearest` | How to resample |
| (Settings › Display) | Bicubic and Lanczos resizes of large pictures run on the graphics card when there is one, with the same result |
| `--format=jpg\|png\|webp\|gif\|bmp\|tif\|ico\|avif\|jxl` | What to write (AVIF and JPEG XL by FFmpeg) |
| `--avif-chroma=420\|444`, `--avif-bits=8\|10` | AVIF's colour resolution and depth |
| `--quality=1..100` | For JPEG and WebP |
| `--progressive` | A progressive JPEG |
| `--lossless`, `--effort=0..6` | A WebP with every pixel as it was; how hard to try for a small one (4 as it comes) |
| `--png-depth=rgba\|rgb\|palette\|gray` | A PNG's colours |
| `--subsampling=auto\|420\|422\|444`, `--grey-jpeg`, `--optimise`, `--restart=0..64` | A JPEG's colour subsampling, grey, optimised tables, restart markers |
| `--png-filter=level\|none\|sub\|up\|average\|paeth\|adaptive`, `--interlace` | A PNG's filter; interlaced |
| `--colours=2..256`, `--no-dither` | A palette's size (PNG's palette, GIF, BMP's 8 bits); flat colours, not dots |
| `--tiff=auto\|none\|lzw\|zip`, `--bmp=32\|24\|8` | A TIFF's compression; a BMP's bits |
| `--near-lossless=0..100`, `--alpha-quality=0..100`, `--sharp-yuv` | A WebP's near-lossless (lossless), alpha quality and sharp colour edges (lossy) |
| `--webp-preset=none\|picture\|photo\|drawing\|icon\|text`, `--target-kb=N`, `--cmyk` | A WebP's preset; a JPEG's or lossy WebP's size at most N KB; a TIFF in CMYK |
| `--details=drop\|file\|txt` | The source's camera details and comments: dropped, kept in the file (as it comes), or that and a `.txt` beside it for what the file cannot hold |
| `--rotate=90\|180\|270\|exif`, `--flip=h\|v` | Turn them |
| `--grayscale`, `--exposure=`, `--contrast=`, `--sharpen=amount[,radius]` | Adjust them |
| `--privacy-clean` | Strip GPS and camera details |
| `--include=*.jpg;*.png`, `--exclude=*_thumb.*` | Which files |
| `--min-size=`, `--max-size=` | By size, in bytes |
| `--name={name}_thumb.{ext}` | What to call the results |
| `--out=DIR` | Where to put them |
| `--recursive` | Include subfolders |

| `--resize=l1920` `--resize=s1080` `--resize=2.5mp` | The long side, the short side, megapixels |
| `--no-enlarge`, `--linear` | Leave a smaller picture its size; resize in linear light |
| `--steps=FILE.rvsteps` | Edit's steps on each picture first (each area and size a ratio or pixels, as saved) |
| `--crop=X,Y,W,H`, `--autocrop` | Cut to a rectangle; cut off a plain border |
| `--auto-colour`, `--negative`, `--effect=oil:4` | Correct the colours; a negative; any effect of Edit (its name in lower case, `radial-blur`, `red-eye`) |
| `--plugin=posterise:6,1`, `--plugin-folder=D:\plug` | A plug-in by its file's name, its parameters in order (the rest at their defaults); the folder, else the default one |
| `--canvas=1920x1080,000000`, `--border=8,FFFFFF` | Centred on a canvas; a frame round it |
| `--watermark=FILE,br,50` | A picture laid over each, at a corner (`br` `bl` `tr` `tl` `c`), at an opacity |
| `{n}` `{n:3}` `{parent}` in `--name=`, `--start=7`, `--replace=IMG_,photo-` | A counter, padded; the folder's name; where it starts; words replaced in each name (`--replace=` again for more, in order) |
| `{taken}` `{taken:yyyymmdd-hhmiss}` `{w}x{h}` in `--name=`, `--n-step=5`, `--case=lower\|upper`, `--no-spaces` | The date taken (else the file's), its parts yyyy yy mm dd hh mi ss; the size each file comes out at; the counter's step; the letters' case (not the extension's); spaces made `_` |
| `--exists=replace\|skip\|number` | A name already there: replaced (as before), left, or the next free `name-1` |
| `--keep-dates`, `--subfolders` | The results take their pictures' dates; with `--recursive`, the same folders under `--out` |
| `--text="{name} {n:3},br,4,FFFFFF"`, `--text-font=` | Words on each, last of all: where, the letters' height as a percent of the shorter side, the colour; a soft shadow under them |
| `--dpi=300` | The DPI written into each file (JPEG, PNG, TIFF, BMP) |

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
- `Shift+[` marks where a repeat starts and `Shift+]` where it ends; `\` turns it off.
  The point you just set wins: an A at or after B moves B to the end of
  the film, a B at or before A (or with no A) moves A to its start.
- `Shift+\` opens the same two points as numbers, so a repeat can be set
  to the millisecond: each point has three fields, hours, minutes and
  seconds (with up to three decimals); a field you come to is replaced by
  the first digit you type, and `:` goes on to the next field. Inside
  that box **A = now** and **B = now** (or `[` and `]`, with or without `Shift`) put the moment you are at into A or B, with the same rule —
  so you can tap a point, correct the number, and tap it again. Click a
  field or press `Tab` to move between them (`Up` / `Down` go to the
  other point), `Enter` keeps them, `Esc` or the `X` at the box's top right leaves them as they were.
  `Ctrl+]` / `Ctrl+[` play faster or slower (0.25x to 4x — the sound's pitch
  follows), `Ctrl+\` goes back to normal.
- The Toolbar's seek bar shows the time; click or drag on it to go
  somewhere else.
- A click on the picture — or on the window, for music — pauses and plays,
  also when the hand moves a little while pressing; a film is dragged only
  past twelve pixels. A line of timed words is the one place a click on a
  song does something else: the song goes to that line.
  The wheel is the volume, 5 a notch (Settings > Video > **Wheel is the
  volume while playing**; with it off the wheel zooms a film). `Ctrl` and
  the wheel zoom, and a drag moves the picture.
- What happens when a film or a song ends is set by the Toolbar's end
  button, `Ctrl+R`, At the end in a song's right click, or Settings › Audio › When a
  file ends: **Once** stops there; **Next** plays the next film or song in
  the folder or playlist and stops after the last; **1 loop** plays the same
  one again; **Loop** goes round all of them; **Shuffle** plays them in a
  random order, each once before any comes again, and never the same one
  twice in a row. Pictures between them are passed over. `Next` is where it
  starts.
- Subtitles come from a file beside the film (`.srt`, `.smi`, `.vtt`,
  `.ass`, a DVD's pictures as `.idx` with its `.sub`, and a Blu-ray's as
  `.sup`) or from inside the film itself. In a SAMI file, `&nbsp;`,
  `&amp;`, `&lt;`, `&#44032;` and the like show as the characters they
  stand for; the file itself is written back as it was.
- The sound can be shaped (Settings › Audio › Sound, the Toolbar's **EQ**,
  **Night** and **Viz** buttons): an equaliser with
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
  half a second earlier or later, `Shift+Z` / `Shift+X` a tenth of a second,
  and `Alt+Z` puts them back to the file's own times. The move is for this film only and leaves the file alone;
  while it is not nought the line under the Toolbar's bar says so
  (`sub -0.3 s`).
- Text subtitles sit in a shaded box, and the box always holds the whole
  subtitle: one of many lines, or one long enough to wrap, makes the box
  grow from the edge it is anchored by (it stays inside the window, and
  may be anywhere in it — in the margin above or under the film too). Settings › Video › **When it does not fit** set to
  `shrink` keeps the box its size and makes the text smaller until it fits
  instead. The same page places the box in percents of the window: a point
  (**Box place, across** and **down**), which part of the box sits on that
  point and how the text is aligned in it (**left / center / right**,
  **top / middle / bottom**), and the box's **width** and **height**.
- Tap the box and its edge thickens and four buttons appear at its
  top-right corner: drag **M** to move it, drag **R** to resize it (both
  write those settings; the text's size is **Subtitle size**), **S** opens
  the subtitle settings and **X** turns subtitles off. Tap anywhere else
  to let go of the box. A DVD's or a Blu-ray's picture subtitles stay
  where the disc puts them.
- `Shift+C` (the speech-bubble button, or Subtitle list in the right click) opens
  the film's subtitles as a list in **a window of its own**: a row each,
  with when it comes, when it goes and what it says; the one playing is
  marked and the list follows the film. A click on a row goes to that
  subtitle. Each row has a box at its left to tick, and the box at the top
  ticks all of them or none (`Ctrl+A` all; `Shift` + click a run, `Ctrl` +
  click one). **< Earlier** and **Later >** above the list move the ticked
  subtitles by the seconds written between them — click the number to
  type another (`0.1` as it comes), `Enter` to finish. A second click on a
  row soon after the first (or `F2`, or `Enter`, for the row last clicked)
  lets you type its text over: it is shown as the file holds it, its tags
  and all, with `\N` where a line breaks; `Enter` keeps it, `Esc` gives it
  up. `Insert` puts a new subtitle where the film is, two seconds long,
  and lets you type its words at once; `Delete` takes out the ticked rows,
  or the one last clicked. A second click on a row's two times (or
  `Ctrl+T`) lets you type them over, as `0:01:02.50 --> 0:01:04.00`; the
  row moves to its place in time. A new subtitle is written like the one
  before it in the file — its number, style, class and settings — and a
  SubRip file's numbers are not counted again. A SAMI caption moved past
  others is moved in the file too, so its SYNCs stay in order of time. A changed row has a yellow mark and the header says "changed, not
  saved". **Save** (`Ctrl+S`) writes the subtitle's file again in its own
  format — SubRip, WebVTT, SubStation or SAMI — changing only the times
  and texts you changed: the styles, the other lines, the line ends and
  the encoding (a Korean SAMI in CP949 stays CP949) are as they were. The
  new file has the subtitle's name; the one before is kept beside it as
  `name.old1.smi`, then `.old2`, and so on, and is not offered as one of
  the film's subtitles. Nothing is saved when the file was changed by
  something else meanwhile; it says which. When a character typed is not in
  the file's encoding (a Korean SAMI in CP949, say), a second **Save** within
  six seconds writes the file as UTF-8 instead. The half-seconds `Z` and `X` moved the whole
  subtitle by are saved too (Save lights when there are any): they go
  into every time, and the subtitle is no longer moved after. With changes
  not saved, turning to another film, another subtitle or closing the
  viewer is held once and it says so; doing it again within five seconds
  goes on and leaves them. A SAMI caption has no end
  of its own: moved, it takes along the empty line that closes it, and one
  closed by the next caption only begins elsewhere. **Take out**, at the
  window's top right (or Take out of film in the right click), writes the text
  subtitles a Matroska film (`.mkv`) or an MP4 (its 3GPP text subtitles,
  as SubRip) holds inside as files beside it —
  `film.kor.srt`, `film.eng.ass`, named by their language, a SubStation
  track with its own styles — and shows the first; the film itself is not
  changed, and no FFmpeg is needed. A long film takes a while: the button
  says how far it is, and a press stops it. Picture subtitles and
  compressed tracks are not taken out; a file of that name already there
  is kept under a numbered name unless it is the same. **Put in film**,
  beside it, does the other way round with the subtitle on screen (its
  changes saved; a Matroska film takes SubRip and SubStation only, and
  says so at the first press): the film is written again with the subtitle as one more
  track — into a Matroska film a SubRip or SubStation file as it is, into
  an MP4 any subtitle's words as 3GPP text (an MP4 subtitle has no tags,
  so italics and colours do not go in). An MP4 keeps every byte where it
  was and grows by its own head written again at the end. It asks first — the first press says
  what will happen, a second press within six seconds goes on. The new film
  is written beside the old one as a temporary file and checked against it
  (every picture and sound block the same, the subtitle as it was put in,
  the file read to its end); only then the film is renamed `film.old1.mkv`,
  kept in its folder, and the new one takes the film's name. Anything that
  fails leaves the film as it was and removes the temporary file; a press
  while it works stops it. It needs room on the disk for a second copy of
  the film. The wheel and the arrow keys move the list;
  other keys do what they do in the viewer, so `Space` pauses and
  `Shift+C` or `Esc` closes it. A DVD's or a Blu-ray's picture subtitles
  are listed by their times only.
- The Toolbar's **CC** button turns subtitles off and on. Tap it twice
  quickly, or hold it for half a second, and a list of the film's subtitles
  opens — tap the one you want (or **Off**).

## Text files

A `.txt`, `.log`, `.nfo` or `.md` file opens as text, in the same window and
the same walk through the folder as pictures and films (`PageDown` at its end
goes on to the next file). A text file inside a ZIP or a 7z opens the same way.
Nothing is ever written to it. Settings › Text › **Show text files** off
leaves text files out of every list and does not open them (Windows may
still send one: which program opens a type is Explorer's choice). The
Toolbar's **Edit** opens the file in the editor Settings › Text ›
**Editor** names (Notepad without one); **Reload** reads it again, at the
same place.

**Encodings.** UTF-8 and UTF-16 (with or without a byte order mark), Korean
(CP949 / EUC-KR, ISO-2022-KR, Johab), Japanese (Shift_JIS, EUC-JP,
ISO-2022-JP), Chinese (GBK / GB2312, GB18030, HZ, Big5, CNS 11643) and
Western (CP1252, DOS's CP437 for `.nfo` art). Each file's encoding is
guessed; the title bar says which it was read as. `Shift+N` reads it in the next
one, and the one chosen by hand is kept for that file. Settings › Text ›
Encoding can name one for every file instead. Files over 64 MB are not shown.

**Line breaks**, four ways (`B` goes round them, or Settings › Text):

| Way | What it does |
|---|---|
| As they are | every break of the file is a break; blank lines stay |
| All in one run | no breaks at all: the whole text runs on |
| Blank lines dropped | the empty lines go; the other breaks stay |
| Paragraphs | lines are joined; a paragraph ends where a line ends a sentence (`.` `!` `?` `。` or a closing quote) **and** more line breaks follow it than usually do in the file, or the next line is indented when only some lines are. A file with only one break everywhere ends a paragraph where a sentence ends and the next line is indented, or, with no indents at all, wherever a line ends a sentence. As in a printed book, each paragraph starts on a new line, in by one space, with no blank line between |

Joined lines get a space between them, except between Chinese or Japanese
characters, which are written without one. `Shift+B` removes the indents at
the start of each line or paragraph, or keeps them.

**Auto-scroll.** `S` (or `Space`) starts and stops it. While it runs, `Ctrl+]` / `Ctrl+[`
(faster and slower, as a film's speed and the slide show's), the
wheel, or `↑` / `↓` change its speed, in lines a minute, shown at the
bottom right; a click stops it. It stops at the end.

**Size and spacing.** `Ctrl+wheel`, `+` / `-` or `Ctrl+Plus` / `Ctrl+Minus` the size of
the letters (as `+` / `-` zoom a picture), `Alt+wheel` or `Alt+Plus` / `Alt+Minus` the space between lines. `W` wraps the
lines to the window or lets long lines run on (`Shift+wheel` then moves
sideways); the Toolbar has the same, and **Word wrap** for the wrapping.
Settings › Text also has the font, the side margins, a column at most so
many characters wide, the paper (dark, sepia, light, black), and colours
of your own for the text, the background, links and what Find found
(`#RRGGBB`; empty keeps the paper's).

**Moving.** The wheel and `↑` / `↓` by lines, `PageUp` / `PageDown` / `Space`
by screens, `Home` / `End` to the start and the end. The place read to is
kept for each file, and the file opens there the next time.

**Finding and copying.** `Ctrl+F` opens a box above the text: `Enter`, `F3` or `Ctrl+G`
the next, `Shift+Enter`, `Shift+F3` or `Ctrl+Shift+G` the one before (with the box closed,
`Ctrl+G` / `Ctrl+Shift+G`; `F3` is the file list there, as everywhere); letters' case and
full-width forms are ignored, and the bar says which match of how many it
is. Drag over the text to select it, `Ctrl+A` for all, `Ctrl+C` copies what
is selected, as it is shown.


Settings › Text › **Kind** under Auto-scroll: **smooth** glides at its
speed; **pages** moves a screen at a time (one line kept to read on from)
every **A screen every** seconds, `Ctrl+]` / `Ctrl+[` changing it while it runs. `A`, `Space` or a click in the middle of
the window starts and stops auto-scroll; the wheel always moves the text
(the speed is `Ctrl+]` / `Ctrl+[` or the arrows). `,` and `.` go to the previous and
the next file; the Toolbar has the list (**List**) too.
**Headings stand alone** keeps a short line with no sentence's end and a
blank line before and after it (`제2장`, `Chapter 2`) as a paragraph of
its own in paragraphs mode. The information bar (`Shift+I`) says the
encoding and how far through the file you are.
## The title bar and the Floating Widget

The title bar starts with **☰**, a list of what the window does: Pin the
title bar, Align the Floating Widget, New window, Open file, Recent
files, Batch, Print, Playlist, Settings and Help. After it comes the
name of the file on screen (the folder's, in the open-file screen); a
click on it opens the open-file screen. At the right are **Set**
(Settings, `F10`), **P** (the title bar stays shown, and the picture or
the film is shown below it instead of under it), **H** (the title
bar and the Floating Widget hidden until the picture is clicked), **T**
(always on top), **F** (full screen), **_**, **[]** (maximise), **R**
(drag it to size the window) and **X**. What is on is lit and bold;
resting the pointer on a button says what it does.

A button that chooses one of several — zoom, fit, layout, speed, what the
end does, the equaliser, the analyser, the slide show's delay, line
breaks, the encoding, the sound track — opens a list of them to tap, rows
a finger wide, and the wheel over it goes to the one before or after;
its key still steps to the next one. A panel's choice (printing,
exporting, Edit) opens the same list, and a panel's slider takes a typed
number: press its number (or its name). Every box that takes typing has
its buttons under it — **OK**, **Cancel**, and **Browse...** where a
folder is asked for. The buttons do not show their keys; their tooltips
do. What the Toolbar has no button for is in the right click, on a key
(the help, `F1`, lists them) or in Settings; the keys are changed on the
Settings window's **Keys** page. The Toolbar's last group has
**Settings**, **Bar pin** and **Info bar** (`Shift+I`); a picture's
Toolbar also has **Centre** (the picture's middle on the window's, as a
double click does) and **Wheel strip**: off, turning pages with the wheel
brings up no thumbnails.

The **Floating Widget** sits at the bottom right (over the information
bar while it shows; ☰ › Align the Floating Widget puts it back there):

- **F** — a click opens or folds the **Toolbar** (it never pins it);
  dragged, the widget moves, and past the window's edge the Toolbar gets
  a window of its own. The wheel over F goes to the file before or
  after, as the wheel over the picture does (the thumbnails come up
  below as **Wheel strip** says); `Alt` + wheel how see-through the
  widget and the Toolbar are (never below 30 %).
- **T** — rest the pointer on it and the Toolbar opens, folding again
  when the pointer leaves. Dragged up or down it goes through the files,
  a step every few millimetres; sideways, through the folders (or the
  archives) beside this one. The wheel over T sets the buttons' size.
- **P** — while the Toolbar is open: on, it stays open when the pointer
  leaves; off, it folds.
- **R** — dragged sideways, more or fewer buttons a row; away or back,
  bigger or smaller buttons. The wheel over R makes the Floating Widget
  itself — F, T, P and R — bigger or smaller, 10 % a notch from 50 % to
  250 %; Settings › Display › Floating Widget size holds the same number,
  and settings.ini keeps it (`[ui] widget_size`; settings.ini's sections are its own, not the keys').

The Toolbar changes with what is on screen — playing controls for a
video or music, frame steps for an animation, layout and the next or
previous archive for a comic archive. It never opens past the window's
edge.

The Toolbar is a strip: the seek bar (click or drag to go there) with the
file's name under it, then small icon buttons in groups — moving between
files, playing, sound, size, turning and layout, files, the window — each
group in a rounded frame of its own. Groups go along a row while they fit
and the next one starts a new row, as words wrap; a group longer than a row
has rows of its own (R beside P sets how many buttons a row).
An open box stays on the side of its anchor it opened on, so a longer name
or another layout does not make it jump. A
film's and a song's first row is Play / Pause, Stop, five seconds back and
forward, then the file before and after. A
long name takes two or three rows instead of being cut. Pointing at a
button puts what it does where the name was. For a film or a song the line
under the bar ends with the time, the volume, the speed and the A-B repeat;
for a picture, with its place in the folder or archive (`4 / 6`), and the
bar is a page bar; where the name needs the whole width, that goes on a
row of its own under it. The seek bar is only in the Toolbar. The information
bar (`Shift+I`, off until asked for) runs along the bottom: the file's name
at the left — a name too long for the room keeps its beginning and its last
ten characters, with "..." between — and at the right the file's size, the
picture's size in pixels and, at the very right, which file it is of how
many in the folder or archive (`3 / 120`).

The Toolbar and the right click read the same way: a switch says whether
it is on, the layout and fit in use have a blue edge, and a grey button or
row cannot do anything with what is on screen (`Next archive` in a plain
folder, `Track` on a film with one sound track). `Delete` in the right
click asks first: its row turns into `Delete?`, and a second click within
five seconds moves the file to the recycle bin. The Layout tile says the layout in use and whether it
runs right to left (`Layout: book rtl`). Open folder lists folders and the files
the viewer can open, and says how many others it left out.

The **shape** (`Shift+A`) shows the picture or the film in another shape without
touching the file: 4:3, 5:4, 3:2, 16:10, 16:9, the cinema's 1.85:1 and
2.39:1, 21:9, 1:1, A4 / B4 and US Letter paper, or **Custom** — the two
numbers under Settings › Viewer › Shape. **On its side** turns the chosen
ratio over (9:16; paper lying down). `Shift+A` goes round them, back to
the file's own shape. The choice lasts until the viewer is closed.

The Floating Widget starts in the bottom-right corner, and ☰ › Align the
Floating Widget puts it back there. It is always whole
inside the window, and when the window changes size it stays the same
part of the way across and down.

100 % is the size the fit option gives. The zoom button shows the zoom from
there (`100%`, `250%`). Real size — one screen pixel for each of the
picture's own — is 0 %, and the button reads `Real size`, on two rows,
while it is on screen. A tap on the button goes round: real size, then
20 % to 200 % by twenties, then real size again. The fit button beside it
goes round the fits: window, width, height, smart (never enlarged),
stretched to fill, and names the one in force. The wheel turned over the button, or `Ctrl` and the wheel over
the picture, zooms by one step — 10 % as it comes (Settings > Viewer > Zoom
step) — to the next round figure: 100 %, 110 %, 120 %. `0` puts 100 %
back with the picture in the middle; `4` shows real size.

The picture follows the pointer when it is dragged, at any zoom. When it
is larger than the window the arrow keys move it too: a seventh of the
window a press toward the side the arrow points to, stopping at the
picture's edge (a picture that fits stays; a film keeps its arrows for
seeking and the volume; in the webtoon strip Up and Down scroll). A click
without a drag is what it was: a page turned at the window's sides, the
boxes shown in its middle.

The Toolbar keeps what is used most: turning pages, the zoom and the fit,
turning, the layout and the slide show, opening, the lists and the
information, editing and exporting; for a film, playing, 5 s back and on
(held, those two offer steps up to 5 min), the sound and the subtitles.
Ten on and back, zooming by steps, the single fits, the filmstrip and
flipping are on keys (the help, `F1`, lists them); renaming, copying and
moving are in the right click; always on top and full screen are the title
bar's, and Settings is its last group's. Its buttons come in the same
order of groups for every kind of page: going and playing, how it plays,
sound, subtitles, view, turning, the file. Resting the pointer on a box's anchor, pin, grip
or seek bar says what a click or a drag does there, and `Esc` folds an
open box before it closes anything else. While a box is open, a second square beside its pin, with
three dots, sizes it: drag it sideways for more or fewer buttons a row, and away from the box or back for bigger or smaller ones; the
size is kept, and Settings › Display has the same four numbers.

At the middle of each side of the window, pointing near the edge shows
four buttons. For pictures the left ones go to the previous file, ten back
and the first, the right ones to the next, ten on and the last; for a film
or a song they go 5 s and 30 s back or on, and to the previous or next file
(`Shift` + `Left`/`Right` also go 30 s). The fourth, |◁ or ▷|, opens the
archive before or after this one in its folder — or, for files in a
folder, the folder before or after — as `[` / `]` do, and the
Toolbar's |◁ ▷|, which stand beside its page buttons (◁◁ ▷▷) for every
kind of file.

`P` or the playlist button shows what is open as a list
floating over the picture: the playlist, or the folder's or the archive's
files when there is none. The file on screen is marked and kept in view; a
click goes to a file, the wheel scrolls, the title drags it, `X` or `Esc`
closes it.

For a folder's files or a playlist (not the pages of a book) the list can
be changed there. At the left of every row are a checkbox, an `x` that
takes the row out of the list, and arrows that move it up or down one
place. Drag a row by its name to put it somewhere else — a line shows
where it will go — and a checked row takes every other checked row along.
Under the title, **Add** puts files you pick in the open dialog at the
end of the list, **Del** takes the checked rows out (the files themselves
stay on the disk), **Save** writes the list to a file (`.m3u8`, or
`.rvlist`), **Load** opens such a file, **Edit to all** writes an edited
copy of every picture (see Edit), and **Rel** / **Abs** says how a
saved list names its files: from the list's own folder, so that the folder
can be moved or copied whole, or by their whole paths (Settings > Files >
A saved list names its files). Files dragged from Explorer onto the list
go in at the row you let them go over. The box at the left of the bar
checks every row, or, when they all are, none. **R**, beside the `X`,
sizes the window: press it and drag, and it stays under the pointer — the
top and the right edge follow it, the bottom and the left stay. A folder whose list you change is from then on
a playlist: it keeps your order.

`F3` (the Files button) opens the same list in
**a window of its own**, with a preview. The viewer goes on showing its
page; the other window lists what the archive holds — or the folder, or
the playlist — each file with its size, the one on screen marked. Point at
a row, or move to it with `Up` and `Down` (`PageUp`, `PageDown`, `Home`,
`End` go further), and its picture is shown on the right with its name,
its pixels and its size. A click, or `Enter`, turns the viewer to that
file. Any other key pressed there does what it does in the viewer, so
`F3` or `Esc` closes it. A film or a sound has no preview, and a page far
inside a solid archive is not decoded for a glance: both say so, and open
with `Enter`. For a folder's files or a playlist it is changed exactly as
the floating list is — the boxes, `x`, the arrows, a row dragged by its
name, the box for all, **Add**, **Del**, **Save**, **Load**, **Rel**/**Abs**,
and files dropped on it from Explorer — and both show the same list, so a
change in one is in the other. The two differ in what they are for: the
floating list lies over the picture, see-through; this one is a window of
its own with the pictures shown large.

Both lists have a row of tools under their buttons. **.ext** lists the
extensions in the list, each with a box: untick one and its files are
not shown (the list keeps them; **All** shows every one again). **Shape**
ticks or unticks the rows of wide, tall or square pictures and films, as
in the open-file screen; it acts on the rows shown. **Filter**
shows only the names holding what is typed — in the floating list the
typing box opens over the picture and the rows follow each letter; in
the window it is typed in place; `Esc` or the **×** shows them all.
**Sort** puts the list itself in order by name, extension, size, date
modified or created, and ▲ / ▼ turns it rising or falling. While a
filter or an extension hides rows, rows are not moved by dragging or by
their arrows. In the floating list, the row under the pointer shows its
file as a thumbnail beside the list.

A right click on a file in either list opens its own short list: **Open**;
**Copy the whole path** (to the clipboard); **Show in Explorer**, the file
picked out there; **Take out of the list** (the file stays on the disk);
and **Delete the file...**, which first asks in a warning of Windows' own —
No is the answer it starts on — and then moves the file to the recycle
bin, from where it can be put back.

`I` (or `Ctrl+I`, the Info button, or Info in the right click) opens the file's information: its
size, dates, pixels and format, and for a photo what the camera wrote —
camera and lens, when it was taken (with the time zone), exposure, aperture,
ISO, focal length, program, metering, white balance, colour space, and where
it was: the place in degrees and minutes, the same in decimal degrees for a
map's search box, the altitude, the direction the camera faced and the GPS
time. A picture without any says so. For a film, **Display** says how its
pictures reach the screen: which decoder, on the graphics card or the
processor (and its threads), where the colours are turned into BGRA, how a
picture gets into the window, what draws it (Direct2D 1.1 on Direct3D 11,
the feature level and the card, or WARP, Windows' software rasteriser), the
card's memory, the swap chain (flip or copy model) and whether it waits for
the screen's refresh, how many video decoders the card offers, and GPU
decoding's setting with the reason a film is decoded in software. An HDR
film adds **HDR**: PQ or HLG, the peak it is tone mapped from and the white
it is mapped to, or why it is not tone mapped. When the sound is
played in another format than the decoder named (see below), **Sound
played at** says which. `I` again or `Esc` closes it.

For a picture it also gives the aspect ratio, the size it prints at
its DPI, how much memory it takes on screen, how long it took to decode
and how many different colours it has (up to 24 megapixels); and the
rest of what a camera writes — the flash in full, the widest aperture and
the lens's range, the light source, contrast, saturation and sharpness,
the subject's distance, the owner, a comment, the EXIF version — and an
IPTC block's title, headline, caption, keywords, credit, place and
copyright, as photo agencies write them.

It also says all the file tells of how it was made. For a film or a song,
each stream in the file has its own block: the codec in words, its profile
and level, the frame and the shape of a pixel, the frame rate, whether it
is interlaced, the bit rate and how much that is against the unpacked
picture or sound, the colour range, primaries, transfer and matrix; for
sound the sample rate, the channels and their speakers, the bits a sample,
and what it was decoded as. For a picture: the bits a pixel it is stored
at and the compression ratio, then what the format itself records — a
JPEG's process, chroma sampling, restart interval and an estimate of the
quality it was saved at; a PNG's depth, colour type, interlace, gamma,
texts and deflate level; a GIF's palette, frames, playing time and
repeats; a WebP's and a BMP's likewise. How much there is depends on the
file and on what opened it (FFmpeg reads more out of a container than
Windows does).

The title bar (point at the top edge) has, after the pin, a `Size` button:
press it and drag, and it stays under the pointer — the top and the right
edge follow it, the bottom and the left stay. A double
click on the title bar maximises the window or gives it back its size.

`Shift+T` pins the Toolbar open; `Ctrl+T` gives it a small window of its own
that stays on top, and `Dock` puts it back. The `Pin`
button on the title bar (point at the top edge) and `Ctrl+Shift+T` keep the
viewer itself on top of other windows. `Ctrl` + arrows size the window,
`Alt` + arrows move it.

## Settings


What can be pressed is blue and underlined, like a link: the pages on the
left, the actions (Register, Import, ...), the buttons at the top, a
switch, a choice, a slider, an extension's boxes, and the box of a folder
or a font. The buttons (**Revert**, **Reset: all**, **Reset: this page**,
**Reset: this setting**, **Close**) are under the page's name; the last row says what
the last change did. A long explanation is wrapped to the window.

A choice opens a list of its words where it is: click one (or the arrow
keys and `Enter`; `Esc` leaves it). A slider's number is typed: click
the number after its bar, or press `Enter` on it, and the box under it
takes the number (**OK**, **Cancel**; a folder's box has **Browse...** too).
The left and right arrows still step either.

Rest the pointer on a setting's name and a fuller explanation comes up
for the harder ones. **Reset: all** puts every setting back to its
default, **Reset: this page** only the page you are on, **Reset: this
setting** only the one with the focus; **Revert** undoes any of them. An
information line too long for the window is wrapped under itself.

**Fonts.** **General › UI font** is the face of every word the program
writes — menus, tiles, lists, panels, messages (empty is Segoe UI); the
text viewer's **Font** and **Subtitle font** are their own. A click on
one opens the font picker: type to find a family (the first whose name
begins with what you typed is chosen), `Up` / `Down` to move, a click to
choose. Right of each name a short sample — `Iil1 aoO0` and a word of
the language in use — is drawn in that family once its row has been in
sight a moment. Under the list: the weight (`<` `>`, or `Left` /
`Right`), **Italic** (`Ctrl+I`; `Ctrl+B` for bold), a large preview,
**Default** (the program's own font), **OK** (`Enter`, or a double click)
and **Cancel** (`Esc`). A setting keeps the font as
`Family, Weight, Italic`, the weight and italic only when they are not
the plain ones. Under it each place may have a face and a size of its own: the title
bar's name, the right-click lists, file names and messages, the lists,
the buttons and the settings window (with its line spacing); an empty
face is the UI font. The settings window is best in a face whose letters
are all one width, so its columns stay lined up; it has a scroll bar when
the page is longer than the window.

**Scroll bars.** A list longer than its window — the playlist, the files
window, recent files, favourites, the subtitle window, the settings page,
the font list, a list of choices, the open-file screen and the
information window — has a bar at its right edge. Drag its thumb and the
list follows; press the track above or below it and the list moves a
screenful that way, again and again while held, until the thumb reaches
the pointer. The two buttons together at the bar's foot, ▲ over ▼, move
it a row (a row of tiles on the open-file screen), again while held.
Edit's panel has one to each of its columns, and its list of edits one;
so does every other panel taller than the window (Export, Print, Batch,
Classify), and a text file's page, where the bar also says where in the
file you are.

`F10` opens the settings. A click on a choice's word goes to the next one;
the `<` before it goes back. On **Keys**, resting the pointer on a row says
what its action does. Every setting there now does what it says; most
take effect at once, a few the next time something is opened (the archive
filename code page, the memory cap). Some worth knowing:

- **General › Language** — the language the viewer speaks: `Auto` (as it
  comes) follows Windows' display language, or choose English, 한국어,
  日本語, 简体中文, 繁體中文 or فارسی; each is named in its own words so it
  can be found whatever is on screen. The change is at once. A language is
  a text file, `lang\<code>.txt` beside `rubraview.exe` (the zip, the
  installer and the Store package carry them; `rubraview.exe` on its own is
  English). A file of the same name in the settings folder's `lang` folder
  is read first, so a translation can be put right there. **Import
  language packs...** under it copies chosen packs (`ko.txt` and the like)
  to `lang` beside the program, or, where that folder takes no files (the
  Store's copy, Program Files), to the settings folder's. Each line is
  `"English" = "translation"`; a line whose `%` placeholders differ from the
  English is left out. Persian is shown in its letters, but the windows are
  still laid out left to right.
- **General › On startup** — started with no file, the viewer shows
  nothing (`blank`), reopens what you read last (`last_file`), or opens the
  picker in its folder (`last_folder`, the default). **Frameless window**
  off gives the window Windows' own title bar and borders.
- **Viewer** — the fit a new session starts with, the layout (the one
  `B` changes), the gap between two pages, how much one zoom step is, and
  whether a narrow window shows one page at a time.
- **Viewer** also has **Turning a page** — a cut, a fade (as it comes), a
  slide, a zoom, or a dissolve (the new page laid over the old one, only
  its opacity rising, so nothing dims in between) — and **Enlarge by whole steps only**: a small drawing
  is then shown at twice, three times ... its size, never in between, so
  every pixel of it is as wide as the next.
- **Files** — **Sort by** orders a folder (by name as text, as Explorer
  does, by date changed or made, by size, or `random`: another order each
  time). **Include subfolders** reads the folders under a folder into the
  same sequence, folder by folder. **Show** lists everything, or pictures,
  films or music only; **Only names like** takes patterns such as
  `*.png;IMG_*` and lists only those. The file you opened is listed
  whatever these say. Changing any of them lists the open folder again.
- **Files** — **Remember the page** off keeps no reading history;
  **Offer to resume** off opens where you stopped without asking.
  **Keep the list** off writes nothing more to the recent files. Under
  **Folders that are not written down** is a list as long as you like: a
  file in a folder named there, or in any folder under it, is written
  neither to the recent list nor to the reading history (and leaves the
  list if it was on it). The list ends in a box for typing a folder: click
  it, or press `Enter` on it, then type, paste or (`Ctrl+O`) browse one, and
  `Enter` adds it. `Enter` on a folder in the list puts it in the same box
  to change (the row says it is being changed); `Esc` leaves it as it was.
  `Delete`, or a click on the **X** at the end of its row, takes a folder
  off. **Folders shown without thumbnails** is a list of the same kind: a
  folder named there, and every folder under it, is shown from the folder
  above as a plain tile with its name, and no picture of what is in it is
  made. Inside it, its files show their own thumbnails as anywhere else.
- **Display** — the opacity of the Floating Widget, the Toolbar, the title
  bar and the thumbnail strip; the Toolbar's buttons a row and their size;
  the size of the Toolbar's buttons and anchors (48, 64 or 96); the size of
  file names and messages (the wheel over the Toolbar's bar sets it too);
  the colour that marks the choice in use, and whether a picture's own
  colour profile is used.
- **Audio** — **Gapless playback** runs one song into the next with no
  silence between; **Crossfade** (0 to 5 seconds) fades one into the next
  instead; **Audio latency** is how much sound is kept ready (20 to
  100 ms).
- **Video** — **GPU decoding** lets the graphics card decode a film (`on`, as
  it comes, falls back to the processor when the card cannot; `always`
  insists on the card; `off` never uses it). It applies to both decoders:
  Windows' (Media Foundation) and, with Decoder set to `ffmpeg`, FFmpeg's,
  which then decodes through D3D11VA and has the card turn the pictures
  into colour. An HDR film (PQ or HLG) goes to the card too while **HDR
  tone mapping** is on and the card gives it as 10-bit 4:2:0 (P010): the
  tone mapping reads the card's picture there. With tone mapping off, or
  another HDR format, FFmpeg decodes it on the processor.
  On the processor FFmpeg uses a thread a core. The information window's
  **Display** says which it is and, on the processor, why.
- **Video** › **HDR tone mapping** (`bt2390`, the default, or `off`) brings
  an HDR film (HDR10 or HLG) to an ordinary screen: on the graphics card,
  its brightest parts are folded under the screen's white (BT.2390, from the
  peak its metadata gives, 1000 nits when it gives none; the white stands
  for 203 nits) and its BT.2020 colours are brought into sRGB, so it no
  longer looks grey and washed out. It needs FFmpeg: an HDR film that
  Windows' decoder opens and names as HDR is handed to FFmpeg. A VP9 or AV1
  film Windows names nothing for (VP9 in MP4) is looked at by FFmpeg too,
  and handed over when FFmpeg finds it HDR.
- **Video** › **HDR output** (`auto`, the default, `off` or `always`): on a
  screen with Windows' HDR on, an HDR film that is tone mapped goes out as
  HDR — its highlights up to the screen's own peak, its colours wider than
  sRGB — while the rest of the window stays at Windows' SDR brightness
  ("SDR content brightness"), as other windows do. Moving the window to
  another screen, or switching Windows' HDR, opens the film again for that
  screen. `always` does it even on an ordinary screen (for testing). The
  information window's **Presented** then says "scRGB FP16", and **HDR**
  "sent to the screen as HDR"; otherwise **HDR output** says why not. A
  frame taken out of such a film is clipped at SDR white.
- **Video** also has the subtitle's **font** (type a family's name, such
  as `Malgun Gothic`) and **colour**, and **Preferred sound** and
  **Preferred subtitles**: a language as films name it (`kor`, `eng`,
  `jpn`), chosen when a film has it.
- **External** — first, the folder Rubraview runs from, with **Open its
  folder** and **Copy the path**. Then what Rubraview can use but does not
  ship, each in a section of its own: a **Get it** link that opens its
  makers' download page in the browser, a note of which file to take, and
  the place you put it, with **Open the folder** under it (FFmpeg's: the one
  in use; the plug-ins' is made if it is not there yet). Nothing is copied
  anywhere; each tool's licence is its own.
  - **FFmpeg** — FFmpeg 9.0's *shared* build (BtbN's
    `ffmpeg-n9.0-latest-win64-lgpl-shared-9.0.zip`; the LGPL build is
    enough). **FFmpeg folder** names its `bin` folder, which must hold all
    five DLLs. Empty, or when that folder does not hold them: beside
    `rubraview.exe`, then the settings folder's `ffmpeg`, where versions
    before 1.33.0 imported a copy. The line above says which was found.
    **In a process of its own** (on as it comes) runs FFmpeg apart from
    Rubraview: a film, a song or a station each in a second
    `rubraview.exe`, the pictures and filters in another, all of them
    ending with the viewer. A damaged file that breaks FFmpeg ends only
    that process; a film then goes on with Windows' decoder from where it
    was, and the viewer says so ("FFmpeg stopped on this file: it was
    opened with Windows' decoder"). Off, FFmpeg is loaded into Rubraview
    itself, as before 1.39.31. The process of a film that ends waits half
    a minute for the next film or song, so a playlist does not start one
    for every track.
  - **Rubraview plug-ins** — see "Plug-ins: filters from outside".
- **Viewer › Behind and between pages** — dark, black, grey or white.
- **General › Open with Windows' own dialog** — `Ctrl+O` then opens
  Windows' dialog, not the picker.
- **Cache** has **In use now** (pages and megabytes of the cap),
  **Clear the picker's thumbnails**, **Forget every reading place**, and
  **A picture's details when it is written again** (see Export).
  **The program's memory** shows what the session keeps (and the most it
  kept), what the song playing carries, the windows open, what is read for
  a moment and all it holds; **Restart Rubraview where it is** writes the
  settings and starts it again on the file or archive on screen, a book
  on its page. When its
  memory is nearly used up, Rubraview offers that restart once (`Y` restarts,
  `N` carries on).
- **Cache** — how much memory pages may take (up to 16 GB) and how many are
  read ahead and kept behind (up to 99 each; as many as the memory holds,
  the nearest first); **Always strip metadata on export** starts Export with
  that box ticked.
- **Keys › Use keymap.ini** off uses the built-in keys whatever the file says.

**H** on the title bar puts the title bar and the Floating Widget away, so nothing
comes up under the pointer. The next click anywhere in the window brings
them back, and does nothing else.

The file picker has a band across its top that holds nothing to press;
pointing at the window's top edge brings the title bar down over it (the
folder's path, and the window's buttons), as it does over a picture.

In the file picker each tile shows its picture — softened and a little
dark, so the name drawn over it stays readable — and a folder shows the
first picture in it (or its first film). The tiles fill in as they come
into view; an archive keeps a plain tile.

Every tile (and every row of the details view) has a check box at its top
left: a click on it picks that file or folder, or lets it go, whatever the
mode. `Ctrl+A` picks everything here, folders too, and again nothing.
**Media** picks the pictures, films, music and text files here, and again
lets them go. **Filter names** at the right of the path bar (or `Ctrl+F`)
shows only the names holding what you type (capitals or not); what you
picked stays picked when the filter changes or is cleared (`Esc`, or its
×). A right click on a tile — or a long press, held still — gives a short
list: on one thing (that one is picked alone), Open, Look inside (a book),
Add to the list, Copy the whole path, Show in Explorer, Rename, Copy to, Move to
and To the recycle bin (click it twice); on one of several picked, Add them
to the list, Make a playlist of them, Copy their paths, Copy or Move them,
Them to the recycle bin and Pick none. A folder picked is copied or moved
whole; added to a list, it brings its pictures, films, music and text.
**Add** on the list's bar opens this screen at the folder of the file on
screen, picking at a tap; **Add to the list** (where Playlist is) joins
what is picked to the list.

Above the tiles (and the rows) is a line of column heads — Name, Type,
Size, Created, Modified, Resolution, Length, as many as the width holds.
A click on a head sorts by that column, and again the other way round
(▲ rising, ▼ falling). **Open**, at the bottom right, opens what is picked — several files open
together as one set — or, with nothing picked, the one in focus;
**Cancel** beside it closes the screen. Opened while an archive is being
read, the screen shows what the archive holds.
`Ctrl+Z` here takes back a rename, a move or a copy made here; a move or a
copy of many is taken back at once, and a copied folder goes whole.

At the path bar's left end, ◀ goes back to the folder you were in
before, ▲ up a folder and ▶ forward again (`Alt+Left`, `Alt+Up`,
`Alt+Right`). The title bar stays over this screen, and the window's
title is **Rubraview - <the folder>**. The tiles a row follow the
window's width. `Ctrl+click` picks or lets go of one, `Shift+click` picks
everything from the last one clicked.

The row over the list picks without holding a key: the **All** box picks
everything here but `..` (a dash when some are), **Individual** makes
every tap turn one file on or off (a tap only picks; nothing opens),
**Range** takes two taps and turns over everything between them, **Same type** takes every file with the same
extension as the one in focus, **Media** the pictures, films, music and
text. **Shape ▾** lists **Wide**, **Tall** and **Square**, each with how
many are picked of how many: a choice picks every picture and film of that
shape, and the same choice again lets them go; the list stays open for
another. A photo turned by its camera counts as it is shown, and square
means width and height exactly equal. The first time, the sizes are read
(how far it has got is shown), then kept. **Sort by ▾** and **Rising ▾** each open a list to choose from, and
**Details** (or **Tiles**) at its end changes the view. The bottom row
acts on what is picked: **Add to the list**, **Recycle** (press
it twice), **Move to**, **Copy to**, **Cancel** and **Open** (**OK** when
files are being picked for the list) — send them to one of the numbered
folders: press the number after the button, or `0` for another folder,
typed or browsed (**Browse...** under the box, or `Ctrl+O`) in the same
box as `Shift+F6`, a new one made on a second `Enter` (with no numbered
folder set, the button opens that box at once). A playlist file of them
and **Change extensions...** are on the right click: the latter lists each
extension the picked files have, a new one beside each (`.txt → .text`,
`.nfo → .inf`; a row left as it was leaves its files alone), edited a
row at a time (a click on a row, `Tab`, or `Enter` for the next) and
done by **Apply**. **Details** shows the files as a list — a row a file, with its type (the
extension), size, created and modified times, resolution and, for a film
or a song, its length; resolution and length fill in as each row comes on
screen. Drag the line between two column names to make the one on its
right wider or narrower, and drag a name to put that column somewhere
else (Name stays first); both are kept. A click on a column's name sorts by it, rising (▲); again,
falling (▼); folders stay first, and the choice is kept (Settings > Files
> sorted by). **Tiles** brings the pictures back (the same switch is in
Settings > Files). `Ctrl+Z` undoes any of it; what was done to many at
once is undone at once. A mode stays on until you press its button again, and
`..` at the start of the list goes up a folder.

**Recent** on the bar under the path shows the files opened lately on
this same screen — newest
first, in tiles or details, sorted and picked like any folder.

Your favourites come first on the bar under the path (`Ctrl+D` or the
star adds or removes the folder on screen; a file or a folder is added or
taken off by its right click, **Favourite (in or out)**). Right-click one to give it a
name of your own (an empty name gives the folder's back); drag one sideways
to put it somewhere else on the bar. **★ Favourites** at the bar's right
end (or **Favourites...** in any right click) opens all of them in a list
window, as the recent files' is: a click opens one — a folder here, a
file in the viewer — ▲ and ▼ move it up or down, its X or the ticked ones'
**Remove** take it off, and **Add this file** adds the file on screen
(**Add to favourites** on a right click does the same). All is kept in
`favorites.ini`.

`F2` renames the file, the extension included — correcting `.jgp` to
`.jpg` is a rename like any other, and nothing is asked.

`F1` opens the help in a window of its own. Its first lines say the
version and the day it was released, and give the project's page on
GitHub as a link a click opens. It lists every key that is
bound, grouped by where it works, and it stays open while you use the
viewer, so you can try a key with the list in front of you. The wheel or
`PageUp` / `PageDown` scrolls it, `F1` or `Esc` closes it. The keys it
shows are the ones in force, so a key you change appears changed.

`F10` or `Ctrl+,` opens the settings window, a window of its own. A change
takes effect at once; **Revert** goes back to what was there when it
opened, **Reset: all** to the defaults, and the file is written when the
window closes. The **Keys** page lists every key, under a heading for each kind of file
that says when its keys are asked (everywhere; files; moving about;
pictures; pages inside one file; the slide show; playing — a film, music or
an animation; films; music; text): select one and press `Enter`, then the
key to add; `Delete` takes the last one off. A key another action already
uses where the two are asked together is refused, and the message says
which. **Show by key / by function** turns the list round: the keys in order, each
key's family together (`A`, `Shift+A`, `Ctrl+A`, ...), with a row for each
kind of file it does something in (`Space`: next page, play or pause,
auto-scroll). **By key, in the order of** puts the keys as they lie on the
keyboard (Esc, F1 to F12, the number row, Q W E R T Y ...) or A to Z, then
0 to 9 and F1 to F12. F1's help ends with a list by key too. One key does one thing,
or a like one, wherever it is pressed: `]` / `[` the folder or archive,
`Ctrl+]` / `Ctrl+[` faster and slower (a film's speed, the slide show's
delay, a text's auto-scroll), `+` / `-` bigger and smaller, `Shift+N` the
next code page. A `keymap.ini` written by an earlier version is read into
these headings by what each key does, and what came after it gets its
built-in keys, unless that key is already yours for something else. **Export keys
to a file** saves them to share or keep; **Import keys from a file** puts a
saved set on the page, and Revert takes it back off.

Settings live in `settings.ini`. If there is one **beside
`rubraview.exe`**, that is the one used and nothing is written anywhere
else on the machine — this is portable mode, for a USB stick. Otherwise
they live in `%APPDATA%\rubraview\`.

Settings shown greyed are ones the program does not read yet. They are
listed rather than hidden so you can see what is coming.

## What is not there yet

- **RAR archives compressed by RAR 1.5** (made in 1994-1996). Versions
  0.0.30 and 0.0.31 read them; archives are now read by FultaArc, which
  does not have this method yet. It returns when it does.
- RAR files that carry a filter program of their own
  (not one of RAR's six standard filters) are not read, by design.
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
| `T` | Open or close the Toolbar |
| `Shift+T` | Pin the Toolbar open |
| `Ctrl+T` | Give the Toolbar a window of its own, or dock it |
| `Ctrl+Shift+T` | Always on top of other windows |
| `Shift+I` | Information bar |
| `Ctrl+N` | A new window: the viewer once more, nothing open |
| `F10`, `Ctrl+,` | Settings |
| `F1` | This help, in a window of its own |
| `I`, `Ctrl+I` | The file's information (size, pixels, codec, EXIF), text you can mark and copy |
| `Shift+P` | Mini player |
| `Esc` | Quit |
| `Ctrl+Left` | Window narrower |
| `Ctrl+Right` | Window wider |
| `Ctrl+Up` | Window shorter |
| `Ctrl+Down` | Window taller |
| `Alt+Left` | Move the window left |
| `Alt+Right` | Move the window right |
| `Alt+Up` | Move the window up |
| `Alt+Down` | Move the window down |

**Files (always)**

| Keys | What it does |
|---|---|
| `O`, `Ctrl+O` | Open a file |
| `Ctrl+Shift+O` | Open a folder |
| `Ctrl+Shift+R` | The recent files in a list window: when each was opened, its size and folder; take files off the list |
| `E` | Adjust the picture |
| `Ctrl+E` | Export |
| `Ctrl+Shift+S` | Save a copy as |
| `Ctrl+B` | Convert many files |
| `Delete` | To the recycle bin; a page inside an archive is deleted from it (asks first) |
| `Shift+Delete` | Delete for good (asks first) |
| `Ctrl+Z` | Undo a move, copy or rename |
| `F2` | Rename, keeping the extension; a page inside an archive too |
| `Ctrl+Enter` | An Explorer window with this file picked out (the archive, for a page inside one) |
| `Ctrl+P` | Print the picture on screen: a panel for the turn, size, scale, place, margin and pictures a sheet, then the system's dialog |
| `F6` | Copy the file on screen to the folder set for it (a page inside an archive too) |
| `F7` | Move the file on screen to the folder set for it |
| `Shift+F6` | Copy the file on screen to a folder typed or browsed; one not there yet is made |
| `Shift+F7` | Move the file on screen to a folder typed or browsed; one not there yet is made |
| `K` | Classify: the nine folders in a window; set pictures aside for them, then copy or move them all at once |
| `Ctrl+C` | Copy the frame on screen (a film, an animation), or the picture itself |
| `Ctrl+Shift+E` | Save the frame on screen as a PNG beside the file |

**Moving about (always)**

| Keys | What it does |
|---|---|
| `PageDown`, `Space`, `Enter`, `.` | Next page |
| `PageUp`, `Backspace`, `Shift+Space`, `,` | Previous page |
| `Home`, `Ctrl+Home` | First page |
| `End`, `Ctrl+End` | Last page |
| `Shift+Right`, `Ctrl+PageDown` | Ten pages on |
| `Shift+Left`, `Ctrl+PageUp` | Ten pages back |
| `Ctrl+Backspace` | Up to the folder |
| `]` | Next folder or archive |
| `[` | Previous folder or archive |
| `F3` | The files in the archive (or the folder) in a window of their own, with a preview; a click or Enter goes to one |
| `F4` | Filmstrip |
| `P` | The playlist, or this folder's files, floating over the picture |
| `Shift+N` | Read the open archive's file names in the next code page (Shift-JIS, GBK, Big5, ...), for that archive only |

**Pictures (a picture, a comic's page, an animation, a film)**

| Keys | What it does |
|---|---|
| `1` | Fit to the window |
| `2` | Fit to the width |
| `3` | Fit to the height |
| `4` | Actual size (1:1) |
| `0` | Zoom back to 100%: the size the fit option gives, centred |
| `5` | Smart fit (shrink only) |
| `6` | Stretch to fill |
| `Shift+A` | Shape shown in: the file's own, 4:3, 16:9, cinema, paper ... (round) |
| `L` | Keep the fit for the next files |
| `+` | Zoom in |
| `-` | Zoom out |
| `Left` | A zoomed picture: see further left |
| `Right` | A zoomed picture: see further right |
| `Up` | A zoomed picture: see further up (the webtoon strip scrolls) |
| `Down` | A zoomed picture: see further down (the webtoon strip scrolls) |
| `R` | Rotate clockwise |
| `Shift+R` | Rotate anticlockwise |
| `H` | Flip left-right |
| `V` | Flip upside down |
| `N` | Crisp scaling for pixel art |
| `G` | Pixel grid past 400% |
| `B` | Layout: one page, two pages, book (the cover alone), webtoon (one long strip), comic (wide scans in halves) — the next of them; two pages, book and comic are each followed by the same read right to left; a tap on its button lists them to choose from |
| `Shift+B` | Detect two-page spreads |

**Pages inside one file (a TIFF or an ICO of several pages)**

| Keys | What it does |
|---|---|
| `.` | Next page of the file |
| `,` | Previous page of the file |

**Slide show (while one runs; S starts one anywhere)**

| Keys | What it does |
|---|---|
| `S`, `F5` | Start or stop the slide show |
| `Ctrl+]` | Faster slides (0.5 s) |
| `Ctrl+[` | Slower slides (0.5 s) |
| `Ctrl+Shift+]` | Faster slides (0.1 s) |
| `Ctrl+Shift+[` | Slower slides (0.1 s) |

**Playing (a film, music, an animation)**

| Keys | What it does |
|---|---|
| `Space` | Play / pause |
| `.` | One frame forward |
| `,` | One frame back |
| `Ctrl+]` | Faster (0.25x a step) |
| `Ctrl+[` | Slower (0.25x a step) |
| `Ctrl+\` | Normal speed |
| `Right` | 5 seconds on; its Toolbar button, held, offers 5 s, 10 s, 30 s, 1 min and 5 min to drag onto |
| `Left` | 5 seconds back; its Toolbar button, held, offers 5 s to 5 min the same way |
| `Shift+Right` | 30 seconds on |
| `Ctrl+Shift+Right` | 1 second on |
| `Ctrl+Shift+Left` | 1 second back |
| `Shift+Left` | 30 seconds back |
| `Up` | Volume up 5% |
| `Down` | Volume down 5% |
| `Shift+M` | Mute / sound |
| `Shift+[` | Repeat from here (A) |
| `Shift+]` | Repeat to here (B) |
| `\` | Repeat off |
| `Shift+\` | Repeat by number: the A-B box (Toolbar `Set A-B`), A = now and B = now take the moment playing |
| `Ctrl+R` | At the end of a film or a song: stop, the next file, this one again, all round, shuffle; a tap on its button lists them to choose from |

**Films**

| Keys | What it does |
|---|---|
| `Z` | Subtitles half a second earlier |
| `X` | Subtitles half a second later |
| `Shift+Z` | Subtitles a tenth of a second earlier |
| `Shift+X` | Subtitles a tenth of a second later |
| `Alt+Z` | Subtitles back to the file's own times |
| `A` | Next sound track; a tap on its button lists them to choose from |
| `C` | Next subtitles |
| `Shift+C` | The film's subtitles as a list in a window of their own: a click goes to one, the boxes choose, Earlier and Later move the chosen |

**Text (a text page)**

| Keys | What it does |
|---|---|
| `S`, `Space` | Auto-scroll a text file: start or stop |
| `Ctrl+]`, `Shift+Up` | Auto-scroll faster |
| `Ctrl+[`, `Shift+Down` | Auto-scroll slower |
| `Down` | Text: a line down (while auto-scrolling, faster) |
| `Up` | Text: a line up (while auto-scrolling, slower) |
| `PageDown` | Text: a screen down; at the end, the next file |
| `PageUp`, `Shift+Space` | Text: a screen up; at the start, the file before |
| `Home` | Text: to its start |
| `End` | Text: to its end |
| `+`, `Ctrl++` | Text: larger letters |
| `-`, `Ctrl+-` | Text: smaller letters |
| `Alt++` | Text: more space between lines |
| `Alt+-` | Text: less space between lines |
| `B` | Text: line breaks as they are, all in one run, blank lines dropped, or paragraphs, round; a tap on its button lists them to choose from |
| `Shift+B` | Text: remove or keep the indents |
| `W` | Text: wrap lines to the window, or not |
| `Ctrl+F` | Text: find |
| `Ctrl+G` | Text: find the next |
| `Ctrl+Shift+G` | Text: find the one before |
| `Shift+N` | Text: read the file in the next encoding; a tap on its button lists them to choose from |
| `Ctrl+C` | Text: copy what is selected |
| `Ctrl+A` | Text: select all |

**No key of its own: on the Toolbar or the right click**

| Where | What it does |
|---|---|
| Toolbar `Stop`, Right click `Stop` | Stop, and back to the beginning |
| Toolbar `1x` | The next playback speed; dragged up or down, or with the wheel over it, 0.1x at a time; a tap on its button lists them to choose from |
| Toolbar `A-B` | A-B repeat: from here, to here, off |
| Toolbar `EQ` | The equaliser's next preset; a tap on its button lists them to choose from |
| Toolbar `EQ bands`, Right click `Equaliser window` | The equaliser's ten bands, in a window over the picture |
| Toolbar `Night` | Night mode: loud passages brought down |
| Toolbar `Sub`, Right click `Subtitles on / off` | Subtitles on or off |
| Toolbar `100%` | Zoom round: real size (0%), 20%, 40% ... 200%, real size again; a tap on its button lists them to choose from |
| Toolbar `Fit` | Fit round: window, width, height, smart, stretch, window again; a tap on its button lists them to choose from |
| Toolbar `Viz` | The analyser: bars, the wave, or none; a tap on its button lists them to choose from |
| Toolbar `Centre` | The picture's middle on the window's, its size as it is (a double click does it too) |
| Toolbar `Wheel strip` | Thumbnails while the wheel turns pages |
| Toolbar `Edit` | Text: open the file in the editor Settings > Text names (Notepad without one) |
| Toolbar `Reload` | Text: read the file again, at the same place |
| Toolbar `Delay` | The slide show's delay: a tap lists 1, 2, 3, 5 and 10 seconds; the wheel over it, or a drag up or down, moves it half a second |
| Right click `Open` | Open what was right-clicked in the open-file screen |
| Right click `Look inside (a book)` | Look inside the book right-clicked |
| Right click `Add to the list`, Right click `Add them to the list` | Add what is picked to the list on screen |
| Right click `Copy the whole path`, Right click `Copy their paths` | Copy the paths of what is picked |
| Right click `Show in Explorer` | Show in Explorer what was right-clicked |
| Right click `Favourite (in or out)` | Make the file or folder that was right-clicked a favourite, or no longer one |
| Right click `Rename` | Rename the file or folder that was right-clicked |
| Right click `Change extensions...`, Right click `Change their extensions...` | Give the picked files new extensions, one for each extension they have |
| Right click `Copy to...`, Right click `Copy them to...` | Copy what is picked to a folder |
| Right click `Move to...`, Right click `Move them to...` | Move what is picked to a folder |
| Right click `To the recycle bin`, Right click `Them to the recycle bin` | Put what is picked in the recycle bin |
| Right click `Make a playlist of them` | Make a playlist of what is picked |
| Right click `Pick none` | Pick nothing in the open-file screen |
| Right click `Open` | A file of a list, right-clicked: open it |
| Right click `Copy the whole path` | A file of a list, right-clicked: its whole path to the clipboard |
| Right click `Show in Explorer` | A file of a list, right-clicked: shown selected in Explorer |
| Right click `Take out of the list` | A file of a list, right-clicked: out of the list (the file stays) |
| Right click `Delete the file...` | A file of a list, right-clicked: to the recycle bin, after a warning and a yes |
| Right click `Favourites...` | The favourites, files and folders, in a list window: open, reorder, remove |
| Right click `Add to favourites` | Make the file on screen a favourite, or no longer one |
| Right click `One page` | Layout: one page at a time |
| Right click `Two pages` | Layout: two pages side by side |
| Right click `Book` | Layout: the cover alone, then two pages side by side |
| Right click `Webtoon` | Layout: every page one under the other, one long strip |
| Right click `Choose subtitles` | Choose the subtitles from a list |
| Right click `Take out of film` | Write the text subtitles held inside a Matroska film as files beside it (`film.kor.srt`, `film.eng.ass`); the film is not changed |
| Right click `Slower` | Play slower |
| Right click `Faster` | Play faster |
| Title bar ☰ `Pin the title bar` | Keep the title bar shown |
| Title bar ☰ `Align the Floating Widget` | The Floating Widget back in the bottom-right corner, over the information bar when it is shown |

<!-- keys:end -->
