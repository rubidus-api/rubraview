# T065 — What this machine can do with hardware decode

Covers: R139 (§5.7), RV-062, D-11

Before the zero-copy path is built, one run says whether the machine in
front of you can exercise it at all: which graphics device is there, how
many decoder profiles it offers, whether Media Foundation accepts the
device, and — the question that decides the shape of RV-062 — whether a
decoded frame then comes back as a texture or as system memory.

## Steps

```
rubraview-v<version>.exe --probe-gpu <a video file>
```

## Expected on a machine with a graphics card

```
Device: hardware, feature level 11.1
Adapter: <the card> (2048 MB of its own memory)
Decoder profiles the card offers: 17
The reader took the device.
The frame came back as a texture on the card — the zero-copy path can be tried here.
```

## Measured on the Windows 11 VM, 2026-09-13 (v0.0.2)

```
Device: hardware, feature level 11.0
Adapter: Microsoft Basic Render Driver (0 MB of its own memory)
Decoder profiles the card offers: none (no video device)
The reader took the device.
but no picture came out of it (hr=0xC00D36B4, last flags=0x1)
```

`0xC00D36B4` is `MF_E_INVALIDMEDIATYPE`. So on this machine attaching the
device does not merely fail to accelerate — it stops the decode: the reader
will no longer hand over the RGB32 pictures it gives without it.

Two things follow for RV-062, and they are requirements on the code, not
observations:

1. **Do not attach a device that offers no decoder profiles.** The count is
   free to ask for and it is the difference between "slower" and "broken".
2. **A reader that errors after the device is attached must be reopened
   without it**, not left to fail. The film plays either way.

The zero-copy path itself cannot be tried on this VM. D-11 keeps RV-062
outside M5 for exactly this reason.

## The viewer's own path (RV-062, 2026-09-22)

The probe now ends with two more lines: the media PAL opening the file with
the device, as `[video] hardware_decode = on` and `= always` would:

```
hardware_decode = on: decoded in software; 30 pictures in 3 s, 0 of them on the card
hardware_decode = always: decoded on the card; 30 pictures in 3 s, 30 of them on the card
```

That is the VM. On a machine with a GPU, `on` should read "decoded on the
card" with pictures on the card; if it reads "in software" there, the reader
refused the device and the fall-back did its job — say so in the report.

Step 3 on a real card, beyond these lines: the same clip with
`hardware_decode = off` and `= on`, CPU per second of playing (both), the
same paused frame compared, a 4K HEVC file, and sleep/resume while a film
plays (a rebuilt renderer is not handled yet — the picture holds until the
film is reopened).
