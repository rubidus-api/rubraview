# Rubraview

**Rubraview v0.0.15** (latest release) download — [rubraview-v0.0.15.zip (Windows 11 x64)](https://github.com/rubidus-api/rubraview/releases/download/v0.0.15/rubraview-v0.0.15.zip) · [all releases](https://github.com/rubidus-api/rubraview/releases)

**English** · [한국어](README.ko.md)

Lightweight, high-performance Windows desktop multimedia viewer, video player, and batch image processor built with pure C23 and WinAPI.

Unzip and run `rubraview-v0.0.15.exe`: there is no installer and nothing to place beside it. FFmpeg's DLLs are the one exception, and only for the formats Media Foundation cannot open.

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
avcodec-62.dll  avformat-62.dll  avutil-60.dll  swscale-9.dll  swresample-6.dll
```

Get them from [BtbN's Windows builds](https://github.com/BtbN/FFmpeg-Builds/releases) —
the file named `ffmpeg-n8.1-latest-win64-lgpl-shared-8.1.zip`. It must be
**8.1** and the name must say **shared**: a build without `shared` holds
only the programs, and 9.x names its DLLs differently, so Rubraview will
not use them. Copy the five files out of the zip's `bin` folder and put
them beside `rubraview-v0.0.15.exe`, then start Rubraview again. The
`lib` folder's `.dll.a` and `.lib` files are for building against FFmpeg,
not for running it — leave them.

Settings (`F10`) › Video says whether they were found, and repeats all of
this at the bottom of the page.

## Documentation

- [RFC-0001: Architecture & Multimedia Pipeline](docs/rfc/rfc-0001-rubraview-architecture.md)
- [RFC Index](docs/rfc/rfc-0000-index.md)
- [User Manual](docs/manual/)
- [Agent Operational Guidance](docs/agents/)

## Building & Testing

### Linux Host (Unit Tests)
```sh
make test
```

### Windows Binary (Cross-Build)
```sh
make win64
```

## License

MIT License. See [LICENSE](LICENSE) for details.
