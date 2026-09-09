/*
 * mfprobe — what can this Windows machine actually play on its own?
 *
 * RFC-0001 §5.1 lists the formats the viewer should handle, and §5.2
 * says it must still work when the FFmpeg DLLs are absent. Whether
 * Windows can carry any of that by itself is not a question that can be
 * answered from the build machine: the Media Foundation *headers* name
 * dozens of formats, but naming a format is not the same as shipping a
 * decoder for it. Which decoders exist depends on the Windows version
 * and on which Store codec extensions are installed.
 *
 * So this asks the machine. Run it with no arguments to list every
 * decoder Media Foundation has registered; run it with a file to find
 * out whether Windows can open that particular file and what it finds
 * inside.
 *
 * It is a diagnostic, not part of the viewer. `make mfprobe` builds it.
 */

#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <mftransform.h>
#include <stdio.h>
#include <stdbool.h>

/* The §5.1 formats worth asking about, with the name a reader would use
   rather than the name the API uses. */
typedef struct probe_format {
    const GUID *guid;
    const char *label;
    const char *matrix_row;   /* which §5.1 row it belongs to */
    /* Some formats are not compressed, so no decoder exists to find and
       asking for one produces a false alarm. PCM is the case: it is raw
       samples, handled by the media type itself. The first run of this
       probe reported "PCM: no" on a Windows 11 machine that plays WAV
       perfectly well — the question was wrong, not the answer. */
    bool needs_no_decoder;
} probe_format_t;

/* Theora has a GUID in newer SDKs but not in this toolchain's headers,
   which is itself worth knowing: it is defined so a third-party decoder
   can register, not because Windows ships one. */
static const GUID RV_MFVideoFormat_Theora =
    { 0x6f6f6552, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };

static const probe_format_t VIDEO_FORMATS[] = {
    { &MFVideoFormat_H264,  "H.264 (AVC)",      "Modern video" },
    { &MFVideoFormat_HEVC,  "H.265 (HEVC)",     "Modern video" },
    { &MFVideoFormat_VP80,  "VP8",              "Modern video" },
    { &MFVideoFormat_VP90,  "VP9",              "Modern video" },
    { &MFVideoFormat_AV1,   "AV1",              "Modern video" },
    { &MFVideoFormat_MPG1,  "MPEG-1 video",     "Broadcast & legacy" },
    { &MFVideoFormat_MPEG2, "MPEG-2 video",     "Broadcast & legacy" },
    { &MFVideoFormat_MP4V,  "MPEG-4 ASP (DivX/XviD)", "Broadcast & legacy" },
    { &MFVideoFormat_H263,  "H.263",            "Broadcast & legacy" },
    { &MFVideoFormat_DVSD,  "DV",               "Broadcast & legacy" },
    { &MFVideoFormat_WMV3,  "WMV3 (WMV 9)",     "Windows Media" },
    { &MFVideoFormat_WVC1,  "VC-1 Advanced",    "Windows Media" },
    { &MFVideoFormat_MJPG,  "Motion JPEG",      "(extra)" },
    { &RV_MFVideoFormat_Theora, "Theora",       "Ogg open media" },
};

static const probe_format_t AUDIO_FORMATS[] = {
    { &MFAudioFormat_AAC,             "AAC",            "Compressed audio" },
    { &MFAudioFormat_MP3,             "MP3",            "Compressed audio" },
    { &MFAudioFormat_WMAudioV9,       "WMA v9",         "Compressed audio" },
    { &MFAudioFormat_WMAudio_Lossless,"WMA Lossless",   "Compressed audio" },
    { &MFAudioFormat_FLAC,            "FLAC",           "Lossless audio" },
    { &MFAudioFormat_ALAC,            "Apple Lossless", "Lossless audio" },
    { &MFAudioFormat_PCM,             "PCM (WAV)",      "Lossless audio", true },
    { &MFAudioFormat_Vorbis,          "Ogg Vorbis",     "Ogg open media" },
    { &MFAudioFormat_Opus,            "Opus",           "Ogg open media" },
    { &MFAudioFormat_Dolby_AC3,       "Dolby AC-3",     "(extra)" },
    { &MFAudioFormat_DTS,             "DTS",            "(extra)" },
};

/* Whether a decoder exists for one format. MFTEnumEx asks the registry
   what is installed *on this machine*, which is the only answer that
   means anything. */
static bool has_decoder(const GUID *category, const GUID *major, const GUID *subtype) {
    MFT_REGISTER_TYPE_INFO input = { *major, *subtype };
    IMFActivate **activates = NULL;
    UINT32 count = 0;

    HRESULT hr = MFTEnumEx(*category, MFT_ENUM_FLAG_ALL, &input, NULL, &activates, &count);
    if (FAILED(hr)) return false;

    for (UINT32 i = 0; i < count; ++i) {
        if (activates[i]) IMFActivate_Release(activates[i]);
    }
    if (activates) CoTaskMemFree(activates);
    return count > 0;
}

