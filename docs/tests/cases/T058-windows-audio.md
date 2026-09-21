# T058: Sound, and the picture following it (M5 slice 2)

The test VM has no audio device of its own, but a session opened over RDP
with the sound channel on has one ("Remote Audio"), and what the viewer
plays there can be recorded and measured — see "Measured on the VM" at
the end. What still needs ears is below: lip sync, clicks, and how it
sounds on a real device.

## Running it

Build with `make package` and copy `dist/rubraview-v<version>/rubraview-v<version>.exe`
to a Windows machine **with speakers or headphones**. Use any folder of
your own files, or the test clips (`resources/cache/media/`, ledger R004:
Big Buck Bunny, CC-BY 3.0, "(c) copyright 2008, Blender Foundation /
www.bigbuckbunny.org").

## Steps and what to look for

1. Open an `.mp4` with sound. **You hear it**, and lips/impacts match the
   picture — no visible lead or lag.
2. `Space` pauses: **sound and picture stop together**. `Space` again:
   both carry on from the same place, without a click or a jump.
3. `Right` / `Left` (5 s; D-16 moved the seek off `Ctrl+arrows`, which now
   size the window): the sound jumps with the picture and
   is in sync again at once — no half-second of old sound after the jump.
4. While paused, `.` and `,` step one frame: the picture moves, the sound
   stays silent.
5. Let the file reach its end: the last picture stays, the sound stops,
   the title says `paused`. `Space` plays it again from the start.
6. Open an `.mp3` or `.flac` **with embedded album art**: the art is the
   page. One without art shows a plain dark square. The title's time runs
   with the music and stops at the end.
7. Open an `.ogg` or `.opus`: if Windows cannot open it, a line says so on
   screen for five seconds and the viewer moves to the next file (D-9).
8. Unplug or disable the audio device, then open a video: it still plays,
   silently, at the right speed.

## What to record

For each step: works / does not work, and for any lag, how much it looked
like (a frame, a quarter-second, more). Note the Windows version and the
audio device (built-in, USB, Bluetooth — Bluetooth adds its own delay).

## Measured on the VM (2026-09-21, over RDP)

How: the RDP session's "Remote Audio" output is recorded inside the guest
with WASAPI loopback (`vmrecord.sh`, beside the other VM helpers) while
`vmkeys.sh` drives the viewer, and the WAV is read back with `wavstat.py`.
The test file is a generated tone that climbs one step a second
(200 Hz + 25 Hz per second, 120 s, -12 dBFS), so the pitch heard says
which second of the file is playing: a seek, a pause or a speed change
shows as a jump, a gap or a stretch in the steps. The recorder reports
its own lost frames (none in these runs).

| Step | Result |
|---|---|
| Sound plays | yes, level and pitch as in the file |
| `Down` x10 | -6.0 dB (50 % amplitude), one step per key |
| `Shift+M` | digital silence; the file keeps running underneath (15.15 s muted = 15 s later in the file), back at the same level |
| `Space` pause / play | silence, then the same second again (21 s paused, position unchanged) |
| `.` `,` while paused on a sound-only file | silent, position unchanged |
| `Right` / `Left` | +5 s / -5 s in the sound, gap about 0.1 s |
| `Ctrl+]` x2, `Ctrl+[` x2, `Ctrl+\` | 1.25x then 1.5x (pitch x1.5, a second lasts 0.6-0.7 s); 0.75x then 0.5x (pitch /2, 2.0 s); back to 1x with no jump |
| End of file | silence held; `Space` starts again at 0 |
| `A` on a two-track file (T062's 440/880 Hz) | 880 -> 440 -> 880 Hz: the track really changes |

Not measured here: lip sync against the picture (step 1), clicks (step
2), and a device being unplugged (step 8). Seen on the way: the
two-track file opened on its `kor` track, not `eng` as T062 expects — on
a Korean Windows, Media Foundation's default pick.
