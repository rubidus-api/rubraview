# T055: What can Windows decode by itself? (M5 groundwork)

RFC-0001 §5.1 lists a wide matrix of formats, and §5.2 says the viewer
must still work when the FFmpeg DLLs are absent. How much of §5.1
Windows can carry on its own decides how much FFmpeg is really needed —
and that question **cannot be answered from the build machine**. The
Media Foundation headers name dozens of formats, but naming a format is
not shipping a decoder for it, and which decoders exist differs between
Windows versions and between machines with and without the Store codec
extensions.

So this asks the machine directly.

## Running it

```
make mfprobe
```

Copy `dist/mfprobe.exe` to the Windows machine and run it:

```
mfprobe.exe
mfprobe.exe "D:\Films\episode.mkv"
```

It is a console program: run it from a command prompt, or its window
will close before you can read it.

## What to record

1. **The decoder list.** `yes` means a decoder is installed on *that*
   machine. Copy the whole list — the interesting part is which rows say
   `-`.
2. **Run it against real files**, one of each kind you actually watch:
   an `.mkv` from an anime release, an `.mp4` from a phone, an `.avi`
   from an old archive, an `.mp3`, a `.flac`. For each, note whether the
   container opened and whether every stream says `decodable: yes`.
3. **The interesting failures.** A file whose container opens but whose
   streams are not decodable means the codec is missing; a container
   that does not open at all means Windows does not know the format.
   Those two need different answers, which is why the probe separates
   them.

## What the answer decides

- If the machines this runs on decode most of what you actually watch,
  then FFmpeg becomes an optional extra for a long tail — RealMedia,
  Monkey's Audio, Ogg, ProRes — and the viewer works out of the box with
  no third-party runtime at all.
- If they do not, FFmpeg is the primary path and Media Foundation is not
  worth a second code path.

Either way this is measured rather than assumed, and the measurement
takes a minute.

## Worth knowing before you read the output

- **Microsoft documents native MKV support** — container and a broad
  codec list, including H.264, HEVC, VP8, VP9, AV1, MPEG-1/2/4 and
  Theora on the video side, and AAC, AC-3, E-AC-3, TrueHD, DTS, MP3,
  FLAC, ALAC, Opus, Vorbis and PCM on the audio side, plus SRT, SSA/ASS,
  VobSub and PGS subtitle tracks. See the Sources note in the M5 section
  of `BACKLOGS.md`.
- **HEVC and AV1 usually need a Store extension.** A machine without it
  will show `-` for those. That is the single most likely difference
  between two Windows machines.
- The MKV documentation's line about "the first track will be played"
  describes the default playback path, not `IMFSourceReader`, which has
  `SetStreamSelection` for choosing among several audio or subtitle
  tracks. The probe reports every stream it finds, so the file's own
  track count is visible.
