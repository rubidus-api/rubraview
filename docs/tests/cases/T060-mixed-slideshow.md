# T060 — A slide show that mixes stills and film

Covers: R129, R130 (RV-061, §3.2.6)

A slide show turns a still page on its interval. A page that holds a film or a
sound track has to be waited out instead: the slide stays until the file has
played to its end.

## Preparation

A folder with at least four pictures and one short video (about ten seconds),
named so that the video sits in the middle of the reading order.

```
make package
```

Copy `dist/rubraview-v<version>.exe` to the Windows machine and open the first
picture with it.

## Steps

1. Press `S` (or `F5`).
2. Read the window title once a second for about half a minute.

## Expected

- The still pages change every three seconds — the default interval.
- The video page stays for the whole length of the film. Its title counts the
  playing position up to the duration, and only then does the next page appear.
- After the last page the show returns to the first one.
- Pressing `S` again stops it: the page on screen stays.

## Measured on the Windows 11 VM, 2026-09-12 (v0.0.2)

Six pages, five pictures and `e_clip.mp4` (10.0 s) in fifth place. Titles
sampled once a second from inside the logged-on session:

```
   961ms d ... (4/6)
  2201ms e_clip.mp4 (5/6) 00:01.208 / 00:10.000
 ...
 10729ms e_clip.mp4 (5/6) 00:09.750 / 00:10.000
 11738ms E_UPPER.JPG (6/6)
 15224ms a_photo.jpg (1/6)
```

The film held the slide for 9.5 s of the sampling window and the stills turned
on the three-second interval.

> The title must be read **inside the logged-on session**. An ssh shell runs in
> session 0 and cannot see the window at all: `MainWindowTitle` comes back empty
> and `MainWindowHandle` zero, which reads exactly like a hung viewer.
