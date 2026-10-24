/*
 * Telling the reader why nothing appeared: the startup failure reported, the media and GPU probes, and the
 * step-by-step diagnostics (RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* ---- telling the reader why nothing appeared ---- */

/*
 * A viewer that shows nothing is the least useful failure there is, and
 * it cannot be diagnosed from another machine. So when the graphics
 * device will not start, the reason is put in front of the reader and
 * written to a file next to the program — with the operating system's
 * own error code, which is the part that identifies the cause.
 */
void report_startup_failure(void) {
    proven_u8str_view_t where = rubraview_pal_render_last_error();
    uint32_t hr = rubraview_pal_render_last_hresult();

    char message[512];
    int n = snprintf(message, sizeof(message),
        "Rubraview " RUBRAVIEW_VERSION_STRING " could not start its graphics.\r\n\r\n"
        "It failed while %.*s.\r\n"
        "Windows reported error 0x%08lX.\r\n\r\n"
        "This has been written to rubraview-diag.txt next to the program.",
        (int)where.size, where.size > 0 ? (const char *)where.ptr : "starting up",
        (unsigned long)hr);
    if (n <= 0) return;

    /* The file first: a message box can be dismissed before it is read,
       and a file can be pasted into a report. */
    HANDLE file = CreateFileW(L"rubraview-diag.txt", GENERIC_WRITE, FILE_SHARE_READ,
                              NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, message, (DWORD)n, &written, NULL);
        CloseHandle(file);
    }

    WCHAR wide[1024];
    if (MultiByteToWideChar(CP_UTF8, 0, message, -1, wide, 1024) > 0) {
        MessageBoxW(NULL, wide, L"Rubraview", MB_OK | MB_ICONERROR);
    }
}

/* §3.22 [video] decoder (D-9): the backend a file is offered to first.
   The other one still gets its turn when this one cannot open it. */
rubraview_media_backend_t preferred_backend(proven_arena_t *arena) {
    proven_u8str_view_t text = rubraview_pal_fs_read_file(arena, PROVEN_LIT("settings.ini"), 256u * 1024u);
    if (text.size == 0) return RUBRAVIEW_BACKEND_MEDIA_FOUNDATION;
    rubraview_ini_doc_t doc = rubraview_ini_parse(arena, text);
    const proven_u8str_view_t *decoder = rubraview_ini_get(&doc, PROVEN_LIT("video"), PROVEN_LIT("decoder"));
    return (decoder && proven_u8str_view_eq(*decoder, PROVEN_LIT("ffmpeg")))
        ? RUBRAVIEW_BACKEND_FFMPEG : RUBRAVIEW_BACKEND_MEDIA_FOUNDATION;
}

/* `--probe-media`: open a file with each backend in turn and say what
   happened — which one took it, what it thinks the file is, and whether
   frames really come out. There is no window, so it answers over a
   remote shell: the media PAL hands over CPU pixels and needs no
   renderer. mfprobe asks Windows what it *could* decode; this asks
   rubraview what it actually does with one file. */
