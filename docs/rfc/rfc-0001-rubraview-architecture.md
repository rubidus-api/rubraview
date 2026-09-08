# RFC-0001: Rubraview Architecture and Multimedia Pipeline

- Status: Accepted
- Author: Antigravity Agent
- Date: 2026-09-07
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
|  - `rv_image_io`: WIC Decoder/Encoder (Windows) / Portable Host Stubs   |
|  - `rv_video_io`: FFmpeg Video Bridge (libav* dynamic loader)          |
|  - `rv_audio_io`: WASAPI Audio Renderer (Windows) / Host Mock Sink     |
|  - `rv_archive_io`: In-memory CBZ (ZIP), CBR (RAR), CB7 (7z) Streams    |
|  - `rv_sysio`: Native directory enumeration & natural alphanumeric sort |
|  - `rv_threadpool`: Pre-caching worker threads & batch job dispatching  |
+-------------------------------------------------------------------------+
                                   |
                                   v
+-------------------------------------------------------------------------+
|                  Core Engine (Pure C23, Portable)                       |
|  - Pixel Buffers (`rv_pixbuf_t`): 8-bit RGBA, 16-bit Float, Grayscale    |
|  - Color Space Math: sRGB <-> Linear, HSL, Exposure, Contrast, Gamma    |
|  - Spatial Filters: Gaussian Blur, Laplacian Sharpen, 3x3/5x5 Kernel    |
|  - Resampling Kernels: Nearest, Bilinear, Bicubic (Catmull-Rom), Lanczos|
|  - Layout Engine (`rv_layout_engine`): Intelligent Spread & AR Matcher  |
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
3. **Codec Decoupling**: The GUI layer interacts only with abstract pixel buffers (`rv_pixbuf_t`), video streams (`rv_video_stream_t`), and archive streams (`rv_archive_t`).
4. **Memory Ownership**: All temporary buffers allocated during single-frame rendering, archive decompression, or batch conversions are managed via `prv_arena_t` instances, ensuring zero heap fragmentation and deterministic teardown.

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
Before starting the presentation, the playback sequence can be sorted by:
- **File Name (Natural Alphanumeric - Default)**: Standard natural sorting (e.g. `1.jpg`, `2.jpg`, `10.jpg`, not alphabetical `1, 10, 2`). Ascending or Descending.
- **Date Modified / Created**: Chronological order (Newest first or Oldest first).
- **File Size**: Sorted by byte length (Smallest first or Largest first).
- **Random Shuffle**: Cryptographically uniform Fisher-Yates shuffle with seed preservation (allows `Previous` key to accurately step back through the shuffled history without re-randomizing).

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
For viewing comic books, manga, scanned documents, and multi-page albums, Rubraview implements an intelligent layout engine (`rv_layout_engine`):

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

1. **Single Page Mode (`RV_PAGE_LAYOUT_SINGLE`)**: Standard centered view.
2. **Dual Page Mode (`RV_PAGE_LAYOUT_DUAL`)**:
   - Renders two consecutive images side-by-side with a configurable gutter (0 to 16 pixels).
   - Page flip advances by 2 pages.
3. **Book / Manga Mode (`RV_PAGE_LAYOUT_BOOK`)**:
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

### 3.4 Viewport Fit & Alignment Modes
Rubraview implements six deterministic viewport fitting modes (`rv_fit_mode_t`):

| Fit Mode | Identifier | Description & Use Case |
| :--- | :--- | :--- |
| **Fit to Window (Inside)** | `RV_FIT_WINDOW` | Scales image proportionally so the entire image fits within the window. Letterboxes/pillarboxes as necessary. Default mode. |
| **Fit to Width** | `RV_FIT_WIDTH` | Scales image width to window width ($\text{scale} = W_{win} / W_{img}$). Image height extends beyond screen; scrollable vertically. Ideal for webtoons and vertical documents. |
| **Fit to Height** | `RV_FIT_HEIGHT` | Scales image height to window height ($\text{scale} = H_{win} / H_{img}$). Image width extends beyond screen; scrollable horizontally. Ideal for wide panoramic photos. |
| **Stretch to Fill** | `RV_FIT_STRETCH` | Scales width and height independently to fill the exact window dimensions, ignoring aspect ratio. |
| **Original Size (100%)** | `RV_FIT_ACTUAL_SIZE` | 1:1 pixel mapping ($\text{scale} = 1.0$). One image pixel equals exactly one screen pixel. |
| **Smart Fit** | `RV_FIT_SMART` | If image dimensions $> W_{win}$ or $> H_{win}$, scale down to fit inside; if image is smaller than window, display at 100% original size to prevent blurry upscaling. |

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

