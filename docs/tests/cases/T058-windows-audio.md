# T058: Sound, and the picture following it (M5 slice 2)

The test VM has no audio device of its own, but a session opened over RDP
with the sound channel on has one ("Remote Audio"), and what the viewer
plays there can be recorded and measured — see "Measured on the VM" at
the end: every step below, lip sync and clicks included, has been
measured there from the recording. A real device adds its own latency
(Bluetooth most of all), which no recording inside the machine sees.

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

Seen on the way: the two-track file opened on its `kor` track, not `eng`
as T062 expects — on a Korean Windows, Media Foundation's default pick.

### Lip sync, clicks and a lost device (2026-09-21, second pass)

**Lip sync (step 1).** A generated film (160x90, 25 fps, uncompressed AVI)
flashes white for two frames and beeps 1 kHz for 80 ms at the start of
every second. Inside the session one program records, on one clock (QPC),
the loopback sound with each packet's time and the brightness of the
pixel in the middle of the screen (`avsync.ps1`); the beep's start minus
the flash's start is the offset. Media Foundation, 19 beeps: the sound
led the picture by 27 ms (median). The RDP session's screen changes only
about 32 times a second, so every value is exact to one 31 ms step: the
offset lies between 27 ms sound-first and 4 ms sound-late, well inside
what broadcast practice allows (125 ms early, 45 ms late). The first three
beeps after opening read 89 ms.

**Clicks (step 2, 3).** On a recorded tone a click is the waveform jumping
to or from silence mid-wave. Pause and play: ten edges out of ten at 0 —
Windows' own stop and start are quiet. Seeks were not: the new position
began at up to full amplitude and the old one was cut mid-wave in half
the cases, and three 10 ms drop-outs appeared while a helper script
compiled on the 2-core VM. Fixed and measured again (12 seeks, both edges
of every one at 0, no step anywhere larger than the tone's own):

- the thread that feeds the device joins Windows' multimedia scheduler
  (MMCSS "Playback"), so a busy machine does not starve it;
- after a seek the sound fades in over 5 ms;
- a seek stops the stream and waits 30 ms before throwing the queue away,
  so the stream ends the quiet way a pause does.

**The device goes away (step 8).** Opening a file with no device plays it
silently at 1 s per second (as before). Losing it *while playing* — the
remote session reconnected without sound — froze the position: the clock
follows the sound, and no sound was being used up. Now the sound is used
up on the wall clock while there is no device (the position ran at 1.0 s
per second; a seek was answered), the default device is tried again once a
second, and when the sound came back the position went on and the tone
heard matched the position shown.
