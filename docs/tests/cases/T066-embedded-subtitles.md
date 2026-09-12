# T066 — Subtitles carried inside the file

Covers: R135 (§3.16.1), RV-059, D-12

A film with its subtitles inside it shows them without any file beside it,
and `C` cycles through the streams, then the files beside it, then off.
Only the FFmpeg backend can read them; Media Foundation lists none, and
neither offers the picture-based formats.

## Preparation

An MKV with two audio tracks and two text subtitle streams, and one
subtitle file beside it:

```sh
ffmpeg -i two_audio.mp4 -i eng.srt -i kor.srt \
       -map 0:v -map 0:a:0 -map 0:a:1 -map 1 -map 2 \
       -c:v copy -c:a copy -c:s srt \
       -metadata:s:s:0 language=eng -metadata:s:s:1 language=kor embedded.mkv
```

`settings.ini` beside the executable with `[video] decoder = ffmpeg`, and the
FFmpeg DLLs beside it too.

## Steps

1. `--probe-media embedded.mkv`, once with `decoder = ffmpeg` and once with
   `decoder = windows`.
2. Open the film. Look at a subtitle.
3. Press `C` three times, reading the OSD each time.

## Expected

- Step 1: under FFmpeg the list holds `subtitle #1 eng (subrip)` and
  `subtitle #2 kor (subrip)`; under Media Foundation the same file lists no
  subtitle track at all. A picture-based stream (DVD, PGS, DVB) is listed by
  neither.
- Step 2: the first stream's text is on screen, with no file beside the video
  needed. This is a visible change: a film that showed nothing now shows its
  own subtitles.
- Step 3: `#2 kor` (the other stream), then the file beside it, then `Off`.
- The OSD says `reading the subtitles out of the file` while a stream is being
  read — the file is walked a second time to collect it.

## Measured on the Windows 11 VM, 2026-09-13 (v0.0.2)

All three, in screenshots: `Embedded English subtitle` on opening,
`subtitles #2 kor (subrip)` then `내장 한국어 자막` drawn, `subtitles #3
unlabelled (srt)` for the file beside it. The Media Foundation run of step 1
listed the two sound tracks and no subtitle track, which is the refusal this
case is looking for.
