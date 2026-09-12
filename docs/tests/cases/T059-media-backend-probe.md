# T059: Which backend opens this file, and does it really decode?

`mfprobe` (T055) asks Windows what decoders it has. This asks rubraview
what it actually does with one file: which backend took it, what it says
the file is, and whether pictures really come out of it. No window is
opened, so it answers over a remote shell — which is how slice 3 was
checked while the test VM had no logged-on session.

## Running it

```
rubraview-v<version>.exe --probe-media "D:\clips\episode.mkv"
```

With FFmpeg's DLLs beside the executable (avutil-60, avcodec-62,
avformat-62, swscale-9, swresample-6 for FFmpeg 8.1) both backends are
tried; without them, only Media Foundation.

## What to record

- the first line: whether the FFmpeg DLLs are there and at a version the
  build knows (a different major version is refused on purpose);
- per backend: opened or not, and the reason when not;
- the frame line: `N picture(s) decoded` with the times — a backend that
  opens a file but decodes nothing is the failure worth catching;
- for sound-only files: "sound only", plus whether an audio device is here.

Measured on the Win11 VM, 2026-09-12: FLV — Media Foundation cannot open,
FFmpeg decodes; MP4, Ogg/Theora, Opus — both backends open and decode.