#### 3.6.1 The Toolbox (`rv_toolbox`): Playback, Navigation & Detachable Window
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
  - **Detached Mode (`rv_toolbox_window`)**:
    - When the user drags the toolbox across the outer window perimeter (or clicks a `[ Detach ↗ ]` tile), it seamlessly transitions into an independent top-level Win32 tool window (`WS_POPUP | WS_EX_TOOLWINDOW | WS_EX_LAYERED`).
    - The detached window can float anywhere across the multi-monitor desktop workspace, remaining on top of other applications (`HWND_TOPMOST` optional toggle).
    - Dragging the detached window back over the main canvas surface docks it back into the in-window Direct2D overlay mode.

#### 3.6.2 The Menu Box (`rv_menubox`): In-Window Hierarchical Settings & Navigation
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

### 3.7 Keyboard-First Control Matrix
Rubraview is fully operable via keyboard without ever touching a mouse:

| Action | Primary Hotkey | Alternative |
| :--- | :--- | :--- |
| **Next Page / Frame** | `Right Arrow` | `PageDown`, `Space`, `Enter` |
| **Previous Page / Frame** | `Left Arrow` | `PageUp`, `Backspace` |
| **First / Last Page** | `Home` | `End` |
| **Zoom In / Out** | `+` (or `=`) | `-` (or `_`) |
| **Reset Zoom (100%)** | `0` | `Ctrl+0` |
| **Pan Viewport** | `Ctrl + Arrow keys` | Drag with mouse / touch |
| **Fit Mode: Window** | `1` | Menu tile |
| **Fit Mode: Width** | `2` | Menu tile |
| **Fit Mode: Height** | `3` | Menu tile |
| **Fit Mode: Original 1:1**| `4` | Menu tile |
| **Fit Mode: Smart Fit** | `5` | Menu tile |
| **Toggle Fit Lock** | `L` | Menu tile |
| **Toggle Book / Manga** | `B` | Menu tile |
| **Toggle Dual Page** | `D` | Menu tile |
| **Switch LTR / RTL** | `T` | Menu tile |
| **Rotate 90° CW / CCW** | `R` | `Shift+R` |
| **Flip Horizontal / Vert** | `H` | `V` |
| **Toggle Fullscreen** | `F11` | `F` |
| **Toggle Slide Show** | `S` | Menu tile |
| **Toggle Pixel Grid** | `G` | Menu tile |
| **Toggle Metro Tile Bar**| `M` | `Esc` |
| **Quick Export / Save As**| `Ctrl+Shift+S`| `Ctrl+E` |
| **Open Batch Dialog** | `Ctrl+B` | Menu tile |
| **Previous / Next Archive**| `[` | `]` |

### 3.8 Comic Archive Container Subsystem (CBZ, CBR, CB7)
To support digital comic books and manga packages directly without prior manual extraction:

1. **Direct In-Memory Streaming**:
   - Supported extensions: `.cbz` (ZIP), `.cbr` (RAR), `.cb7` (7z).
   - The PAL archive reader (`rv_archive_io`) opens the archive file header, extracts the file manifest, filters for supported image mime types, and naturally sorts the internal page entries.
   - Individual page files are decompressed directly into memory arenas (`prv_arena_t`) on demand. **Zero temporary files are created on disk**.
2. **Archive Pre-caching**:
   - Consecutive compressed streams are decompressed in worker threads ahead of time, ensuring reading comics from `.cbz` feels identical to reading uncompressed folders.
3. **Seamless Folder Navigation Across Archives**:
   - Pressing `[` or `]` navigates to the previous or next archive in the parent directory (e.g. automatically opening `Volume 02.cbz` after reaching the last page of `Volume 01.cbz`).

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
- **Metadata Handling**:
  - Toggle to keep or strip EXIF, XMP, and GPS location metadata for privacy-sensitive exporting.

### 3.11 Batch Processing Subsystem
A robust batch engine designed for bulk media processing:
- **Input Pipeline**:
  - Multiple file selection, directory tree scanning (with optional recursive subfolder traversal), or drag-and-drop ingestion.
  - Filter criteria: Include/exclude file patterns (`*.jpg;*.png`), minimum/maximum file size, or dimension thresholds.
