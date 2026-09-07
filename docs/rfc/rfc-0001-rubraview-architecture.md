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

**Rubraview** (`rubraview`) resolves these issues by delivering a lightweight, zero-bloat, high-performance multimedia viewer and batch image processing tool designed with:
- **Pure C23 Foundation**: Structured upon memory arenas, string slices, and dynamic arrays provided by a vendored snapshot of `proven_c_lib`.
- **Zero-Dependency Native Image Subsystem**: Leveraging the **Windows Imaging Component (WIC)** for decoding/encoding JPEG, PNG, WebP, GIF, TIFF, BMP, and ICO without linking external image libraries (`libpng`, `libjpeg`, etc.).
- **Hardware-Accelerated Canvas**: Utilizing **Direct2D (D2D1) / Direct3D 11** for butter-smooth 60–144 FPS pan, sub-pixel zoom, and real-time shader-based image adjustments (exposure, contrast, saturation, blur, sharpen).
- **Universal Multimedia Engine**: An isolated Platform Abstraction Layer (PAL) with an **FFmpeg C API (`libavcodec`, `libavformat`, `libswscale`)** dynamic bridge, providing playback, frame-accurate seeking, and frame capture across any video format.
- **Dual-Mode Headless Core**: Image filters, geometric transformations, and batch pipelines exist in portable C23 modules that run and test natively on Linux hosts while compiling to native Windows binaries via MinGW-w64 on the `linux-build` build environment.

---

## 2. Layered Architectural Boundaries

Rubraview is strictly organized into four decoupled layers:

```
+-------------------------------------------------------------------------+
|                       Application Layer (GUI / CLI)                     |
|  - Win32 Window Procedure (WndProc), Raw Message Pump                   |
|  - Custom Dark UI Controls (Toolbar, Canvas Viewport, Filmstrip)        |
|  - Modal Dialogs (Batch Processor, Settings, Exif Inspector)            |
|  - Headless CLI Driver (`rubraview.exe --batch ...`)                    |
+-------------------------------------------------------------------------+
                                   |
                                   v
+-------------------------------------------------------------------------+
|                  Presentation & Rendering Layer (Direct2D)              |
|  - ID2D1Factory, ID2D1HwndRenderTarget / ID2D1DeviceContext             |
|  - Sub-pixel Pan/Zoom Viewport Matrix Transformations                   |
|  - Direct2D Built-in Effect Graph (Real-time Shader Adjustments)        |
|  - DirectWrite Font Rendering for On-Screen Display (OSD) & Exif Info   |
+-------------------------------------------------------------------------+
                                   |
                                   v
+-------------------------------------------------------------------------+
|                  Platform Abstraction Layer (PAL)                       |
|  - `rv_image_io`: WIC Decoder/Encoder (Windows) / Portable Host Stubs   |
|  - `rv_video_io`: FFmpeg Decoder Bridge (libav* dynamic loader)         |
|  - `rv_sysio`: Native directory enumeration & file watching             |
|  - `rv_threadpool`: Multi-threaded job dispatching                      |
+-------------------------------------------------------------------------+
                                   |
                                   v
+-------------------------------------------------------------------------+
|                  Core Engine (Pure C23, Portable)                       |
|  - Pixel Buffers (`rv_pixbuf_t`): 8-bit RGBA, 16-bit Float, Grayscale    |
|  - Color Space Math: sRGB <-> Linear, HSL, Exposure, Contrast, Gamma    |
|  - Spatial Filters: Gaussian Blur, Laplacian Sharpen, 3x3/5x5 Kernel    |
|  - Resampling Kernels: Nearest, Bilinear, Bicubic (Catmull-Rom), Lanczos|
|  - Batch Execution Engine: Job Queue, Worker Scheduling, Multi-file I/O |
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
2. **Codec Decoupling**: The GUI layer interacts only with the abstract `rv_pixbuf_t` and `rv_video_stream_t` interfaces, never directly calling WIC or FFmpeg COM/C APIs.
3. **Memory Ownership**: All temporary buffers allocated during single-frame rendering or batch conversions are managed via `prv_arena_t` instances, ensuring zero heap fragmentation and deterministic teardown.

---

## 3. Canvas & Rendering Pipeline

### 3.1 Direct2D GPU Acceleration
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
   Panning and zooming simply update `M` and trigger an immediate `ID2D1RenderTarget_SetTransform()` followed by `ID2D1RenderTarget_DrawBitmap()`. The GPU rasterizer scales the image using bilinear or bicubic texture filtering at native display refresh rates (60–144 Hz).

3. **Real-Time Adjustment Graph**:
   Direct2D 1.1+ exposes an effect pipeline:
   `Source Bitmap` $\rightarrow$ `Exposure Effect` $\rightarrow$ `Color Matrix (Contrast/Saturation)` $\rightarrow$ `Gamma Transfer` $\rightarrow$ `Output Canvas`.
   Sliders for brightness, contrast, and saturation modify shader constants directly, providing instant 60 FPS previews without executing CPU pixel loops. When the user clicks "Save" or "Apply", the Core Engine applies the transformation to the persistent pixel buffer.

---

## 4. Multimedia & Video Playback Subsystem

### 4.1 FFmpeg C API Integration
To fulfill the requirement of viewing all video and multimedia formats without depending on brittle OS codec installations, Rubraview utilizes FFmpeg (`libavcodec`, `libavformat`, `libavutil`, `libswscale`).

### 4.2 Dynamic Loading & Graceful Fallback
Rather than hard-linking FFmpeg DLLs (which would prevent the application from launching if DLLs are missing), Rubraview implements a dynamic loader:
1. At startup, the video PAL checks for `avcodec-*.dll` and `avformat-*.dll` in the application directory or system path.
2. If present, it resolves function pointers dynamically (`avformat_open_input`, `avcodec_send_packet`, `avcodec_receive_frame`, `sws_scale`).
3. If absent, Rubraview operates in pure image-viewer mode, or falls back to basic Windows Media Foundation (`IMFSourceReader`) for elementary MP4 files.

### 4.3 Frame Extraction & Synchronization Pipeline
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

## 5. Core Image Processing & Filter Engine

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
    prv_arena_t       *arena;        // Owning arena (or NULL for external view)
} rv_pixbuf_t;
```

