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

**The chain works, and was traced end to end.** A build with a temporary
trace beside the program recorded the decisions of one run:

```text
prepare index=1 media_page=0 media=1 bgm=0
adopt? media=1 page=0 video=0 ended=0 audio=1     the track becomes background music
opened audio=1 out=1 video=1                      the film opens, with sound
bgm_do action=1 bgm=1 holds=1 aside=1             PAUSE: the music stands aside
bgm_do action=2 bgm=1 holds=1 aside=0             RESUME: the film ended
```

That is steps 1, 2 and 3, each in its turn.

**What is not settled.** In runs measured by recording rather than
tracing, the film sometimes opened with **no audio at all** — neither its
own sound in the recording nor the `has_audio` that would tell the
arbiter anything — and the music therefore played on through it. The same
file plays with sound when it is opened with no background music behind
it. So: when the film has sound the arbiter does the right thing; whether
a film *gets* its sound while a second player already holds the device is
the open question, and it is a platform one rather than a decision one.
Written up in `BACKLOGS.md`.

Steps 4, 5 and 6 are not measured: 4 and 5 need the listener's own pause
on a page with no player, which the key path reaches only while music is
still running, and the tone track outlasts a measurement badly.
