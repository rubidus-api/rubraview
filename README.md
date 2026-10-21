[한국어](README.ko.md) | **English** — **Rubraview v1.32.0** — [Microsoft Store v1.0.0](https://apps.microsoft.com/detail/9n99b2x63k7x) · [ZIP(x64)](https://github.com/rubidus-api/rubraview/releases/download/v1.32.0/rubraview-v1.32.0.zip) · [MSI(x64)](https://github.com/rubidus-api/rubraview/releases/download/v1.32.0/rubraview-v1.32.0.msi) · [EXE only](https://github.com/rubidus-api/rubraview/releases/latest/download/rubraview.exe)

# Rubraview

Lightweight, high-performance Windows desktop multimedia viewer, video player, and batch image processor built with pure C23 and WinAPI.

Unzip and run `rubraview.exe` — or download `rubraview.exe` alone and run it: nothing needs installing. Or get it from the [Microsoft Store](https://apps.microsoft.com/detail/9n99b2x63k7x) (version 1.0.0 there for now), which installs it and keeps it up to date. The MSI is for those who want it installed: Program Files, the Start menu, and Explorer's right-click menus (Windows 11's own among them) registered for every user. FFmpeg's DLLs are optional, beside it, only for the formats Media Foundation cannot open. Comic archives — CBZ / ZIP, CB7 / 7z and CBR / RAR (RAR 2.0 to 7.0), solid, split or password-protected — are read by FultaArc (MIT, no UnRAR code), which Rubraview carries in its source. A page inside an archive can be deleted (`Delete`), renamed (`F2`) or saved after an edit: a ZIP / CBZ or a 7z / CB7 keeps its format, anything else (a solid 7z, RAR / CBR, ALZ, EGG, a split set) is written again as a ZIP or a 7z of your choice and the old file goes to the Recycle Bin. Encrypted archives are not changed.

> This English README is the canonical version; the Korean one is its translation.

## Screenshots

|  |  |
|---|---|
| <img src="docs/manual/images/screenshot-1-picture.png" alt="A picture and the filmstrip" width="420"> | <img src="docs/manual/images/screenshot-2-comic.png" alt="A comic, two pages" width="420"> |
| A picture and the filmstrip | A comic, two pages |
| <img src="docs/manual/images/screenshot-3-video.png" alt="A film with subtitles" width="420"> | <img src="docs/manual/images/screenshot-4-picker.png" alt="The open-file screen" width="420"> |
| A film with subtitles | The open-file screen |
| <img src="docs/manual/images/screenshot-5-info.png" alt="The information window" width="420"> | <img src="docs/manual/images/screenshot-6-settings.png" alt="Settings" width="420"> |
| The information window | Settings |

The pictures, the comic and the film in these screenshots were generated for them.

## Overview

Rubraview is designed as a fast, bloat-free desktop tool that combines responsive media viewing with flexible non-destructive image adjustments and high-throughput batch operations.

- **GPU-Accelerated Canvas**: Butter-smooth 60–144 FPS pan and sub-pixel zoom powered by Direct2D (D2D1) and Direct3D 11.
- **Zero-Dependency Native Image Decoding**: Decodes JPEG, PNG, GIF, WebP, TIFF, BMP, and ICO out of the box using the native Windows Imaging Component (WIC).
- **AVIF Without A Codec To Install**: a built-in decoder (libavif + dav1d) reads AVIF stills and animations with their transparency; JPEG XL opens where Windows has a codec for it, or with FFmpeg's DLLs beside the program (a moving one plays through FFmpeg). BigTIFF under 4 GB opens as any TIFF.
- **WebP Written Without A Codec To Install**: Export and `--batch --format=webp` write lossy or lossless WebP with the built-in libwebp; an animated GIF, PNG or WebP becomes an animated WebP.
- **Universal Multimedia Playback**: Smooth video decoding, frame-by-frame stepping, and still frame capture via a decoupled FFmpeg (`libavcodec`/`libavformat`) Platform Abstraction Layer.
- **Edit As Steps On A Selection**: a rectangle or an ellipse (with a soft edge) dragged, sized and typed; tones, colour balance, automatic colour, 21 effects, crop and resize as steps on it, undone and redone until saved; save over the original (to the Recycle Bin) or a copy.
- **Resize Your Way**: percent, pixels, or a length in cm, mm or inches at a DPI; HD to 8K, long side, megapixels; in linear light; sharpen after (Nearest, Bilinear, Bicubic, Lanczos-3).
- **Batch Processing Engine**: multi-threaded, from a window or the command line: sizes, crop, cut borders, automatic colour, any effect, canvas, border, watermark, names with counters and replacing, dates kept; the choices tried, remembered, saved and loaded.
- **Classify**: the nine sorting folders in a window; set pictures aside, then copy or move them all at once.
- **Everything About A Picture**: EXIF in full and IPTC, aspect ratio, print size, unique colours, decode time.
- **Text Files, For Reading**: `.txt`, `.log`, `.nfo` and `.md` open as text, in Korean, Japanese and Chinese encodings guessed or chosen; line breaks as they are, all in one run, blank lines dropped, or joined into paragraphs; indents kept or removed; auto-scroll whose speed changes while it runs; the size and the line spacing a key or a wheel away; find, select and copy.
- **In Your Language**: English, Korean, Japanese, Chinese (simplified and traditional) and Persian, following Windows or chosen in Settings › General › Language; a language is a text file in the `lang` folder beside the program, so one can be corrected or added without building anything.
- **Pure C23 Architecture**: Built upon vendored `proven_c_lib` arenas, string slices, and dynamic arrays with zero heap fragmentation.

## Keyboard shortcuts

`F1` opens this same list in a window of its own, which stays open while
you read. Every key here can be changed on the settings window's Keys
page (`F10`), and the table below is generated from the keys the viewer
ships with, so it cannot drift from them.

<!-- keys:begin — generated from src/core/default_keymap.c by scripts/check-actions.py --write; do not edit -->

**Everywhere**

| Keys | What it does |
|---|---|
| `F`, `F11`, `Alt+Enter` | Full screen |
| `T` | Open or close the Toolbar |
| `Shift+T` | Pin the Toolbar open |
| `Ctrl+T` | Give the Toolbar a window of its own, or dock it |
| `Ctrl+Shift+T` | Always on top of other windows |
| `O`, `Ctrl+O` | Open a file |
| `Ctrl+Shift+O` | Open a folder |
| `F4` | Filmstrip |
| `Shift+I` | Information bar |
| `E` | Adjust the picture |
| `Ctrl+E` | Export |
| `Ctrl+Shift+S` | Save a copy as |
| `Ctrl+C` | Copy the frame on screen (a film, an animation), or the picture itself |
| `Ctrl+Shift+E` | Save the frame on screen as a PNG beside the file |
| `Ctrl+B` | Convert many files |
| `Ctrl+N` | A new window: the viewer once more, nothing open |
| `F10`, `Ctrl+,` | Settings |
| `F1` | This help, in a window of its own |
| `I`, `Ctrl+I` | The file's information (size, pixels, codec, EXIF), text you can mark and copy |
| `P` | The playlist, or this folder's files, floating over the picture |
| `Ctrl+R` | At the end of a film or a song: stop, the next file, this one again, all round, shuffle; a tap on its button lists them to choose from |
| `Shift+P` | Mini player |
| `G` | Pixel grid past 400% |
| `Esc` | Quit |
| `Delete` | To the recycle bin; a page inside an archive is deleted from it (asks first) |
| `Shift+Delete` | Delete for good (asks first) |
| `Ctrl+Z` | Undo a move, copy or rename |
| `F2` | Rename, keeping the extension; a page inside an archive too |
| `Ctrl+Enter` | An Explorer window with this file picked out (the archive, for a page inside one) |
| `Ctrl+P` | Print the picture on screen: a panel for the turn, size, scale, place, margin and pictures a sheet, then the system's dialog |
| `F3` | The files in the archive (or the folder) in a window of their own, with a preview; a click or Enter goes to one |
| `F6` | Copy the file on screen to the folder set for it (a page inside an archive too) |
| `K` | Classify: the nine folders in a window; set pictures aside for them, then copy or move them all at once |
| `Ctrl+Shift+R` | The recent files in a list window: when each was opened, its size and folder; take files off the list |
| `F7` | Move the file on screen to the folder set for it |
| `Shift+F6` | Copy the file on screen to a folder typed or browsed; one not there yet is made |
| `Shift+F7` | Move the file on screen to a folder typed or browsed; one not there yet is made |
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
| `PageDown`, `Space`, `Enter`, `.` | Next page |
| `PageUp`, `Backspace`, `Shift+Space`, `,` | Previous page |
| `Home`, `Ctrl+Home` | First page |
| `End`, `Ctrl+End` | Last page |
| `Shift+Right`, `Ctrl+PageDown` | Ten pages on |
| `Shift+Left`, `Ctrl+PageUp` | Ten pages back |
| `Ctrl+Backspace` | Up to the folder |
| `B` | Layout: one page, two pages, book (the cover alone), webtoon (one long strip), comic (wide scans in halves) — the next of them; two pages, book and comic are each followed by the same read right to left; a tap on its button lists them to choose from |
| `Shift+B` | Detect two-page spreads |
| `Ctrl+]` | Next archive in the folder |
| `Ctrl+[` | Previous archive in the folder |

**The view**

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
| `Right` | 5 seconds on; its Toolbar button, held, offers 5 s, 10 s, 30 s, 1 min and 5 min to drag onto |
| `Left` | 5 seconds back; its Toolbar button, held, offers 5 s to 5 min the same way |
| `Shift+Right` | 30 seconds on |
| `Ctrl+Shift+Right` | 1 second on |
| `Ctrl+Shift+Left` | 1 second back |
| `Shift+Left` | 30 seconds back |
| `Up` | Volume up 5% |
| `Down` | Volume down 5% |
| `Shift+M` | Mute / sound |
| `[` | Repeat from here (A) |
| `]` | Repeat to here (B) |
| `\` | Repeat off |
| `Shift+\` | Repeat by number: the A-B box (Toolbar `Set A-B`), A = now and B = now take the moment playing |
| `Ctrl+\` | Normal speed |
| `Z` | Subtitles half a second earlier |
| `X` | Subtitles half a second later |
| `Shift+Z` | Subtitles a tenth of a second earlier |
| `Shift+X` | Subtitles a tenth of a second later |
| `Alt+Z` | Subtitles back to the file's own times |
| `A` | Next sound track; a tap on its button lists them to choose from |
| `C` | Next subtitles |
| `Shift+C` | The film's subtitles as a list in a window of their own: a click goes to one, the boxes choose, Earlier and Later move the chosen |

**A multi-page TIFF or ICO**

| Keys | What it does |
|---|---|
| `.` | Next page of the file |
| `,` | Previous page of the file |

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

## Mouse and touch

| Do this | And this happens |
|---|---|
| Click the left or right part of the window | The file before or the next one, in every layout (right to left too) |
| Click the middle | Nothing |
| Double-click the picture | Its middle back on the window's |
| Hold the left button still for a moment | The right-click list |
| Hold the left button and drag | Moves a picture zoomed past the window, and the webtoon strip |
| Hold a Toolbar back / forward button | Five steps open over it — 5 s, 10 s, 30 s, 1 min, 5 min; drag onto one and let go (a tap seeks 5 s) |
| Click the Toolbar's seek bar | Goes there, in a film, a song or an animated GIF / WebP / PNG |
| Middle click | Actual size, and back to fit |
| The mouse's back / forward buttons | Previous / next page |
| Click on a film or on music | Pause / play |
| Wheel over a picture, `Ctrl` + wheel, or the wheel over the zoom button | Zoom in or out, 10 % a notch |
| Click the zoom button | Real size (0%), 20% to 200% by twenties, real size again |
| Wheel over a Toolbar button that chooses one of several | The one before or after |
| Wheel over the Toolbar's bar | The size of file names and messages |
| Wheel while a film or music plays | Volume |
| `Shift` + wheel | Ten pages back or forward |
| Wheel over the filmstrip | Goes through its thumbnails; click one to go to it |
| Floating Widget **F**: click, drag, wheel, `Alt` + wheel | Opens or folds the Toolbar (never pinned); moves the widget; buttons a row; see-through |
| Floating Widget **T**: point, drag, wheel | Opens the Toolbar while pointed at; up or down through the files, sideways through the folders or archives; button size |
| Point at the window's top edge | The title bar: ☰ (its list), the name (the open-file screen), **Set P H T F _ [] R X**; drag it to move the window, double-click it to maximise |
| Point near the middle of the left or right side | A button for the previous or next page |
| Drop files or a folder on the window | Opens them |
| Touch: drag, pinch | Moves the picture, zooms it |
| Touch: a drag sideways over a fitted page | Turns the page |

## Keys in the other windows

These belong to their window and are not on the Keys page.

| Where | Keys |
|---|---|
| The file picker (`O`) | Arrows move; `Enter` opens (`Shift+Enter` shows an archive's pages as tiles); `Backspace` goes up a folder; a letter or a digit jumps to the next name starting with it; `Ctrl+L` types a path; `Ctrl+D` stars the folder; `Esc` closes. Right-click an archive for its pages, a starred folder to rename it |
| The file list window (`F3`) | `Up` / `Down`, `PageUp` / `PageDown`, `Home` / `End` choose a file and preview it; `Enter` or a click turns the viewer to it; `Esc` closes; every other key does what it does in the viewer |
| Settings (`F10`) | `Up` / `Down` move; `Left` / `Right` change a value (`PageUp` / `PageDown` by ten steps); `Enter` or `Space` switches or presses; `Tab` / `Shift+Tab` change the page; `Esc` closes |
| Help (`F1`), information (`I`) | `Up` / `Down`, `PageUp` / `PageDown`, `Home` / `End` scroll; `Esc` closes |
| The playlist (`P`) | A click goes to a file, the wheel scrolls, `Esc` closes |
| Rename (`F2`), a typed path, a password | `Enter` accepts, `Esc` gives up |

## Layouts

`B`, the Toolbar's Layout button and a book's right click choose one of eight, and Settings > Viewer > Layout keeps it:
**single** (one page), **dual** (two side by side), **book** (the cover alone, then two), **webtoon** (every page one
under the other at one width, read as one long strip) and **comic** (one page, a wide scan as its two halves) — and
**dual-rtl**, **book-rtl** and **comic-rtl**, each right after its own, read right to left as a Japanese comic is: the
earlier page on the right, a wide scan's right half first, clicks and swipes turning the other way.

## Explorer's right-click menu

Settings > Explorer registers a **Rubraview** item on the extensions you switch on — Open, Open these only, Add to the
list, Browse the archive, Convert..., Print... — each working on one file or on many selected together. For this user
it is the classic menu ("Show more options" on Windows 11); "for every user" (an administrator, once) also puts it in
Windows 11's own menu, using `rubraview_menu.dll` and `rubraview_menu.msix` from the zip (they must stay beside `rubraview.exe`). The MSI does that registration when it installs. Plain archives (ZIP, 7z, RAR,
ALZ, EGG) get the menu without becoming Rubraview's to open by double click.

## Command line

| Run | For |
|---|---|
| `rubraview.exe FILE` (or a folder, or an archive) | Opens it. With a window already open the file goes to it: added to the end of its list and shown (Settings > General > Opening a file from Explorer changes this) |
| `--sort=natural\|lexical\|modified\|created\|size\|random`, `--descending`, `--ascending` | The order of this run's folders, over the settings |
| `--recursive` (without `--batch`) | Subfolders are read into the same sequence |
| `--new-instance`, `--new-window FILES...` | A window of its own whatever the settings say |
| `--register-shell`, `--unregister-shell` | Makes Windows open files with Rubraview and puts Rubraview in Explorer's right-click menu, or takes that back. `--types=jpg,png,comics,...` (extensions, or `pictures`, `comics`, `video`, `music`, `archives`, `all`) for some only; `--no-types` / `--no-menu` leave one of the two as it is; `--all-users` from an administrator's prompt for every user (and Windows 11's own menu); `--setup` is the installer's (it ends with 0 when only Windows 11's menu could not be registered). The same is in Settings > Explorer |
| `--wipe-shell` | Removes every Rubraview entry from Explorer's right-click menu and the file types, those an older version or another copy registered too. With `--all-users`, from an administrator's prompt, for every user. |
| `--open`, `--new-window`, `--open-only`, `--add`, `--browse`, `--convert`, `--print` `FILES...` | What the right-click menu's items run: open; open in a new window; open only these; add to the open window's list; an archive's entries; the batch panel on these; print |
| `--batch ...` | Converts many files without a window; the [manual](docs/manual/rubraview-manual.md) lists its options |
| `--version` | The version |

## Stack & Architecture

- **Language**: Pure ISO C23 (`-std=c23`)
- **Presentation**: Win32 GUI, Direct2D 1.1+, DirectWrite, DXGI
- **Codecs**: Windows Imaging Component (WIC), FFmpeg (`libav*` dynamic bridge)
- **Base Library**: `proven_c_lib` (memory arenas, `u8str`, dynamic arrays)
- **Portability**: Headless core algorithms testable natively on Linux; Windows binary cross-compiled via MinGW-w64 on a Linux build host.

## FFmpeg (optional)

Rubraview plays most files with Windows' own decoders. FFmpeg is only for
what they cannot open, and it is **five DLLs, not `ffmpeg.exe`**:

```
avcodec-63.dll  avformat-63.dll  avutil-61.dll  swscale-10.dll  swresample-7.dll
```

Get them from [BtbN's Windows builds](https://github.com/BtbN/FFmpeg-Builds/releases) —
the file named `ffmpeg-n9.0-latest-win64-lgpl-shared-9.0.zip`. It must be
**9.0** and the name must say **shared**: a build without `shared` holds
only the programs, and another version names its DLLs differently, so
Rubraview will not use them. Copy the five files out of the zip's `bin` folder and put
them beside `rubraview.exe`, then start Rubraview again. The
`lib` folder's `.dll.a` and `.lib` files are for building against FFmpeg,
not for running it — leave them.

Or let Rubraview put them in place: Settings (`F10`) › Video ›
**Import FFmpeg's DLLs...** asks for that `bin` folder and copies the five
beside `rubraview.exe`, or, where that folder takes no files (the Microsoft
Store's copy, Program Files), into the settings folder, where Rubraview
reads them too; they are used at once. Or leave them in a folder of your
own and name it in **FFmpeg folder** on the same page.

Settings › Video says whether they were found, and repeats all of this at
the bottom of the page.

## Documentation

- [User Manual](docs/manual/)

## Building

### Windows Binary (Cross-Build)
```sh
make win64
```

## License

MIT License. See [LICENSE](LICENSE) for details.