### 5.1 Color Adjustments
- **Brightness & Exposure**: Linear scaling in non-linear sRGB creates color distortion. Rubraview converts sRGB to linear RGB via a lookup table (LUT), applies exposure scale $C_{linear}' = C_{linear} \cdot 2^{EV}$, and converts back.
- **Contrast**: $C' = (C - 0.5) \cdot \text{factor} + 0.5$.
- **Hue & Saturation**: RGB to HSL transform, delta adjustment, and HSL to RGB reconstitution.

### 5.2 Resampling Filters
High-quality resizing is essential for both display and batch export:
1. **Nearest Neighbor**: Fast preview and pixel-art rendering.
2. **Bilinear**: Standard 2x2 interpolation.
3. **Bicubic**: Catmull-Rom cubic spline filtering (sharp edges without ringing).
4. **Lanczos-3**: Windowed sinc filter ($\text{sinc}(x) \cdot \text{sinc}(x/3)$) with radius $r=3$, delivering optimal sharpness for downsampling large photographs.

### 5.3 Spatial Filtering
Convolutions are executed over separable kernels where possible ($O(K)$ per pixel instead of $O(K^2)$):
- **Gaussian Blur**: Separable 1D horizontal pass followed by 1D vertical pass.
- **Unsharp Mask (Sharpening)**: $\text{Sharpened} = \text{Original} + \alpha \cdot (\text{Original} - \text{Blurred})$.
- **Edge Detection**: Sobel or Laplacian 3x3 convolution matrix.

---

## 6. Batch Processing Subsystem

The batch engine supports batch resizing, format conversion, watermarking, and color adjustments across thousands of images.

### 6.1 Architecture
- **Job Queue**: A thread-safe work-stealing queue storing individual conversion specifications (`rv_batch_job_t`).
- **Worker Threads**: Sized according to hardware concurrency (`GetActiveProcessorCount()` or `sysconf(_SC_NPROCESSORS_ONLN)`).
- **Per-Thread Memory Arenas**: Each worker thread possesses its own scratch arena (`prv_arena_t`). Memory is reset at the completion of each file, guaranteeing constant memory consumption regardless of batch size.

### 6.2 CLI Execution Interface
Rubraview supports headless execution for CI/CD, scripts, and automated workflows:
```sh
rubraview.exe --batch \
  --input "C:\Photos\*.jpg" \
  --output "C:\Photos\Optimized" \
  --format webp --quality 85 \
  --resize 1920x1080 --resample lanczos \
  --sharpen 0.5 \
  --threads 8
```

