# T072 — The toolbox follows what is on screen; the menu has File, Playback, Help

Covers: RFC-0002 §4–§5, D-15

## Steps and expected

Open each file, click the toolbox anchor's `T`:

| File | Tiles |
|---|---|
| a picture | Prev · Next · Zoom- · Zoom+ · 1:1 · Rotate · Slides · Full |
| a video | Play/Pause · Stop · -5 s · +5 s · Vol - · Vol + · Mute (Unmute while muted) · Subs · Track · A-B · 1x · Full |
| a music file | Prev · Pause/Play · Stop · -5 s · +5 s · Next · Vol - · Vol + · Mute |
| a CBZ | Prev · Next · Layout · Order · < Archive · Archive > · Fit · Full |

- The grid opens beside the anchor — above it near the bottom edge — and
  every tile is inside the window.
- On the video, **Stop** puts it at 0:00, paused.
- Click `M`: File · View · Playback · Show · Help (Playback only while a
  video or music page is on screen). File holds Open file, Open folder,
  Recent, Next / Prev archive, Rename, Delete, Export, Batch, Settings, Quit;
  Recent lists the reading history newest first, and an entry opens it.
- (2026-09-21) Submenus end in `>`; switches read `name: on/off`; the layout
  and fit in use have a blue edge; a tile that cannot act is grey and a
  click on it does nothing. Close the menu one level down and open it
  again: it opens at `Menu`, not where it was left.

## Measured on the Windows 11 VM, 2026-09-21

By screenshot: root `On top: off · File > · View > · Show > · Help >`; in a
plain folder File's `Next archive` / `Prev archive` grey; Layout with
`Single` edged blue and `Spreads: on`; Fit with `Window` edged and
`Lock: off`. Closed at `Menu > View > Layout` and reopened (click, and
hover): at `Menu`. Before the fix it reopened at the level it was closed on
— the anchor's click goes through the box-drag path, which never went back
to the root. A muted film's toolbox reads `Unmute`; `Track` and `Subs` are
grey on a file with one sound track and one subtitle file.

## Measured on the Windows 11 VM, 2026-09-15

All of the above by screenshot; Stop: `00:10.042 paused → 00:00.083 paused`;
Recent listed `rbtest`, `rbmedia` and `rbtest` opened at its remembered page
(`c_plain.png (3/6)`). Before this change the toolbox by the bottom-right
corner opened over its own anchor with two tiles off the screen.

Found on the way: `history.ini`'s `time` held seconds since the machine
started (`14496`), not a date — fixed in the next change.
