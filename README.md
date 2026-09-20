# Rubraview

**Rubraview v0.0.4** (latest release) download — [rubraview-v0.0.4.zip (Windows 11 x64)](https://github.com/rubidus-api/rubraview/releases/download/v0.0.4/rubraview-v0.0.4.zip) · [all releases](https://github.com/rubidus-api/rubraview/releases)

**English** · [한국어](README.ko.md)

Lightweight, high-performance Windows desktop multimedia viewer, video player, and batch image processor built with pure C23 and WinAPI.

Unzip and run `rubraview-v0.0.4.exe`: there is no installer and nothing to place beside it. FFmpeg's DLLs are the one exception, and only for the formats Media Foundation cannot open.

> This English README is the canonical version; the Korean one is its translation.

## Overview

Rubraview is designed as a fast, bloat-free desktop tool that combines responsive media viewing with flexible non-destructive image adjustments and high-throughput batch operations.

- **GPU-Accelerated Canvas**: Butter-smooth 60–144 FPS pan and sub-pixel zoom powered by Direct2D (D2D1) and Direct3D 11.
- **Zero-Dependency Native Image Decoding**: Decodes JPEG, PNG, GIF, WebP, TIFF, BMP, and ICO out of the box using the native Windows Imaging Component (WIC).
- **Universal Multimedia Playback**: Smooth video decoding, frame-by-frame stepping, and still frame capture via a decoupled FFmpeg (`libavcodec`/`libavformat`) Platform Abstraction Layer.
- **Image Enhancement & Post-Processing**: Real-time adjustments for exposure, contrast, saturation, gamma, sharpening (unsharp mask), and gaussian blur, plus high-fidelity resampling (Nearest, Bilinear, Bicubic, Lanczos-3).
- **Batch Processing Engine**: Multi-threaded batch converter and resizer accessible via UI dialog and headless CLI.
- **Pure C23 Architecture**: Built upon vendored `proven_c_lib` arenas, string slices, and dynamic arrays with zero heap fragmentation.

## Stack & Architecture

- **Language**: Pure ISO C23 (`-std=c23`)
- **Presentation**: Win32 GUI, Direct2D 1.1+, DirectWrite, DXGI
- **Codecs**: Windows Imaging Component (WIC), FFmpeg (`libav*` dynamic bridge)
- **Base Library**: `proven_c_lib` (memory arenas, `u8str`, dynamic arrays)
- **Portability**: Headless core algorithms testable natively on Linux; Windows binary cross-compiled via MinGW-w64 on a Linux build host.

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