---

## 7. Memory Model & proven_c_lib Integration

Rubraview rejects arbitrary `malloc()`/`free()` allocations in favor of structured memory arenas:

1. **Frame Scratch Arenas**:
   UI redraws, temporary scaling buffers, and thumbnail decoding use a transient frame arena that resets once per event cycle.
2. **Batch Task Arenas**:
   Worker threads allocate input and output pixel buffers within dedicated thread-local arenas.
3. **String Safety**:
   File paths, EXIF keys, and UI labels are managed using `u8str_t` (immutable UTF-8 slices) from `proven_c_lib`, preventing buffer overruns and null-termination ambiguities.
4. **Dynamic Collections**:
   Directory file listings and batch job queues use `prv_dynarray_t` for typed, amortized growth with boundary safety.

---

## 8. Platform Abstraction Layer (PAL) & Test Strategy

To uphold the core rule: **"An unrun test is a claim, not evidence"**, the architecture completely isolates platform-specific code:

```
rubraview/
├── include/
│   ├── rubraview/
│   │   ├── core.h           // Portable pixbuf, color, filters, resample
│   │   ├── pal.h            // Platform abstraction interfaces
│   │   └── batch.h          // Batch job queue & worker declarations
├── src/
│   ├── core/                // Pure C23 (compiled on Linux and Windows)
│   │   ├── pixbuf.c
│   │   ├── color.c
│   │   ├── filters.c
│   │   ├── resample.c
│   │   └── batch.c
│   ├── pal/
│   │   ├── win32/           // Windows implementations
│   │   │   ├── pal_wic.c
│   │   │   ├── pal_d2d.c
│   │   │   ├── pal_ffmpeg.c
│   │   │   └── pal_fs_win.c
│   │   └── host/            // Linux host test implementations
│   │       ├── pal_mock_io.c
│   │       └── pal_fs_posix.c
│   └── app/                 // GUI entry & WinProc
│       ├── main_win.c
│       └── cli_batch.c
└── tests/                   // Executed natively on Linux
    ├── test_pixbuf.c
    ├── test_color.c
    ├── test_resample.c
    ├── test_filters.c
    └── test_batch.c
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
  -Iinclude -Ivendor/proven/include \
  src/core/*.c src/pal/win32/*.c src/app/*.c \
  vendor/proven/src/*.c \
  -ld2d1 -ldwrite -lole32 -lwindowscodecs -lshcore \
  -o bin/rubraview.exe
```

---

## 10. Security & Safety Model

1. **Untrusted Codec Safety**: Media viewers frequently process malicious or malformed image/video payloads. WIC operates with Microsoft-maintained memory bounds, and FFmpeg decoding can be isolated to a separate worker thread or child process.
2. **Buffer Bounds Checking**: All image resamplers and convolution kernels strictly validate coordinate bounds. Coordinates outside $[0, W-1] \times [0, H-1]$ are clamped or mirrored, preventing out-of-bounds memory accesses.
3. **No Dynamic Code Execution**: The application links no scripting runtimes and executes zero unverified dynamic code.

---

## 11. Implementation Roadmap & Milestones

- **Milestone 1 (Foundations & Core Engine)**:
  - Pixel buffer structures (`rv_pixbuf`), memory arena integration.
  - Image resampling kernels (Nearest, Bilinear, Bicubic, Lanczos-3).
  - Spatial filters (Blur, Sharpen) and color adjustments.
  - Comprehensive unit test suite running on Linux host.
- **Milestone 2 (Windows Canvas & WIC Decoder)**:
  - Win32 main window and message pump.
  - Direct2D render target initialization, sub-pixel pan/zoom matrix.
  - WIC loader for JPEG/PNG/WebP/TIFF into D2D bitmaps.
- **Milestone 3 (Video Playback Engine)**:
  - Dynamic FFmpeg loader (`rv_ffmpeg_load()`).
  - Audio/video demux and decode loop.
  - Video presentation to D2D bitmap with frame stepping and capture.
- **Milestone 4 (Batch Engine & UI Polish)**:
  - Multi-threaded batch processor and headless CLI mode.
  - UI toolbar, filmstrip thumbnail gallery, and inspector sidebar.
- **Milestone 5 (Distribution & Packaging)**:
  - Remote `linux-build` production cross-build.
  - Release packaging staging (`build/dist/`).