- **Action Chain**:
  1. *Orientation*: Apply EXIF rotation or manual fixed rotation/flip.
  2. *Resizing*: Resize by percentage ($50\%$), bounding box ($1920\times 1080$ fit inside), fixed width, or fixed height using selectable resampling algorithm (Lanczos-3, Bicubic, Bilinear, Nearest).
  3. *Color & Filters*: Bulk exposure correction, contrast boost, unsharp mask sharpening, or grayscale conversion.
  4. *Format Conversion*: Target format, quality factor, and compression parameters.
  5. *Output Naming*: Flexible naming patterns (e.g. `{name}_thumb.{ext}`, `{date}_{name}_{w}x{h}.{ext}`).
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
  - *Commit Layer (C23 Core Engine)*: When the user clicks "Apply" (`Enter`) or "Save Copy" (`Ctrl+S`), the core engine executes the exact math on the persistent `rv_pixbuf_t` buffer.
- **Color & Tone Controls**:
  - Sliders: Exposure ($\pm 3.0\,EV$), Brightness ($\pm 100$), Contrast ($\pm 100$), Saturation ($\pm 100$), Color Temperature (Warm/Cool), Tint (Green/Magenta).
- **Interactive Color Graph & Tone Curves (`rv_curves`)**:
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

### 3.14 Audio Playback, Music Player Mode & Background Music (BGM)
Rubraview seamlessly handles pure audio files as first-class media items, bridging image viewing and music playback:
- **Audio Format Compatibility**:
  - Uncompressed / Lossless: WAV, FLAC, AIFF, APE.
  - Compressed: MP3, AAC, M4A, OGG / OGA (Vorbis/Opus), WMA (v1/v2/Pro).
  - Legacy Streaming: RealAudio (`.ra`, `.ram` via Cook / ATRAC / 14.4 / 28.8 codecs).
- **Embedded Cover Art Extraction**:
  - Automatically extracts embedded album artwork from audio tags (ID3v2 APIC, FLAC Vorbis comments, MP4 `covr`, WMA metadata).
  - The extracted cover is decoded into an `rv_pixbuf_t` and presented prominently on the Direct2D canvas as a high-resolution square album cover with an optional blurred background backdrop.
- **Dynamic Waveform Visualizer**:
  - When an audio file contains no embedded cover art, the canvas renders a clean, real-time audio waveform visualizer or 32-band spectrum analyzer computed from the decoded PCM audio buffer.
  - On-Screen Display (OSD) presents Track Title, Artist, Album, Year, Duration, Bitrate (kbps), and Sample Rate (Hz).
- **Dedicated Audio HUD & Controls**:
  - Minimalist floating or docked touch tile controls: Play / Pause (`Space`), Seek scrubber (`Left`/`Right` arrow $\pm 5\text{s}$, `Ctrl + Left/Right` $\pm 30\text{s}$), Volume slider (`Up`/`Down` arrow $\pm 5\%$), Mute (`M`).
- **Slide Show Background Music (BGM Mode)**:
  - Users can attach an audio track or background playlist to an ongoing image slide show. The audio stream plays continuously while images auto-advance according to their configured interval.

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
   - For basic MP4 video and WAV/MP3 audio, the engine provides an optional native fallback using Windows Media Foundation (`IMFSourceReader`) and Direct2D.

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
- **Still Frame Capture**: Clicking `Ctrl+C` or the Capture tile instantly captures the current video frame as a full-resolution `rv_pixbuf_t` and pushes it into the clipboard or the Image Adjustment Workbench.

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
  - `RV_FIT_WINDOW`: Scales video proportionally to fit completely inside the window; letterbox/pillarbox as necessary (Default).
  - `RV_FIT_WIDTH`: Matches video width to window width; if video height exceeds window, user can vertically scroll/pan.
  - `RV_FIT_HEIGHT`: Matches video height to window height; if video width exceeds window, user can horizontally scroll/pan.
  - `RV_FIT_STRETCH`: Stretches video to fill the entire window surface, ignoring aspect ratio.
  - `RV_FIT_ACTUAL_SIZE`: 1:1 original pixel mapping (1 video pixel = 1 monitor pixel).
  - `RV_FIT_SMART`: If video dimensions exceed window, scale down to fit inside; if smaller, display at 100% original size to prevent blurry upscaling.