static void report_group(const char *heading, const probe_format_t *formats, size_t count,
                         const GUID *category, const GUID *major) {
    printf("\n%s\n", heading);
    for (size_t i = 0; i < count; ++i) {
        if (formats[i].needs_no_decoder) {
            printf("  [n/a] %-24s  %s  (uncompressed — no decoder needed)\n",
                   formats[i].label, formats[i].matrix_row);
            continue;
        }
        bool present = has_decoder(category, major, formats[i].guid);
        printf("  [%s] %-24s  %s\n",
               present ? "yes" : " - ", formats[i].label, formats[i].matrix_row);
    }
}

/* Opening an actual file is the real test: a decoder can exist while the
   *container* still cannot be read. That is the case for Matroska, which
   is why an .mkv can fail on a machine whose H.264 decoder is fine. */
static int probe_file(const wchar_t *path) {
    IMFSourceReader *reader = NULL;
    HRESULT hr = MFCreateSourceReaderFromURL(path, NULL, &reader);

    if (FAILED(hr) || !reader) {
        printf("\nOpening the file: FAILED (hr = 0x%08lX)\n", (unsigned long)hr);
        if (hr == (HRESULT)MF_E_UNSUPPORTED_BYTESTREAM_TYPE) {
            printf("  Windows does not recognise this container at all.\n");
        } else if (hr == (HRESULT)MF_E_UNSUPPORTED_SCHEME) {
            printf("  Windows does not recognise this kind of location.\n");
        }
        printf("  => this file needs FFmpeg.\n");
        return 1;
    }

    printf("\nOpening the file: OK — Windows can read this container.\n");

    for (DWORD stream = 0; ; ++stream) {
        IMFMediaType *type = NULL;
        hr = IMFSourceReader_GetNativeMediaType(reader, stream, 0, &type);
        if (hr == (HRESULT)MF_E_INVALIDSTREAMNUMBER) break;
        if (FAILED(hr) || !type) continue;

        GUID major = {0}, subtype = {0};
        IMFMediaType_GetGUID(type, &MF_MT_MAJOR_TYPE, &major);
        IMFMediaType_GetGUID(type, &MF_MT_SUBTYPE, &subtype);

        const char *kind = "other";
        if (IsEqualGUID(&major, &MFMediaType_Video)) kind = "video";
        else if (IsEqualGUID(&major, &MFMediaType_Audio)) kind = "audio";

        /* Asking the reader to hand over decoded frames is what proves a
           decoder is really there — the format being listed is not
           enough. */
        IMFMediaType *wanted = NULL;
        bool decodable = false;
        if (SUCCEEDED(MFCreateMediaType(&wanted))) {
            IMFMediaType_SetGUID(wanted, &MF_MT_MAJOR_TYPE, &major);
            IMFMediaType_SetGUID(wanted, &MF_MT_SUBTYPE,
                                 IsEqualGUID(&major, &MFMediaType_Video)
                                   ? &MFVideoFormat_NV12 : &MFAudioFormat_PCM);
            decodable = SUCCEEDED(IMFSourceReader_SetCurrentMediaType(reader, stream, NULL, wanted));
            IMFMediaType_Release(wanted);
        }

        printf("  stream %lu: %-5s  subtype {%08lX-...}  decodable: %s\n",
               (unsigned long)stream, kind, (unsigned long)subtype.Data1,
               decodable ? "yes" : "NO");

        IMFMediaType_Release(type);
    }

    IMFSourceReader_Release(reader);
    return 0;
}

int wmain(int argc, wchar_t **argv) {
    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) {
        printf("COM would not start.\n");
        return 2;
    }
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
        printf("Media Foundation would not start — this Windows has no Media Foundation.\n");
        CoUninitialize();
        return 2;
    }

    printf("mfprobe — what Media Foundation can decode on this machine\n");
    printf("==========================================================\n");
    printf("\"yes\" means a decoder is installed here. It says nothing about\n");
    printf("other machines: Store codec extensions differ per install.\n");

    report_group("Video decoders:", VIDEO_FORMATS,
                 sizeof(VIDEO_FORMATS) / sizeof(VIDEO_FORMATS[0]),
                 &MFT_CATEGORY_VIDEO_DECODER, &MFMediaType_Video);

    report_group("Audio decoders:", AUDIO_FORMATS,
                 sizeof(AUDIO_FORMATS) / sizeof(AUDIO_FORMATS[0]),
                 &MFT_CATEGORY_AUDIO_DECODER, &MFMediaType_Audio);

    int result = 0;
    if (argc > 1) {
        printf("\n----------------------------------------------------------\n");
        printf("Trying the file you named.\n");
        result = probe_file(argv[1]);
    } else {
        printf("\nIf HEVC says \" - \" above: install \"HEVC Video Extensions\" from the\n");
    printf("Microsoft Store to add it. Much recent video is encoded with it.\n");

    printf("\nPass a file to find out whether Windows can open that file:\n");
        printf("  mfprobe.exe \"D:\\Films\\episode.mkv\"\n");
        printf("\nA container Windows cannot read is the thing to watch for.\n");
        printf("A decoder can be present while the container still is not.\n");
    }

    MFShutdown();
    CoUninitialize();
    return result;
}
