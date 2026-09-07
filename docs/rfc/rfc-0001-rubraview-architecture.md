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
|  - `rv_video_io`: FFmpeg Decoder Bridge (libav* dynamic loader)         |
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

### 3.2 Slide Show Engine
- **Autonomous Playback**:
  - Configurable advance interval (1 to 60 seconds, default: 3.0s).
  - Looping modes: Loop all, Play once and stop, Random / Shuffle.
- **Fullscreen Presentation**:
  - Borderless fullscreen mode (`F11` or `Enter`) maximizing display real estate.
  - Automatic mouse cursor hiding after 1.5 seconds of mouse stillness.
- **Transition Effects**:
  - Direct2D hardware-accelerated transitions: Instant cut, Cross-fade (alpha blend), Slide left/right, Zoom-in fade.
- **Interactive Control**:
  - `Space`: Pause / Resume.
  - Mouse hover, zoom, or pan interaction temporarily pauses the auto-advance timer until the user returns to the default viewport state.

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

### 3.6 Touch-Friendly Metro Tile Interface & Remote Desktop (RDP) Optimization
When accessing a Windows PC from a mobile phone via Remote Desktop Protocol (Microsoft Remote Desktop, Chrome Remote Desktop, Moonlight), desktop UI controls with small dropdowns or thin bars become frustrating and error-prone.

```
+-------------------------------------------------------------------------+
|                                                                         |
|                          [ Main Canvas Area ]                           |
|                                                                         |
+-------------------------------------------------------------------------+
|  Touch Overlay Bar (Metro Square Tiles, Auto-hide or Pin):              |
|  +--------+ +--------+ +--------+ +--------+ +--------+ +--------+      |
|  |   ◀    | |   ▶    | |   ⟳    | |   📖   | |   ⊡    | |   ⚙    |      |
|  |  Prev  | |  Next  | | Rotate | |  Book  | |  Fit   | |  Menu  |      |
|  +--------+ +--------+ +--------+ +--------+ +--------+ +--------+      |
|   (64x64)    (64x64)    (64x64)    (64x64)    (64x64)    (64x64)        |
+-------------------------------------------------------------------------+
```

1. **Square Tile Touch Targets**:
   - Function buttons are shaped as geometric square tiles with a minimum touch surface of $48\times 48\text{ px}$ (default $64\times 64\text{ px}$).
   - Generous touch margins prevent accidental mis-taps with thumbs.
   - Design inspired by the clean, flat geometry of Windows 8 Metro UI.
2. **Zero Ornamental Lag / RDP Network Optimization**:
   - RDP connections suffer severe frame drops and bandwidth spikes when rendering smooth gradient animations, continuous alpha fading, or motion blur.
   - Rubraview intentionally avoids ornamental UI animations. UI tile overlays toggle states instantly (0ms cut), and button feedback uses simple high-contrast border state changes.
3. **Multi-Touch Gestures**:
   - Native `WM_GESTURE` handling:
     - `GID_ZOOM`: Two-finger pinch-to-zoom centered on gesture midpoint.
     - `GID_PAN`: Two-finger drag to pan viewport.
     - Single-finger swipe left/right for page flips.

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

## 5. Multimedia & Video Playback Subsystem

### 5.1 FFmpeg C API Integration
To fulfill the requirement of viewing all video and multimedia formats without depending on brittle OS codec installations, Rubraview utilizes FFmpeg (`libavcodec`, `libavformat`, `libavutil`, `libswscale`).

### 5.2 Dynamic Loading & Graceful Fallback
Rather than hard-linking FFmpeg DLLs (which would prevent the application from launching if DLLs are missing), Rubraview implements a dynamic loader:
1. At startup, the video PAL checks for `avcodec-*.dll` and `avformat-*.dll` in the application directory or system path.
2. If present, it resolves function pointers dynamically (`avformat_open_input`, `avcodec_send_packet`, `avcodec_receive_frame`, `sws_scale`).
3. If absent, Rubraview operates in pure image-viewer mode, or falls back to basic Windows Media Foundation (`IMFSourceReader`) for elementary MP4 files.

### 5.3 Frame Extraction & Synchronization Pipeline
```
[ Video File ] ---> avformat_open_input() / av_read_frame()
                            | (Compressed Packets)
                            v
                    avcodec_send_packet()
                    avcodec_receive_frame()
                            | (Raw YUV420P / NV12 Frame)
                            v
                    sws_scale() (or GPU Pixel Shader)
                            | (32-bit BGRA Pixel Buffer)
                            v
                    Direct2D Bitmap Upload & Presentation (Audio-Clock Sync)
```

- **Frame Stepping**: The decoder preserves frame timestamps (`pts`). Backward and forward step buttons seek to keyframes (`av_seek_frame`) and decode forward to the exact frame target.
- **Still Frame Capture**: The active decoded video frame can be cloned directly into an `rv_pixbuf_t`, allowing video stills to enter the image adjustment and filtering pipeline.

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

### 6.1 Color Adjustments
- **Brightness & Exposure**: Linear scaling in non-linear sRGB creates color distortion. Rubraview converts sRGB to linear RGB via a lookup table (LUT), applies exposure scale $C_{linear}' = C_{linear} \cdot 2^{EV}$, and converts back.
- **Contrast**: $C' = (C - 0.5) \cdot \text{factor} + 0.5$.
- **Hue & Saturation**: RGB to HSL transform, delta adjustment, and HSL to RGB reconstitution.

### 6.2 Resampling Filters
High-quality resizing is essential for both display and batch export:
1. **Nearest Neighbor**: Fast preview and pixel-art rendering.
2. **Bilinear**: Standard 2x2 interpolation.
3. **Bicubic**: Catmull-Rom cubic spline filtering (sharp edges without ringing).
4. **Lanczos-3**: Windowed sinc filter ($\text{sinc}(x) \cdot \text{sinc}(x/3)$) with radius $r=3$, delivering optimal sharpness for downsampling large photographs.

### 6.3 Spatial Filtering
Convolutions are executed over separable kernels where possible ($O(K)$ per pixel instead of $O(K^2)$):
- **Gaussian Blur**: Separable 1D horizontal pass followed by 1D vertical pass.
- **Unsharp Mask (Sharpening)**: $\text{Sharpened} = \text{Original} + \alpha \cdot (\text{Original} - \text{Blurred})$.
- **Edge Detection**: Sobel or Laplacian 3x3 convolution matrix.

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
