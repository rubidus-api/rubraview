# T095 — Resizing on the graphics card (export and batch)

Covers: RFC-0001 §3.10-§3.11, §6.5.5, D-38

With `[display] gpu_resize = on` (the default), bicubic and Lanczos-3
resizes of a megapixel or more run on the graphics card when it can run
compute shaders: the card does the CPU's two passes with the CPU's weight
tables (`rubraview_resample_axis_weights`), so the result is the CPU's.
Without a card, or when anything fails, the CPU resizes — a batch run on
every core (RV-067). `always` also uses Windows' software adapters, for
testing; `off` never uses the card.

Host coverage: `tests/test_resample.c` — the weight tables reproduce the
CPU's two-pass bytes exactly; the accelerator is asked only for bicubic
and Lanczos, four bytes a pixel and large enough, and a refusal leaves the
CPU's result.

## Steps and expected

1. `rubraview.exe --probe-gpu <any file>`: the first lines say what
   `gpu_resize = on` would use and, with `always`, compare the card with the
   CPU on six resizes — "max diff" 0 or 1.
2. On a PC with a graphics card, the probe's "card" times are below the
   "cpu" times for the enlargements.
3. A batch run with `--resize` and Lanczos writes the same file with
   `gpu_resize = on`, `always` and `off`.
4. Export (`E`, a resize, Save a copy) on a large photo is faster with a card.

## Measured on the Windows 11 VM, 2026-09-25

The VM has no graphics card; Windows offers its Basic Render Driver (a
software adapter presented as hardware) and the shaders compile with
`d3dcompiler_47.dll` from System32.

| # | Result |
|---|---|
| 1 | PASS: `on` → "no graphics card (Basic Render Driver): the CPU resizes (not used)"; `always` → all six resizes (bicubic and Lanczos; 1600x1200 to 2400x1800, 1000x750, 320x240) **max diff 0, 0 % of bytes differ**. |
| 2 | NOT RUN: no card. On the software adapter the card path was 1.2-3x slower than the CPU, which is why `on` does not use it. |
| 3 | PASS: a Lanczos 150 % batch of a 3840x2400 photo gave the same PNG (SHA-256 `4E7334EB…`) with `always`, with `off` on one thread, and with `off` on the new worker pool (2 cores: 1981 → ~1790 ms end to end; the PNG write is most of it). |
| 4 | NOT RUN. |
