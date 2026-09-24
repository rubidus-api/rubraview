# T085 — Background music, and who gets the speakers

Covers: SPEC §21, RFC-0001 §3.14.6, RV-081

Music does not stop because the reader turned to a picture: the player
leaves the page's hands and goes on as background music. Then a film with
sound is opened and two things want the speakers, so an arbiter decides —
the music stands aside and comes back when the film is over or closed.

The rule that matters, and the one players get wrong, is that **the
listener's own pause outranks the arbiter**: music stopped by hand must
not come back to life because a film ended. `test_music` covers the
arbiter itself on the host, including that rule. This case is about the
sound.

## Making a folder to test with

Three pages whose sound can be told apart: a long tone track (`1
track.wav`, 587 Hz, forty seconds), a picture (`2 picture.png`), and a
film with sound of its own (`3 film.mp4`). The recording then says which
of them is being heard at any moment; a zero-crossing pitch cannot, so
the two frequencies are measured separately.

## Steps and expected

1. Open the track, then turn to the picture. The music plays on — the
   recording shows the tone unbroken across the page change.
2. Turn to the film. The music stands aside and the film is heard.
3. Let the film end. The music comes back.
4. On the picture page, `Space` pauses and resumes the music itself.
5. With the music paused by hand, open and end a film: the music stays
   paused.
6. Turn `audio.bgm_pause_on_video` off: the two simply play together.

## Measured on the Windows 11 VM, 2026-09-24

Steps 1, 2 and 3, measured by recording the session and reading the two
tones apart — the background track at 587 Hz, the film's own sound at
440 Hz — with the viewer's own decisions traced beside them so the two
clocks could be lined up:

```text
app 753207.4   the track opens and plays
app 753217.9   the reader turns to the picture: it becomes background music
app 753232.6   the film opens  -> the music is paused
app 753242.8   the film ends   -> the music is resumed

recording  4.0   587 on                     (the track)
recording 29.5   587 off, 440 on            (the film opened: 753232.6)
recording 39.5   440 off, 587 on            (the film ended:  753242.8)
```

The two agree to within the half-second measuring window: the music
stands aside the moment the film opens and comes back the moment it ends,
and the film is heard in between.

### The wrong turning this case took first, and what it cost

Four earlier recordings showed the music playing straight through the
film with no film sound at all, which was written up here as "a film
opened while music plays sometimes gets no sound" and put in `BACKLOGS.md`
as an open platform question. It was neither.

Every call to the VM key tool takes five to six seconds of its own — it
registers and starts a scheduled task inside the session. The test script
had `sleep 4` between steps and assumed the keys landed there, so the
film actually opened twenty to thirty seconds later than intended, after
the recording had already stopped. The recordings were truthful about the
window they covered; the window was simply the wrong one.

The fix in method is the one this project keeps relearning in a new
dress: **do not date events by the sleeps in the script.** Have the
program say when it did each thing, and line the measurement up against
that. A trace of four lines settled in one run what four recordings could
not.

Steps 4, 5 and 6 are not measured: the listener's own pause from the
keyboard on a page with no player, the same with a film coming and going,
and the setting turned off. The first two are covered by the mini player
instead (T086), which pauses the background music by the same path.