- **Live Wheel Zoom & Drag Pan During Active Playback**:
  - Users can zoom in and out of a video using the mouse wheel (or touch pinch gesture) **in real time while the video is playing**.
  - When zoomed in, clicking and dragging with the mouse (or touch drag) pans the video viewport freely across the active video surface without pausing playback.
  - Zoom and pan operations modify only the Direct2D affine transformation matrix $\mathbf{M}$ on the GPU, incurring 0% CPU decoding overhead.
- **Strict Window Stability Invariant**:
  - Starting a video, changing videos, toggling fit modes, or zooming in/out **NEVER resizes or repositions the outer application window**.

---

## 6. Core Image Processing & Filter Engine

The portable core engine operates on a standardized, cache-friendly pixel buffer:

```c
typedef enum rv_pixel_format {
    RV_PIXFMT_RGBA8 = 0,   // Standard 32-bit sRGB
    RV_PIXFMT_BGRA8,       // Direct2D/WIC native layout
    RV_PIXFMT_GRAY8,       // 8-bit luminance
    RV_PIXFMT_RGBA16F,     // High-dynamic-range floating point
} rv_pixel_format_t;

typedef struct rv_pixbuf {
    uint8_t           *pixels;
    int32_t            width;
    int32_t            height;
    int32_t            stride;       // Bytes per scanline
    rv_pixel_format_t  format;
    proven_arena_t    *arena;        // Owning arena (or NULL for external view)
} rv_pixbuf_t;
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
- **Spline-Based Tone Curve (`rv_curves`)**:
  - Supports Monotone Cubic Spline (Fritsch-Carlson) interpolation through user control points $(x_0, y_0), \dots, (x_k, y_k)$, guaranteeing no overshoot or unnatural oscillations.
  - Generates a 256-entry transformation LUT applied with $O(1)$ per pixel:
    $$\text{pixel}_{out} = \text{LUT}_{curve}[\text{pixel}_{in}]$$
  - Multi-channel support: Master (RGB), Red, Green, Blue, or Luminance channel.
- **Histogram & Levels Generator**:
  - Computes 256-bin histograms for R, G, B, and Luminance channels in a single vectorized pass.
  - Levels Tool calculates Black Point $B$, Midtone $\gamma$, and White Point $W$:
    $$V_{out} = 255 \cdot \left(\text{clamp}\left(\frac{V_{in} - B}{W - B}, 0, 1\right)\right)^{1/\gamma}$$

### 6.3 Threshold & Luminance-Based Auto-Crop Engine
- **Border Trim / Scan Margin Auto-Crop (`rv_crop_autotrim`)**:
  - Scans pixel luminance $Y = 0.2126R + 0.7152G + 0.0722B$ along image borders.
  - Identifies bounding boxes of meaningful content by advancing inward from Top, Bottom, Left, and Right until scanlines deviate from the background threshold:
    - *White Margins* (scanned manga / documents): Scanlines where $> 98\%$ of pixels have $Y \ge T_{white}$ (default 245).
    - *Black Margins* (letterboxed screenshots / video stills): Scanlines where $> 98\%$ of pixels have $Y \le T_{black}$ (default 15).
  - Produces a non-destructive cropped sub-buffer `rv_pixbuf_sub()` or rewrites the buffer in-place.
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

Rubraview rejects arbitrary `malloc()`/`free()` allocations in favor of structured memory arenas:

1. **Frame Scratch Arenas**:
   UI redraws, temporary scaling buffers, and thumbnail decoding use a transient frame arena that resets once per event cycle.
2. **Archive Stream Arenas**:
   Decompressed image buffers from CBZ/CBR/CB7 archives are allocated within dedicated file-level arenas that release memory immediately when navigated away.
3. **Batch Task Arenas**:
   Worker threads allocate input and output pixel buffers within dedicated thread-local arenas.
4. **String Safety**:
   File paths, EXIF keys, and UI labels are managed using `u8str_t` (immutable UTF-8 slices) from `proven_c_lib`, preventing buffer overruns and null-termination ambiguities.
5. **Dynamic Collections**:
   Directory file listings, playlist queues, and batch job lists use `prv_dynarray_t` for typed, amortized growth with boundary safety.

---

## 8. Platform Abstraction Layer (PAL) & Test Strategy

To uphold the core rule: **"An unrun test is a claim, not evidence"**, the architecture completely isolates platform-specific code:

```
rubraview/
├── include/
│   ├── rubraview/
│   │   ├── core.h           // Portable pixbuf, color, filters, resample
│   │   ├── layout.h         // Layout engine: AR matching & spread rules
│   │   ├── pal.h            // Platform abstraction interfaces
│   │   ├── archive.h        // CBZ, CBR, CB7 virtual archive streams
│   │   ├── batch.h          // Batch job queue & worker declarations
│   │   └── playlist.h       // Playlist and collection interfaces
├── src/
│   ├── core/                // Pure C23 (compiled on Linux and Windows)
│   │   ├── pixbuf.c
│   │   ├── color.c
│   │   ├── filters.c
│   │   ├── resample.c
│   │   ├── layout.c
│   │   ├── archive.c
│   │   ├── batch.c
│   │   └── playlist.c
│   ├── pal/
│   │   ├── win32/           // Windows implementations
│   │   │   ├── pal_wic.c
│   │   │   ├── pal_d2d.c
│   │   │   ├── pal_ffmpeg.c
│   │   │   ├── pal_archive_win.c
│   │   │   └── pal_fs_win.c
│   │   └── host/            // Linux host test implementations
│   │       ├── pal_mock_io.c
│   │       ├── pal_archive_posix.c
│   │       └── pal_fs_posix.c
│   └── app/                 // GUI entry & WinProc
│       ├── main_win.c
│       ├── view_modes.c
│       ├── touch_tile_ui.c
│       └── cli_batch.c
└── tests/                   // Executed natively on Linux
    ├── test_pixbuf.c
    ├── test_color.c
    ├── test_resample.c
    ├── test_filters.c
    ├── test_layout.c
    ├── test_archive.c
    ├── test_batch.c
    └── test_playlist.c
