[한국어](README.ko.md) | **English** — **Rubraview v0.0.40** — [ZIP(x64)](https://github.com/rubidus-api/rubraview/releases/download/v0.0.40/rubraview-v0.0.40.zip) · [EXE only](https://github.com/rubidus-api/rubraview/releases/latest/download/rubraview.exe)

# Rubraview

Lightweight, high-performance Windows desktop multimedia viewer, video player, and batch image processor built with pure C23 and WinAPI.

Unzip and run `rubraview.exe` — or download `rubraview.exe` alone and run it: there is no installer. FFmpeg's DLLs are optional, beside it, only for the formats Media Foundation cannot open. Comic archives — CBZ / ZIP, CB7 / 7z and CBR / RAR (RAR 2.0 to 7.0), solid, split or password-protected — are read by FultaArc (MIT, no UnRAR code), which Rubraview carries in its source.

> This English README is the canonical version; the Korean one is its translation.

## Overview

Rubraview is designed as a fast, bloat-free desktop tool that combines responsive media viewing with flexible non-destructive image adjustments and high-throughput batch operations.

- **GPU-Accelerated Canvas**: Butter-smooth 60–144 FPS pan and sub-pixel zoom powered by Direct2D (D2D1) and Direct3D 11.
- **Zero-Dependency Native Image Decoding**: Decodes JPEG, PNG, GIF, WebP, TIFF, BMP, and ICO out of the box using the native Windows Imaging Component (WIC).
- **Universal Multimedia Playback**: Smooth video decoding, frame-by-frame stepping, and still frame capture via a decoupled FFmpeg (`libavcodec`/`libavformat`) Platform Abstraction Layer.
- **Image Enhancement & Post-Processing**: Real-time adjustments for exposure, contrast, saturation, gamma, sharpening (unsharp mask), and gaussian blur, plus high-fidelity resampling (Nearest, Bilinear, Bicubic, Lanczos-3).
- **Batch Processing Engine**: Multi-threaded batch converter and resizer accessible via UI dialog and headless CLI.
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
| `Ctrl+Enter` | An Explorer window with this file picked out (the archive, for a page inside one) |
| `F3` | The files in the archive (or the folder) in a window of their own, with a preview; a click or Enter goes to one |
| `F6` | Copy the file on screen to the folder set for it (a page inside an archive too) |
| `F7` | Move the file on screen to the folder set for it |
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
| `B` | Layout: one page, two pages, book (the cover alone), webtoon (one long strip), comic (wide scans in halves) — the next of them |
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
| `Right` | 5 seconds on; its toolbox button, held, offers 5 s, 10 s, 30 s, 1 min and 5 min to drag onto |
| `Left` | 5 seconds back; its toolbox button, held, offers 5 s to 5 min the same way |
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

**No key of its own: in the toolbox or the menu**

| Where | What it does |
|---|---|
| Toolbox `Stop`, Menu `Stop` | Stop, and back to the beginning |
| Toolbox `Sub`, Menu `On/off` | Subtitles on or off |
| Toolbox `A-B` | A-B repeat: from here, to here, off |
| Toolbox `1x` | The next playback speed |
| Toolbox `EQ`, Menu `Equaliser` | The equaliser's next preset |
| Toolbox `EQ bands`, Menu `Equaliser window` | The equaliser's ten bands, in a window over the picture |
| Toolbox `Night`, Menu `Night mode` | Night mode: loud passages brought down |
| Toolbox `Viz`, Menu `Analyser` | The analyser: bars, the wave, or none |
| Menu `Auto` | Read the open archive's file names in the code page that fits them |
| Menu `UTF-8` | Read the open archive's file names as UTF-8 |
| Menu `Korean` | Read the open archive's file names as Korean (CP949) |
| Menu `Japanese (Shift-JIS)` | Read the open archive's file names as Japanese (Shift-JIS) |
| Menu `Chinese (GBK)` | Read the open archive's file names as Chinese (GBK) |
| Menu `Chinese (Big5)` | Read the open archive's file names as Chinese (Big5) |
| Menu `Western` | Read the open archive's file names as Western (CP1252) |
| Menu `Single` | Layout: one page at a time |
| Menu `Dual` | Layout: two pages side by side |
| Menu `Book` | Layout: the cover alone, then two pages side by side |
| Menu `Webtoon` | Layout: every page one under the other, one long strip |
| Menu `Comic` | Layout: one page at a time, a wide scan as its two halves |
| Menu `Slower` | Play slower |
| Menu `Faster` | Play faster |
| Menu `Choose` | Choose the subtitles from a list |
| Menu `100%` | The floating boxes fully opaque |
| Menu `80%` | The floating boxes 80 % opaque |
| Menu `60%` | The floating boxes 60 % opaque |
| Menu `40%` | The floating boxes 40 % opaque |
| Menu `Change keys` | Settings, opened on its Keys page |
| Menu `About` | About Rubraview: the version and the licences of what it carries |

<!-- keys:end -->

## Mouse and touch

| Do this | And this happens |
|---|---|
| Click the left or right third of the window | Previous or next page, when a folder or an archive is open (the sides swap when reading right to left) |
| Click the middle (or anywhere on a single picture) | The toolbox opens or closes |
| Hold the left button and drag | Moves a picture zoomed past the window, and the webtoon strip |
| Hold a toolbox back / forward button | Five steps open over it — 5 s, 10 s, 30 s, 1 min, 5 min; drag onto one and let go (a tap seeks 5 s) |
| Click the toolbox's seek bar | Goes there, in a film, a song or an animated GIF / WebP / PNG |
| Middle click | Actual size, and back to fit |
| The mouse's back / forward buttons | Previous / next page |
| `Ctrl` + wheel | Zoom in or out |
| `Shift` + wheel | Ten pages back or forward |
| Wheel | Scrolls the webtoon strip, and a page shown at Fit Width; otherwise nothing — pages turn on the keys, not the wheel |
| `Alt` + wheel over a floating box | Makes that box more or less see-through |
| Point at the window's top edge | The title bar: drag it to move the window, double-click it to maximise, **Size** to resize, **Pin** for always on top, **Box** to bring the floating boxes back to their corners |
| Point near the middle of the left or right side | A button for the previous or next page |
| Drop files or a folder on the window | Opens them |
| Touch: drag, pinch | Moves the picture, zooms it |

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

`B`, the toolbox's Layout button and Menu > View > Layout choose one of five, and Settings > Viewer > Layout keeps it:
**single** (one page), **dual** (two side by side), **book** (the cover alone, then two), **webtoon** (every page one
under the other at one width, read as one long strip) and **comic** (one page, a wide scan as its two halves).

## Command line

| Run | For |
|---|---|
| `rubraview.exe FILE` (or a folder, or an archive) | Opens it; a second launch hands the file to the window already open |
| `--new-instance` | A second window all the same |
| `--register-shell`, `--unregister-shell` | Makes Windows open files with Rubraview, or takes that back. `--types=pictures,comics,video,music` for some kinds only; `--all-users` from an administrator's prompt for every user. The same is in Settings > General |
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

Settings (`F10`) › Video says whether they were found, and repeats all of
this at the bottom of the page.

## Documentation

- [RFC-0001: Architecture & Multimedia Pipeline](docs/rfc/rfc-0001-rubraview-architecture.md)
- [RFC Index](docs/rfc/rfc-0000-index.md)
- [User Manual](docs/manual/)

## Building

### Windows Binary (Cross-Build)
```sh
make win64
```

## License

MIT License. See [LICENSE](LICENSE) for details.
