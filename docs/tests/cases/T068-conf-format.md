# T068 — Every configuration file is INI and TOML at once

Covers: R148 (§3.22.1), D-13

The files the viewer writes — `settings.ini`, `layout.ini`, `history.ini`, and
the built-in keymap users copy into `keymap.ini` — are in the smallest common
subset of INI and TOML: lines that a TOML 1.0 parser and a plain INI reader both
accept with the same meaning.

## How it is checked

```sh
python3 scripts/check-conf-format.py
```

It builds `tools/conf_samples.c` against the real writers, lets them write
sample files — every value kind, the strings most likely to break one parser
or the other (a Windows path, quotes, `%`, `#` and `;`, Korean text, spaces at
the ends, a number-looking folder name), an old hand-edited file read and
written back, the default keymap, a reading history with a quoted path,
settings at their defaults and saved over an older file — and reads each one
with `tomllib` and with `configparser` (no interpolation).

"The same meaning" is: the INI text, with the subset's one quoting rule undone
(outer double quotes, `\\` and `\"`), equals what TOML reads.

## Expected

`check-conf-format: ok — N files, M values, the same under TOML and INI`; it is
part of `scripts/project-check.sh`. A run that measured no file fails.

## Measured, 2026-09-13

7 files, 178 values. Injecting a fault — the writer leaving strings unquoted —
made it fail on both files it then had, with TOML refusing them.

Adding the real writers to the samples found a defect straight away: saving
over an older file wrote the General tab's keys back above the first section,
where INI refuses a key. The save now moves them under `[general]`, once.