```

### Verification Ladder:
- **T0/T1 (Host Linux)**: `make test` runs all core algorithms under `gcc -std=c23 -Wall -Wextra -Werror` and AddressSanitizer (`-fsanitize=address,undefined`).
- **T2 (Cross-Build)**: MinGW-w64 x86_64 cross-compilation on `linux-build` validates WinAPI headers, Direct2D/WIC COM bindings, and PE binary generation.

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

- **Milestone 1 (Foundations & Core Engine)**:
  - Pixel buffer structures (`rv_pixbuf`), memory arena integration.
  - Image resampling kernels (Nearest, Bilinear, Bicubic, Lanczos-3).
  - Spatial filters (Blur, Sharpen) and color adjustments.
  - Intelligent layout engine (`rv_layout_engine`) for pre-merged spreads and AR matching.
  - Archive streaming engine (`rv_archive_io`) for CBZ, CBR, CB7.
  - Playlist data structures and parsers.
  - Comprehensive unit test suite running on Linux host.
- **Milestone 2 (Windows Canvas & WIC Decoder)**:
  - Win32 main window and message pump with fixed-window stability guarantee.
  - Direct2D render target initialization, sub-pixel pan/zoom matrix.
  - Viewport fit modes (Fit Window, Fit Width, Fit Height, Smart Fit, 1:1 Actual).
  - Multi-page layouts (Single, Dual, Book / Manga with LTR/RTL).
  - WIC loader for JPEG/PNG/WebP/TIFF into D2D bitmaps.
- **Milestone 3 (Touch Metro Tile UI & Slide Show)**:
  - Metro-style square tile touch overlay bar optimized for Remote Desktop.
  - Fullscreen slide show engine with auto-advance and Direct2D transition effects.
  - Non-destructive rotation, flip, and EXIF orientation handling.
  - Single-image export and format transcoding dialog.
- **Milestone 4 (Video Playback Engine)**:
  - Dynamic FFmpeg loader (`rv_ffmpeg_load()`).
  - Audio/video demux and decode loop.
  - Video presentation to D2D bitmap with frame stepping and capture.
  - Seamless mixed-media playlist integration.
- **Milestone 5 (Batch Engine & UI Polish)**:
  - Multi-threaded batch processor and headless CLI mode.
  - Filmstrip thumbnail gallery and inspector sidebar.
- **Milestone 6 (Distribution & Packaging)**:
  - Remote `linux-build` production cross-build.
  - Release packaging staging (`build/dist/`).
