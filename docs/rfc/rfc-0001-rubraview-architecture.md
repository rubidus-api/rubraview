# RFC-0001: Rubraview Architecture and Multimedia Pipeline

- Status: Accepted
- Author: Antigravity Agent
- Date: 2026-09-07
- Revised: 2026-09-08 (§8.3 marked against the tree, §11 rewritten; D-1..D-7 decided by the owner the same day — §5.2, §8.1, §11.4 updated)
- Target Stack: C23, WinAPI, Direct2D, WIC, FFmpeg, proven_c_lib
- Distribution: Windows x86_64 Desktop Executable (`rubraview.exe`)

---

## 1. Executive Summary & Problem Scope

Modern Windows image and media viewers often suffer from two extremes:
1. **Bloated Electron/C++ frameworks**: Excessive memory footprints (hundreds of megabytes), sluggish startup times, and complex dependency graphs.
2. **Outdated legacy GDI viewers**: CPU-bound software rendering (`StretchBlt`) causing jittery 20-30 FPS zooming and panning on 4K/8K displays, lack of modern codec support, and brittle monolithic codebases.

**Rubraview** (`rubraview`) resolves these issues by delivering a lightweight, zero-bloat, high-performance multimedia viewer, comic reader, and batch image processing tool designed with:
- **Pure C23 Foundation**: Structured upon memory arenas, string slices, and dynamic arrays provided by a vendored snapshot of `proven_c_lib`.
- **Zero-Dependency Native Image Subsystem**: Leveraging the **Windows Imaging Component (WIC)** for decoding/encoding JPEG, PNG, WebP, GIF, TIFF, BMP, and ICO without linking external image libraries (`libpng`, `libjpeg`, etc.).
- **Hardware-Accelerated Canvas**: Utilizing **Direct2D (D2D1) / Direct3D 11** for butter-smooth 60–144 FPS pan, sub-pixel zoom, and real-time shader-based image adjustments (exposure, contrast, saturation, blur, sharpen).
- **Universal Multimedia Engine**: An isolated Platform Abstraction Layer (PAL) with an **FFmpeg C API (`libavcodec`, `libavformat`, `libswscale`)** dynamic bridge, providing playback, frame-accurate seeking, and frame capture across any video format.
- **Touch-Friendly & Remote-Desktop Optimized UI**: High-contrast, Metro-style square tile controls designed for effortless thumb tapping on mobile devices over Remote Desktop (RDP), free of laggy animations.
- **Direct Comic Archive Ingestion**: Seamless streaming of `.cbz`, `.cbr`, and `.cb7` archives directly into memory without disk extraction.
- **Intelligent Aspect-Ratio & Spread Adaptation**: Context-aware book/dual-page engine that intelligently detects pre-merged 2-page scans and automatically adapts between landscape and portrait window orientations.
- **Dual-Mode Headless Core**: Image filters, geometric transformations, and batch pipelines exist in portable C23 modules that run and test natively on Linux hosts while compiling to native Windows binaries via MinGW-w64 on the `linux-build` build environment.

---

## 2. Layered Architectural Boundaries

Rubraview is strictly organized into four decoupled layers:

```
+-------------------------------------------------------------------------+
|                       Application Layer (GUI / CLI)                     |
|  - Win32 Window Procedure (WndProc), Raw Message Pump                   |
|  - Metro-Style Square Tile Touch UI (Large Finger-Tappable Controls)    |
|  - Keyboard-First Command Dispatcher (100% Keyboard Operable)           |
|  - Multi-touch Gestures (Pinch-to-zoom, Drag-to-pan via WM_GESTURE)     |
|  - Modal Dialogs (Batch Processor, Settings, Exif Inspector, Playlist)  |
|  - Headless CLI Driver (`rubraview.exe --batch ...`)                    |
+-------------------------------------------------------------------------+
                                   |
                                   v
+-------------------------------------------------------------------------+
|                  Presentation & Rendering Layer (Direct2D)              |
|  - ID2D1Factory, ID2D1HwndRenderTarget / ID2D1DeviceContext             |
|  - Sub-pixel Pan/Zoom Viewport Matrix Transformations (Fixed Window)    |
|  - Intelligent Multi-Page Compositor (Single, Dual, Book / Manga)       |
|  - Direct2D Built-in Effect Graph (Real-time Shader Adjustments)        |
|  - DirectWrite Font Rendering for On-Screen Display (OSD) & Exif Info   |
+-------------------------------------------------------------------------+
                                   |
                                   v
+-------------------------------------------------------------------------+
|                  Platform Abstraction Layer (PAL)                       |
|  - `rubraview_image_io`: WIC Decoder/Encoder (Windows) / Portable Host Stubs   |
|  - `rubraview_video_io`: FFmpeg Video Bridge (libav* dynamic loader)          |
|  - `rubraview_audio_io`: WASAPI Audio Renderer (Windows) / Host Mock Sink     |
|  - `rubraview_archive_io`: In-memory CBZ (ZIP), CBR (RAR), CB7 (7z) Streams    |
|  - `rubraview_sysio`: Native directory enumeration & natural alphanumeric sort |
|  - `rubraview_threadpool`: Pre-caching worker threads & batch job dispatching  |
+-------------------------------------------------------------------------+
                                   |
                                   v
+-------------------------------------------------------------------------+
|                  Core Engine (Pure C23, Portable)                       |
|  - Pixel Buffers (`rubraview_pixbuf_t`): 8-bit RGBA, 16-bit Float, Grayscale    |
|  - Color Space Math: sRGB <-> Linear, HSL, Exposure, Contrast, Gamma    |
|  - Spatial Filters: Gaussian Blur, Laplacian Sharpen, 3x3/5x5 Kernel    |
|  - Resampling Kernels: Nearest, Bilinear, Bicubic (Catmull-Rom), Lanczos|
|  - Layout Engine (`rubraview_layout_engine`): Intelligent Spread & AR Matcher  |
|  - Batch Execution Engine: Job Queue, Worker Scheduling, Multi-file I/O |
|  - Playlist & Collection Manager: Mixed Media Sequences, RVLIST / M3U8  |
+-------------------------------------------------------------------------+
                                   |
                                   v
+-------------------------------------------------------------------------+
|                    Base Foundation (proven_c_lib)                       |
|  - Arena Allocators (`prv_arena_t`) & Scratch Pools                     |
|  - UTF-8 String Slices (`u8str_t`) & Formatting                         |
|  - Dynamic Arrays (`prv_dynarray_t`) & Sorting / Binary Search          |
+-------------------------------------------------------------------------+
```

### Invariants:
1. **Core Independence**: Code inside `src/core/` MUST NOT include `<windows.h>`, `<d2d1.h>`, or any OS-specific header. It must compile on both Linux `gcc`/`clang` and Windows MinGW-w64.
2. **Window Stability Invariant**: The desktop window size is strictly owned and controlled by the user or OS window manager. Loading an image or altering zoom levels **MUST NEVER change the window dimensions**.
3. **Codec Decoupling**: The GUI layer interacts only with abstract pixel buffers (`rubraview_pixbuf_t`), video streams (`rubraview_video_stream_t`), and archive streams (`rubraview_archive_t`).
4. **Memory Ownership**: All temporary buffers allocated during single-frame rendering, archive decompression, or batch conversions are managed via `prv_arena_t` instances, ensuring zero heap fragmentation and deterministic teardown.
5. **Zero-Margin Chrome Invariant**: The main application window operates strictly without permanent OS titlebars or window border outlines (`WM_NCCALCSIZE`). 100% of the window surface is dedicated to media rendering, with an auto-hiding hover titlebar appearing only when the cursor approaches the top edge.

---

## 3. Detailed Functional Specifications & Viewing Modes

This section defines the user-facing and processing features of Rubraview.

### 3.1 Image Viewing & Navigation Subsystem
- **Interactive Viewport**:
  - Continuous drag panning when an image is zoomed in.
  - Sub-pixel zoom centered precisely at mouse cursor position or multi-touch pinch centroid.
  - Smooth 60–144 Hz refresh rate without screen tearing.
- **File & Directory Traversal**:
  - Automatically indexes sibling media files when an image is opened.
  - Natural alphanumeric sorting (e.g. `img1.jpg`, `img2.jpg`, `img10.jpg`), sort by date modified, or sort by file size.
- **Asynchronous Pre-caching Pipeline**:
  - Background worker thread decodes the next 2 images and previous 1 image into memory arenas ahead of time.
  - Page-flip navigation operates with instantaneous 0ms perceived latency.
- **Filmstrip & Thumbnail Bar**:
  - Collapsible bottom filmstrip showing thumbnails extracted asynchronously via WIC low-resolution decoders.
- **On-Screen Display (OSD)**:
  - Non-intrusive DirectWrite overlay displaying file name, resolution, file size, zoom percentage, current index / total count, and color bit-depth. Automatically fades out after 2 seconds of inactivity.

### 3.2 Advanced Slide Show & Sequence Playback Engine
Rubraview features a high-precision, highly configurable slide show engine suitable for both relaxed photo presentations and high-speed rapid frame inspection.

#### 3.2.1 High-Resolution Sub-Second Timer (0.1s Granularity)
- **Granular Interval Control**:
  - Configurable advance delay from **0.1 seconds to 300.0 seconds** in exact **0.1-second increments** (e.g. 0.1s, 0.2s, 0.5s, 1.0s, 3.0s, 5.0s).
  - Hotkeys: `[` / `]` adjust interval by $\pm 0.5\text{s}$; `Shift + [` / `Shift + ]` adjust interval by $\pm 0.1\text{s}$.
- **Hardware Timer Precision**:
  - Implemented via Windows Multimedia Timers (`CreateTimerQueueTimer` or high-resolution `QueryPerformanceCounter` polling) to avoid standard Win32 `WM_TIMER` jitter (~15.6ms inaccuracy), ensuring precise 10.0 FPS pacing at the 0.1s interval.
- **High-Speed Stress Caching**:
  - When the interval is set below 0.5s (up to 10 FPS), the pre-caching engine expands its lookahead ring buffer to 6–10 frames in RAM arenas (`prv_arena_t`), preventing disk I/O bottlenecks and stutter.

#### 3.2.2 Ingestion Scopes & Source Selection
The slide show can operate over diverse file targets:
1. **Current Folder (Default)**: Automatically includes all viewable media files in the active directory.
2. **Recursive Subfolder Traversal**: Optionally aggregates media from all nested subdirectories.
3. **Active Playlist Mode**: Plays through the currently loaded playlist (`.rvlist`, `.m3u8`).
4. **Selected / Marked Files Only**: Operates strictly on user-selected items (files checked in the filmstrip, tagged via `Space`, or multi-selected via standard file dialog).

#### 3.2.3 Multi-Criteria Sorting Engine
Before starting the presentation or browsing directory collections, the file sequence can be organized by two distinct name-sorting modes, as well as metadata-based sorting:
1. **Name: Natural / Logical Alphanumeric (`RUBRAVIEW_SORT_NAME_NATURAL` - Default)**:
   - Evaluates contiguous digit sequences as single integrated numeric quantities rather than raw ASCII code points.
   - Specifically handles bracketed and indexed sequences: `(1)`, `(2)`, ..., `(9)`, `(10)`, ..., `(99)`, `(100)` are sorted in strict numerical order ($1 < 2 < 9 < 10 < 99 < 100$).
   - Prevents the classic sorting bug where `(10)` and `(100)` are sorted ahead of `(2)`.
   - Supports arbitrary digit placement: beginning (`1.jpg`), middle (`chapter_1_page_10.png`), inside parentheses/brackets (`img (10).jpg`, `[05].png`), and mixed delimiters.
   - Arbitrary precision: handles numbers with more than 64 bits without integer overflow by comparing significant digit spans.
   - Resolves leading zero ties deterministically (e.g. `1` vs `01`).
2. **Name: Strict Lexicographical / Ordinal (`RUBRAVIEW_SORT_NAME_LEXICAL`)**:
   - Compares characters strictly byte-by-byte according to Unicode/ASCII code points (`'('` < `'0'` < `'1'` < `'2'`).
   - Results in literal ordinal sequence: `(1)` < `(10)` < `(100)` < `(2)` < `(20)` < `(9)`.
   - Essential for programmers, command-line scripting alignment, exact matching with raw POSIX `ls`, and database consistency where byte order is required.
3. **Date Modified / Created (`RUBRAVIEW_SORT_DATE_MODIFIED` / `RUBRAVIEW_SORT_DATE_CREATED`)**: Chronological order (Newest first or Oldest first).
4. **File Size (`RUBRAVIEW_SORT_FILE_SIZE`)**: Sorted by byte length (Smallest first or Largest first).
5. **Random Shuffle (`RUBRAVIEW_SORT_RANDOM`)**: Cryptographically uniform Fisher-Yates shuffle with seed preservation (allows `Previous` key to accurately step back through the shuffled history without re-randomizing).
- **Ascending / Descending Toggle**: Every sorting mode supports an instantaneous Ascending $\leftrightarrow$ Descending direction flip. Both `RUBRAVIEW_SORT_NAME_NATURAL` and `RUBRAVIEW_SORT_NAME_LEXICAL` are exposed as distinct selectable options in the In-Window Menu Box, the In-App Metro Picker sort chips, and CLI arguments (`--sort=natural` vs `--sort=lexical`).

#### 3.2.4 File Extension & Media Type Filtering
Users can filter the active sequence without moving files:
- **Preset Filters**:
  - *All Supported Media* (Images + Videos + Archives)
  - *Images Only* (JPEG, PNG, WebP, GIF, BMP, TIFF, ICO)
  - *Videos Only* (MP4, MKV, WebM, MOV, AVI, FLV, TS)
  - *Comic Archives Only* (CBZ, CBR, CB7)
- **Custom Pattern Whitelist**:
  - User-defined glob filters (e.g. `*.png;*.webp` to exclude low-res JPEGs, or `*_4k.*`).

#### 3.2.5 Presentation Controls & Transitions
- **Fullscreen Presentation**:
  - Borderless fullscreen mode (`F11` or `Enter`) maximizing display real estate.
  - Automatic mouse cursor hiding after 1.5 seconds of mouse stillness.
- **Hardware-Accelerated Transitions**:
  - Direct2D transitions: Instant cut (0ms, recommended for intervals $< 0.5\text{s}$), Cross-fade (alpha blend), Slide left/right, Zoom-in fade.
- **Interactive Control**:
  - `Space`: Pause / Resume auto-advance.
  - User interaction (mouse hover, pinch zoom, pan) temporarily freezes the advance timer until the viewport is released.

#### 3.2.6 Mixed Media Slideshow & Playlist Synchronization
When a sequence contains a heterogeneous mix of still images, animated images (GIF, Animated WebP, APNG), audio tracks, and video clips:
- **Still Images**: Play strictly for the user-configured slide show interval ($T_{slide}$, e.g. 3.0s or granular 0.1s~300s).
- **Video Files (`.mp4`, `.mkv`, `.webm`, `.avi`, `.wmv`, etc.)**: The slide show **plays the video in its entirety from start to finish** ($0:00 \rightarrow T_{duration}$), and advances to the next playlist item only after the video finishes playback.
- **Animated Images (GIF / WebP / APNG)**: Advance only after completing at least one full animation cycle ($\max(T_{slide}, T_{cycle})$), ensuring animations are not abruptly cut off.
- **Playlist Loop Synchronicity**:
  - The playlist loop policy (`Loop All`, `Play Once`, `Repeat Single Item`) applies uniformly across both images and video files. When `Loop All` is active, reaching the end of the folder or playlist seamlessly cycles back to the first media item.

### 3.3 Intelligent Multi-Page & Book Reading Layouts
For viewing comic books, manga, scanned documents, and multi-page albums, Rubraview implements an intelligent layout engine (`rubraview_layout_engine`):

```
1. Single Page Mode:
   +-----------------------+
   |                       |
   |        [Page 1]       |
   |                       |
   +-----------------------+

2. Dual Page (Side-by-Side) Mode:
   +-----------+-----------+
   |           |           |
   |  [Page 1] |  [Page 2] |
   |           |           |
   +-----------+-----------+

3. Book / Manga Mode:
   First Screen (Cover):
   +-----------+-----------+
   | (Gutter)  | [Cover 1] |
   +-----------+-----------+
   Subsequent Screens (Spreads):
   +-----------+-----------+
   |  [Page 2] |  [Page 3] | (Left-to-Right: Western)
   +-----------+-----------+
   or
   +-----------+-----------+
   |  [Page 3] |  [Page 2] | (Right-to-Left: Manga)
   +-----------+-----------+

4. Pre-merged Spread Handling:
   If an image is already scanned as a wide 2-page spread (AR >= 1.15):
   +-----------------------+
   |  [ Page 4 & Page 5 ]  |  <-- Rendered full-width alone;
   +-----------------------+      NOT paired with Page 6!
```

1. **Single Page Mode (`RUBRAVIEW_PAGE_LAYOUT_SINGLE`)**: Standard centered view.
2. **Dual Page Mode (`RUBRAVIEW_PAGE_LAYOUT_DUAL`)**:
   - Renders two consecutive images side-by-side with a configurable gutter (0 to 16 pixels).
   - Page flip advances by 2 pages.
3. **Book / Manga Mode (`RUBRAVIEW_PAGE_LAYOUT_BOOK`)**:
   - **Cover Page 1 Exception**: Page 1 (cover) is displayed as a standalone single page. Starting from page 2, pages are paired as two-page spreads (2–3, 4–5, 6–7).
   - **Reading Direction Toggle**:
     - *Left-to-Right (LTR)*: Page $N$ on Left, Page $N+1$ on Right (standard Western books, comics).
     - *Right-to-Left (RTL)*: Page $N$ on Right, Page $N+1$ on Left (Japanese/Korean manga, Eastern reading order).
4. **Intelligent Pre-merged Spread Detection**:
   - In manga and comic book releases, some chapters contain pre-stitched two-page spreads (aspect ratio $W/H \ge 1.15$).
   - The layout engine detects $AR \ge 1.15$ and automatically displays the spread as a single full-width item without pairing it with the next page, preventing misalignment of all subsequent page pairings.
5. **Intelligent Window Orientation Adaptation**:
   - The engine continuously monitors the window aspect ratio $AR_{win} = W_{win} / H_{win}$.
   - **Portrait Window Auto-Collapse**: When viewed on a smartphone held vertically over Remote Desktop ($AR_{win} < 1.0$), displaying two portrait pages side-by-side creates tiny, unreadable postage-stamp images. The layout engine dynamically collapses Book/Dual mode into **Single Page Fit-to-Width** mode. When the user rotates the device to landscape ($AR_{win} \ge 1.3$), it automatically resumes side-by-side dual spreads.
6. **Webtoon Continuous Vertical Strip Scroll Mode (`RUBRAVIEW_PAGE_LAYOUT_WEBTOON`)**:
   - Tailored specifically for digital webtoons where chapters consist of dozens of vertical image slices.
   - Slices within an archive or folder are stitched seamlessly with zero vertical gutter ($\text{Gutter} = 0$).
   - **Virtual Viewport Compositing**: The renderer allocates Direct2D textures only for slices intersecting the current visible window height (plus $\pm 1$ lookahead slice). As the user scrolls via mouse wheel, touch drag, or `PageDown`, images glide continuously without jarring page-transition cuts.
