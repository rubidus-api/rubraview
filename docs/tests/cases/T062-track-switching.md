# T062 — Sound tracks and subtitle files, switched while playing

Covers: R135 (§3.16.2), D-10

A film with two sound tracks and two subtitle files beside it: the viewer
lists what it found, plays another sound track on `A`, and cycles the
subtitles — every file, then off — on `C`.

## Preparation

A ten-second video with two AAC tracks tagged `eng` and `kor`, and two
subtitle files with the same base name and different text:

```
two_audio.mp4      video + sound(eng) + sound(kor)
two_audio.eng.srt
two_audio.kor.srt
```

Made from one of the R004 sample clips and two generated tones:

```sh
ffmpeg -f lavfi -i "sine=frequency=440:duration=10" -c:a aac tone_eng.m4a
ffmpeg -f lavfi -i "sine=frequency=880:duration=10" -c:a aac tone_kor.m4a
ffmpeg -i clip_h264.mp4 -i tone_eng.m4a -i tone_kor.m4a -map 0:v -map 1:a -map 2:a \
       -c copy -metadata:s:a:0 language=eng -metadata:s:a:1 language=kor two_audio.mp4
```

FFmpeg's DLLs beside the executable make the second backend testable too.

## Steps

1. `rubraview-v<version>.exe --probe-media two_audio.mp4`
2. Open the file, let it play, press `C` three times.
3. Press `A`.

## Expected

- The probe lists every track of both backends, marking the current one:
  `sound * #1 en (aac 1.0)`, `sound   #2 ko (aac 1.0)`, `picture * #1 (H264)`.
  Media Foundation and FFmpeg may number and name them differently, and may
  start on different tracks — each reports what it sees.
- `C` moves to the next subtitle file (the OSD says `subtitles #2 kor (srt)`
  and the text on the picture changes), then to `Off`, then round again.
- `A` on a machine with sound plays the other track from the same moment;
  the picture does not stop. **On a machine with no audio device** it says
  `the sound track cannot be changed here` and nothing changes — there is no
  sound to switch.

## Measured on the Windows 11 VM, 2026-09-13 (v0.0.2)

Steps 1 and 2 as described, in screenshots: English file → `subtitles #2 kor
(srt)` with Korean text → `subtitles Off`. Step 3 gave the refusal message,
which is right for this machine: the VM has no audio device.

> The audible half of step 3 — the other language actually coming out of the
> speakers, in sync, without the picture stopping — can only be checked where
> there is sound. It is part of T058.