int probe_media_file(proven_arena_t *arena, proven_u8str_view_t path) {
    char line[512];
    /* Where the viewer itself would look (RV-122): the imported copy in the
       settings folder, and the folder settings.ini names. */
    proven_u8str_view_t chosen = PROVEN_LIT(""), exe = rubraview_pal_process_executable(arena);
    proven_u8str_view_t beside = exe.size > 0 ? rubraview_path_dirname(exe) : PROVEN_LIT(".");
    char appdata[1024];
    DWORD got = GetEnvironmentVariableA("APPDATA", appdata, (DWORD)sizeof(appdata));
    rubraview_config_mode_t mode = rubraview_config_mode_for(rubraview_pal_fs_exists(rubraview_path_join(arena, beside, PROVEN_LIT("settings.ini"))));
    proven_u8str_view_t settings = rubraview_config_path(arena, mode, beside,
                                             got > 0 && got < sizeof(appdata) ? (proven_u8str_view_t){ .ptr = (const proven_byte_t *)appdata, .size = got } : PROVEN_LIT("."),
                                             PROVEN_LIT("settings.ini"));
    proven_u8str_view_t ini = rubraview_pal_fs_read_file(arena, PROVEN_LIT("settings.ini"), 256u * 1024u);
    if (ini.size > 0) {
        rubraview_ini_doc_t doc = rubraview_ini_parse(arena, ini);
        const proven_u8str_view_t *dir = rubraview_ini_get(&doc, PROVEN_LIT("video"), PROVEN_LIT("ffmpeg_dir"));
        if (dir) chosen = rubraview_pal_fs_expand_env(arena, rubraview_path_typed_folder(*dir));
    }
    rubraview_pal_ffmpeg_set_folders(rubraview_path_join(arena, rubraview_path_dirname(settings), PROVEN_LIT("ffmpeg")), chosen);
    bool ffmpeg_here = rubraview_pal_media_backend_available(RUBRAVIEW_BACKEND_FFMPEG);
    console_line(ffmpeg_here
        ? "FFmpeg: its DLLs are here, at a version these headers know"
        : rubraview_pal_ffmpeg_refused() == RUBRAVIEW_FFMPEG_OTHER_VERSION
            ? "FFmpeg: DLLs are beside the program but not 9.0 (avcodec-63): not loaded (Media Foundation alone)"
        : rubraview_pal_ffmpeg_refused() == RUBRAVIEW_FFMPEG_NOT_WHOLE
            ? "FFmpeg: avcodec-63.dll is beside the program but the set could not be loaded — avformat-63, avutil-61, swscale-10 and swresample-7 are needed with it (Media Foundation alone)"
            : "FFmpeg: no usable DLLs beside the program (Media Foundation alone)");
    if (ffmpeg_here) {
        console_line(rubraview_pal_ffmpeg_jxl_state() == 2
            ? "FFmpeg: it has a JPEG XL decoder (libjxl) — .jxl pictures are read through it"
            : "FFmpeg: it has no JPEG XL decoder — .jxl pictures need a codec in Windows");
    }

    rubraview_media_backend_t preferred = preferred_backend(arena);
    console_line(preferred == RUBRAVIEW_BACKEND_FFMPEG
        ? "settings.ini [video] decoder = ffmpeg — FFmpeg is tried first"
        : "settings.ini [video] decoder = windows (or unset) — Media Foundation is tried first");

    rubraview_media_backend_t order[2];
    size_t count = rubraview_media_backend_order(preferred, ffmpeg_here, order);
    for (size_t i = 0; i < count; ++i) {
        const char *name = order[i] == RUBRAVIEW_BACKEND_FFMPEG ? "FFmpeg" : "Media Foundation";
        rubraview_media_open_result_t opened = rubraview_pal_media_open(path, order[i], NULL);
        if (!opened.media) {
            proven_u8str_view_t why = rubraview_media_failure_text_with(opened.failure, rubraview_pal_ffmpeg_refused());
            snprintf(line, sizeof(line), "%s: cannot open — %.*s", name, (int)why.size, why.ptr);
            console_line(line);
            continue;
        }

        snprintf(line, sizeof(line), "%s: opened — %dx%d, %.3f s, %.2f fps, picture %s, sound %s%s",
                 name, opened.info.width, opened.info.height, opened.info.duration_seconds,
                 opened.info.frame_rate, opened.info.has_video ? "yes" : "no",
                 opened.info.has_audio ? "yes" : "no",
                 opened.info.has_audio
                     ? (opened.info.audio_output ? " (going to a device)" : " (no audio device here)") : "");
        console_line(line);

        /* §3.16.2: what the container holds, as the menu would show it. */
        rubraview_track_set_t set = {0};
        if (rubraview_pal_media_tracks(opened.media, &set) && set.count > 0) {
            for (size_t t = 0; t < set.count; ++t) {
                char label[160];
                proven_u8str_view_t text = rubraview_track_label(label, sizeof(label), &set, (int32_t)t);
                bool current = (int32_t)t == set.current_video || (int32_t)t == set.current_audio;
                const char *kind = set.tracks[t].kind == RUBRAVIEW_TRACK_AUDIO ? "sound"
                                 : set.tracks[t].kind == RUBRAVIEW_TRACK_SUBTITLE ? "subtitle"
                                 : "picture";
                snprintf(line, sizeof(line), "%s: %-8s %s %.*s", name, kind,
                         current ? "*" : " ", (int)text.size, text.ptr);
                console_line(line);
            }
        } else {
            snprintf(line, sizeof(line), "%s: the track list is empty", name);
            console_line(line);
        }

        /* Opening is not playing: take some frames and see. */
        int frames = 0;
        double first = -1.0, last = -1.0;
        for (int spin = 0; spin < 600 && frames < 24; ++spin) {
            rubraview_video_frame_t frame;
            if (rubraview_pal_media_peek_frame(opened.media, &frame)) {
                if (first < 0.0) first = frame.pts;
                last = frame.pts;
                frames++;
                rubraview_pal_media_pop_frame(opened.media);
                continue;
            }
            if (rubraview_pal_media_finished(opened.media)) break;
            rubraview_pal_time_sleep_ms(10);
        }
        if (opened.info.has_video) {
            snprintf(line, sizeof(line), "%s: %d picture(s) decoded, %.3f s to %.3f s", name, frames, first, last);
        } else {
            snprintf(line, sizeof(line), "%s: sound only — nothing to decode into pictures", name);
        }
        console_line(line);
        rubraview_pal_media_close(opened.media);
    }

    /* §3.16.1: the same search the viewer does, said out loud — which
       of discovery, reading and parsing worked, and what came out. */
    proven_u8str_view_t sub_name = PROVEN_LIT("");
    const char *why = NULL;
    rubraview_subtitle_track_t track = subtitle_find(arena, path, &sub_name, &why);
    if (track.count == 0) {
        snprintf(line, sizeof(line), "Subtitles: none — %s", why ? why : "not looked for");
    } else {
        snprintf(line, sizeof(line), "Subtitles: %.*s — %zu line(s), the first %.3f s to %.3f s",
                 (int)sub_name.size, sub_name.ptr, track.count,
                 track.cues[0].start_seconds, track.cues[0].end_seconds);
    }
    console_line(line);
    return 0;
}

