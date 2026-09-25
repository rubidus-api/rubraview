# Resampling in two passes (2026-09-25, D-37)

Change: bicubic and Lanczos-3 filter each source row horizontally once per
block of 128 columns into a small cache of float rows and sum those
vertically — taps + taps multiplications a pixel instead of taps x taps —
unless the image shrinks vertically by taps - 1 or more, where the old
2-D sum is cheaper and is used unchanged.

Method: `tools/resample_compare.c` against `tools/resample_reference.c`
(the resampler before this change), one thread, `-O2`, the Linux build
host, on a photograph-like source (gradients, noise, hard edges). Build
line in the tool's header.

| Filter | Pixels | Size | Before s | After s | Speed-up | Max diff | Bytes changed |
|---|---|---|---|---|---|---|---|
| bicubic | rgba | 640x480 -> 1920x1080 | 0.161 | 0.050 | x3.2 | 1 | 0.001% |
| lanczos3 | rgba | 640x480 -> 1920x1080 | 0.348 | 0.070 | x4.9 | 1 | 0.001% |
| bicubic | gray | 640x480 -> 1920x1080 | 0.086 | 0.020 | x4.4 | 1 | 0.001% |
| lanczos3 | gray | 640x480 -> 1920x1080 | 0.197 | 0.025 | x7.8 | 1 | 0.001% |
| bicubic | rgba | 333x217 -> 1000x701 | 0.053 | 0.015 | x3.6 | 1 | 0.001% |
| lanczos3 | rgba | 333x217 -> 1000x701 | 0.121 | 0.022 | x5.5 | 1 | 0.001% |
| bicubic | gray | 333x217 -> 1000x701 | 0.029 | 0.006 | x4.5 | 1 | 0.001% |
| lanczos3 | gray | 333x217 -> 1000x701 | 0.065 | 0.007 | x9.6 | 1 | 0.001% |
| bicubic | rgba | 4000x3000 -> 1200x900 | 0.081 | 0.081 | x1.0 | 0 | 0.000% |
| lanczos3 | rgba | 4000x3000 -> 1200x900 | 0.178 | 0.131 | x1.4 | 1 | 0.001% |
| bicubic | gray | 4000x3000 -> 1200x900 | 0.043 | 0.043 | x1.0 | 0 | 0.000% |
| lanczos3 | gray | 4000x3000 -> 1200x900 | 0.098 | 0.053 | x1.9 | 1 | 0.001% |
| bicubic | rgba | 1920x1080 -> 480x270 | 0.010 | 0.010 | x1.0 | 0 | 0.000% |
| lanczos3 | rgba | 1920x1080 -> 480x270 | 0.021 | 0.018 | x1.2 | 1 | 0.001% |
| bicubic | gray | 1920x1080 -> 480x270 | 0.005 | 0.005 | x1.0 | 0 | 0.000% |
| lanczos3 | gray | 1920x1080 -> 480x270 | 0.012 | 0.007 | x1.6 | 1 | 0.002% |
| bicubic | rgba | 5x3 -> 200x130 | 0.002 | 0.000 | x5.4 | 0 | 0.000% |
| lanczos3 | rgba | 5x3 -> 200x130 | 0.004 | 0.001 | x8.1 | 1 | 0.003% |
| bicubic | gray | 5x3 -> 200x130 | 0.001 | 0.000 | x7.6 | 0 | 0.000% |
| lanczos3 | gray | 5x3 -> 200x130 | 0.002 | 0.000 | x12.9 | 0 | 0.000% |

Bilinear and nearest are unchanged (identical bytes). The largest change in
any output byte is 1, in at most 0.003 % of bytes: the float intermediate
keeps one rounding, only the order of the additions differs. Bands still
agree byte for byte with one pass (T042).
