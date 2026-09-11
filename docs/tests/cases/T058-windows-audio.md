# T058: Sound, and the picture following it (M5 slice 2)

The test VM has no audio device, so everything here that needs a
loudspeaker has to be checked on a real Windows machine. On the VM the
same build plays video silently on the wall clock, and sound-only files
run for their duration — that part is already checked there.

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
3. `Ctrl+Right` / `Ctrl+Left` (5 s): the sound jumps with the picture and
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
