# Resampling: column weights computed once (2026-09-25, D-32)

Change: `src/core/resample.c` bicubic and Lanczos-3 compute each column's
horizontal weights and clamped source offsets once per block of 128
columns, instead of once per pixel of every row (commit "proven review 5/5").

Method: the previous `resample.c` was copied with its band function renamed
to `old_resample_band`, linked beside the new one, and both were run over the
same random image (`srand(7)`) into two buffers, compared with `memcmp` and
timed with `CLOCK_MONOTONIC`. One thread, `cc -std=gnu2x -O2`, the Linux build
host. Sizes also checked but too small to time: 17x9->5x3, 5x3->200x130,
1x1->7x7, 300x200->300x201.

| Filter | Pixels | Size | Old s | New s | Speed-up | Output |
|---|---|---|---|---|---|---|
| bicubic | rgba | 640x480 -> 1920x1080 | 0.233s | 0.149s | x1.6 | identical |
| lanczos3 | rgba | 640x480 -> 1920x1080 | 0.627s | 0.337s | x1.9 | identical |
| bicubic | gray | 640x480 -> 1920x1080 | 0.142s | 0.080s | x1.8 | identical |
| lanczos3 | gray | 640x480 -> 1920x1080 | 0.444s | 0.188s | x2.4 | identical |
| bicubic | rgba | 333x217 -> 1000x701 | 0.084s | 0.056s | x1.5 | identical |
| lanczos3 | rgba | 333x217 -> 1000x701 | 0.256s | 0.112s | x2.3 | identical |
| bicubic | gray | 333x217 -> 1000x701 | 0.048s | 0.027s | x1.8 | identical |
| lanczos3 | gray | 333x217 -> 1000x701 | 0.160s | 0.066s | x2.4 | identical |
| bicubic | rgba | 4000x3000 -> 1200x900 | 0.119s | 0.081s | x1.5 | identical |
| lanczos3 | rgba | 4000x3000 -> 1200x900 | 0.331s | 0.177s | x1.9 | identical |
| bicubic | gray | 4000x3000 -> 1200x900 | 0.074s | 0.043s | x1.7 | identical |
| lanczos3 | gray | 4000x3000 -> 1200x900 | 0.234s | 0.098s | x2.4 | identical |

All byte-identical: each pixel adds the same weights in the same order.
T042 (`test_resample_mt`) still gives the same bytes on four workers as on one.

Not done: a separable two-pass filter would cut Lanczos-3 from 36 to 12 taps a
pixel, but changes the output by rounding (BACKLOGS, M6).