/* `--probe-gpu`: what graphics card this machine has, whether Media
   Foundation will take it for decoding, and — the question that decides
   RV-062's shape — whether a decoded frame then comes back as a texture
   or as system memory. Written before building the zero-copy path so
   that "can this be tried here at all" is measured, not assumed. */
/* D-38: the card's resize against the CPU's on the same picture: how many
   bytes differ, by how much, and how long each took. With `always` the
   software rasteriser stands in when there is no card, so this measures
   correctness anywhere and speed only on a machine with a card. */
void probe_gpu_resize(void) {
    char line[256];
    bool on = rubraview_pal_gpu_resample_start(RUBRAVIEW_GPU_RESIZE_ON);
    snprintf(line, sizeof(line), "gpu resize with gpu_resize = on: %s%s", rubraview_pal_gpu_resample_describe(),
             on ? "" : " (not used)");
    console_line(line);
    bool started = rubraview_pal_gpu_resample_start(RUBRAVIEW_GPU_RESIZE_ALWAYS);
    snprintf(line, sizeof(line), "gpu resize with gpu_resize = always: %s", rubraview_pal_gpu_resample_describe());
    console_line(line);
    if (!started) return;
    size_t cap = (size_t)96 << 20;
    void *memory = malloc(cap);
    if (!memory) return;
    static const struct { int32_t sw, sh, dw, dh; } SIZES[] = { {1600, 1200, 2400, 1800}, {1600, 1200, 1000, 750}, {1600, 1200, 320, 240} };
    static const rubraview_resample_filter_t FILTERS[2] = { RUBRAVIEW_FILTER_BICUBIC, RUBRAVIEW_FILTER_LANCZOS3 };
    for (size_t k = 0; k < sizeof(SIZES) / sizeof(SIZES[0]); ++k) {
        for (int f = 0; f < 2; ++f) {
            proven_arena_t a = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)memory, .size = cap });
            rubraview_pixbuf_t src = rubraview_pixbuf_create(&a, SIZES[k].sw, SIZES[k].sh, RUBRAVIEW_PIXFMT_RGBA8);
            rubraview_pixbuf_t cpu = rubraview_pixbuf_create(&a, SIZES[k].dw, SIZES[k].dh, RUBRAVIEW_PIXFMT_RGBA8);
            rubraview_pixbuf_t gpu = rubraview_pixbuf_create(&a, SIZES[k].dw, SIZES[k].dh, RUBRAVIEW_PIXFMT_RGBA8);
            if (!rubraview_pixbuf_is_valid(&src) || !rubraview_pixbuf_is_valid(&cpu) || !rubraview_pixbuf_is_valid(&gpu)) break;
            unsigned seed = 11;
            for (int32_t y = 0; y < src.height; ++y)
                for (int32_t x = 0; x < src.width; ++x) {
                    uint8_t *p = src.pixels + (ptrdiff_t)y * src.stride + x * 4;
                    seed = seed * 1103515245u + 12345u;
                    int noise = (int)((seed >> 16) & 15) - 8;
                    bool edge = ((x / 97) + (y / 61)) % 2 == 0;
                    int v[3] = { x * 255 / src.width, y * 255 / src.height, 128 + (int)(100.0 * sin(x * 0.02 + y * 0.01)) };
                    for (int c = 0; c < 3; ++c) { int t = v[c] + noise + (edge ? 40 : 0); p[c] = (uint8_t)(t < 0 ? 0 : t > 255 ? 255 : t); }
                    p[3] = 255;
                }
            double t0 = rubraview_pal_time_now_seconds();
            rubraview_resample_band(&src, &cpu, FILTERS[f], 0, cpu.height);
            double t1 = rubraview_pal_time_now_seconds();
            bool ok = rubraview_resample_try_accel(&src, &gpu, FILTERS[f]);
            double t2 = rubraview_pal_time_now_seconds();
            size_t n = (size_t)cpu.stride * (size_t)cpu.height, changed = 0;
            int worst = 0;
            for (size_t i = 0; ok && i < n; ++i) {
                int d = abs((int)cpu.pixels[i] - (int)gpu.pixels[i]);
                if (d) changed++;
                if (d > worst) worst = d;
            }
            snprintf(line, sizeof(line), "gpu resize %s %dx%d -> %dx%d: %s; cpu %.3f s, card %.3f s; max diff %d, %.4f%% of bytes differ",
                     f ? "lanczos3" : "bicubic", SIZES[k].sw, SIZES[k].sh, SIZES[k].dw, SIZES[k].dh,
                     ok ? "done on the card" : "the card refused (CPU would do it)", t1 - t0, t2 - t1, worst,
                     n ? 100.0 * (double)changed / (double)n : 0.0);
            console_line(line);
        }
    }
    free(memory);
    rubraview_pal_gpu_resample_stop();
}

