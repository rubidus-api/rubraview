# T073 — Timeline, playback speed and A-B repeat

Covers: RFC-0002 §4.1–§4.2 and Q6, D-15

## Steps and expected

1. Play a video; `Ctrl+]` four times: OSD `speed 2x`; in 2 s of wall time
   the title's time moves about 4 s.
2. `Ctrl+\`: back to `1x`.
3. Paused near the start, `[` sets A; `Right` (5 s on), `]` sets B; `Left`,
   `Space`: the film plays to B and starts again from A, again and again.
4. `\` turns the repeat off.
5. Paused, the strip above the information bar shows elapsed time, the seek
   bar, the total, the volume and the speed when it is not 1x. Clicking the
   middle of the bar lands in the middle of the film; dragging follows.
6. The toolbox's `1x` tile cycles 1, 1.25, 1.5, 2, 0.5, 0.75; `A-B` walks
   A → B → off.

## Measured on the Windows 11 VM, 2026-09-15

1 `00:00.583 → 00:05.125` in 2.26 s; 3 A `00:00.083`, B `00:05.083`, then
`… 04.875 → 00.208 → 00.708 …` twice round; 5 the strip read
`00:03 ▬ 00:10 vol 100% 1.25x` and a click at the bar's middle gave
`00:05.000`. The VM has no audio device, so speed ran on the wall clock;
through a device the sound is resampled (the pitch follows the speed) and
that is not heard here.

Found while measuring, fixed in the same change: at B the repeat sought
back to A on every tick, because the frame still on screen was past B until
the next one arrived — the film stuck at B. The shown position now moves to
A at once.
