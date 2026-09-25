# T094 — A film decoded on the card survives a lost graphics device

Covers: RFC-0001 §5.7, RV-062 (T065's sleep/resume row)

After a lost device (a driver reset, a TDR, waking from sleep) the renderer
is rebuilt. A film decoded on the card is now opened again at the same
place, paused or playing as it was, on the same sound and subtitle tracks;
before, its decoder lived on the lost card and no more frames came until the
film was reopened by hand. The rebuild also lets go of the decoder's device
when that device was removed too, so the reopened film gets a fresh one.
Software-decoded films and sound are left alone.

`debug_lose_device` (no default key; bind it in a test machine's
keymap.ini — which replaces the built-in keys, so start from a full copy)
makes the next present act as a lost device.

## Steps and expected

1. With `[video] hardware_decode = always` (or `on` on a machine with a
   decoder), play a film with two subtitle files and pick the second.
2. Trigger `debug_lose_device`: the film goes on from where it was, the
   same subtitles show, the picture keeps moving. Paused, it stays paused.
3. On a real GPU PC: put it to sleep with a film playing and wake it (or
   trigger a TDR); the film goes on as in step 2.

## Measured on the Windows 11 VM, 2026-09-25

Portable folder `rbgpu`: `settings.ini` with `hardware_decode = always`
(`--probe-gpu`: 240 of 240 pictures on the card), a full keymap plus
`debug_lose_device = "Ctrl+Shift+F12"`, the film slowed with `Ctrl+[` so
the tools' five-second calls land while it plays.

| # | Result |
|---|---|
| 2 | PASS (synthetic loss): position 05.500 → 05.542 → … → 06.125 after the trigger, no restart and no pause; `한국어 자막` (the second track) still shown; two photographs 1.5 s apart show different frames. |
| 3 | NOT RUN: a real device loss cannot be caused on the VM; the synthetic one does not remove the decoder's device, so the fresh-device half is not exercised here. |

A false start: a keymap.ini holding only the trigger line removed every
other key (it replaces the built-in keymap by design), so `C` and `Left`
did nothing.