int probe_gpu_file(proven_arena_t *arena, proven_u8str_view_t path) {
    (void)arena;
    char line[512];

    typedef HRESULT (WINAPI *create_dxgi_manager_fn)(UINT*, IMFDXGIDeviceManager**);
    HMODULE plat = LoadLibraryExW(L"mfplat.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE rw = LoadLibraryExW(L"mfreadwrite.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!plat || !rw) { console_line("Media Foundation is not installed here"); return 2; }
    create_dxgi_manager_fn create_manager =
        (create_dxgi_manager_fn)(void*)GetProcAddress(plat, "MFCreateDXGIDeviceManager");
    HRESULT (WINAPI *mf_startup)(ULONG, DWORD) =
        (HRESULT (WINAPI *)(ULONG, DWORD))(void*)GetProcAddress(plat, "MFStartup");
    HRESULT (WINAPI *mf_shutdown)(void) =
        (HRESULT (WINAPI *)(void))(void*)GetProcAddress(plat, "MFShutdown");
    HRESULT (WINAPI *mf_create_attributes)(IMFAttributes**, UINT32) =
        (HRESULT (WINAPI *)(IMFAttributes**, UINT32))(void*)GetProcAddress(plat, "MFCreateAttributes");
    HRESULT (WINAPI *mf_create_reader)(LPCWSTR, IMFAttributes*, IMFSourceReader**) =
        (HRESULT (WINAPI *)(LPCWSTR, IMFAttributes*, IMFSourceReader**))
            (void*)GetProcAddress(rw, "MFCreateSourceReaderFromURL");
    if (!create_manager || !mf_startup || !mf_shutdown || !mf_create_attributes || !mf_create_reader) {
        console_line("Media Foundation is here but not the calls this needs");
        return 2;
    }

    /* 1. A device. Hardware if there is one, otherwise Windows' own
          software rasteriser (WARP) — which is what a machine with no
          graphics card falls back to. */
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    D3D_FEATURE_LEVEL level = (D3D_FEATURE_LEVEL)0;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    const char *kind = "hardware";
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, NULL, 0,
                                   D3D11_SDK_VERSION, &device, &level, &context);
    if (FAILED(hr)) {
        kind = "WARP (software)";
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, flags, NULL, 0,
                               D3D11_SDK_VERSION, &device, &level, &context);
    }
    if (FAILED(hr) || !device) {
        console_line("no Direct3D 11 device at all, with or without video support");
        return 2;
    }
    snprintf(line, sizeof(line), "Device: %s, feature level %u.%u",
             kind, (unsigned)(level >> 12) & 0xF, (unsigned)(level >> 8) & 0xF);
    console_line(line);

    IDXGIDevice *dxgi = NULL;
    if (SUCCEEDED(ID3D11Device_QueryInterface(device, &IID_IDXGIDevice, (void**)&dxgi)) && dxgi) {
        IDXGIAdapter *adapter = NULL;
        if (SUCCEEDED(IDXGIDevice_GetAdapter(dxgi, &adapter)) && adapter) {
            DXGI_ADAPTER_DESC desc = {0};
            if (SUCCEEDED(IDXGIAdapter_GetDesc(adapter, &desc))) {
                char name[256] = {0};
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, (int)sizeof(name), NULL, NULL);
                snprintf(line, sizeof(line), "Adapter: %s (%u MB of its own memory)",
                         name, (unsigned)(desc.DedicatedVideoMemory / (1024u * 1024u)));
                console_line(line);
            }
            IDXGIAdapter_Release(adapter);
        }
        IDXGIDevice_Release(dxgi);
    }

    /* 2. Does it offer decoders at all? */
    ID3D11VideoDevice *video = NULL;
    UINT profile_count = 0;
    if (SUCCEEDED(ID3D11Device_QueryInterface(device, &IID_ID3D11VideoDevice, (void**)&video)) && video) {
        UINT profiles = ID3D11VideoDevice_GetVideoDecoderProfileCount(video);
        profile_count = profiles;
        snprintf(line, sizeof(line), "Decoder profiles the card offers: %u", profiles);
        console_line(line);
        ID3D11VideoDevice_Release(video);
    } else {
        console_line("Decoder profiles the card offers: none (no video device)");
    }

    /* 3. Hand the device to Media Foundation and read one frame. */
    if (FAILED(mf_startup(((ULONG)0x00020070), 0))) {
        console_line("Media Foundation would not start");
        return 2;
    }
    int code = 0;
    /* mingw-w64 declares the interface but ships no IID for it. */
    static const GUID iid_multithread =
        { 0x9B7E4E00, 0x342C, 0x4106, { 0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0 } };
    ID3D11Multithread *mt = NULL;
    if (SUCCEEDED(ID3D11DeviceContext_QueryInterface(context, &iid_multithread, (void**)&mt)) && mt) {
        /* Media Foundation uses the device from its own threads. */
        ID3D11Multithread_SetMultithreadProtected(mt, TRUE);
        ID3D11Multithread_Release(mt);
    }

    IMFDXGIDeviceManager *manager = NULL;
    UINT token = 0;
    if (FAILED(create_manager(&token, &manager)) || !manager ||
        FAILED(IMFDXGIDeviceManager_ResetDevice(manager, (IUnknown*)device, token))) {
        console_line("Media Foundation refused the device manager");
        code = 2;
    } else {
        WCHAR wide[MAX_PATH * 2];
        if (MultiByteToWideChar(CP_UTF8, 0, (const char *)path.ptr, (int)path.size, wide,
                                (int)(sizeof(wide) / sizeof(wide[0])) - 1) <= 0) {
            console_line("that path cannot be read");
            code = 2;
        } else {
            wide[MultiByteToWideChar(CP_UTF8, 0, (LPCCH)path.ptr, (int)path.size, NULL, 0)] = L'\0';
            IMFAttributes *attrs = NULL;
            IMFSourceReader *reader = NULL;
            if (SUCCEEDED(mf_create_attributes(&attrs, 3)) && attrs) {
                IMFAttributes_SetUnknown(attrs, &MF_SOURCE_READER_D3D_MANAGER, (IUnknown*)manager);
                IMFAttributes_SetUINT32(attrs, &MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
                IMFAttributes_SetUINT32(attrs, &MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
                hr = mf_create_reader(wide, attrs, &reader);
                IMFAttributes_Release(attrs);
            } else {
                hr = E_FAIL;
            }
            if (FAILED(hr) || !reader) {
                console_line("the reader would not open the file with the device attached");
                code = 2;
            } else {
                console_line("The reader took the device.");
                /* The first read often answers with a format change and
                   no picture at all; ask a few times before believing
                   that nothing is coming. */
                DWORD stream = 0, sample_flags = 0;
                LONGLONG timestamp = 0;
                IMFSample *sample = NULL;
                DWORD last_flags = 0;
                for (int attempt = 0; attempt < 40 && !sample; ++attempt) {
                    hr = IMFSourceReader_ReadSample(reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                                    &stream, &sample_flags, &timestamp, &sample);
                    last_flags = sample_flags;
                    if (FAILED(hr) || (sample_flags & MF_SOURCE_READERF_ENDOFSTREAM) ||
                        (sample_flags & MF_SOURCE_READERF_ERROR)) break;
                }
                if (FAILED(hr) || !sample) {
                    snprintf(line, sizeof(line),
                             "but no picture came out of it (hr=0x%08lX, last flags=0x%lX)",
                             (unsigned long)hr, (unsigned long)last_flags);
                    console_line(line);
                    code = 2;
                } else {
                    IMFMediaBuffer *buffer = NULL;
                    bool on_the_card = false;
                    if (SUCCEEDED(IMFSample_GetBufferByIndex(sample, 0, &buffer)) && buffer) {
                        IMFDXGIBuffer *dxgi_buffer = NULL;
                        if (SUCCEEDED(IMFMediaBuffer_QueryInterface(buffer, &IID_IMFDXGIBuffer,
                                                                    (void**)&dxgi_buffer)) && dxgi_buffer) {
                            on_the_card = true;
                            IMFDXGIBuffer_Release(dxgi_buffer);
                        }
                        IMFMediaBuffer_Release(buffer);
                    }
                    console_line(on_the_card
                        ? "The frame came back as a texture on the card — the zero-copy path can be tried here."
                        : "The frame came back as system memory — this machine cannot exercise the zero-copy path.");
                    IMFSample_Release(sample);
                }
                IMFSourceReader_Release(reader);
            }
        }
    }
    if (manager) IMFDXGIDeviceManager_Release(manager);
    mf_shutdown();

    /* 4. The viewer's own path (RV-062): the media PAL handed this device
          the way [video] hardware_decode = on and = always hand it over.
          Whether the film decodes on the card, whether pictures come, and
          whether they arrive as textures (zero copy) or as pixels. */
    static const char *const MODE_NAMES[] = { "off", "on", "always" };
    for (int32_t mode = 0; mode <= 2; ++mode) {
        rubraview_media_gpu_t gpu = { .device = device, .decoder_profiles = profile_count, .mode = mode };
        rubraview_media_open_result_t opened = rubraview_pal_media_open(path, RUBRAVIEW_BACKEND_MEDIA_FOUNDATION, &gpu);
        if (!opened.media) {
            /* Why, so "cannot open" and "the card broke it" read differently. */
            proven_u8str_view_t why = rubraview_media_failure_text(opened.failure);
            snprintf(line, sizeof(line), "hardware_decode = %s: the film did not open - %.*s",
                     MODE_NAMES[mode], (int)why.size, why.ptr);
            console_line(line);
            code = 2;
            continue;
        }
        /* As fast as it decodes, for four seconds: pictures per second and
           the CPU the whole program spent on them, so off and on compare. */
        FILETIME created, exited, kernel0, user0, kernel1, user1;
        GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel0, &user0);
        int pictures = 0, on_card = 0;
        double start = rubraview_pal_time_now_seconds(), elapsed = 0.0;
        while ((elapsed = rubraview_pal_time_now_seconds() - start) < 4.0) {
            rubraview_video_frame_t frame;
            if (rubraview_pal_media_peek_frame(opened.media, &frame)) {
                pictures++;
                if (frame.gpu_texture) on_card++;
                rubraview_pal_media_pop_frame(opened.media);
            } else if (rubraview_pal_media_finished(opened.media)) {
                break;
            } else {
                rubraview_pal_time_sleep_ms(1);
            }
        }
        GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel1, &user1);
        #define RV_FT(ft) ((double)(((uint64_t)(ft).dwHighDateTime << 32) | (ft).dwLowDateTime) / 1e7)
        double cpu = RV_FT(kernel1) - RV_FT(kernel0) + RV_FT(user1) - RV_FT(user0);
        #undef RV_FT
        uint32_t cc = opened.info.video_fourcc;
        char codec[5] = { (char)(cc & 0xFF), (char)((cc >> 8) & 0xFF), (char)((cc >> 16) & 0xFF), (char)(cc >> 24), 0 };
        for (int k = 0; k < 4; ++k) if (codec[k] < 32 || codec[k] > 126) codec[k] = '?';
        snprintf(line, sizeof(line),
                 "hardware_decode = %s: %s %dx%d, decoded %s; %d pictures in %.1f s (%.0f a second), "
                 "%d of them on the card; CPU %.2f s (%.0f%% of one core)",
                 MODE_NAMES[mode], codec, opened.info.width, opened.info.height,
                 opened.info.hardware_decode ? "on the card" : "in software",
                 pictures, elapsed, elapsed > 0.0 ? pictures / elapsed : 0.0, on_card,
                 cpu, elapsed > 0.0 ? 100.0 * cpu / elapsed : 0.0);
        console_line(line);
        if (pictures == 0 && opened.info.has_video) code = 2;
        rubraview_pal_media_close(opened.media);
    }

    ID3D11DeviceContext_Release(context);
    ID3D11Device_Release(device);

    /* 5. The viewer's own renderer (0.0.7 on the owner's card: the probe
          played the film, the viewer showed a black window). A hidden
          window and the real renderer; the decoder's device from it, the
          film opened with it, frames copied into a film texture and drawn
          once, and what was drawn read back. */
    HWND hidden = CreateWindowExW(0, L"STATIC", L"rubraview probe", WS_POPUP, 0, 0, 320, 240, NULL, NULL,
                                  GetModuleHandleW(NULL), NULL);
    rubraview_renderer_t *renderer = hidden ? rubraview_pal_render_create(arena, hidden, 320, 240) : NULL;
    if (!renderer) {
        console_line("viewer path: the renderer would not start here");
        if (hidden) DestroyWindow(hidden);
        return 2;
    }
    uint32_t viewer_profiles = 0;
    void *decode_device = rubraview_pal_render_video_device(renderer, &viewer_profiles);
    snprintf(line, sizeof(line), "viewer path: the decoder's own device %s, %u decoder profiles",
             decode_device ? "made" : "NOT made", (unsigned)viewer_profiles);
    if (!decode_device) {
        console_line(line);
        snprintf(line, sizeof(line), "viewer path: the last Direct3D answer was 0x%08X",
                 (unsigned)rubraview_pal_render_last_hresult());
    }
    console_line(line);
  for (int32_t vmode = 1; vmode <= 2; ++vmode) {
    rubraview_media_gpu_t vgpu = { .device = decode_device, .decoder_profiles = viewer_profiles, .mode = vmode };
    double opened_in = rubraview_pal_time_now_seconds();
    rubraview_media_open_result_t vopen = rubraview_pal_media_open(path, RUBRAVIEW_BACKEND_MEDIA_FOUNDATION, &vgpu);
    opened_in = rubraview_pal_time_now_seconds() - opened_in;
    if (!vopen.media) {
        proven_u8str_view_t why = rubraview_media_failure_text(vopen.failure);
        snprintf(line, sizeof(line), "viewer path (%s): the film did not open (%.1f s) - %.*s", MODE_NAMES[vmode], opened_in, (int)why.size, why.ptr);
        console_line(line);
        code = 2;
    } else {
        rubraview_texture_t *page = vopen.info.hardware_decode
            ? rubraview_pal_texture_create_video(renderer, vopen.info.width, vopen.info.height) : NULL;
        int pictures = 0, copied = 0, drawn = 0;
        double brightness = -1.0;
        double start = rubraview_pal_time_now_seconds();
        while (rubraview_pal_time_now_seconds() - start < 3.0 && pictures < 60) {
            rubraview_video_frame_t frame;
            if (!rubraview_pal_media_peek_frame(vopen.media, &frame)) { rubraview_pal_time_sleep_ms(2); continue; }
            pictures++;
            if (page && frame.gpu_texture &&
                rubraview_pal_texture_copy_video_frame(page, frame.gpu_texture, frame.gpu_subresource)) {
                copied++;
                rubraview_pal_render_begin(renderer, 0xFF000000u);
                rubraview_pal_render_draw_texture(renderer, page,
                    (rubraview_mat3x2_t){ .a = 320.0 / vopen.info.width, .d = 240.0 / vopen.info.height },
                    RUBRAVIEW_INTERP_LINEAR);
                if (rubraview_pal_render_end(renderer)) drawn++;
                if (pictures > 20) rubraview_pal_texture_video_brightness(page, &brightness);
            }
            rubraview_pal_media_pop_frame(vopen.media);
        }
        snprintf(line, sizeof(line),
                 "viewer path (%s): opened in %.1f s, decoded %s; %d pictures, %d copied to the screen texture, "
                 "%d drawn; brightness of the picture %.0f of 255",
                 MODE_NAMES[vmode], opened_in, vopen.info.hardware_decode ? "on the card" : "in software", pictures, copied, drawn,
                 brightness);
        console_line(line);
        if (vopen.info.hardware_decode && (copied == 0 || drawn == 0)) code = 2;
        if (page) rubraview_pal_texture_destroy(page);
        rubraview_pal_media_close(vopen.media);
    }
  }
    rubraview_pal_render_destroy(renderer);
    DestroyWindow(hidden);
    return code;
}

