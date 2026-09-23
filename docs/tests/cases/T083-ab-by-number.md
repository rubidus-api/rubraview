# T083 — The A-B repeat points, by key and by number

Covers: SPEC §5.5, D-28, RFC-0001 §5.5

The owner (2026-09-24): "A B 반복을 수동으로 시간을 입력할 수 있으면
좋겠네요. 키로 눌러서 지정하고 그걸 다시 수동으로 숫자를 입력하게 하고
다시 그걸 키로 고치고 등 서로 교차해서 지정할 수 있게요."

So the two ways of naming a moment have to cross over freely: a point
tapped in with a key shows as a number that can be edited, and a number
typed in can be overwritten by tapping the key again.

`rubraview_parse_timecode` — the reading half — is covered on the host by
`test_playback`, including that everything the timeline writes reads back.
This case is about the box.

## Steps and expected

1. Play a film or a track and press `[` and `]` as before. They still set
   A and B where the playhead is, and say so.
2. Press `Shift+\`. A box opens showing both points as numbers — the ones
   just tapped in, in the timeline's own spelling (`01:14.200`).
3. Type over the field in hand: digits, `:` and `.` only. Anything else
   is ignored rather than shown.
4. `Tab` moves between A and B. The field in hand is the one in brackets,
   with a caret blinking in it.
5. With the box open, press `[` or `]`. **This is the crossing over**:
   the playhead's time goes into that field as a number, replacing what
   was there, and can then be typed over again.
6. `\` empties both fields. `Esc` closes without changing the points.
7. `Enter` keeps what is written: the loop is set to those two times, and
   the OSD says the range in seconds.
8. Type nonsense (`12s`) and press `Enter`: the box stays open, says
   which field is not a time, and moves to it. Nothing is applied.
9. Give B a time at or before A and press `Enter`: it says B has to come
   after A and stays open.
10. Leave a field empty and press `Enter`: that point is unset — A empty
    means the repeat is off.
11. While the box is open, the keys that normally turn pages or play do
    nothing: a box taking a number must not also drive the viewer.

## Measured on the Windows 11 VM

NOT RUN YET — to be run on the packaged build from its own folder.