7. **Smart Two-Page Spread Splitting (`RUBRAVIEW_SPREAD_SPLIT_AUTO`)**:
   - When viewing pre-scanned 2-page wide spreads on a vertical/portrait display or mobile phone, reading the whole spread zoomed out is difficult.
   - The engine can optionally bisect wide images ($AR \ge 1.15$) down the center into two distinct virtual single pages:
     - **LTR Mode**: Left half displays first (Page $N$), Right half displays second (Page $N+1$).
     - **RTL Manga Mode**: Right half displays first (Page $N$), Left half displays second (Page $N+1$).

### 3.4 Viewport Fit & Alignment Modes
Rubraview implements six deterministic viewport fitting modes (`rubraview_fit_mode_t`):

| Fit Mode | Identifier | Description & Use Case |
| :--- | :--- | :--- |
| **Fit to Window (Inside)** | `RUBRAVIEW_FIT_WINDOW` | Scales image proportionally so the entire image fits within the window. Letterboxes/pillarboxes as necessary. Default mode. |
| **Fit to Width** | `RUBRAVIEW_FIT_WIDTH` | Scales image width to window width ($\text{scale} = W_{win} / W_{img}$). Image height extends beyond screen; scrollable vertically. Ideal for webtoons and vertical documents. |
| **Fit to Height** | `RUBRAVIEW_FIT_HEIGHT` | Scales image height to window height ($\text{scale} = H_{win} / H_{img}$). Image width extends beyond screen; scrollable horizontally. Ideal for wide panoramic photos. |
| **Stretch to Fill** | `RUBRAVIEW_FIT_STRETCH` | Scales width and height independently to fill the exact window dimensions, ignoring aspect ratio. |
| **Original Size (100%)** | `RUBRAVIEW_FIT_ACTUAL_SIZE` | 1:1 pixel mapping ($\text{scale} = 1.0$). One image pixel equals exactly one screen pixel. |
| **Smart Fit** | `RUBRAVIEW_FIT_SMART` | If image dimensions $> W_{win}$ or $> H_{win}$, scale down to fit inside; if image is smaller than window, display at 100% original size to prevent blurry upscaling. |

- **Fit Lock**: User can toggle "Lock Fit Mode" (`L` key) so that navigating between images of disparate resolutions maintains the chosen fit mode instead of resetting zoom.
- **Window Stability Invariant**: Under no circumstance will changing fit mode or loading an image alter the host window's position or size.

### 3.5 Low-Resolution & Pixel Art Rendering Modes
High-resolution photos require smooth interpolation, but low-resolution retro game assets, sprites, icons, and pixel art become blurry and degraded when subjected to standard bilinear filtering. Rubraview provides dedicated scaling engines:

1. **Nearest Neighbor (Crisp / Integer Scaling)**:
   - Sets Direct2D interpolation mode to `D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR`.
   - **Pixel-Perfect Integer Zoom**: Optional toggle constraining zoom levels to exact integer multiples ($1\times, 2\times, 3\times, 4\times, \dots$), guaranteeing zero pixel distortion or uneven pixel widths.
2. **Smooth Interpolation**:
   - `D2D1_INTERPOLATION_MODE_LINEAR`: Fast bilinear smoothing.
   - `D2D1_INTERPOLATION_MODE_CUBIC`: High-quality bicubic smoothing for standard photographic viewing.
   - `D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC`: 16-sample bicubic kernel for downsampled anti-aliasing.
3. **Pixel Grid Overlay**:
   - When viewing pixel art zoomed in beyond $400\%$, an optional 1-pixel hairline grid overlay (`G` key) can be drawn over pixel boundaries to assist developers and artists in inspecting individual pixel values and coordinates.

### 3.6 Floating Semi-Transparent Anchor Points & Dual-Box UI Architecture
When accessing a Windows PC from a mobile phone via Remote Desktop Protocol (Microsoft Remote Desktop, Chrome Remote Desktop, Moonlight) or interacting on touchscreens, traditional desktop dropdown menus and thin title bars become frustrating and error-prone. Rubraview introduces a modern, high-contrast, semi-transparent **Dual Floating Box UI** based on flat Metro square tiles:

```
+-----------------------------------------------------------------------------------+
|  [Main Canvas Area]                                                               |
|                                                                                   |
|   +---------------+ (Draggable Menu Anchor)                                       |
|   |  [ ☰ Menu ]   | -> Expands on hover/click to In-Window Hierarchical Menu Box   |
|   +---------------+    (Strictly stays inside window bounds)                      |
|                                                                                   |
|                                                                                   |
|                                                                                   |
|                                             +--------------------+                |
|                                             |  [ ⏯ Tools ] [📌]  | (Toolbox Anchor)|
|                                             +--------------------+                |
|                                             |  ▲ Pinned / Hover                   |
|                                             |  Can be dragged outside as          |
|                                             |  independent Win32 tool window!     |
+-----------------------------------------------------------------------------------+
```