/* `--diag`: bring the graphics up, say what it got, and stop. One run
   from a command prompt answers "what is this machine actually using",
   which is otherwise guesswork from far away. */
/*
 * The stages are deliberately separated and each one announces itself
 * *before* it runs, so a hang names the step it hung in rather than
 * leaving a silent window. The first run of this found exactly that: the
 * report stopped after "walking the image path", which said the failure
 * was in the very first call and nowhere else.
 *
 * They also run in order of dependency — COM, then the codec, then a
 * window, then the GPU — so the earliest broken thing is found before
 * anything downstream can confuse the picture.
 */
void pump_messages(void) {
    /* A window whose messages nobody reads is a window Windows calls
       "not responding", and an STA that never pumps can deadlock COM
       outright. Anywhere this code waits, it waits like this. */
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static void wait_pumping(int milliseconds) {
    for (int i = 0; i < milliseconds; i += 16) {
        pump_messages();
        rubraview_pal_time_sleep_ms(16);
    }
}

int run_diagnostics(proven_arena_t *arena, proven_u8str_view_t image_path) {
    console_line("rubraview " RUBRAVIEW_VERSION_STRING " - diagnostics");
    console_line("rubraview: step 1 - starting COM's imaging factory (WIC)");
    /* Before any window exists, so a hang here cannot be blamed on one. */
    IWICImagingFactory *probe_factory = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IWICImagingFactory, (void**)&probe_factory);
    if (FAILED(hr) || !probe_factory) {
        char line[160];
        snprintf(line, sizeof(line), "rubraview: step 1 FAILED (0x%08lX) - no imaging codecs at all", (unsigned long)hr);
        console_line(line);
        return 1;
    }
    console_line("rubraview: step 1 ok");

    if (image_path.size > 0) {
        console_line("rubraview: step 2 - opening the file with WIC (no window, no GPU)");

        char narrow[1024];
        size_t n = image_path.size < sizeof(narrow) - 1 ? image_path.size : sizeof(narrow) - 1;
        memcpy(narrow, image_path.ptr, n);
        narrow[n] = '\0';

        WCHAR wide[1024];
        IWICBitmapDecoder *decoder = NULL;
        if (MultiByteToWideChar(CP_UTF8, 0, narrow, -1, wide, 1024) > 0) {
            hr = IWICImagingFactory_CreateDecoderFromFilename(probe_factory, wide, NULL, GENERIC_READ,
                                                              WICDecodeMetadataCacheOnDemand, &decoder);
        } else {
            hr = E_INVALIDARG;
        }

        char line[256];
        if (FAILED(hr) || !decoder) {
            snprintf(line, sizeof(line), "rubraview: step 2 FAILED (0x%08lX) - the path or the format", (unsigned long)hr);
            console_line(line);
            IWICImagingFactory_Release(probe_factory);
            return 1;
        }

        UINT frames = 0;
        IWICBitmapDecoder_GetFrameCount(decoder, &frames);
        IWICBitmapFrameDecode *frame = NULL;
        hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
        UINT w = 0, h = 0;
        if (SUCCEEDED(hr) && frame) IWICBitmapFrameDecode_GetSize(frame, &w, &h);
        snprintf(line, sizeof(line), "rubraview: step 2 ok - %u frame(s), %ux%u", frames, w, h);
        console_line(line);
        if (frame) IWICBitmapFrameDecode_Release(frame);
        IWICBitmapDecoder_Release(decoder);
    }
    IWICImagingFactory_Release(probe_factory);

    console_line("rubraview: step 3 - creating the window");
    rubraview_window_config_t config = { .title = "Rubraview " RUBRAVIEW_VERSION_STRING " diagnostics", .width = 640, .height = 400, .frameless = false };
    rubraview_window_t *window = rubraview_pal_window_create(arena, &config);
    if (!window) {
        console_line("rubraview: step 3 FAILED - the window could not be created");
        return 1;
    }
    pump_messages();
    console_line("rubraview: step 3 ok");

    console_line("rubraview: step 4 - starting the graphics device");
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(window, &w, &h);
    rubraview_renderer_t *renderer = rubraview_pal_render_create(
        arena, rubraview_pal_window_native_handle(window), w, h);

    if (!renderer) {
        char line[512];
        proven_u8str_view_t where = rubraview_pal_render_last_error();
        snprintf(line, sizeof(line), "rubraview: step 4 FAILED while %.*s (0x%08lX)",
                 (int)where.size, where.ptr, (unsigned long)rubraview_pal_render_last_hresult());
        console_line(line);
        rubraview_pal_window_destroy(window);
        return 1;
    }

    char described[256];
    proven_u8str_view_t info = rubraview_pal_render_describe(renderer, described, sizeof(described));
    char line[320];
    snprintf(line, sizeof(line), "rubraview: step 4 ok - %.*s", (int)info.size, info.ptr);
    console_line(line);

    console_line("rubraview: step 5 - drawing a plain rectangle");
    rubraview_pal_render_begin(renderer, 0xFF203040u);
    rubraview_pal_rect_t box = { 40.0, 40.0, 200.0, 120.0 };
    rubraview_pal_render_fill_rect(renderer, box, 0xFFCC4444u, 4.0);
    bool presented = rubraview_pal_render_end(renderer);
    console_line(presented ? "rubraview: step 5 ok - it was presented"
                           : "rubraview: step 5 FAILED - the device was lost");
    wait_pumping(800);

    if (image_path.size > 0) {
        console_line("rubraview: step 6 - the viewer's own decode path, stage by stage");

        char report[2048];
        proven_u8str_view_t text = rubraview_pal_image_diagnose(renderer, image_path, report, sizeof(report));

        size_t start = 0;
        for (size_t i = 0; i <= text.size; ++i) {
            bool at_end = i == text.size;
            if (!at_end && text.ptr[i] != '\n') continue;
            size_t stop = i;
            while (stop > start && (text.ptr[stop - 1] == '\r' || text.ptr[stop - 1] == '\n')) stop--;
            if (stop > start) {
                char one[256];
                size_t take = stop - start < sizeof(one) - 1 ? stop - start : sizeof(one) - 1;
                memcpy(one, text.ptr + start, take);
                one[take] = '\0';
                console_line(one);
            }
            start = i + 1;
        }

        console_line("rubraview: step 7 - loading it the way the viewer does");
        rubraview_image_load_result_t loaded =
            rubraview_pal_image_load_texture(renderer, image_path, true);
        console_line(loaded.ok ? "rubraview: step 7 ok - it became a texture"
                               : "rubraview: step 7 FAILED - it did not become a texture");

        if (loaded.ok && loaded.texture) {
            console_line("rubraview: step 8 - putting it on the screen (5 seconds)");
            rubraview_pal_render_begin(renderer, 0xFF101010u);
            rubraview_mat3x2_t place = rubraview_mat3x2_identity();
            rubraview_pal_render_draw_texture(renderer, loaded.texture, place, RUBRAVIEW_INTERP_LINEAR);
            bool shown = rubraview_pal_render_end(renderer);
            console_line(shown ? "rubraview: step 8 ok - look at the window now"
                               : "rubraview: step 8 FAILED - it did not present");
            wait_pumping(5000);
            rubraview_pal_texture_destroy(loaded.texture);
        }
    } else {
        wait_pumping(1200);
    }

    console_line("rubraview: done");
    rubraview_pal_render_destroy(renderer);
    rubraview_pal_window_destroy(window);
    return presented ? 0 : 1;
}
