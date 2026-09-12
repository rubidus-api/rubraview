# T061 — External subtitles over a film

Covers: R135 (§3.16.1), RV-059

A subtitle file that shares the film's name is found, read and drawn over
the picture; `Z` and `X` move the whole track half a second at a time.

## Preparation

Put a `.srt` beside a short video with the same base name — `e_clip.mp4` and
`e_clip.srt`. Three cues are enough, one of them two lines:

```
1
00:00:01,000 --> 00:00:04,000
첫 번째 자막입니다

2
00:00:05,000 --> 00:00:08,000
두 줄짜리 자막
아랫줄입니다

3
00:00:08,500 --> 00:00:10,000
마지막 줄
```

## Steps

1. `rubraview-v<version>.exe --probe-media <video>` — the last line reports the
   subtitle file, how many lines it holds and when the first one starts.
2. Open the video in the viewer. The OSD says `subtitles: <file>`.
3. Watch a moment inside the first cue, and one inside the two-line cue.
4. Press `X` four times while it plays, then look again two seconds before a
   cue would normally start.

## Expected

- The probe line names the file: `Subtitles: e_clip.srt — 3 line(s), the first
  1.000 s to 4.000 s`. When there is none it says so, and why.
- Each cue appears at its own time, centred near the bottom, white on a dark
  outline that stays readable over a bright picture.
- A two-line cue is drawn on two lines.
- Each `X` moves the track 0.5 s later (`Z`, earlier) and the OSD shows the
  offset, for example `subtitles +2.0 s`. The picture follows: with `+2.0 s`
  the first cue is on screen at 4.5 s, where nothing would otherwise be.

## Measured on the Windows 11 VM, 2026-09-12 (v0.0.2)

All four steps as described. The film reopens from the start when it is played
again after the end, and reopening re-reads the subtitle file — so a sync
offset set by `Z`/`X` does not survive a replay. That is recorded as a backlog
item, not a defect against this case.

> Capture the screen from inside the logged-on session and close to the moment
> you mean: `vmkeys.sh <keys> 1 50 <ms>` photographs the screen `<ms>` after the
> last key. A separate screenshot run arrives about two seconds later, by which
> time the OSD has faded.