#### 3.6.1 The Toolbox (`rubraview_toolbox`): Playback, Navigation & Detachable Window
The **Toolbox** manages real-time media manipulation and playback operations:
- **Core Tool Set**:
  - Media Controls: Previous (`◀◀`), Next (`▶▶`), Play/Pause (`⏯`), Step Backward/Forward (`,`/`.`), Stop (`⏹`).
  - Video & Audio: A-B Section Repeat (`[ A-B ]`), Clear Loop (`\`), Volume / Mute (`🔊`).
  - Viewport Controls: Zoom In (`+`), Zoom Out (`-`), Zoom 100% Reset (`1:1`), Rotate 90° (`⟳`), Flip (`⇄`).
  - Window Presentation: Fullscreen Borderless Toggle (`⛶`).
- **Pin Feature (`Pin` Toggle `📌`)**:
  - When pinned (`pinned = true`), the expanded toolbox **stays permanently visible and interactive**, disabling the auto-collapse idle timer.
  - Hovering over individual tool tiles while pinned provides instant tactile feedback without the toolbox disappearing or flickering.
- **Detachable Floating Window Mode (창 바깥 분리 - Independent Win32 Window)**:
  - **In-Window Mode**: When positioned inside the host window, the toolbox renders as a Direct2D hardware-accelerated semi-transparent overlay directly on the canvas.
  - **Detached Mode (`rubraview_toolbox_window`)**:
    - When the user drags the toolbox across the outer window perimeter (or clicks a `[ Detach ↗ ]` tile), it seamlessly transitions into an independent top-level Win32 tool window (`WS_POPUP | WS_EX_TOOLWINDOW | WS_EX_LAYERED`).
    - The detached window can float anywhere across the multi-monitor desktop workspace, remaining on top of other applications (`HWND_TOPMOST` optional toggle).
    - Dragging the detached window back over the main canvas surface docks it back into the in-window Direct2D overlay mode.

#### 3.6.2 The Menu Box (`rubraview_menubox`): In-Window Hierarchical Settings & Navigation
The **Menu Box** manages deep configurations, layout switching, image filters, and batch pipelines:
- **Strict In-Window Boundary Invariant**:
  - Unlike the Toolbox, the Menu Box **strictly remains inside the client area of the main application window**.
  - Its coordinates are clamped at all times: $0 \le X \le W_{win} - W_{menu}$ and $0 \le Y \le H_{win} - H_{menu}$.
  - It never spawns an external Win32 desktop window, ensuring full touch containment on constrained mobile RDP views.
- **Hierarchical Multi-Level Navigation (다단계 메트로 메뉴 탐색)**:
  - The menu expands into a grid of semi-transparent Metro square tiles ($48\times 48\text{ px}$ to $64\times 64\text{ px}$).
  - Multi-level drill-down structure:
    - **Level 0 (Root Categories)**:
      - `[ 📖 Layout ]`: Single, Dual Side-by-Side, Book / Manga (LTR / RTL), Pre-merged Spread Detection.
      - `[ ⊡ Fit & Zoom ]`: Fit Window, Fit Width, Fit Height, Stretch, 1:1 Actual, Smart Fit, Nearest Neighbor, Pixel Grid.
      - `[ 🎨 Adjust & Filter ]`: Exposure EV, Contrast, Saturation, Gamma, Tone Curves, Levels, Gaussian Blur, Unsharp Mask, Auto-Trim Margins.
      - `[ ⏱ Slideshow ]`: Timer interval (0.1s~300.0s), Sort criteria (Name, Date, Size, Shuffle), Extension filters, Loop policies.
      - `[ 📑 Playlist & Bookmarks ]`: Load `.rvlist`/`.m3u8`, Save Playlist, Add Bookmark, Recent File History.
      - `[ ⚙ Batch Export ]`: Multi-threaded batch processor launcher, Single-image quick transcoding.
      - `[ ⌨ Settings ]`: Keymap bindings (`keymap.ini`), RDP optimization presets, UI tile sizing.
    - **Level 1+ (Sub-menus)**:
      - Clicking a root category smoothly transitions the grid into that category's action tiles.
      - The top-left tile dynamically transforms into a prominent `[ ◀ Back ]` tile, allowing one-click return to the previous menu level.
      - A lightweight breadcrumb string (e.g. `Menu > Adjust > Tone Curves`) is displayed at the top border.

#### 3.6.3 Draggable Floating Anchors & Hover/Click Expansion
- **Compact Anchor State**:
  - When collapsed, both the Toolbox and Menu Box appear as unobtrusive, semi-transparent compact anchor tiles (e.g. $40\times 40\text{ px}$, 60% alpha) with clean glyphs (`[ ⏯ ]` and `[ ☰ ]`).
  - Users can click/touch and drag the anchor handle to any preferred position on screen. Positions are saved and restored across application launches.
- **Hover & Click Expansion Semantics**:
  - **Hover Expansion**: Moving the cursor over an anchor instantly unfolds the semi-transparent Metro tile grid.
  - **Idle Auto-Collapse**: If unpinned, moving the cursor away from the expanded grid collapses it back to the compact anchor after a 500ms grace delay.
  - **Click-to-Lock**: Tapping or clicking an anchor locks the menu open until an outside tap, `Esc` key, or close tile is pressed.

#### 3.6.4 Metro Square Tile Specifications, Semi-Transparency & RDP Performance
- **Square Tile Dimensions**:
  - Minimum touch target: $48\times 48\text{ px}$ (touch-friendly on high-DPI smartphones).
  - Standard desktop target: $64\times 64\text{ px}$ with 8px gutters between tiles.
  - Generous hit-test padding eliminates mis-clicks and accidental activations when using touch remote desktop.
- **Semi-Transparent Backdrop**:
  - Rendered with 80%–85% dark slate background (`#1A1A1AE0`) and crisp 1px high-contrast borders (`#FFFFFF30`), allowing the underlying media to remain partially visible while ensuring text/icon legibility.
- **Zero Ornamental Lag / RDP Network Optimization**:
  - RDP networks experience heavy latency spikes and frame drops when streaming continuous alpha fading, smooth sliding transitions, or motion blur.
  - Rubraview intentionally avoids heavy procedural animations. Tile transitions use instantaneous cuts (0ms) or single-frame state flips, maintaining 60 FPS remote responsiveness over constrained cellular connections.

#### 3.6.5 Multi-Touch Gestures
- Handled natively via Win32 `WM_GESTURE`:
  - `GID_ZOOM`: Two-finger pinch-to-zoom centered on gesture midpoint.
  - `GID_PAN`: Two-finger drag to pan viewport.
  - Single-finger horizontal swipe for page navigation.

### 3.7 Unified Keyboard, Mouse & Touch Control Matrix
Rubraview is designed with a **keyboard-first, touch-optimized philosophy**: 100% of viewer actions can be performed via keyboard alone without touching a mouse, while also offering fluid mouse gestures and touch controls.

#### 3.7.1 Context-Aware Keymap Dispatching
To prevent key collisions while keeping shortcuts intuitive, the input dispatcher evaluates the active viewing context:
- **Image / Comic Mode**: Standard page flipping, fit mode switching, and layout toggling.
- **Video & Audio Mode**: Play/Pause, precision frame stepping, seek, and A-B section repeat.
- **Slideshow Active**: Timer interval adjustment ($\pm 0.5\text{s}$ / $\pm 0.1\text{s}$).
- **Modal / File Dialog Active**: Traversal, selection, and cancellation.

#### 3.7.2 Comprehensive Keyboard Hotkey Reference

(D-16, 2026-09-15.) The table below is generated from `src/core/default_keymap.c`
by `scripts/check-actions.py --write`, and the check fails when it and the keymap
part. The hand-written table this section held was a plan; what it planned and
the keymap does not do is listed after it.

<!-- keys:begin — generated from src/core/default_keymap.c by scripts/check-actions.py --write; do not edit -->

**Everywhere**

| Keys | What it does |
|---|---|
| `F`, `F11`, `Alt+Enter` | Full screen |
| `Tab`, `F1` | Open or close the menu box |
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

**Planned here, not built:** seek ±30 s (`Ctrl+Left` / `Ctrl+Right` size the
window instead — owner, D-16) and ±1 s; pan with `W A S D` (A and D are taken);
capture the current frame (`Ctrl+C`, `Ctrl+S`); `P` for play/pause; the middle
click toggling 1:1 (§3.7.3); up to the folder on `Backspace` or `Alt+Up` (it is
`Ctrl+Backspace`).

#### 3.7.3 Mouse Controls & Hit-Testing Zones
1. **Left Click & Drag**:
   - When zoomed in beyond fit: Drags/pans the canvas viewport in sub-pixel coordinates.
   - When comic mode is active:
     - Click **Left 30%** of screen: Previous Page (LTR) / Next Page (RTL).
     - Click **Right 30%** of screen: Next Page (LTR) / Previous Page (RTL).
     - Click **Center 40%** of screen: Toggles floating anchor points and OSD overlay.
2. **Double Click**: Toggles borderless fullscreen mode.
3. **Right Click**: Opens the Metro context menu at the cursor position.
4. **Middle Click (Wheel Click)**: Toggles instantaneously between 1:1 original resolution (100%) and Fit to Window.
5. **Mouse Wheel**:
   - Plain Wheel: Next / Previous image (or vertical scroll when in Fit to Width mode).
   - `Ctrl + Mouse Wheel`: Real-time continuous sub-pixel zoom centered exactly at mouse cursor position.
   - `Shift + Mouse Wheel`: Horizontal canvas pan or 10-page fast skip.
6. **Side Buttons (XButton1 / XButton2)**: Previous / Next page.

#### 3.7.4 Touch & Pen Gestures (`WM_GESTURE` / Pointer Input)
- **Single Tap**: Toggles floating anchors and OSD info overlay.
- **Two-Finger Drag (`GID_PAN`)**: Fluid sub-pixel panning across canvas.
- **Pinch-to-Zoom (`GID_ZOOM`)**: Continuous zoom centered at the touch centroid.
- **Horizontal Swipe**: Fast swipe left/right triggers next/previous page flip with inertial feel.
- **Long Press (Press & Hold)**: Opens context menu.

#### 3.7.5 Plain-Text Keymap Configuration (`keymap.ini`)
Every action identifier in Rubraview is bound via an external, human-readable configuration file (`keymap.ini`). Users can customize shortcuts without rebuilding the executable. (D-14: it lives beside `settings.ini` — the program's folder in portable mode, AppData otherwise — and the settings window writes it; since D-13 values are quoted and the global section is `[ui]`.)
```ini
[navigation]
next_page = Right, PageDown, Space, J, D
prev_page = Left, PageUp, Shift+Space, K, A
skip_forward = Shift+Right
skip_backward = Shift+Left

[media]
play_pause = Space, P
frame_step_fwd = Period
frame_step_back = Comma
set_loop_a = BracketLeft
set_loop_b = BracketRight
clear_loop = Backslash
```

### 3.8 Comic Archive Container Subsystem & VFS Streaming (CBZ, CBR, CB7)
To provide frictionless digital comic and manga viewing without disk clutter:

#### 3.8.1 Virtual File System (VFS) Streaming Architecture
Unlike legacy comic viewers that unpack entire multi-gigabyte archives into the `%TEMP%` directory on disk, Rubraview implements a true **In-Memory Virtual File System (VFS)**:
1. **Zero-Disk Extraction Invariant**:
   - The engine **NEVER writes temporary image files to disk**.
   - Preserves SSD write endurance, avoids disk space exhaustion, and eliminates orphaned temp files on crash.
2. **$O(1)$ Central Directory Header Indexing**:
   - Upon opening a `.cbz` (ZIP), `.cbr` (RAR), or `.cb7` (7z) file, the VFS parser reads only the archive's central directory metadata located at the end of the file.
   - Extracts filenames, compression methods, uncompressed sizes, and stream byte offsets in under **5 milliseconds**, regardless of whether the archive is 50MB or 10GB.
   - Pages are naturally sorted using natural alphanumeric ordering (`01.jpg`, `02.jpg`, `10.jpg`).
3. **On-Demand Chunk Streaming**:
   - When a specific page $N$ is requested, the VFS seeks directly to that page's compressed byte offset in the archive file.
   - The compressed chunk is decompressed directly into a memory arena (`prv_arena_t`) and fed into WIC / Direct2D.
   - Memory usage is bounded strictly to the active viewing spread and pre-cache ring buffer (typically 30–60 MB RAM total).
4. **Consecutive Archive Traversal**:
   - Reaching the last page of an archive (or pressing `Ctrl + ]`) seamlessly opens the next archive in the parent directory (e.g. `Vol 01.cbz` $\rightarrow$ `Vol 02.cbz`), creating a continuous reading experience.

#### 3.8.2 Solid vs. Non-Solid Streaming Architecture (7z / CB7 & RAR / CBR)
A key architectural question arises regarding **7z (`.7z`, `.cb7`)** files:
Unlike ZIP archives where every file is compressed independently (Non-solid), 7-Zip by default creates **Solid archives**, concatenating multiple files into a single unified LZMA/LZMA2 stream to maximize compression ratio across similar images.

Rubraview successfully enables **Zero-Disk VFS Streaming for 7z** through a dual-mode decoding strategy:

1. **Non-Solid 7z (Individual File Streams)**:
   - When an archive is packed with solid compression disabled (`-ms=off`), each image has an isolated stream offset.
   - Operates identically to ZIP: true $O(1)$ random access to any page index in 5ms.
2. **Solid 7z (Progressive Sequential Streaming Decoder)**:
   - **Header Indexing ($O(1)$)**: The 7z metadata header at the end of the archive is indexed immediately upon open, providing the complete file list and solid block boundaries without decoding image payloads.
   - **Persistent Worker Decoder Context**: Because comic and image browsing is inherently sequential ($1\text{p} \rightarrow 2\text{p} \rightarrow 3\text{p}$), the background worker thread keeps a persistent `CLzmaDec` / `CSzArEx` decoder state alive.
   - When Page 1 finishes decoding, the decoder pauses at that exact dictionary state. When the user flips to Page 2, the decoder resumes seamlessly from the current dictionary offset rather than re-decoding from the beginning.
   - Lookahead pre-caching ensures that the next 2–3 pages are already decompressed in `prv_arena_t` memory, delivering the **identical 0ms instantaneous page-flip experience as uncompressed folders**.
3. **Handling Arbitrary Random Jumps in Solid Archives**:
   - If the user suddenly jumps from Page 1 to Page 150 via the slider:
     - The decoder identifies the solid block containing Page 150.
     - Performs a high-speed payload skip (discarding uncompressed bytes without generating Direct2D textures or pixel buffers).
     - Modern CPU LZMA decoding throughput reaches 60–120 MB/s, allowing even a 100-page skip across typical manga scans to resolve in ~0.1 to 0.3 seconds.
   - **Zero Disk Extraction Guaranteed**: Even during deep random jumps in solid archives, no intermediate files are written to `%TEMP%` or storage. All buffers cycle strictly within `prv_arena_t` memory arenas.
4. **Pure C23 Implementation via Official LZMA SDK**:
   - Built on Igor Pavlov's official **7-Zip C LZMA SDK (`7zDec.c`, `LzmaDec.c`, `Bra86.c`)**.
   - Written in 100% pure ANSI C / C23 with Public Domain / permissive licensing.
   - The SDK's memory allocator interface (`ISzAlloc`) is wired directly to `proven_arena_t`, ensuring zero heap fragmentation and instant teardown upon closing the archive.

#### 3.8.3 Archive Filename Encodings & User Override Options
A notorious issue in comic and manga archives across East Asia is that legacy ZIP archivers frequently encode internal filenames using non-UTF-8 local code pages (e.g., Korean CP949 / EUC-KR, Japanese Shift-JIS / CP932, Simplified Chinese GBK / CP936, or Traditional Chinese Big5 / CP950) without setting ZIP General Purpose Flag Bit 11 (the UTF-8 flag).

Rubraview resolves this through a resilient two-tier encoding strategy:
1. **Intelligent Auto-Detection Pipeline**:
   - Step 1: Check ZIP General Purpose Bit 11. If set, parse filenames strictly as UTF-8.
   - Step 2: If unset or absent, validate byte stream against strict UTF-8 rules. If the byte sequence conforms to valid UTF-8, retain UTF-8.
   - Step 3: If invalid UTF-8 sequences are encountered, automatically fall back to the host operating system's active ANSI/OEM code page (e.g. CP949 on Korean Windows, Shift-JIS on Japanese Windows, CP1252 on Western Windows).
2. **Explicit User Override Options**:
   - Users can manually force an encoding from the Menu Box (`Menu > Archive > Filename Encoding`) or In-App File Picker:
     - `Auto-Detect (Default)`
     - `UTF-8`
     - `Korean (CP949 / EUC-KR)`
     - `Japanese (Shift-JIS / CP932)`
     - `Simplified Chinese (GBK / CP936)`
     - `Traditional Chinese (Big5 / CP950)`
     - `Western European (CP1252 / ISO-8859-1)`
   - The selected encoding is transcoded into clean `u8str_t` (UTF-8) within a temporary scratch arena upon indexing, guaranteeing that all internal sorting, natural number grouping, and DirectWrite text rendering operate on 100% valid UTF-8.

#### 3.8.4 macOS Unicode NFC / NFD Decomposed Hangul & Accent Normalization
Files and comic archives created on macOS (HFS+ / APFS) store filenames in **Normalization Form D (NFD, Decomposed)**. For instance, the Korean syllable `한` (U+D55C) is physically split into three separate Jamo code points: `ㅎ` (Lead, U+1112) + `ㅏ` (Vowel, U+1161) + `ㄴ` (Trail, U+11AB). When transferred to Windows or Linux, these filenames appear broken into disjoint letters (`ㅎ ㅏ ㄴ`), string searches fail to match, and natural sorting is corrupted.

Rubraview implements a zero-allocation, high-performance C23 Unicode **NFC (Normalization Form C) Folding Pass**:
1. **Algorithmic Hangul Syllable Composition (Unicode §3.12)**:
   - Evaluates consecutive Jamo sequences in $O(N)$ single-pass:
     $$S = \text{SBase} + (L \times \text{VCount} + V) \times \text{TCount} + T$$
     Where $\text{SBase} = \text{0xAC00}$, $\text{VCount} = 21$, $\text{TCount} = 28$.
   - Executes purely via integer arithmetic with zero external Unicode library dependencies and sub-microsecond latency.
2. **Latin & Diacritical Combining Mark Composition**:
   - Re-combines common base characters with following combining diacritical marks (U+0300..U+036F) into standard precomposed NFC characters.
3. **Application Invariant**:
   - All filenames enumerated from local filesystems and all entry paths parsed from archive VFS streams are automatically normalized to NFC before being passed to `rubraview_sort_paths`, the OSD title renderer, or bookmark history.

#### 3.8.5 ComicInfo.xml Ingestion & Intelligent Reading Orientation
Digital comic archives frequently embed a standardized `ComicInfo.xml` manifest in their root:
1. **Metadata Parsing**:
   - The VFS reader checks for `ComicInfo.xml` upon indexing.
   - Extracts title, series, volume number, writer, penciller, summary, and page type tags (`<Page Image="0" Type="FrontCover"/>`).
2. **Automatic Reading Order Adaptation**:
   - Inspects the `<Manga>` tag:
     - `YesAndRightToLeft`: Automatically switches Rubraview into **Book Mode with RTL (Right-to-Left)** reading order without requiring the user to manually press `M`.
     - `No` or `Yes`: Defaults to Western LTR reading order.
3. **Explicit Cover Tagging**:
   - Pages tagged as `FrontCover` or `InnerCover` are strictly treated as standalone single pages in Book Mode, even if numbered unexpectedly.

### 3.9 Geometric Transforms & Rotation
- **Rotation Operations**:
  - Rotate $90^\circ$ Clockwise (`R`)
  - Rotate $90^\circ$ Counter-Clockwise (`Shift+R`)
  - Rotate $180^\circ$
- **Flip Operations**:
  - Horizontal Mirror Flip (`H`)
  - Vertical Flip (`V`)
- **Non-Destructive View Transform**:
  - Interactive rotations update the Direct2D affine transformation matrix $\mathbf{M}$ instantly (0ms latency, zero re-encoding).
- **EXIF Orientation Auto-Detection**:
  - Automatically parses EXIF tag `0x0112` (Orientation) upon loading and applies the required rotation/flip transform before initial rendering.
- **Lossless JPEG Transform**:
  - Capability to write $90^\circ/180^\circ$ rotations and flips back to disk losslessly by rearranging DCT coefficient blocks without decoding and re-compressing pixel arrays.

### 3.10 Single Image Conversion & Quick Export
- **Export Capabilities**:
  - Quick Save As (`Ctrl+Shift+S`) or Quick Export (`Ctrl+E`).
  - Supported Target Formats: JPEG, PNG, WebP (lossy/lossless), GIF, BMP, TIFF, ICO.
- **Quality & Compression Controls**:
  - JPEG: Quality slider (1–100), optional progressive scan encoding.
  - WebP: Lossless mode toggle, or Lossy quality slider (1–100) with compression effort parameter.
  - PNG: Compression level (0–9), color depth options (32-bit RGBA, 24-bit RGB, 8-bit Paletted/Grayscale).
  - ICO: Multi-icon generator packing 16x16, 32x32, 48x48, and 256x256 mipmaps into a single `.ico` file.
- **Privacy Clean: EXIF & GPS Location Stripping**:
  - Embedded metadata poses severe privacy risks: GPS latitude/longitude coordinates, altitude, timestamp, camera serial number, lens parameters, and author information.
  - Quick Export provides an explicit toggle: `[x] Privacy Clean (Strip GPS, Camera Serial & Personal Metadata)`.
  - **Zero-Touch Lossless Metadata Strip**: When saving without re-encoding, Rubraview directly excises EXIF APP1 markers (`0xFFE1`), XMP blocks, and IPTC headers from the JPEG byte stream without touching DCT coefficients, preserving 100% original photographic quality while neutralizing privacy leaks.

### 3.11 Batch Processing Subsystem
A robust batch engine designed for bulk media processing:
- **Input Pipeline**:
  - Multiple file selection, directory tree scanning (with optional recursive subfolder traversal), or drag-and-drop ingestion.
  - Filter criteria: Include/exclude file patterns (`*.jpg;*.png`), minimum/maximum file size, or dimension thresholds.
- **Action Chain**:
  1. *Orientation*: Apply EXIF rotation or manual fixed rotation/flip.
  2. *Resizing*: Resize by percentage ($50\%$), bounding box ($1920\times 1080$ fit inside), fixed width, or fixed height using selectable resampling algorithm (Lanczos-3, Bicubic, Bilinear, Nearest).
  3. *Color & Filters*: Bulk exposure correction, contrast boost, unsharp mask sharpening, or grayscale conversion.
  4. *Privacy Scrub*: Batch-strip GPS coordinates, device identifiers, and EXIF metadata before publishing or archiving.
  5. *Format Conversion*: Target format, quality factor, and compression parameters.
  6. *Output Naming*: Flexible naming patterns (e.g. `{name}_thumb.{ext}`, `{date}_{name}_{w}x{h}.{ext}`).
- **Execution Engine**:
  - Work-stealing thread pool utilizing hardware thread count.
  - Bounded memory footprint via per-thread arena allocators (`prv_arena_t`) that reset completely between processed files.

### 3.12 Playlist & Collection Management
Rubraview treats collections of media as first-class citizens:
- **File Formats**:
  - Native `.rvlist`: Human-readable, UTF-8 formatted JSON or plaintext list with relative file paths and playback parameters.
  - Standard `.m3u8` / `.m3u`: Import and export support for cross-app playlist compatibility.
- **Mixed Media Sequences**:
  - Playlists can interleave high-resolution still images, animated GIFs, and video files (`.mp4`, `.mkv`, `.webm`).
  - Slide show transitions seamlessly from an image (pausing for $N$ seconds) to a video (playing full video or fixed duration), and continuing to subsequent media items.
- **Quick Collections & Bookmarks**:
  - `Insert` or `Ctrl+D`: Instant addition of active image to "Favorites" or temporary scratchpad playlist.
  - Bookmark persistence: Remembers the last viewed file index and timestamp within long playlists.
### 3.13 Interactive Image Editing & Adjustment Panel
Rubraview incorporates a streamlined, non-destructive editing workbench accessible via hotkey `E` or the `[ ✏ Edit ]` Metro tile:
- **Real-Time Dual-Layer Architecture**:
  - *Preview Layer (Direct2D GPU Shaders)*: Sliders for exposure, contrast, tone curves, and filters modify Direct2D effect parameters directly. This yields instant 60–144 FPS visual feedback with 0% CPU pixel recalculation.
  - *Commit Layer (C23 Core Engine)*: When the user clicks "Apply" (`Enter`) or "Save Copy" (`Ctrl+S`), the core engine executes the exact math on the persistent `rubraview_pixbuf_t` buffer.
- **Color & Tone Controls**:
  - Sliders: Exposure ($\pm 3.0\,EV$), Brightness ($\pm 100$), Contrast ($\pm 100$), Saturation ($\pm 100$), Color Temperature (Warm/Cool), Tint (Green/Magenta).
- **Interactive Color Graph & Tone Curves (`rubraview_curves`)**:
  - Direct2D-rendered spline curve widget with interactive control points.
  - Channel selection: Composite RGB, Red, Green, Blue, or Luminance.
  - Live 256-bin histogram background behind the curve.
  - Levels controls: Black Point, Midtone (Gamma), White Point sliders.
- **Threshold & Luminance-Based Auto-Crop**:
  - **Auto Border Trim**: Scans image edges and crops away uniform white ($\text{Lum} \ge 245$) or black ($\text{Lum} \le 15$) margins, ideal for cleaning scanned manga pages, documents, and screen captures.
  - **Manual Rectangle Crop**: Draggable bounding box overlay with aspect-ratio locks (Free, 1:1, 4:3, 16:9, Original).
- **Spatial Filters & Resizing**:
  - **Gaussian Blur & Box Blur**: Adjustable radius ($\sigma = 0.5$ to $50.0\text{ px}$).
  - **Unsharp Mask (Sharpen)**: Adjustable Amount ($0\sim 300\%$), Radius ($0.5\sim 10.0\text{ px}$), and Threshold ($0\sim 255$) to sharpen edges without amplifying photographic sensor grain.
  - **Resize**: Target pixel width/height with aspect ratio lock, or percentage scaling ($10\%\sim 500\%$) using Lanczos-3, Bicubic, Bilinear, or Nearest Neighbor.

### 3.14 Dedicated Music Player Subsystem & Background Audio (BGM)
Rubraview seamlessly incorporates a dedicated, audiophile-grade **Music Player Subsystem**, elevating pure audio files into first-class multimedia citizens alongside images and videos.

#### 3.14.1 Audio Codec Compatibility & Gapless Playback Engine
- **Supported Formats**:
  - *Lossless*: FLAC, Apple Lossless (ALAC), Monkey's Audio (APE), WAV (PCM 8/16/24/32-bit), AIFF.
  - *Compressed*: MP3, AAC, M4A, Ogg Vorbis, Opus Audio, WMA (v1/v2/Pro).
  - *Legacy*: RealAudio (`.ra`, `.ram`).
- **Gapless Playback Engine**:
  - Pre-buffers the next audio track in memory to eliminate inter-track silence gaps, essential for continuous live concert recordings and classical movements.
- **Configurable Crossfade**:
  - Optional smooth volume crossfade ($0.0\text{s} \sim 5.0\text{s}$, adjustable in 0.5s increments) during track transitions.

#### 3.14.2 Visual Presentation Modes & Real-Time Spectrum Visualizer
The Direct2D canvas transforms into an immersive music visualizer workbench:
1. **High-Resolution Album Art Showcase**:
   - Automatically extracts embedded album artwork from ID3v2 APIC, FLAC Vorbis comments, MP4 `covr`, and WMA metadata.
   - Renders a prominent, high-resolution square album cover centered on the screen, accompanied by a dynamic, real-time blurred backdrop derived from the cover artwork.
2. **Real-Time 64-Band FFT Spectrum Analyzer**:
   - Computes a 1024-point Fast Fourier Transform (FFT) on active PCM audio frames.
   - Renders a butter-smooth 60–144 FPS 64-band frequency spectrum with peak-hold decay needles and high-contrast Metro bars.
3. **Smooth Oscilloscope Waveform**:
   - Alternative visualization mode rendering an analog-style oscilloscope waveform path across the lower canvas.
4. **On-Screen Display (OSD) Track Metadata**:
   - DirectWrite typographic display: Track Title, Artist, Album, Release Year, Track Number, Bitrate (kbps), Sample Rate (Hz), and Codec type.

#### 3.14.3 Synced Lyrics Engine (`.lrc` & Embedded USLT/SYLT)
- **Synced Lyrics Auto-Discovery**:
  - Automatically locates external `.lrc` lyrics files sharing the audio basename (e.g. `track01.mp3` $\rightarrow$ `track01.lrc`).
  - Parses embedded ID3 `SYLT` (synchronized lyrics) and `USLT` (unsynchronized lyrics) tags.
- **Karaoke Real-Time Scrolling Typography**:
  - Renders multi-line lyrics on the Direct2D canvas with current timestamp tracking:
    - Active lyric line is highlighted in high-contrast accent color with enlarged typography.
    - Previous and upcoming lines fade gently into semi-transparent secondary text.
    - Smooth vertical auto-scrolling keeps the current line centered.
    - Users can click/tap any lyric line to jump playback directly to that timestamp!

#### 3.14.4 Cue Sheet (`.cue`) Parsing & Single-Image Album Splitting
- Audiophiles frequently store entire albums as a single continuous `.flac` or `.ape` file accompanied by a `.cue` sheet.
- Rubraview parses the `.cue` file and exposes individual songs as distinct virtual tracks in the playlist and file picker, with instant seeking to track index offsets without splitting files on disk.

#### 3.14.5 Audio DSP: 10-Band Graphic Equalizer, ReplayGain & Night Mode
1. **10-Band Graphic Equalizer**:
   - Frequency bands: $31\,\text{Hz}, 62\,\text{Hz}, 125\,\text{Hz}, 250\,\text{Hz}, 500\,\text{Hz}, 1\,\text{kHz}, 2\,\text{kHz}, 4\,\text{kHz}, 8\,\text{kHz}, 16\,\text{kHz}$ ($\pm 12\,\text{dB}$).
   - Factory presets: Flat, Rock, Pop, Jazz, Classical, Bass Boost, Vocal Boost, Acoustic.
2. **Volume Normalization (ReplayGain)**:
   - Reads Track Gain and Album Gain metadata tags to prevent sudden volume spikes between tracks from disparate albums.
3. **Night Mode (Dynamic Range Compression)**:
   - Compresses extreme dynamic peaks (quiet whispers vs loud explosions) for comfortable listening at low volume.

#### 3.14.6 Floating Mini-Player Mode & Slideshow/Reading BGM Integration
1. **Compact Floating Mini-Player**:
   - Hotkey `Shift + P` or Toolbox tile transitions Rubraview into an ultra-compact, semi-transparent floating pill window ($320\times 80\text{ px}$):
     - Displays miniature album art, scrolling track title/artist, scrub bar, and compact playback controls (`[⏮] [⏯] [⏭] [🔊]`).
     - Optional `Always on Top` toggle (`WS_EX_TOPMOST`).
2. **Slideshow & Comic Reading Background Music (BGM)**:
   - Users can play music seamlessly in the background while browsing photo galleries, running slideshows, or reading manga.
   - **Intelligent Audio Arbiter**: If the user opens a video file with an active audio stream, the BGM automatically pauses, resuming when the video ends or is closed.

### 3.15 Independent File Selection & Dialog Subsystem (`rubraview_file_dialog`)
To ensure that Rubraview remains portable across desktop operating systems and friendly to touchscreens and Remote Desktop sessions, file selection is decoupled into an independent abstraction layer (`rubraview_file_dialog`):

```c
typedef struct rubraview_file_filter {
    const char *name;     // e.g. "Image Files (*.jpg;*.png;*.webp)"
    const char *pattern;  // e.g. "*.jpg;*.png;*.webp;*.gif"
} rubraview_file_filter_t;

typedef struct rubraview_file_dialog_opts {
    const char             *title;
    const char             *default_dir;
    const rubraview_file_filter_t *filters;
    size_t                  filter_count;
    bool                    allow_multi;
    bool                    folder_mode;
} rubraview_file_dialog_opts_t;

typedef struct rubraview_dialog_result {
    u8str_t *paths;       // Array of selected UTF-8 paths allocated in caller arena
    size_t   count;
    bool     accepted;
} rubraview_dialog_result_t;
```

#### 3.15.1 Initial Windows Implementation (`rubraview_file_dialog_win32`)
- Utilizes the modern COM `IFileOpenDialog` and `IFileSaveDialog` interfaces (with a fallback to `GetOpenFileNameW` where needed).
- Supports multi-file selection (`FOS_ALLOWMULTISELECT`), directory picking (`FOS_PICKFOLDERS`), and custom filter specifications.
- Translates native wide-character UTF-16 paths into clean, bounded UTF-8 slices (`u8str_t`) backed by `proven_arena_t`.

#### 3.15.2 In-App Metro Tile File Picker (`rubraview_file_dialog_metro`)
Standard OS file dialogs (such as Win32 `IFileOpenDialog`) are designed primarily for high-precision mouse input on desktop monitors. When accessed over mobile Remote Desktop (RDP) on small smartphone screens, their 12px list fonts, tiny folder tree controls, and microscopic scrollbars become frustrating and error-prone to operate with thumbs.

Rubraview implements a native, touch-first **In-App Metro Tile File Picker (`rubraview_file_dialog_metro`)** that renders directly onto the Direct2D canvas:

```
+-----------------------------------------------------------------------------------+
|  [ C: ]  [ D: ]  [ 📁 Pictures ]  [ 📁 Downloads ]  [ 📚 Comics ]  [ 🕒 Recent ]  |
|  [ ⮤ Up ]  C:  >  Comics  >  Berserk  >  [ Vol_01.cbz ]                           |
|  Filter: [ All Media ] [ Images ] [ Videos ] [ Archives ] | Sort: [ Name 🔤 ]     |
+-----------------------------------------------------------------------------------+
|  +--------------+  +--------------+  +--------------+  +--------------+           |
|  |  📁 Folder   |  | [Thumbnail]  |  | [Thumbnail]  |  | [Thumbnail]  |           |
|  |  Chapter 01  |  |  Cover.jpg   |  |  Page_001.png|  |  Vol_02.cbz  |           |
|  |  (42 items)  |  |  2.4 MB [4K] |  |  1.8 MB [HD] |  |  180 MB [CBZ]|           |
|  +--------------+  +--------------+  +--------------+  +--------------+           |
|  +--------------+  +--------------+  +--------------+  +--------------+           |
|  | [Thumbnail]  |  | [Thumbnail]  |  | [Thumbnail]  |  | [Thumbnail]  |           |
|  |  Page_002.png|  |  Page_003.png|  |  Teaser.mp4  |  |  Anim.gif    |           |
|  |  1.9 MB [HD] |  |  2.1 MB [HD] |  |  45 MB [MOV] |  |  8.2 MB [GIF]|           |
|  +--------------+  +--------------+  +--------------+  +--------------+           |
+-----------------------------------------------------------------------------------+
|  Selected: [ 3 files (24.2 MB) ]  |  [ 📂 Open ]  [ ⏯ Slideshow ]  [ ❌ Cancel ]  |
+-----------------------------------------------------------------------------------+
```

##### 1. Three-Tier Visual Layout Architecture
1. **Top Navigation & Breadcrumb Header**:
   - **Quick-Access Drive & Directory Chips**: Large square/rectangular chips for mounted drives (`C:`, `D:`) and standard media libraries (`Pictures`, `Downloads`, `Comics`, `Recent`).
   - **Breadcrumb Navigation Path**: Every segment in the directory path (`C: > Comics > Berserk`) is rendered as a standalone touchable Metro tile. Tapping any parent segment navigates there directly. An `[ ⮤ Up ]` tile ascends one directory level.
   - **Filter & View Tiles**:
     - Media filter chips: `[ All Media ]`, `[ Images Only ]`, `[ Videos Only ]`, `[ Comic Archives ]`.
     - Sorting chips: `[ Name 🔤 ]`, `[ Date 📅 ]`, `[ Size ⚖ ]`.
     - Tile size toggle: Large Grid ($160\times 160\text{ px}$), Medium Grid ($96\times 96\text{ px}$), or Touch List ($48\text{ px}$ row height).
2. **Central Metro Tile Grid Viewport**:
   - **Virtual Scrolling**: Even in directories containing 10,000+ media files, the renderer only allocates and draws tiles currently visible within the screen viewport (typically 12–30 tiles), maintaining a solid 60 FPS refresh rate and sub-millisecond touch response.
   - **Folder Tiles**: Display a prominent folder glyph, folder name, and total item count (`📁 Volume 01 (184 items)`).
   - **Media Tiles**:
     - Asynchronous WIC low-resolution thumbnail rendering with an in-memory thumbnail LRU cache (`rubraview_thumb_cache`) in memory arenas.
     - Filename rendered in DirectWrite with clean middle-ellipsis truncation (e.g. `Scans_2026...001.jpg`).
     - Overlay badge indicators: File format (`[CBZ]`, `[GIF]`, `[MP4]`), resolution class (`[4K]`, `[FHD]`), and file size.
   - **Interactive Archive Ingestion**:
     - Tapping a `.cbz` / `.zip` archive offers two actions: direct opening in the viewer, or **virtual folder exploration**, allowing users to browse individual comic pages inside the archive as thumbnails before opening!
3. **Bottom Action & Multi-Select Bar**:
   - In single-file mode: Double-tapping any tile instantly opens the file and dismisses the picker.
   - In multi-select mode:
     - Tapping the top-right selection checkbox on any tile marks it with a high-contrast accent border and checkmark.
     - Bottom bar displays selection metrics: `Selected: 5 files (340 MB)`.
     - Action buttons: `[ 📂 Open Selected ]`, `[ ⏯ Play as Slideshow ]`, `[ 📑 Create Playlist ]`, `[ ❌ Cancel (Esc) ]`.

##### 2. Mobile RDP & Touchscreen Optimizations
- **Minimum Hit Target**: All buttons, chips, and tiles enforce a minimum hit target of $48\times 48\text{ px}$ (default $64\times 64\text{ px}$ to $160\times 160\text{ px}$), ensuring effortless tapping with human thumbs.
- **Zero-Latency RDP Palette**: Uses flat, solid and semi-transparent Metro colors with zero ornamental animation loops or continuous alpha fades, minimizing Remote Desktop frame-encode bandwidth.
- **Swappable PAL Integration**: The In-App Metro Picker conforms directly to the `rubraview_pal_file_dialog` interface contract (`rubraview_file_dialog_metro`). Users can configure their default picker (`use_native_dialog = true/false` in `settings.ini`) or swap between them on the fly.

#### 3.15.3 Cross-Platform Native Backends
- **macOS**: Bridges to `NSOpenPanel` / `NSSavePanel` via a lightweight C runtime wrapper.
- **Linux**: Interacts with the FreeDesktop `org.freedesktop.portal.FileChooser` DBus portal or Zenity, falling back gracefully to the In-App Metro Picker.

#### 3.15.4 Quick Search & Hybrid Native IME Text Input Bar
To allow rapid filtering through thousands of files without reinventing a complex East Asian Input Method Editor (IME) text stack in pure Direct2D:
1. **Type-Ahead First-Letter Jump**:
   - Pressing alphanumeric keys in the tile grid immediately scrolls and jumps focus to the first matching filename without opening a text box.
2. **Hybrid Native IME Search Bar**:
   - Tapping the `[ 🔍 Search ]` tile reveals an input bar. Rather than managing complex composition states (Windows Imm32 / TSF) inside Direct2D, Rubraview instantiates a lightweight, transparent Win32 child `EDIT` control (`CreateWindowExW(0, L"EDIT", ...)`).
   - Windows natively handles Hangul (한글 자음/모음 조합), Japanese (Kana/Kanji), and Chinese composition effortlessly.
   - On `EN_CHANGE` notifications, the virtual grid dynamically re-filters items with middle-substring and regex matching in real time, dismissing the child edit control when `Enter` or `Esc` is pressed.

### 3.16 Video Subtitle Pipeline & Multi-Track Audio/Subtitle Selection
To deliver a complete multimedia viewing experience matching dedicated video players:

#### 3.16.1 Subtitle Formats & Auto-Discovery
1. **External Subtitles**:
   - Automatically detects subtitle files sharing the base filename in the active directory:
     - SubRip (`.srt`)
     - SAMI (`.smi`) with language class parsing (`<SYNC Start=...>`)
     - WebVTT (`.vtt`)
     - Advanced SubStation Alpha (`.ass` / `.ssa`)
2. **Container-Embedded Subtitle Streams**:
   - Demuxes embedded text streams from MKV and MP4 containers (`subrip`, `ass`, `mov_text`) via the FFmpeg PAL bridge.
3. **High-Contrast DirectWrite Rendering**:
   - Subtitles are rendered on the Direct2D canvas above the video frame using DirectWrite glyph runs with a 2-pixel black outline and subtle drop shadow, guaranteeing 100% legibility over dark or bright movie scenes.
4. **Interactive Timing Synchronization**:
   - Hotkeys `Z` and `X` adjust subtitle synchronization offset in $\pm 0.5\text{s}$ steps (e.g. `OSD: Subtitle Sync: +0.5s`).

#### 3.16.2 Multi-Track Stream Switching UI
- Modern anime and international movies often contain multiple audio tracks (e.g. Japanese 5.1, Korean Stereo, English Commentary) and multi-lingual subtitles.
- **Menu Box / Toolbox Integration**:
  - `[ 🎙 Audio Track ]`: Pops out a list of available audio streams (`#1: Japanese (FLAC 5.1)`, `#2: Korean (AAC 2.0)`). Switching streams rebinds the WASAPI audio resampler seamlessly without stopping video playback.
  - `[ 💬 Subtitles ]`: Lists available internal and external subtitles (`#1: Korean (SMI)`, `#2: English (SRT)`, `Off`).

### 3.17 Reading Session Persistence, Last-Read Resume & Portable Mode

#### 3.17.1 Automatic Last-Read Page Bookmarks (Resume Reading)
When reading long manga series or multi-volume comic archives, users should never lose their place:
- **Per-Archive Reading Position Tracker**:
  - Automatically records the current page index upon closing or switching files:
    ```ini
    [history]
    C:\Comics\OnePiece_Vol100.cbz = page:58, total:192, time:1725792000
    ```
- **Frictionless Resume Prompt**:
  - When re-opening an archive with an active reading history, Rubraview displays an unobtrusive Metro tile notification: `[ 📖 Resume Page 58 / 192 (Enter) ]`, auto-resuming on `Enter` or dismissing to Page 1 on `Esc`.

#### 3.17.2 Configuration Storage Hierarchy & Portable Mode
Many users run comic viewers from portable USB drives or external SSDs without installation:
1. **Portable Mode Priority**:
   - Upon startup, Rubraview checks for the existence of `settings.ini` in the same directory as `rubraview.exe`.
   - If present, **Portable Mode** is engaged: all configuration (`settings.ini`), reading bookmarks (`bookmarks.ini`), and recent files (`history.ini`) are stored exclusively in the application directory. No files or registry entries are written to the host computer.
2. **Standard AppData Fallback**:
   - If `settings.ini` is not found alongside the executable, settings are saved to `%APPDATA%\rubraview\settings.ini`, conforming to Windows application standards.

### 3.18 File Management, Curation & Fast Triage Subsystem
Rubraview provides a streamlined file management and curation workflow directly inside the viewer, empowering users to triage, organize, and sanitize thousands of files without switching back and forth to Windows Explorer:

#### 3.18.1 Recycle Bin Deletion with Session Undo & Permanent Purge
1. **Safe Recycle Bin Deletion (`Delete`)**:
   - Pressing `Delete` dispatches the active file to the Windows Recycle Bin using the native Windows Shell API (`SHFileOperationW` or `IFileOperation`) with the `FOF_ALLOWUNDO | FOF_NOCONFIRMATION` flags.
   - **Seamless Viewport Transition**: The viewer advances seamlessly to the next media item in the sequence (or the preceding item if the deleted file was the last item) without flickering or blank screens.
2. **Session Undo Buffer (`Ctrl + Z`)**:
   - Rubraview maintains an in-memory deletion history stack for the active session.
   - Pressing `Ctrl + Z` undoes the deletion via `IFileOperation::Undo()`, restores the file from the Recycle Bin, and re-inserts it into the active playlist and directory index at its original position.
3. **Permanent Purge (`Shift + Delete`)**:
   - Pressing `Shift + Delete` opens a high-contrast Metro confirmation dialog: `[ ⚠️ Permanently delete this file from disk? (Y/N) ]`.
   - Upon confirmation (`Y` or `Enter`), calls `DeleteFileW` directly, permanently removing the file without writing to the Recycle Bin.

#### 3.18.2 Inline File Rename (`F2`)
1. **In-Place Filename Editing**:
   - Pressing `F2` activates an inline rename overlay box directly above the current filename display.
   - Under Win32, Rubraview instantiates the transparent child `EDIT` control (`CreateWindowExW(0, L"EDIT", ...)`), enabling full native IME support for Korean, Japanese, and Chinese input.
   - Automatically selects only the file stem (excluding extension) so users can type a new title without accidentally altering the file format extension.
2. **Atomic File Move & Index Update**:
   - Pressing `Enter` validates the filename (disallowing Windows invalid characters `\ / : * ? " < > |`) and executes `MoveFileExW(old_path, new_path, MOVEFILE_COPY_ALLOWED)`.
   - Immediately updates the active playlist node, VFS cache, and OSD title bar without re-scanning or resetting the entire directory list. Pressing `Esc` cancels the edit cleanly.

#### 3.18.3 1~9 Number Key Quick Folder Curation (Fast Triage Mode)
Photographers, collectors, and digital archivists frequently process massive directories of uncurated media, sorting files into target categories (e.g. Keep, Best, Wallpaper, Delete, Review):
1. **Configurable Curation Mapping (`settings.ini`)**:
   - Keys `1` through `9` map directly to dedicated target folder paths:
     ```ini
     [curation]
     dir_1 = D:\Curation\Keep
     dir_2 = D:\Curation\Best
     dir_3 = D:\Curation\Wallpaper
     dir_4 = D:\Curation\Manga_Archive
     dir_5 = D:\Curation\Reference
     curation_mode = move   ; 'move' or 'copy'
     ```
2. **Single-Key Fast Triage Action**:
   - Pressing any number key `1`–`9`:
     - **Move Mode (`curation_mode = move`)**: Atomically relocates the active file to the assigned directory using `MoveFileExW` ($O(1)$ fast directory pointer update on the same volume). The file is unlinked from the active playlist, and the viewer automatically advances to the next image.
     - **Copy Mode (`curation_mode = copy`)**: Spawns an asynchronous background worker to duplicate the file into the target folder without interrupting viewing; the viewer remains on the current image.
   - **OSD Confirmation**: Displays an immediate DirectWrite notification badge: `[ 📁 Moved to: Best (2) ]`.
   - **Triage Undo**: Pressing `Ctrl + Z` reverses the move operation, returning the file to its source directory and re-inserting it into the viewport.

### 3.19 Application Lifecycle, Single-Instance IPC & Shell Integration

#### 3.19.1 Single-Instance Reuse vs. Multi-Window Execution
1. **Configurable Single-Instance Mode**:
   - Controlled via `settings.ini` (`single_instance = true` by default).
2. **Win32 Mutex & Window Discovery**:
   - On startup, Rubraview queries a named kernel mutex: `CreateMutexW(NULL, FALSE, L"Local\\Rubraview_SingleInstance_Mutex")`.
   - If `GetLastError() == ERROR_ALREADY_EXISTS`:
     - Discovers the existing running window via `FindWindowW(L"Rubraview_MainWindow_Class", NULL)`.
     - Restores the existing window if minimized (`ShowWindow(hWnd, SW_RESTORE)`) and brings it to the foreground (`SetForegroundWindow(hWnd)`).
3. **Inter-Process Communication (IPC) via `WM_COPYDATA`**:
   - The secondary process packages command-line arguments (file path to open, page jump index) into a `COPYDATASTRUCT` with `dwData = RUBRAVIEW_IPC_CMD_OPEN`.
   - Sends the message via `SendMessageW(hTargetWnd, WM_COPYDATA, ...)`:
     - Primary instance's `WndProc` receives the payload, unpacks the UTF-8 file path, switches the active media item immediately, and repaints the canvas.
   - The secondary process terminates instantly with exit code 0, conserving system memory and avoiding duplicate processes.
   - If `single_instance = false`, Rubraview bypasses the mutex check and launches a fully isolated, standalone viewer instance in a new desktop window.

#### 3.19.2 Drag-and-Drop Ingestion (`WM_DROPFILES` & OLE `IDropTarget`)
- **Native Explorer Drag-and-Drop**:
  - Registers `DragAcceptFiles(hWnd, TRUE)` to handle standard Win32 `WM_DROPFILES` messages.
  - Queries dropped paths via `DragQueryFileW`.
- **Modern OLE `IDropTarget` Interop**:
  - Implements lightweight C-style OLE `IDropTarget` and `IDataObject` COM interfaces.
  - Accepts drag-and-drop operations not only from Windows Explorer, but also from third-party file managers, web browsers, and archive managers (e.g. dragging images directly out of 7-Zip or WinRAR).
- **Intelligent Dropped Payload Routing**:
  - Dropping a single image/video/archive: Instantly opens and displays the file.
  - Dropping a directory: Opens the directory as a sequential gallery.
  - Dropping multiple files: Generates an on-the-fly temporary playlist containing only the dropped items.

#### 3.19.3 Windows 10/11 Shell Integration & File Associations
Rubraview provides zero-installer, portable shell registration options executable from the command line:
1. **Registration Commands**:
   - `rubraview.exe --register-shell`: Registers file associations and context menu entries.
   - `rubraview.exe --unregister-shell`: Cleanly removes all registry entries, leaving zero traces.
2. **Per-User Registry Architecture (`HKCU\Software\Classes`)**:
   - Operates strictly under `HKEY_CURRENT_USER\Software\Classes`, requiring **zero administrator privileges (No UAC prompt)**.
   - Registers Rubraview ProgIDs (`Rubraview.Image`, `Rubraview.Comic`, `Rubraview.Video`).
   - Context Menu Integration: Adds `"Open with Rubraview"` to Explorer right-click menus for supported extensions (`.jpg`, `.png`, `.webp`, `.cbz`, `.cbr`, `.cb7`, `.mp4`, `.mkv`, etc.).
   - Windows Default Apps: Registers within `HKCU\Software\RegisteredApplications`, allowing Rubraview to be selected as the system default photo viewer and video player inside Windows 10/11 Settings.

### 3.20 Animated Images & Multi-Page / Sub-Frame Formats

#### 3.20.1 Animated Image Engine (GIF, Animated WebP, APNG)
1. **WIC Multi-Frame Animation Decoding**:
   - WIC decodes animated GIF and WebP frames via `IWICBitmapDecoder::GetFrameCount()`.
   - Reads per-frame metadata via `IWICMetadataQueryReader`:
     - Frame delay in 1/100ths second (`/grctlext/Delay`).
     - Disposal method (`/grctlext/Disposal`).
     - Background transparency color index.
2. **Interactive Animation Controls**:
   - **Play / Pause**: Pressing `Space` (or clicking the animation tile) freezes or resumes playback.
   - **Precision Frame-by-Frame Stepping**:
     - Step forward one frame: `.` (period)
     - Step backward one frame: `,` (comma)
   - **Variable Playback Speed**: Cycle through $0.25\times, 0.5\times, 1.0\times, 1.5\times, 2.0\times$ speed multipliers via hotkey `Ctrl + [` and `Ctrl + ]`.
   - **OSD Sub-Frame Information**: DirectWrite HUD displays current frame and timing metrics: `[ GIF: Frame 12 / 48 (0.36s) | 1.0x ]`.
3. **Still Frame Export**:
   - Pressing `Ctrl + Shift + E` or selecting the Export tile captures the current frozen animation frame as a full-resolution standalone PNG, WebP, or JPEG file.

#### 3.20.2 Multi-Page TIFF Documents & Multi-Resolution ICO Formats
1. **Multi-Page TIFF Sub-Page Navigation**:
   - Scanned multi-page TIFF legal/medical documents contain dozens of pages within a single file.
   - Rubraview treats sub-frames as virtual pages:
     - Navigates between internal sub-pages using `Ctrl + PageDown` (Next Page) and `Ctrl + PageUp` (Previous Page), without jumping to the next file on disk.
     - OSD displays: `[ TIFF: Page 4 / 24 ]`.
2. **Multi-Resolution Windows Icons (`.ico`)**:
   - ICO files pack multiple resolution mipmaps ($16\times 16, 32\times 32, 48\times 48, 64\times 64, 128\times 128, 256\times 256\text{ px}$).
   - Rubraview defaults to displaying the highest available resolution frame, with sub-page hotkeys allowing instant inspection of each individual icon mipmap layer.

### 3.21 Frameless Borderless Window Architecture & Auto-Hiding Hover Titlebar Subsystem
Traditional desktop media viewers waste significant display real estate with fixed OS titlebars (30–40 px) and window border outlines (8 px). Rubraview adopts a **100% borderless, zero-margin canvas presentation** paired with an intelligent **hover-activated auto-hiding titlebar**:

#### 3.21.1 Zero-Margin Borderless Window Mechanics (`WM_NCCALCSIZE`)
1. **Complete Non-Client Area Stripping**:
   - To eliminate standard OS caption bars and border outlines without sacrificing native window management capabilities, Rubraview intercepts Win32 `WM_NCCALCSIZE`:
     - Returning `0` when `wParam == TRUE` expands the client rendering surface across the entirety of the window rectangle, completely eliminating traditional OS caption bars, thick borders, and corner padding.
     - 100% of physical window pixels are directly addressable by the Direct2D canvas.
2. **Subtle Native DWM Drop Shadow**:
   - Rubraview engages DWM frame composition via `DwmExtendFrameIntoClientArea(hWnd, &(MARGINS){0, 0, 1, 0})`, maintaining clean OS drop shadows against the desktop without visual border clutter.
3. **Invisible Border Resize Hit-Testing (`WM_NCHITTEST`)**:
   - Even without visible borders, users must be able to resize window edges intuitively.
   - Rubraview inspects mouse coordinates in `WM_NCHITTEST`:
     - Within 6 pixels of Left / Right / Top / Bottom boundaries, the window procedure returns `HTLEFT`, `HTRIGHT`, `HTTOP`, `HTBOTTOM`, `HTTOPLEFT`, `HTTOPRIGHT`, `HTBOTTOMLEFT`, or `HTBOTTOMRIGHT`.
     - The OS automatically swaps the cursor to standard resize double-arrows and manages smooth native window resizing.
4. **Taskbar-Aware Window Maximization (`WM_GETMINMAXINFO`)**:
   - Ordinary borderless windows frequently obscure the Windows Taskbar when maximized.
   - Rubraview intercepts `WM_GETMINMAXINFO` and queries `MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST)` and `GetMonitorInfoW()`:
     - Constrains `ptMaxSize` and `ptMaxPosition` strictly to `MONITORINFO.rcWork` (the screen area excluding the Windows Taskbar).
     - Maximizing the window docks it cleanly into the active display workspace without covering the taskbar.
     - In true Fullscreen Mode (`F11` / `F`), it expands to cover the full `MONITORINFO.rcMonitor` including the taskbar.

#### 3.21.2 Hover-Activated Auto-Hiding Titlebar (Top-Zone Slide/Fade HUD)
To provide window titles and control buttons without consuming persistent screen space:
1. **Top-Zone Hover Detection**:
   - The titlebar remains completely invisible (0% opacity, 0px canvas intrusion) during ordinary reading and media playback.
   - When the user hovers the mouse within the top trigger zone ($Y \le 12\text{ px}$ from the top edge), the titlebar slides down or fades in smoothly over the Direct2D canvas.
   - Registers Win32 `TrackMouseEvent(&tme)` with `TME_LEAVE | TME_HOVER`.
   - When the cursor exits the titlebar region, a 500ms grace period elapses before the titlebar smoothly transitions out of view.
2. **Titlebar Visual & Interaction Layout**:
   - **Form Factor**: Height $36\text{ px}$ (scales dynamically with DPI: $36\text{ px} \times \text{DPI}/96$).
   - **Backdrop**: Semi-transparent dark acrylic/Metro surface (`rgba(20, 20, 20, 0.88)`) with subtle bottom separator line.
   - **Left / Center Zone (Title & Metrics)**:
     - DirectWrite typography displaying the application name, current filename, page index (`[ 42 / 184 ]`), zoom scale, and resolution class.
   - **Window Dragging**:
     - Clicking and dragging any empty space on the titlebar initiates window movement via `WM_SYSCOMMAND` + `SC_MOVE + 2` (or returning `HTCAPTION` from hit-testing).
     - Double-clicking the titlebar toggles between Maximized and Restored window states.
     - Full support for Windows Aero Snap (`Win + Up/Down/Left/Right`).

#### 3.21.3 Top-Right Window State Control Button Matrix
The rightmost zone of the hover titlebar hosts a clean, high-contrast Metro vector button cluster:
1. **`[ 🗕 ]` Minimize Button**:
   - Hit target: $40\times 36\text{ px}$.
   - Triggers `ShowWindow(hWnd, SW_MINIMIZE)`.
2. **`[ 🗖 ]` / `[ 🗗 ]` Maximize / Restore Toggle Button**:
   - Dynamic glyph switching:
     - Displays `🗖` (single square) in normal windowed mode $\rightarrow$ triggers `ShowWindow(hWnd, SW_MAXIMIZE)`.
     - Displays `🗗` (overlapping squares) when maximized $\rightarrow$ triggers `ShowWindow(hWnd, SW_RESTORE)`.
3. **`[ ⛶ ]` True Borderless Fullscreen Button**:
   - Toggles true fullscreen mode covering the entire monitor including the taskbar (hotkey `F11` / `F`).
4. **`[ ✕ ]` Close Button**:
   - Hover accent: Metro Crimson Red (`#E81123`) background with crisp white glyph.
   - Dispatches `PostMessage(hWnd, WM_CLOSE, 0, 0)` for graceful application teardown.

### 3.22 Dedicated Tab-Styled Settings Window Subsystem (`rubraview_settings_dialog`)
To provide deep, user-friendly configuration across all multimedia viewing, comic reading, audio playback, and rendering behaviors without cluttering the main canvas, Rubraview incorporates an independent, tab-styled **Settings Window**:

#### 3.22.1 Window Architecture & Lifecycle
1. **Invocation & Modality**:
   - Hotkeys: `F10` or `Ctrl + ,` (Universal settings shortcut).
   - Menu & Titlebar Trigger: Selecting `[ ⚙ Settings ]` in the In-Window Menu Box or Hover Titlebar.
   - Dedicated Win32 Window: Spawns a dedicated top-level window owned by the viewer's window — above it, no taskbar button of its own, and closing it ends only itself (D-13).
   - Modality: Non-modal; the viewer keeps working. A change applies to the viewer at once — the subtitle on a playing video resizes while its value moves (D-13).
   - Rendering: Every page is described in one internal document (page · section · toggle · choice · int · float · path · info · preview · table · action) that also declares each setting's type, range and default; one interpreter lays it out on a grid of fixed-width cells. A plain look — lists, tables, fixed-width text — is intended; previews and live data sit on the same grid (D-13).
2. **Persistence & Serialization**:
   - Writes `settings.ini` when the window closes, in the INI ∩ TOML subset (D-13): UTF-8 without a BOM, whole-line `#` comments, `[section]` and lower-case `key = value`, values `true`/`false`, integers, `1.5`-style floats or double-quoted strings escaping only `\\` and `\"`, never a key above the first section. Older files still load; the next save rewrites them:
     - If running in **Portable Mode** (`settings.ini` adjacent to `rubraview.exe`), saves exclusively to the local executable directory.
     - Otherwise, saves to `%APPDATA%\rubraview\settings.ini`.
   - Action Bar: `[ Revert ]` (back to what the file held when the window opened), `[ Defaults ]` (factory settings; Revert undoes it), `[ Close ]` (write the file and close). There is no Apply: every change is already applied (D-13).

#### 3.22.2 Tabbed Category Architecture
The Settings Window features an intuitive tab strip (Horizontal Metro tab bar or vertical sidebar tab list):

1. **Tab 1: General (일반)**:
   - *Startup Behavior*: Open blank canvas, re-open last viewed file, or open last viewed directory/playlist.
   - *Single Instance*: Toggle single-instance window reuse (`single_instance = true/false`).
   - *Window & Chrome*: Enable/disable frameless borderless mode, hover titlebar trigger sensitivity ($Y \le 12\text{ px}$) and hide delay ($500\text{ms}$).
   - *Windows Shell Integration*: Buttons for `[ Register Shell Associations ]` and `[ Unregister ]` (`HKCU\Software\Classes`).
   - *Portable Status*: Visual badge indicating whether Portable Mode is active.

2. **Tab 2: Viewer & Layout (뷰어 & 레이아웃)**:
   - *Default Fit Mode*: Fit to Window, Fit to Width, Fit to Height, Smart Fit, 1:1 Actual Size, Stretch.
   - *Default Page Layout*: Single Page, Dual Page, Book/Manga (LTR/RTL), Webtoon Continuous Vertical Strip.
   - *Spread Options*: Smart 2-Page Spread Auto-Splitting toggle ($AR \ge 1.15$), Spread gutter spacing ($0\sim 32\text{ px}$), gutter background color.
   - *Orientation Adaptation*: Portrait window auto-collapse toggle ($AR_{win} < 1.0$).
   - *Zoom & Resampling*: Default zoom step factor ($5\%\sim 50\%$), interpolation filter (Nearest, Bilinear, Bicubic Catmull-Rom, Lanczos-3).
   - *Pixel Art Mode*: Pixel Grid overlay threshold toggle ($> 400\%$).

3. **Tab 3: Files & Comic Archives (파일, 정렬 & 만화책)**:
   - *Default Sorting Mode*: Natural Numeric (`(1)` < `(2)` < `(10)` < `(100)`) vs Strict Lexical (`(1)` < `(10)` < `(2)`) vs Date vs Size; Ascending / Descending default.
   - *Archive Codepage Fallback*: Auto-detect, CP949 (Korean), Shift-JIS (Japanese), GBK (Simplified Chinese), Big5 (Traditional Chinese), UTF-8 override.
   - *Comic Metadata*: Automatic `ComicInfo.xml` ingestion and `<Manga>` reading direction auto-switch toggle.
   - *Reading History*: Last-read page position tracking toggle & auto-resume prompt on archive open.
   - *1~9 Quick Folder Curation*: Configuration paths for target folders `dir_1` through `dir_9`, Curation action mode (`move` vs `copy`).
   - *Deletion Safety*: Recycle Bin deletion (`Delete`) with session undo vs Permanent purge confirmation prompt.

4. **Tab 4: Music & Audio (음악 플레이어 & 오디오)**:
   - *Playback Engine*: Gapless playback toggle and volume crossfade duration slider ($0.0\text{s}\sim 5.0\text{s}$).
   - *Lyrics & Karaoke*: External `.lrc` and ID3 `USLT`/`SYLT` auto-discovery, font size, active lyric highlight color.
   - *Spectrum Visualizer*: 64-band FFT Metro bar style vs Oscilloscope waveform path, visualizer FPS limiter ($60 / 120 / 144\text{ FPS}$).
   - *Audio DSP & Equalizer*: 10-band Graphic Equalizer preset manager (Flat, Rock, Pop, Jazz, Classic, Bass Boost) and custom EQ sliders ($\pm 12\,\text{dB}$).
   - *Volume Leveling*: ReplayGain mode (Track Gain, Album Gain, Off) & pre-amp boost slider.
   - *BGM Arbiter*: Background music auto-pause during video playback toggle.
   - *WASAPI Settings*: Audio buffer latency slider ($20\text{ms}\sim 100\text{ms}$).

5. **Tab 5: Video & Subtitles (동영상 & 자막)**:
   - *Hardware Acceleration*: D3D11VA / DXVA2 GPU hardware decoding toggle with software fallback diagnostic display.
   - *Subtitle Styling*: Subtitle font selector, font size, text color, outline width ($2\text{ px}$), drop shadow toggle.
   - *Subtitle Language*: Default preferred audio track and subtitle language tags (`kor`, `eng`, `jpn`).
   - *A-B Looping*: Loop timestamp step adjustment ($0.1\text{s}\sim 1.0\text{s}$).
   - *Video Viewport*: Enable/disable real-time mouse wheel zoom during active video playback.

6. **Tab 6: Display, HiDPI & Color Management (디스플레이, HiDPI & 색상 관리)**:
   - *High-DPI Awareness*: Per-Monitor V2 DPI scaling behavior and Metro touch tile base size ($48\text{px}, 64\text{px}, 96\text{px}$).
   - *Color Management*: Ingest embedded ICC profiles (Display P3, Adobe RGB, ProPhoto RGB) via WIC and transform to sRGB or monitor hardware ICC profile (`GetICMProfileW`).
   - *UI Themes*: Dark Metro theme accent color picker (Crimson Red, Cobalt Blue, Emerald Green, Amber, Metro Teal, Purple).

7. **Tab 7: Cache, Memory & Privacy (캐시, 메모리 & 프라이버시)**:
   - *Memory Cap*: Hard Memory Budget Cap slider ($256\text{ MB}\sim 4096\text{ MB}$, default $512\text{ MB}$) with live memory usage meter.
   - *Two-Tier Eviction*: Automatic LRU VRAM texture and decoded pixbuf eviction policy.
   - *Pre-caching Ring*: Lookahead frame count configuration (default: forward 2, backward 1; high-speed slideshow: 6–10).
   - *Privacy Clean*: Global default toggle to automatically scrub GPS coordinates, camera serial numbers, and author tags during export and batch conversion.
   - *Maintenance Actions*: `[ Clear Thumbnail Cache ]` and `[ Reset Reading History ]` buttons.

8. **Tab 8: Keyboard & Shortcuts (단축키 설정)**:
   - *Interactive Keymap Table*: Visual table listing all actions (Navigation, Zoom, View Modes, Video, Audio, Bookmarks) and their assigned hotkeys.
   - *In-Place Key Binding*: Click an action, press a new key combination to rebind. (D-14: `Enter` or a click on the focused row adds the pressed key, `Delete` removes the last one; `keymap.ini` is written beside `settings.ini` when the window closes.)
   - *Conflict Detection*: Highlights duplicate shortcut assignments in real time. (D-14: a key another action holds where the two contexts meet is refused and the holder named; `navigation`, the global section and `view` meet each other, while `slideshow`, `animation` and `subpage` keep their own meaning for a shared key.)
   - *Export / Import*: Save or reload custom keymaps (`keymap.ini`), `[ Reset to Default Keymap ]` button.

---

## 4. Canvas & Rendering Pipeline

### 4.1 Direct2D GPU Acceleration
Rendering high-resolution photographs (24MP to 100MP) in legacy GDI requires continuous CPU resizing (`StretchBlt`), which introduces high latency and frame drops. Direct2D resolves this by keeping the active image as an `ID2D1Bitmap` resident in GPU VRAM.

1. **Direct WIC-D2D Interop**:
   ```c
   IWICFormatConverter *converter = ...; // 32bppPBGRA conversion
   ID2D1Bitmap *d2d_bitmap = NULL;
   ID2D1RenderTarget_CreateBitmapFromWicBitmap(
       render_target,
       (IWICBitmapSource*)converter,
       NULL,
       &d2d_bitmap
   );
   ```
   This guarantees zero-copy or optimized DMA upload into DirectX VRAM.

2. **Sub-pixel Viewport Matrix**:
   Interactive zooming and panning are represented as a 3x2 affine transformation matrix:
   $$\mathbf{M} = \mathbf{T}(dx, dy) \cdot \mathbf{S}(scale, scale) \cdot \mathbf{T}(-cx, -cy)$$
   Panning and zooming simply update $\mathbf{M}$ and trigger an immediate `ID2D1RenderTarget_SetTransform()` followed by `ID2D1RenderTarget_DrawBitmap()`. The GPU rasterizer scales the image using bilinear or bicubic texture filtering at native display refresh rates (60–144 Hz).

3. **Multi-Page Layout Compositor**:
   When Dual-Page or Book Mode is active, the Direct2D compositor computes independent translation and scaling matrices for Left and Right bitmaps:
   $$\mathbf{M}_{left} = \mathbf{M}_{viewport} \cdot \mathbf{T}(-W_{left} - \frac{gutter}{2}, 0), \quad \mathbf{M}_{right} = \mathbf{M}_{viewport} \cdot \mathbf{T}(\frac{gutter}{2}, 0)$$
   Both images are drawn in a single render pass without CPU stitching or memory re-allocation.

4. **Real-Time Adjustment Graph**:
   Direct2D 1.1+ exposes an effect pipeline:
   `Source Bitmap` $\rightarrow$ `Exposure Effect` $\rightarrow$ `Color Matrix (Contrast/Saturation)` $\rightarrow$ `Gamma Transfer` $\rightarrow$ `Output Canvas`.
   Sliders for brightness, contrast, and saturation modify shader constants directly, providing instant 60 FPS previews without executing CPU pixel loops. When the user clicks "Save" or "Apply", the Core Engine applies the transformation to the persistent pixel buffer.

### 4.2 Per-Monitor V2 High-DPI Scaling & Dynamic Canvas Recreation
1. **Per-Monitor V2 High-DPI Manifest**:
   - Rubraview declares `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2` in its application manifest and initializes it via `SetProcessDpiAwarenessContext()`.
   - Guarantees 100% sharp, unscaled Direct2D vector and typography rendering, avoiding blurry Windows DWM bitmap stretching on high-resolution displays (4K/8K displays and high-density laptops).
2. **Dynamic `WM_DPICHANGED` Message Handling**:
   - When a user moves the Rubraview window across monitors with disparate DPI scale factors (e.g. from a 100% 1080p desktop monitor to a 200% 4K display):
     - Win32 delivers a `WM_DPICHANGED` message containing the new target DPI ($X, Y$) and a suggested new window rectangle in `lParam`.
     - Rubraview immediately resizes the window via `SetWindowPos()` to match the suggested bounds.
     - Updates internal scaling factor $S_{DPI} = \text{DPI} / 96.0$.
     - Recomputes Metro touch tile dimensions: $W_{tile} = 48\text{ px} \times S_{DPI}$ (e.g. $72\text{ px}$ at 150% scaling, $96\text{ px}$ at 200% scaling).
     - Resizes the Direct2D `ID2D1HwndRenderTarget` physical backbuffer size to match the new client rectangle, refreshing DirectWrite text layouts and glyph caches with zero visual distortion.

### 4.3 Wide Color Gamut (WCG) & WIC Color Management
1. **Embedded ICC Color Profile Extraction**:
   - Digital SLRs, mirrorless cameras, and modern smartphones (Apple iPhone, Samsung Galaxy) capture photos in wide color gamut color spaces: **Display P3**, **Adobe RGB (1998)**, and **ProPhoto RGB**.
   - Standard image viewers ignore embedded color profiles, rendering P3/Adobe RGB photos with dull, muted, or shifted colors on standard sRGB monitors.
   - Rubraview extracts embedded ICC color profile contexts directly using WIC (`IWICBitmapFrameDecode::GetColorContexts`).
2. **Real-Time Color Transform Pipeline (`IWICColorTransform`)**:
   - If an image contains an embedded color profile differing from standard sRGB:
     - Rubraview instantiates an `IWICColorTransform` component via `IWICImagingFactory_CreateColorTransformer()`.
     - Initializes the transform between the source ICC profile and destination profile (standard sRGB `FACILITY_WINCODEC_ERR_WRONGSTATE` or the monitor's calibrated hardware ICC profile retrieved via `GetICMProfileW`).
     - Performs hardware-accelerated color space conversion during bitmap decoding, guaranteeing 100% color-accurate reproduction across all display hardware.

---

## 5. Multimedia, Video & Audio Playback Subsystem

Rubraview incorporates a unified multimedia playback engine capable of streaming, demuxing, and decoding virtually any media format through an isolated FFmpeg Platform Abstraction Layer (PAL), coupled with a native Windows WASAPI audio presentation pipeline.

### 5.1 Comprehensive Multimedia Format Compatibility Matrix

| Category | Supported Containers & Extensions | Supported Codecs & Standards |
| :--- | :--- | :--- |
| **Modern Video** | `.mp4`, `.m4v`, `.mkv`, `.webm`, `.mov` | H.264 (AVC), H.265 (HEVC), VP8, VP9, AV1, Apple ProRes |
| **Broadcast & Legacy Video** | `.avi`, `.mpg`, `.mpeg`, `.ts`, `.m2ts`, `.vob`, `.3gp` | MPEG-1 Video, MPEG-2 Video, DivX, XviD, H.263, DV |
| **Windows Media** | `.wmv`, `.asf` | WMV1 (7), WMV2 (8), WMV3 (9), VC-1 Advanced Profile |
| **RealNetworks Media** | `.rm`, `.rmvb` | RealVideo 1.0 (RV10), 2.0 (RV20), 3.0 (RV30), 4.0 (RV40) |
| **Lossless Audio** | `.wav`, `.flac`, `.aiff`, `.ape` | Uncompressed Linear PCM (8/16/24/32-bit), FLAC, Monkey's Audio |
| **Compressed Audio** | `.mp3`, `.aac`, `.m4a`, `.wma` | MPEG Layer III, AAC-LC/HE-AAC, WMA v1, WMA v2, WMA Pro |
| **Ogg Open Media** | `.ogg`, `.oga`, `.ogv`, `.opus` | Ogg Vorbis, Opus Audio, Speex, Theora Video |
| **RealAudio** | `.ra`, `.ram` | RealAudio 1.0 (14.4), 2.0 (28.8), Cooker (Cook), ATRAC3 |

### 5.2 Dynamic Loading & Graceful Fallback
To keep `rubraview.exe` completely standalone, FFmpeg shared libraries are resolved dynamically at runtime:
1. The PAL loader checks for `avformat-*.dll`, `avcodec-*.dll`, `avutil-*.dll`, `swscale-*.dll`, and `swresample-*.dll` in the executable directory and system path.
2. If found, all demuxer, decoder, resampler, and color-space conversion function pointers are bound dynamically.
3. If absent:
   - Audio and video playback graceful degradation: Pure image viewer mode remains 100% operational.
   - No second decoder path exists (D-4, 2026-09-08): without the FFmpeg DLLs the OSD reports the missing bridge and video/audio files are skipped. Windows Media Foundation is not used.

### 5.3 Video Demuxing, Decoding & Presentation Pipeline
```
[ Video File ] ---> avformat_open_input() / av_read_frame()
                            | (Demuxed A/V Packets)
            +---------------+---------------+
            | (Video Packets)               | (Audio Packets)
            v                               v
    avcodec_send_packet()           avcodec_send_packet()
    avcodec_receive_frame()         avcodec_receive_frame()
            | (YUV420P / NV12)              | (Raw PCM Audio)
            v                               v
    sws_scale() (or GPU Shader)     swr_convert() (Resample to 48kHz Float)
            | (32-bit BGRA Pixels)          |
            v                               v
    Direct2D Bitmap Upload & Draw   WASAPI IAudioRenderClient (Audio Clock)
```

- **Audio-Clock Master Synchronization**: The audio stream serves as the master clock. Video frames are displayed or dropped based on their Presentation Timestamp ($PTS$) relative to the current WASAPI hardware audio clock position ($\Delta t = PTS_{video} - T_{audio}$).
- **Precision Seeking & Frame Stepping**: Backward and forward step hotkeys (`.` and `,`) seek to the nearest preceding keyframe (`av_seek_frame`) and decode forward to the exact requested frame index.
- **Still Frame Capture**: Clicking `Ctrl+C` or the Capture tile instantly captures the current video frame as a full-resolution `rubraview_pixbuf_t` and pushes it into the clipboard or the Image Adjustment Workbench.

### 5.4 Audio Playback & Native WASAPI Audio Engine
- **Low-Latency Audio Rendering**:
  - Rubraview connects to the Windows Audio Session API (WASAPI) in **Shared Mode** (`IAudioClient`, `IAudioRenderClient`).
  - Zero external audio libraries: Uses pure Windows native COM interfaces without DirectSound or waveOut latency.
- **Channel Mapping & Audio Resampling**:
  - `libswresample` converts multi-channel audio (e.g. 5.1/7.1 surround in MKV/TS files) down to standard stereo or 2.1 channels matching the user's active audio endpoint.
  - Normalizes arbitrary sample rates (8 kHz up to 192 kHz) to the system mixer rate (typically 48 kHz 32-bit float).
- **Buffer Ring & Underrun Prevention**:
  - Decoded audio samples populate an internal circular ring buffer (`prv_ring_t`). A high-priority event-driven audio thread feeds WASAPI buffers smoothly with zero pops or audio stuttering.

### 5.5 A-B Looping Subsystem (Section Repeat)
A-B Looping is a core multimedia feature in Rubraview for studying video clips, analyzing motion, or repeating animated sequences:
- **Intuitive Point Setting & Release**:
  - **Set Point A**: Pressing `[` or `A` locks the current playback timestamp as the loop start point ($T_A$).
  - **Set Point B**: Pressing `]` or `B` locks the current playback timestamp as the loop end point ($T_B$, where $T_B > T_A$). Playback immediately begins looping between $T_A$ and $T_B$.
  - **Clear Loop**: Pressing `\` or `C` (or the `[Clear Loop]` Metro tile) instantly clears the loop markers and resumes continuous full-length playback.
- **Low-Latency Loop Engine**:
  - When the playback clock reaches $T_B$, the decoder immediately flushes audio/video packet queues and performs a precise seek back to $T_A$ (`av_seek_frame` with forward demuxing), maintaining uninterrupted audio/video synchronicity with minimal latency.
- **Seekbar Timeline Visualization**:
  - The seek scrubber HUD draws prominent high-contrast brackets (`[ A` and `B ]`) and shades the active loop region, displaying the exact timestamps (e.g. `[01:14.200 - 01:28.500]`).

### 5.6 Video Viewport Fit Modes & Interactive Real-Time Wheel Zoom
Video playback in Rubraview adheres to the exact same canvas principles as still images:
- **Deterministic Video Fit Modes**:
  - `RUBRAVIEW_FIT_WINDOW`: Scales video proportionally to fit completely inside the window; letterbox/pillarbox as necessary (Default).
  - `RUBRAVIEW_FIT_WIDTH`: Matches video width to window width; if video height exceeds window, user can vertically scroll/pan.
  - `RUBRAVIEW_FIT_HEIGHT`: Matches video height to window height; if video width exceeds window, user can horizontally scroll/pan.
  - `RUBRAVIEW_FIT_STRETCH`: Stretches video to fill the entire window surface, ignoring aspect ratio.
  - `RUBRAVIEW_FIT_ACTUAL_SIZE`: 1:1 original pixel mapping (1 video pixel = 1 monitor pixel).
  - `RUBRAVIEW_FIT_SMART`: If video dimensions exceed window, scale down to fit inside; if smaller, display at 100% original size to prevent blurry upscaling.
- **Live Wheel Zoom & Drag Pan During Active Playback**:
  - Users can zoom in and out of a video using the mouse wheel (or touch pinch gesture) **in real time while the video is playing**.
  - When zoomed in, clicking and dragging with the mouse (or touch drag) pans the video viewport freely across the active video surface without pausing playback.
  - Zoom and pan operations modify only the Direct2D affine transformation matrix $\mathbf{M}$ on the GPU, incurring 0% CPU decoding overhead.
### 5.7 GPU Hardware-Accelerated Video Decoding (D3D11VA / DXVA2)
To achieve smooth, stutter-free playback of 4K and 8K 60fps high-bitrate video (HEVC / H.265, VP9, AV1) with low CPU usage and minimal battery consumption:
1. **FFmpeg Hardware Device Context**:
   - The FFmpeg bridge initializes `AV_HWDEVICE_TYPE_D3D11VA` linked to the active `ID3D11Device`.
   - Compressed video packets are decoded directly into Direct3D 11 video memory textures (`DXGI_FORMAT_NV12` or `DXGI_FORMAT_P010` for 10-bit HDR).
2. **Zero-Copy Direct2D Presentation**:
   - The decoded `ID3D11Texture2D` is wrapped as a Direct2D bitmap via `ID2D1DeviceContext::CreateBitmapFromDxgiSurface`.
   - Eliminates CPU round-trips (`sws_scale` or `memcpy` from system RAM to VRAM): video frames flow entirely inside GPU VRAM directly to the Direct2D canvas.
3. **Graceful Fallback to CPU Software Decoder**:
   - If hardware acceleration is unsupported by the GPU driver or codec profile, the bridge falls back cleanly to multi-threaded CPU software decoding (`avcodec_send_packet` / `avcodec_receive_frame`).

---

## 6. Core Image Processing & Filter Engine

The portable core engine operates on a standardized, cache-friendly pixel buffer:

```c
typedef enum rubraview_pixel_format {
    RUBRAVIEW_PIXFMT_RGBA8 = 0,   // Standard 32-bit sRGB
    RUBRAVIEW_PIXFMT_BGRA8,       // Direct2D/WIC native layout
    RUBRAVIEW_PIXFMT_GRAY8,       // 8-bit luminance
    RUBRAVIEW_PIXFMT_RGBA16F,     // High-dynamic-range floating point
} rubraview_pixel_format_t;

typedef struct rubraview_pixbuf {
    uint8_t           *pixels;
    int32_t            width;
    int32_t            height;
    int32_t            stride;       // Bytes per scanline
    rubraview_pixel_format_t  format;
    proven_arena_t    *arena;        // Owning arena (or NULL for external view)
} rubraview_pixbuf_t;
```

### 6.1 Color & Tone Adjustments
- **Linearized Exposure Math**:
  - Processing color directly in non-linear sRGB creates hue shifting and muddy midtones. Rubraview converts 8-bit sRGB to linear float via a 256-entry lookup table (LUT):
    $$C_{linear} = \text{LUT}_{srgb\_to\_linear}[C_{srgb}]$$
  - Exposure scaling is applied in linear space:
    $$C_{linear}' = C_{linear} \cdot 2^{EV}$$
  - Result is converted back to sRGB via a fast inverse table.
- **Contrast & Gamma**:
  - Contrast: $C' = \text{clamp}((C - 0.5) \cdot \text{factor} + 0.5, 0.0, 1.0)$ where $\text{factor} = \tan((contrast + 100) \cdot \frac{\pi}{400})$.
  - Gamma correction: $C' = C^{1/\gamma}$ for midtone compression or expansion.
- **HSL / HSV Color Balance**:
  - RGB to HSL transform allows isolating Hue, Saturation, and Lightness independently.
  - Temperature & Tint: Modifies Red/Blue balance (Kelvin shift) and Green/Magenta balance.

### 6.2 Tone Curves & Histogram Pipeline
- **Spline-Based Tone Curve (`rubraview_curves`)**:
  - Supports Monotone Cubic Spline (Fritsch-Carlson) interpolation through user control points $(x_0, y_0), \dots, (x_k, y_k)$, guaranteeing no overshoot or unnatural oscillations.
  - Generates a 256-entry transformation LUT applied with $O(1)$ per pixel:
    $$\text{pixel}_{out} = \text{LUT}_{curve}[\text{pixel}_{in}]$$
  - Multi-channel support: Master (RGB), Red, Green, Blue, or Luminance channel.
- **Histogram & Levels Generator**:
  - Computes 256-bin histograms for R, G, B, and Luminance channels in a single vectorized pass.
  - Levels Tool calculates Black Point $B$, Midtone $\gamma$, and White Point $W$:
    $$V_{out} = 255 \cdot \left(\text{clamp}\left(\frac{V_{in} - B}{W - B}, 0, 1\right)\right)^{1/\gamma}$$

### 6.3 Threshold & Luminance-Based Auto-Crop Engine
- **Border Trim / Scan Margin Auto-Crop (`rubraview_crop_autotrim`)**:
  - Scans pixel luminance $Y = 0.2126R + 0.7152G + 0.0722B$ along image borders.
  - Identifies bounding boxes of meaningful content by advancing inward from Top, Bottom, Left, and Right until scanlines deviate from the background threshold:
    - *White Margins* (scanned manga / documents): Scanlines where $> 98\%$ of pixels have $Y \ge T_{white}$ (default 245).
    - *Black Margins* (letterboxed screenshots / video stills): Scanlines where $> 98\%$ of pixels have $Y \le T_{black}$ (default 15).
  - Produces a non-destructive cropped sub-buffer `rubraview_pixbuf_sub()` or rewrites the buffer in-place.
- **Arbitrary Rectangular Crop**:
  - Sub-pixel coordinate bounding box $[x, y, w, h]$ with optional aspect-ratio constraints (1:1, 4:3, 16:9, 16:10, Original).

### 6.4 Spatial Filtering & Edge Enhancement
Convolutions are executed over separable 1D kernels where possible to reduce complexity from $O(W \cdot H \cdot K^2)$ to $O(W \cdot H \cdot 2K)$:
- **Gaussian Blur**:
  - Separable 1D kernel $G(x) = \frac{1}{\sqrt{2\pi}\sigma} e^{-\frac{x^2}{2\sigma^2}}$ truncated at radius $r = \lceil 3\sigma \rceil$.
  - Multi-threaded horizontal pass writes to temporary row buffer, followed by vertical pass writing to destination.
- **Box Blur**:
  - Moving-average accumulator filter with $O(1)$ complexity per pixel independent of radius, providing lightning-fast previews.
- **Unsharp Mask (Sharpening)**:
  - High-pass edge amplification:
    $$\text{Diff} = \text{Original} - \text{Gaussian}(\text{Original}, \sigma)$$
    $$\text{Output} = \begin{cases} \text{Original} + \text{Amount} \cdot \text{Diff} & \text{if } |\text{Diff}| \ge \text{Threshold} \\ \text{Original} & \text{otherwise} \end{cases}$$
  - The threshold parameter prevents amplifying smooth surfaces or image sensor noise.
- **Laplacian Edge Sharpening**:
  - 3x3 convolution with kernel $\begin{bmatrix} 0 & -1 & 0 \\ -1 & 5 & -1 \\ 0 & -1 & 0 \end{bmatrix}$ for instant micro-contrast enhancement.

### 6.5 Resampling Engine
High-quality resizing is essential for both display and batch export:
1. **Nearest Neighbor**: Fast preview and pixel-perfect retro/sprite art rendering ($O(1)$ per target pixel).
2. **Bilinear**: Standard 2x2 area-weighted interpolation.
3. **Bicubic**: Catmull-Rom cubic spline filtering ($\alpha = -0.5$) with 4x4 sample window:
   $$W(x) = \begin{cases} 1.5|x|^3 - 2.5|x|^2 + 1 & \text{for } |x| \le 1 \\ -0.5|x|^3 + 2.5|x|^2 - 4|x| + 2 & \text{for } 1 < |x| \le 2 \\ 0 & \text{otherwise} \end{cases}$$
4. **Lanczos-3**: Windowed sinc filter with radius $r=3$:
   $$L(x) = \begin{cases} \text{sinc}(x) \cdot \text{sinc}(x/3) & \text{for } -3 < x < 3 \\ 0 & \text{otherwise} \end{cases}$$
   Delivers the gold standard in sharp, artifact-free downsampling of large photographs.
5. **Multi-Threaded Tiled Resampling**:
   - The destination image is divided into horizontal stripes (e.g. 64 scanlines each) dispatched to the `proven` worker thread pool, maximizing cache locality and utilizing all CPU cores during batch exports.

---

## 7. Memory Model & proven_c_lib Integration

Rubraview rejects arbitrary, fragmented `malloc()`/`free()` allocations in favor of structured memory arenas and string slices provided by `proven_c_lib`:

### 7.1 Structured Memory Arenas (`prv_arena_t`)
1. **Frame Scratch Arenas**:
   UI redraws, temporary scaling buffers, and thumbnail decoding use a transient frame arena that resets once per event cycle (`proven_arena_reset()`).
2. **Archive Stream Arenas**:
   Decompressed image buffers from CBZ/CBR/CB7 archives are allocated within dedicated file-level arenas that release memory immediately when navigated away.
3. **Batch Task Arenas**:
   Worker threads allocate input and output pixel buffers within dedicated thread-local arenas, eliminating inter-thread contention.

### 7.2 UTF-8 String Slice Architecture (`u8str_t`) for Paths & Collections
Standard C null-terminated strings (`char*`) introduce heavy memory allocation overheads and buffer overrun risks when manipulating thousands of filenames. Rubraview standardizes all internal filenames, directory paths, metadata tags, and playlist entries on `proven_c_lib`'s **`u8str_t` string slices** (`{ const char *ptr; proven_size_t len; }`):

1. **Zero-Copy Path Deconstruction ($O(1)$ Time, 0 Bytes Allocated)**:
   - Extracting directory (`dirname`), filename (`basename`), extension (`ext`), and stem (`stem`) requires zero memory allocation and zero buffer copying:
     ```c
     // Path: "D:/Manga/Chapter01/page_042.webp"
     u8str_t dir  = rubraview_path_dirname(path);   // "D:/Manga/Chapter01"
     u8str_t base = rubraview_path_basename(path);  // "page_042.webp"
     u8str_t ext  = rubraview_path_ext(path);       // ".webp"
     u8str_t stem = rubraview_path_stem(path);      // "page_042"
     ```
   - In a collection of 50,000 images, filtering extensions or building playlist indices executes with **zero heap churn**.

2. **$O(1)$ Early-Out String Comparisons & Extension Matching**:
   - String equality `u8str_eq(a, b)` checks `a.len == b.len` first; if lengths differ, it rejects immediately in $O(1)$ without scanning characters.
   - Extension validation (e.g. checking whether a file matches `.png`, `.jpg`, `.mp4`) executes with minimal CPU cycles.

3. **The Null-Terminated Allocation Invariant (경계 널 종료 보장 규칙)**:
   - When a full filesystem path is initially allocated into a `prv_arena_t` (e.g. during directory enumeration or file dialog return), Rubraview strictly allocates `len + 1` bytes and writes a `\0` terminator at `ptr[len]`.
   - **POSIX Advantage (Linux / macOS)**: Slices can be passed directly to native OS calls (`fopen(path.ptr, "rb")`, `stat(path.ptr, ...)`) with zero copying or conversion.
   - **Windows Advantage**: Win32 PAL performs stack-buffered UTF-8 to UTF-16 conversion (`WCHAR wpath[MAX_PATH]`) strictly at the final OS call boundary (`CreateFileW`).

4. **Natural UTF-8 Standardization**:
   - Internal paths, archive table-of-contents, playlists, and EXIF keys are universally encoded in UTF-8, eliminating code-page and mojibake issues with Korean, Japanese, and accented characters across Windows, Linux, and macOS.

### 7.3 Dynamic Collections (`prv_dynarray_t`)
- Directory file listings, playlist queues, and batch job lists use `prv_dynarray_t` for typed, amortized growth backed by arenas with strict boundary safety.

### 7.4 Hard Memory Budget Cap & Two-Tier LRU Cache Eviction Policy
To prevent out-of-memory crashes and resource hogging when viewing 100-megapixel scans or high-speed slideshows:
1. **Hard Memory Budget Cap**:
   - Rubraview enforces a configurable memory budget cap (default: **512 MB**, adjustable from 256 MB to 4,096 MB in `settings.ini`).
2. **Two-Tier LRU Cache Eviction**:
   - **Tier 1: VRAM Texture Cache (`ID2D1Bitmap`)**: GPU textures outside the active viewport and immediate $\pm 2$ pre-cache ring are evicted immediately, reclaiming VRAM.
   - **Tier 2: Decoded Pixbuf Cache (`rubraview_pixbuf_t` in Arenas)**:
     - All cached pages are tracked in an LRU queue with their allocated byte sizes.
     - When total cached memory exceeds the configured budget cap, the oldest cached pages are evicted and their memory arenas are reset.
   - Ensures memory consumption remains strictly bounded and deterministic under all browsing loads.

---

## 8. Platform Abstraction Layer (PAL) & Multi-Platform Strategy

To guarantee that Rubraview remains **pure C23** and can expand smoothly from Windows to **Linux and macOS** without contaminating or refactoring the core algorithmic engine, all operating-system and hardware-dependent facilities are strictly isolated behind the **Platform Abstraction Layer (PAL)**.

### 8.1 Language & Foundation Invariants
1. **Pure C23 Standard Compliance**:
   - The entire codebase is written in ISO C23 (`-std=c23`).
   - Strict avoidance of C++ runtime dependencies (`libstdc++`/`libc++` are forbidden). Native COM interfaces on Windows are consumed purely via C vtables or C wrapper macros (`ID2D1Factory_CreateHwndRenderTarget(...)`).
2. **proven_c_lib as Sole Base Foundation**:
   - Dynamic memory management, string slicing, dynamic arrays, sorting, and assertions are strictly provided by vendored `proven_c_lib` (`prv_arena_t`, `u8str_t`, `prv_dynarray_t`, `prv_panic`).
   - Zero uncontrolled standard library allocations (`malloc`/`free` calls are prohibited in `src/core/`).
   - **Vendored permissive C libraries count as foundation for one job each** (D-2/D-3/D-5, 2026-09-08): `miniz` (inflate), the 7-Zip LZMA SDK (7z), and `libjpeg-turbo` (DCT-domain lossless transforms only). Each lives under `vendor/`, is recorded in `docs/resources/`, must be redistributable alongside the MIT licence, is wrapped behind one `rubraview_` module, and is never called from outside that module. Its allocator is routed to `prv_arena_t` where the library allows it.

### 8.2 Subsystem Abstraction Matrix across Platforms

| Subsystem | PAL Interface Header | Windows (Initial Target) | Linux (Expansion Target) | macOS (Expansion Target) |
| :--- | :--- | :--- | :--- | :--- |
| **Windowing & Events** | `rubraview_pal_window.h` | Win32 `WndProc`, Raw Input | Wayland (`xdg-shell`) / X11 | Cocoa / AppKit (`NSWindow`) |
| **2D & Canvas Render** | `rubraview_pal_render.h` | Direct2D 1.1+ / Direct3D 11 | Vulkan / OpenGL / Cairo | Metal / Quartz |
| **Native Image Codecs**| `rubraview_pal_image.h` | WIC (Windows Imaging Component)| libspng / libjpeg-turbo / FFmpeg| ImageIO / CoreGraphics |
| **Audio Output** | `rubraview_pal_audio.h` | WASAPI (Shared Mode) | PipeWire / PulseAudio / ALSA | CoreAudio (`AudioQueue`) |
| **Video & Multimedia** | `rubraview_pal_ffmpeg.h` | FFmpeg PAL Dynamic Bridge | FFmpeg PAL Dynamic Bridge | FFmpeg PAL Dynamic Bridge |
| **File Open/Save Dialog**| `rubraview_pal_file_dialog.h`| COM `IFileOpenDialog` (Native) | In-App Metro Picker / Portal | In-App Metro Picker / `NSOpenPanel` |
| **Filesystem & Traversal**| `rubraview_pal_fs.h` | Win32 `FindFirstFileW` / Shell | POSIX `opendir` / `readdir` | POSIX `opendir` / `readdir` |
| **High-Precision Clock**| `rubraview_pal_time.h` | `QueryPerformanceCounter` | `clock_gettime(CLOCK_MONOTONIC)`| `mach_absolute_time` |
| **Threading & Concurrency**| `rubraview_pal_thread.h` | Win32 Threads / ThreadPool | POSIX Threads (`pthread`) | POSIX Threads (`pthread`) |

### 8.3 Directory & Header Organization

Marked against the tree on 2026-09-08: `✓` exists and is under `make test`,
`·` planned (milestone in §11.3). The layout is unchanged; only the marks and the two
already-shipped headers (`path.h`, `sort.h`) were added.

```
rubraview/
├── include/
│   ├── rubraview/
│   │   ├── core.h      ✓  // Portable pixbuf, format conversions
│   │   ├── color.h     ✓  // Color adjustments, LUTs, curves, histogram
│   │   ├── resample.h  ✓  // Resampling kernels (Nearest, Bilinear, Bicubic, Lanczos)
│   │   ├── filter.h    ✓  // Spatial convolutions (Gaussian blur, sharpen, autotrim)
│   │   ├── path.h      ✓  // u8str_t zero-copy path slicing (§7.2)
│   │   ├── sort.h      ✓  // Natural / lexical name ordering (§3.2.3)
│   │   ├── layout.h    ·  // Layout engine: AR matching & spread rules (M1)
│   │   ├── archive.h   ·  // CBZ, CBR, CB7 virtual archive streams (M1/M4)
│   │   ├── batch.h     ·  // Batch job queue & worker declarations (M1/M6)
│   │   ├── playlist.h  ·  // Playlist and collection interfaces (M1)
│   │   └── pal/           // Pure C PAL interface contracts
│   │       ├── pal_file_dialog.h ✓
│   │       ├── pal_window.h      ·  (M2)
│   │       ├── pal_render.h      ·  (M2)
│   │       ├── pal_image.h       ·  (M2)
│   │       ├── pal_fs.h          ·  (M2)
│   │       ├── pal_audio.h       ·  (M5)
│   │       └── pal_ffmpeg.h      ·  (M5)
├── src/
│   ├── core/                // 100% Pure C23 (compiled on Linux, macOS, and Windows)
│   │   ├── pixbuf.c    ✓
│   │   ├── color.c     ✓
│   │   ├── resample.c  ✓
│   │   ├── filter.c    ✓
│   │   ├── path.c      ✓
│   │   ├── sort.c      ✓
│   │   ├── layout.c    ·
│   │   ├── archive.c   ·
│   │   ├── batch.c     ·
│   │   └── playlist.c  ·
│   ├── pal/
│   │   ├── win32/           // Windows implementations
│   │   │   ├── pal_file_dialog_win32.c ✓ (compiles only under MinGW; not yet linked)
│   │   │   ├── pal_window_win32.c      ·
│   │   │   ├── pal_render_d2d.c        ·
│   │   │   ├── pal_image_wic.c         ·
│   │   │   ├── pal_audio_wasapi.c      ·
│   │   │   ├── pal_ffmpeg_win32.c      ·
│   │   │   └── pal_fs_win32.c          ·
│   │   ├── custom/          // Cross-platform fallback implementations
│   │   │   └── file_dialog_metro.c     · // In-app touch Metro tile file picker (M3)
│   │   └── host/            // Linux host test implementations
│   │       ├── pal_file_dialog_host.c  ✓
│   │       ├── pal_mock_render.c       ·
│   │       ├── pal_mock_audio.c        ·
│   │       ├── pal_archive_posix.c     ·
│   │       └── pal_fs_posix.c          ·
│   └── app/                 // Application entry, dispatch & UI state (M2+)
│       ├── main.c           ·
│       ├── touch_tile_ui.c  ·
│       ├── view_modes.c     ·
│       └── cli_batch.c      ·
└── tests/                   // Executed natively on Linux
    ├── test_pixbuf.c    ✓
    ├── test_color.c     ✓
    ├── test_resample.c  ✓
    ├── test_filters.c   ✓
    ├── test_path.c      ✓
    ├── test_sort.c      ✓
    ├── test_layout.c    ·
    ├── test_archive.c   ·
    ├── test_batch.c     ·
    └── test_playlist.c  ·
```

### 8.4 Verification Ladder:
- **T0/T1 (Host Linux)**: `make test` runs all core algorithms under `gcc -std=c23 -Wall -Wextra -pedantic -Werror` and AddressSanitizer (`-fsanitize=address,undefined`).
- **T2 (Cross-Build)**: MinGW-w64 x86_64 cross-compilation on `linux-build` validates WinAPI headers, Direct2D/WIC COM bindings, and PE binary generation.
- **T3 (Multi-Platform Smoke)**: Future native builds on Linux (via Wayland/Cairo) and macOS (via Metal/Cocoa).

---

## 9. Build & Cross-Compilation Pipeline

### 9.1 Host Linux Build Driver
A standard `Makefile` (and lightweight `nob.c`) allows fast, iterative local compilation:
```sh
make test         # Compiles core + host stubs, executes test suite
make check        # Runs project-check.sh and context-budget.sh
```

### 9.2 Remote linux-build MinGW-w64 Cross-Build
Building the Windows executable utilizes the MinGW-w64 toolchain on `linux-build`:
```sh
x86_64-w64-mingw32-gcc -std=c23 -O2 \
  -Iinclude -Ivendor/proven/include -Ivendor/proven/platform \
  src/core/*.c src/pal/win32/*.c src/app/*.c \
  vendor/proven/src/proven/*.c vendor/proven/platform/*.c \
  -ld2d1 -ldwrite -lole32 -lwindowscodecs -lshcore \
  -o dist/rubraview.exe
```

---

## 10. Security & Safety Model

1. **Untrusted Codec Safety**: Media viewers frequently process malicious or malformed image/video payloads. WIC operates with Microsoft-maintained memory bounds, and FFmpeg decoding can be isolated to a separate worker thread or child process.
2. **Archive Decompression Safety (Zip-Bomb Prevention)**:
   - Archive extraction limits uncompressed stream size per image (e.g. maximum 512 MB per frame buffer).
   - Never extracts files onto disk; streams decompressed bytes directly into bounded memory arenas.
3. **Buffer Bounds Checking**: All image resamplers and convolution kernels strictly validate coordinate bounds. Coordinates outside $[0, W-1] \times [0, H-1]$ are clamped or mirrored, preventing out-of-bounds memory accesses.
4. **No Dynamic Code Execution**: The application links no scripting runtimes and executes zero unverified dynamic code.

---

## 11. Implementation Roadmap & Milestones

*Revised 2026-09-08. The roadmap below replaces the six-milestone list written on
2026-09-07, which predates §3.6, §3.8.2–3.8.5, §3.14–3.22, §4.2–4.3, §5.5–5.7 and §7.4
and therefore no longer covered what §3 specifies.*

### 11.1 Planning Rules

1. **One row per specification section.** Every subsection of §3–§7 appears exactly once
   in the traceability table (§11.3) with a milestone and one or more backlog ids
   (`RV-nnn`). A section that is absent from the table is *not planned* — see §11.5.
2. **Host-testable first, inside every milestone** (DECISIONS 2026-09-07 "Headless-First
   Dual Build"). Pure C23 modules that run under `make test` on Linux are built and
   tested before the Win32 code that consumes them. The table marks each row
   **H** (host-testable under `make test`) or **W** (needs Windows, verified by
   `make win64` and a manual smoke run on the target machine).
3. **"Done means" is observable.** Each milestone names the command or the on-screen
   behaviour that closes it. A milestone with green tests but no observable behaviour is
   not done.
4. **Ids are stable.** `RV-001`–`RV-020` keep their meaning from `BACKLOGS.md`; new work
   starts at `RV-021`. `RV-007`/`RV-008` are assigned retroactively to the already
   shipped `path` and `sort` modules so that the table is complete.
5. **Sequencing hazards are encoded as dependencies, not remembered:**
   - The frameless `WM_NCCALCSIZE` shell (§3.21.1) is built in the **first** Windows
     milestone. Adding it later rewrites `WndProc`.
   - The INI reader (`settings.ini`, `keymap.ini`) precedes every UI milestone; each
     subsystem reads it.
   - The memory budget / LRU tracker (§7.4) precedes the pre-cache worker (§3.1);
     retrofitting a budget onto a running cache is a rewrite.
   - The settings window (§3.22) is last; it configures everything before it.
6. **Owner-only choices are not made here.** They are listed in §11.4 and mirrored in
   `BACKLOGS.md ## Blocked`. Work that depends on one of them is scheduled *after* the
   choice, never assumed.

### 11.2 Milestones

**M0 — Core engine foundation — SHIPPED (2026-09-07)**
- Shipped: `pixbuf` (RV-001), `color` (RV-002), `resample` (RV-003), `filter` (RV-004),
  `path` (RV-007), `sort` (RV-008), ASan/UBSan test harness (RV-006).
- Evidence: `make test` — 6 test binaries, all pass under `-fsanitize=address,undefined`.
- Gap against §8.4: the Makefile does not yet pass `-Werror`; RV-009 is the first M1 item (D-7). Measured 2026-09-08: the tree builds warning-free with `-Werror` and all 6 tests pass.

**M1 — Portable core, remainder (all H)**
- Goal: every algorithm §3 needs that has no OS dependency exists, is tested, and is
  runnable from Linux before a single Win32 line is written.
- RV-005 playlist (`.rvlist`, `.m3u8`) · RV-021 layout engine (§3.3: single/dual/book,
  LTR/RTL, cover exception, spread AR ≥ 1.15, portrait auto-collapse, webtoon strip
  geometry, spread splitting) · RV-022 fit-mode and viewport-matrix math (§3.4, §4.1.2,
  §5.6 shares it) · RV-023 INI reader/writer (§3.7.5, §3.17.2, §3.22.1) ·
  RV-024 Unicode NFC fold for Hangul and combining marks (§3.8.4) · RV-025 ZIP central
  directory index and **stored**-entry streaming (§3.8.1; `deflate` waits on D-2) ·
  RV-026 archive filename encoding steps 1–2, UTF-8 flag and strict validation
  (§3.8.3; code-page fallback is W, RV-046) · RV-027 `ComicInfo.xml` minimal reader
  (§3.8.5) · RV-028 memory budget and two-tier LRU tracker (§7.4; VRAM tier is a
  callback) · RV-029 EXIF `0x0112` orientation reader and APP1/XMP/IPTC marker strip
  (§3.9 auto-detection, §3.10 privacy clean) · RV-030 keymap action table and
  context-aware dispatcher core (§3.7.1–3.7.2, §3.7.5; key events arrive as data) ·
  RV-031 sort criteria beyond name — date, size, seeded shuffle with history (§3.2.3) ·
  RV-032 slide-show sequencer state machine (§3.2.1–3.2.2, §3.2.4, §3.2.6; the timer is
  injected) · RV-033 batch job model — action chain, filters, naming pattern, no threads
  yet (§3.11 input pipeline and action chain).
- Depends on: M0.
- Done means: `make test` runs one test binary per new module and all pass under ASan;
  `docs/tests/test-index.md` lists them.

**M2 — First window on Windows (W, plus PAL headers)**
- Goal: `rubraview.exe` opens a folder, shows an image, pans, zooms, flips pages, and
  the window never changes size on its own (§2 invariant 2).
- RV-010 Win32 window, message pump, HiDPI Per-Monitor V2 (§4.2) · RV-034 frameless
  shell — `WM_NCCALCSIZE`, `WM_NCHITTEST` resize zones, `WM_GETMINMAXINFO` taskbar-aware
  maximise, DWM shadow (§3.21.1) · RV-011 Direct2D render target and viewport matrix
  wired to RV-022 · RV-014 WIC decode into `ID2D1Bitmap` for the §1 still formats,
  EXIF auto-rotation via RV-029 · RV-035 `pal_fs_win32` — directory enumeration into
  `u8str_t` with the null-terminated allocation invariant (§7.2.3), sibling indexing,
  natural sort via RV-008 · RV-036 `pal_time_win32` (`QueryPerformanceCounter`) ·
  RV-012 multi-page compositor driven by RV-021 (§4.1.3) · RV-013 nearest-neighbour /
  integer zoom / pixel grid (§3.5) · RV-019 `make win64` cross-build on linux-build
  producing a PE that links (needs `src/app/main.c`).
- Depends on: M1 (RV-021, RV-022, RV-023, RV-029).
- Done means: `make win64` links; on the target machine, opening a folder of JPEG/PNG
  shows the first image, `Right`/`Left` flip pages in natural order, `Ctrl+Wheel` zooms
  at the cursor, `1`–`5` switch fit modes, `B`/`M` switch layouts, and the window
  rectangle is unchanged after every one of those actions.

**M3 — Reading UI: floating boxes, keyboard, slide show (W)**
- Goal: the viewer is usable without a mouse and over mobile RDP.
- RV-020 dual floating boxes — anchors, toolbox, in-window menu box, pin, detach
  (§3.6.1–3.6.4) · RV-037 mouse zones, wheel, side buttons, `WM_GESTURE` touch
  (§3.6.5, §3.7.3–3.7.4) · RV-038 keymap dispatcher wired to Win32 input and
  `keymap.ini` (RV-030 + RV-023) · RV-039 OSD and DirectWrite text (§3.1) · RV-040
  hover titlebar and window-state buttons (§3.21.2–3.21.3) · RV-015 slide show —
  multimedia timer, transitions, fullscreen, cursor hiding (§3.2.1, §3.2.5) on top of
  RV-032 · RV-041 rotation/flip transforms (§3.9 non-destructive part) · RV-042
  filmstrip and async thumbnail cache (§3.1) under the RV-028 budget · RV-043 file
  dialog — Win32 `IFileOpenDialog` backend already stubbed, plus the in-app Metro tile
  picker with virtual scrolling, type-ahead and the native `EDIT` search bar
  (§3.15.1–3.15.2, §3.15.4).
- Depends on: M2.
- Done means: every row of the §3.7.2 hotkey table that belongs to "Navigation",
  "Zoom & Fit", "Book & Manga", "Slideshow" and "UI & Windows" performs its action;
  the toolbox can be dragged outside the window and docked back; the menu box never
  leaves the client rectangle.

**M4 — Comic archives, caching, persistence (H core + W wiring)**
- Goal: `.cbz` reads without touching disk, page flips are instantaneous, and the reader
  resumes where the user stopped.
- RV-044 pre-cache worker on `proven` `job.h` (§3.1 lookahead ring, §3.2.1 stress
  caching) bounded by RV-028 · RV-045 CBZ wired to WIC (RV-025 + RV-014) with
  consecutive-archive traversal · RV-046 code-page fallback via `MultiByteToWideChar`
  and the user override list (§3.8.3 step 3) · RV-047 `ComicInfo.xml` → reading
  direction and cover tagging in the layout engine (RV-027 + RV-021) · RV-048 reading
  history, resume prompt, portable-mode hierarchy (§3.17) · RV-049 animated GIF/WebP/APNG
  frame engine, multi-page TIFF and ICO sub-frames (§3.20) · RV-050 wide-gamut ICC
  transform through `IWICColorTransform` (§4.3).
- RV-051 `deflate` for CBZ via vendored `miniz` (D-2) · RV-052 CB7 via the LZMA SDK,
  solid-stream persistent decoder (D-3). RV-053 CBR is post-1.0 (D-3, §11.5).
- Depends on: M3; RV-051 on the `miniz` ledger entry, RV-052 on the LZMA SDK ledger entry.
- Done means: a 1 GB stored-entry `.cbz` opens with the index built in under 5 ms
  (measured, §3.8.1); no file appears under `%TEMP%` during reading; reopening the
  archive offers the last page; memory stays under the configured cap while flipping
  200 pages (measured against `settings.ini`).

**M5 — Video and audio playback (W)**
- Goal: any file the format matrix (§5.1) lists plays with sound, and the viewer still
  starts when no FFmpeg DLL is present.
- RV-016 FFmpeg dynamic loader with graceful absence (§5.2 dynamic part) · RV-054
  demux/decode loop, `sws_scale`, D2D upload (§5.3) · RV-055 WASAPI shared-mode
  renderer, `swresample`, `proven` `ring.h` buffer, audio-clock master sync (§5.4) ·
  RV-056 seek, frame step, still-frame capture to clipboard (§5.3) · RV-057 A-B loop
  and seek-bar HUD (§5.5) · RV-058 video fit modes and live wheel zoom (§5.6, reuses
  RV-022) · RV-059 subtitles — SRT/SMI/VTT/ASS discovery and DirectWrite rendering,
  embedded streams, sync offset (§3.16.1) · RV-060 multi-track audio/subtitle switching
  (§3.16.2) · RV-061 mixed-media slide show and playlist loop policy (§3.2.6, §3.12)
  wired to RV-032.
- **Out of M5 by decision, the work itself unchanged:** RV-062 (D3D11VA
  zero-copy, §5.7) waits for a machine that can measure it — D-11 chose the
  zero-copy shape over a cheaper read-back one, so it is a piece of work of
  its own rather than a corner of this milestone. RV-059 is
  complete: subtitle files beside a video and text subtitle streams inside
  it are both found, shown and switched (D-12); picture-based subtitle
  formats stay out, since nothing can draw them.
- RV-084 audio-only files (§5.1 audio rows) play from the playlist and slide show with
  embedded album art shown — the D-6 minimum; everything else of §3.14 is post-1.0.
  RV-063 (Windows Media Foundation fallback) is withdrawn (D-4).
- Depends on: M3 (toolbox, keymap), M4 (RV-044 for pre-buffering).
- Done means: a file with two audio tracks and a subtitle file beside it plays
  in sync and both can be switched while it plays, `.`/`,` step single frames,
  the A-B loop takes and releases, and with no FFmpeg DLL present every format
  Media Foundation knows still plays while the rest are reported and skipped
  (D-9). Reached 2026-09-13; the sound itself is heard in T058, which needs a
  machine with an audio device.

**M6 — Editing, export, batch (H core + W UI)**
- Goal: adjustments preview on the GPU and commit through the core engine; batch runs
  headless.
- RV-064 Direct2D effect graph preview — exposure, colour matrix, gamma (§4.1.4,
  §3.13 preview layer) · RV-065 adjustment panel, curves widget with histogram, levels,
  manual crop overlay (§3.13, §6.2 UI) · RV-066 `pal_image_wic` encoders (§3.10 formats) · RV-018 quick export dialog with
  per-format controls and privacy-clean toggle (§3.10) ·
  RV-067 multi-threaded tiled resampling on `job.h` (§6.5.5) · RV-017 batch engine and
  `--batch` CLI on RV-033 with per-thread arenas (§3.11 execution engine) · RV-068
  batch dialog (§3.11 UI).
- RV-069 lossless JPEG rotation and flip through vendored `libjpeg-turbo` coefficient
  read/write (`jpeg_read_coefficients` / `jpeg_write_coefficients`, the `jpegtran`
  path) — no pixel decode, WIC stays the only pixel codec (D-5).
- Depends on: M3; RV-017 on M1 (RV-033) and RV-044 (job pool).
- Done means: `rubraview.exe --batch --resize=50% --format=webp <dir>` converts a
  directory with no window; the same directory converted through the dialog yields
  byte-identical output; the privacy-clean export of a GPS-tagged JPEG contains no APP1
  segment (checked with a hex dump).

**M7 — File management, lifecycle, shell (W)**
- RV-070 recycle-bin delete with session undo, permanent purge, inline rename with the
  native `EDIT` control (§3.18.1–3.18.2) · RV-071 1–9 quick-folder curation with undo
  (§3.18.3) · RV-072 single-instance mutex and `WM_COPYDATA` IPC (§3.19.1) · RV-073
  `WM_DROPFILES` and OLE `IDropTarget` ingestion (§3.19.2) · RV-074
  `--register-shell` / `--unregister-shell` under `HKCU` (§3.19.3).
- Depends on: M3.
- Done means: a second `rubraview.exe <file>` launches in the running window and exits
  0; dragging three files from Explorer plays them as a temporary playlist;
  `--unregister-shell` leaves no `Rubraview.*` ProgID in `HKCU\Software\Classes`.

**M8 — Music player (W; post-1.0 by D-6)**
- RV-075 audio-only playback, gapless pre-buffer, crossfade (§3.14.1) · RV-076 album
  art extraction, blurred backdrop, track OSD (§3.14.2.1, §3.14.2.4) · RV-077 1024-point
  FFT, 64-band analyser, oscilloscope (§3.14.2.2–3.14.2.3) · RV-078 `.lrc`, `SYLT`/`USLT`
  synced lyrics (§3.14.3) · RV-079 `.cue` virtual tracks (§3.14.4) · RV-080 10-band EQ,
  ReplayGain, night mode (§3.14.5) · RV-081 mini-player window and BGM arbiter
  (§3.14.6).
- Depends on: M5 (RV-055 audio engine, RV-084 minimum). The FFT, EQ, `.lrc` and `.cue`
  parsers are H and can be built and tested on Linux before the Windows wiring.
- Done means: not part of 1.0; defined when M8 is scheduled.

**M9 — Settings window and release (W)**
- RV-082 tab-styled settings window, all eight tabs, `Apply` without closing, keymap
  rebinding with conflict detection (§3.22) · RV-083 release packaging under
  `dist/rubraview-v<version>/` (owner, 2026-09-11), manual under `docs/manual/`,
  `CHANGELOG.md` entry.
- Depends on: every milestone whose settings the tabs expose.
- Done means: every key in `settings.ini` written by earlier milestones is reachable from
  a tab, and a fresh machine runs the packaged `rubraview.exe` with no DLL beside it
  except the optional FFmpeg set.

### 11.3 Traceability: Specification Section → Milestone → Backlog Id

| § | Subject | Milestone | Ids | H/W |
| :--- | :--- | :--- | :--- | :--- |
| 3.1 | Viewport, traversal, pre-cache, filmstrip, OSD | M2 · M3 · M4 | RV-011, RV-035, RV-039, RV-042, RV-044 | W |
| 3.2.1 | Sub-second timer, stress caching | M1 · M3 · M4 | RV-032, RV-015, RV-044 | H+W |
| 3.2.2 | Ingestion scopes | M1 | RV-032 | H |
| 3.2.3 | Multi-criteria sorting | M0 · M1 | RV-008, RV-031 | H |
| 3.2.4 | Extension and media-type filtering | M1 | RV-032 | H |
| 3.2.5 | Presentation controls, transitions | M3 | RV-015 | W |
| 3.2.6 | Mixed-media slide show | M1 · M5 | RV-032, RV-061 | H+W |
| 3.3 | Layout engine, spreads, webtoon, splitting | M1 · M2 · M4 | RV-021, RV-012, RV-047 | H+W |
| 3.4 | Fit modes, fit lock | M1 · M2 | RV-022, RV-011 | H+W |
| 3.5 | Pixel-art rendering, pixel grid | M2 | RV-013 | W |
| 3.6.1–3.6.4 | Toolbox, menu box, anchors, tiles | M3 | RV-020 | W |
| 3.6.5 | Multi-touch gestures | M3 | RV-037 | W |
| 3.7.1–3.7.2 | Context-aware keymap, hotkey table | M1 · M3 | RV-030, RV-038 | H+W |
| 3.7.3–3.7.4 | Mouse zones, touch and pen | M3 | RV-037 | W |
| 3.7.5 | `keymap.ini` | M1 · M3 | RV-023, RV-030, RV-038 | H+W |
| 3.8.1 | ZIP VFS, O(1) index, streaming, traversal | M1 · M4 | RV-025, RV-045, RV-051 | H+W |
| 3.8.2 | 7z solid/non-solid, RAR | M4 · post-1.0 | RV-052, RV-053 (post-1.0) | H+W |
| 3.8.3 | Filename encoding detection and override | M1 · M4 | RV-026, RV-046 | H+W |
| 3.8.4 | NFC normalisation | M1 | RV-024 | H |
| 3.8.5 | `ComicInfo.xml` | M1 · M4 | RV-027, RV-047 | H+W |
| 3.9 | Rotation, EXIF orientation, lossless JPEG | M1 · M3 · M6 | RV-029, RV-041, RV-069 | H+W |
| 3.10 | Quick export, privacy clean | M1 · M6 | RV-029, RV-066, RV-018 | H+W |
| 3.11 | Batch pipeline, action chain, execution | M1 · M6 | RV-033, RV-017, RV-068 | H+W |
| 3.12 | Playlists, collections, bookmarks | M1 · M5 | RV-005, RV-061 | H+W |
| 3.13 | Adjustment panel, curves, crop | M0 · M6 | RV-002, RV-004, RV-064, RV-065 | H+W |
| 3.14 | Music player | M5 (minimum) · M8 post-1.0 | RV-084, RV-075 – RV-081 | H+W |
| 3.15.1 | Win32 file dialog | M3 | RV-043 | W |
| 3.15.2 | In-app Metro picker | M3 | RV-043 | W |
| 3.15.3 | macOS / Linux dialog backends | post-1.0 (D-1) | — | — |
| 3.15.4 | Type-ahead, native IME search bar | M3 | RV-043 | W |
| 3.16.1 | Subtitle formats, rendering, sync | M5 (text streams inside the file: D-12; picture formats out) | RV-059 | W |
| 3.16.2 | Multi-track switching | M5 | RV-060 | W |
| 3.17 | Reading history, resume, portable mode | M1 · M4 | RV-023, RV-048 | H+W |
| 3.18 | Delete/undo, rename, 1–9 curation | M7 | RV-070, RV-071 | W |
| 3.19 | Single instance, drag-and-drop, shell registration | M7 | RV-072, RV-073, RV-074 | W |
| 3.20 | Animated images, multi-page TIFF, ICO | M4 | RV-049 | W |
| 3.21.1 | Frameless window mechanics | M2 | RV-034 | W |
| 3.21.2–3.21.3 | Hover titlebar, state buttons | M3 | RV-040 | W |
| 3.22 | Settings window | M9 | RV-082 | W |
| 4.1 | D2D interop, viewport matrix, compositor, effect graph | M1 · M2 · M6 | RV-022, RV-011, RV-012, RV-064 | H+W |
| 4.2 | Per-Monitor V2 HiDPI | M2 | RV-010 | W |
| 4.3 | Wide colour gamut | M4 | RV-050 | W |
| 5.1 | Format matrix | M5 | RV-016, RV-054, RV-084 | W |
| 5.2 | Dynamic loading, no fallback | M5 | RV-016 (RV-063 withdrawn) | W |
| 5.3 | Demux, decode, sync, seek, capture | M5 | RV-054, RV-056 | W |
| 5.4 | WASAPI audio engine | M5 | RV-055 | W |
| 5.5 | A-B looping | M5 | RV-057 | W |
| 5.6 | Video fit modes, live zoom | M5 | RV-058 | W |
| 5.7 | D3D11VA | after M5 (D-11) | RV-062 | W |
| 6.1–6.5 | Colour, curves, auto-crop, filters, resampling | M0 · M6 | RV-001 – RV-004, RV-067 | H |
| 7.1–7.3 | Arenas, `u8str_t`, dynarrays | M0 | RV-001, RV-007 | H |
| 7.4 | Memory budget, LRU | M1 · M4 | RV-028, RV-044 | H+W |
| 8.1–8.4 | PAL matrix, layout, verification ladder | M2 (Windows column only) | RV-019, RV-009 | W |
| 9 | Build pipeline | M0 · M2 | RV-006, RV-019 | H+W |
| 10 | Security model | M1 · M4 | RV-025 (size caps), RV-028 | H |

### 11.4 Owner Decisions (all decided 2026-09-08)

The options are kept for the record; the **Decided** line is what the milestones assume. Vendored libraries are admitted under the §8.1 rule added the same day: permissive licence redistributable with MIT, one module each, ledger entry in `docs/resources/`.

- **D-1 Multi-platform PAL.** §8 plans Linux and macOS back ends; all four accepted
  decisions and the distribution line say Windows x86_64. Options: (a) record §8 as a
  decision and keep the PAL headers platform-neutral now; (b) keep §8 as intent only,
  Windows-only until 1.0; (c) remove §8.2's non-Windows columns. Blocks: nothing in
  M0–M9; determines whether `include/rubraview/pal/*.h` grow host implementations
  beyond test mocks.
  **Decided: (b).** Windows-only until 1.0; §8 stays as intent; PAL headers carry no Win32 types.
- **D-2 `deflate` for CBZ.** §3.8.1 needs an inflater; §8.1 forbids non-`proven`
  dependencies in `src/core/`, and `proven` has none. Options: (a) vendor a permissive
  single-file inflater (`miniz`, `stb`-style) under `vendor/` with a
  `docs/resources/` ledger entry; (b) write a bounded C23 inflater in-house;
  (c) `stored`-only CBZ until (a) or (b). Blocks: RV-051.
  **Decided: (a)** — vendor `miniz` (MIT). An in-house inflater would need its own fuzzing history to satisfy §10.
- **D-3 CB7 and CBR.** §3.8.2 names the public-domain LZMA SDK for 7z (vendoring still
  needs the ledger). RAR has no pure-C permissively licensed reader that the author of
  this revision could confirm; `libarchive`'s RAR reader is C/BSD but is a large
  dependency, and the official UnRAR source is C++. Options for CBR: (a) `libarchive`
  subset; (b) shell out to an external unrar the user installs; (c) drop CBR from 1.0.
  Blocks: RV-052, RV-053.
  **Decided:** CB7 via the LZMA SDK (public domain) in M4; **CBR is post-1.0** — no pure-C permissively licensed reader was found, and `libarchive`/external `unrar` conflict with the zero-dependency rule.
- **D-4 Windows Media Foundation fallback.** §5.2 offers WMF when FFmpeg is absent;
  DECISIONS 2026-09-07 chose FFmpeg *because* WMF is insufficient. Options: (a) keep as
  a limited MP4/MP3 fallback; (b) drop it — no FFmpeg means image viewer only.
  Blocks: RV-063.
  **Decided: (b)** — dropped; §5.2 amended. One decode path, one test surface.
- **D-5 Lossless JPEG rotation.** §3.9 requires DCT-domain rearrangement, which WIC
  does not offer. The metadata strip in §3.10 is a plain marker walk and is scheduled
  (RV-029) regardless. Options: (a) vendor `libjpeg-turbo` for `jpegtran`-class
  transforms; (b) in-house DCT block transposer; (c) drop lossless rotation, keep
  non-destructive view rotation only. Blocks: RV-069.
  **Decided: (a)** — vendor `libjpeg-turbo` (BSD-3-Clause + IJG + zlib, all redistributable with MIT) for the coefficient-transform path only; in-house DCT-domain code was judged too large (entropy decode/encode, block transposition, subsampling edges). Pixel decoding stays WIC-only. Licence texts ship with the binary.
- **D-6 Music player scope.** §3.14 is a second product (gapless engine, FFT, lyrics,
  cue, EQ, mini-player). Options: (a) in 1.0 as M8; (b) after 1.0; (c) reduce to
  "audio files play in the playlist with album art" and fold into M5. Blocks:
  RV-075–RV-081.
  **Decided: (b) with (c)'s minimum** — audio files play with album art in M5 (RV-084); the rest of §3.14 is post-1.0.
- **D-7 `-Werror`.** §8.4 promises `-Werror`; the Makefile does not pass it. Options:
  (a) add it now (RV-009, cheap while the tree is small); (b) keep warnings advisory.
  **Decided: (a)** — measured warning-free on 2026-09-08.

### 11.5 Explicitly Not Scheduled

Listed so that silence is not read as "planned". **Post-1.0** items keep their ids and
their §3 text but have no milestone:

- CBR reader (RV-053) — D-3.
- Music player beyond the M5 minimum (RV-075–RV-081, M8) — D-6.
- Windows Media Foundation fallback (RV-063) — withdrawn, not post-1.0 — D-4.

- Linux and macOS native back ends (§3.15.3, §8.2 non-Windows columns) — awaiting D-1.
- T3 multi-platform smoke builds (§8.4) — awaiting D-1.
- `nob.c` build driver (§9.1) — the Makefile suffices; revisit if the Makefile stops
  being enough.
- Separate-process FFmpeg isolation (§10.1) — worker-thread isolation is scheduled
  (RV-054); process isolation is not.

### 11.6 Backlog Mirror

`BACKLOGS.md` carries one line per id above under `## Ready` (M1), `## Later`
(M2–M7, M9) and `## Post-1.0` (M8, RV-053); `## Blocked` is empty since D-1..D-7 were decided. When this section and `BACKLOGS.md`
disagree, this section is the source: correct it here first, then the backlog line.
