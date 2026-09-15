# T072 — The toolbox follows what is on screen; the menu has File, Playback, Help

Covers: RFC-0002 §4–§5, D-15

## Steps and expected

Open each file, click the toolbox anchor's `T`:

| File | Tiles |
|---|---|
| a picture | Prev · Next · Zoom- · Zoom+ · 1:1 · Rotate · Slides · Full |
| a video | Play/Pause · Stop · -5 s · +5 s · Vol - · Vol + · Mute · Subs · Sound · Full |
| a music file | Prev · Pause/Play · Stop · -5 s · +5 s · Next · Vol - · Vol + · Mute |
| a CBZ | Prev · Next · Layout · Order · Vol < · Vol > · Fit · Full |

- The grid opens beside the anchor — above it near the bottom edge — and
  every tile is inside the window.
- On the video, **Stop** puts it at 0:00, paused.
- Click `M`: File · View · Playback · Show · Help (Playback only while a
  video or music page is on screen). File holds Open file, Open folder,
  Recent, Next / Prev volume, Rename, Delete, Export, Batch, Settings, Quit;
  Recent lists the reading history newest first, and an entry opens it.

## Measured on the Windows 11 VM, 2026-09-15

All of the above by screenshot; Stop: `00:10.042 paused → 00:00.083 paused`;
Recent listed `rbtest`, `rbmedia` and `rbtest` opened at its remembered page
(`c_plain.png (3/6)`). Before this change the toolbox by the bottom-right
corner opened over its own anchor with two tiles off the screen.

Found on the way: `history.ini`'s `time` held seconds since the machine
started (`14496`), not a date — fixed in the next change.
