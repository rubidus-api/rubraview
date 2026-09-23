# T037: Windows archive and caching smoke check (M4)

M4 gave the viewer comic archives, a pre-cache ring, reading history and
colour management. The decisions behind all of it are covered on the
host by T033-T036 and by the archive tests in T014 — including real
DEFLATE round trips through the vendored miniz. What the build machine
still cannot show is a CBZ actually rendering, because there is no
Windows here.

Run this after T025 (canvas) and T031 (reading UI) on the Windows target.

## Prerequisites

- `dist/rubraview-v<version>.exe` from `make win64`.
- A real `.cbz` of a comic or manga volume, ideally one produced by a
  Korean or Japanese archiver so its filenames are not UTF-8.
- A second volume in the same folder, named so the two sort in order
  (`Vol 01.cbz`, `Vol 02.cbz`).
- A photo from a phone or camera carrying a Display P3 or Adobe RGB
  profile.
- A `.cbz` containing a `ComicInfo.xml` with
  `<Manga>YesAndRightToLeft</Manga>` and a `FrontCover` page tag.

## Steps and expected results

| # | Action | Expected |
|---|---|---|
| 1 | `rubraview.exe "Vol 01.cbz"` | The first page appears. Nothing is unpacked to `%TEMP%` — check it during and after (§3.8.1's zero-disk invariant). |
| 2 | Flip through twenty pages quickly | Pages appear without a visible decode pause: the ring decoded them ahead (§3.1). |
| 3 | Watch memory in Task Manager while flipping through a long volume | It settles rather than climbing without bound — the budget is evicting (§7.4). |
| 4 | Check the page order against the archive's own numbering | `1, 2, 9, 10` order, not `1, 10, 2, 9` (§3.2.3 natural sort inside the archive). |
| 5 | Open the archive with non-UTF-8 filenames | Page names read correctly rather than as mojibake; the host code page was applied (§3.8.3 step 3). |
| 6 | Reach the last page and press `Right` | The next volume opens automatically and shows its first page (§3.8.1 point 4). Press `Left` on its first page to go back. |
| 7 | `Ctrl+]` and `Ctrl+[` from anywhere in a volume | Jump directly to the next and previous archive. |
| 8 | Open the manga archive with `ComicInfo.xml` | It opens in Book mode reading right to left with no key press, and the tagged cover is shown alone rather than paired (§3.8.5). |
| 9 | Close the viewer partway through a volume, then reopen the same file | A prompt offers the page you stopped on; `Enter` accepts it (§3.17.1). |
| 10 | Check where the history was written | With no `settings.ini` beside the exe: `%APPDATA%\rubraview\history.ini`. Put an empty `settings.ini` next to the exe and reopen: the history is written *there* and nothing new appears under `%APPDATA%` (§3.17.2 portable mode). |
| 11 | Open the wide-gamut photo, and the same photo converted to sRGB | The two look the same rather than the wide-gamut one looking dull or oversaturated (§4.3). |
| 12 | Open a folder of ordinary images | Everything behaves as before: archives did not change how folders work — both are the same page source. |
| 13 | Start a fast slide show (`S`, then `[` down to about 0.2 s) | Slides keep pace without stalling on decode: the ring widened (§3.2.1). |

## Steps for animated and multi-frame files (§3.20)

| # | Action | Expected |
|---|---|---|
| 14 | Open an animated GIF | It plays. `Space` freezes it and `Space` resumes; `.` and `,` step one frame at a time and leave it frozen. |
| 15 | With the GIF playing, press `Ctrl+]` a few times, then `Ctrl+[` | It speeds up to 2x and slows to 0.25x, stopping at each end rather than wrapping. Note that the same chords step *archives* when no animation is on screen — that is deliberate (§3.7.2 and §3.20.1 both claim them). |
| 16 | Open a folder of GIFs and press `Right` | Paging still works: `Space` belongs to the animation, but `Right`, `PageDown`, `J` and `D` still turn pages. |
| 17 | Run a slide show over a folder containing one long GIF | The GIF is not cut off mid-loop; the slide waits for one full pass (§3.2.6). |
| 18 | Open a multi-size `.ico` | The largest layer is shown, not the 16x16 one. `.` and `,` walk the layers and the window does not resize (§2 invariant 2). |
| 19 | Open a multi-page `.tif` | The first page shows and nothing advances on its own; `.` and `,` step pages. |
| 20 | Open an animated GIF **inside a CBZ** | It animates there too — an archive page has no filename, and the frames come from the bytes. |

## Known limitations (not defects)

- CBR (`.cbr`, RAR) is post-1.0 by owner decision D-3 — no pure-C
  permissively licensed reader was found.
- An encrypted `.cb7` is refused with "unsupported coder". AES is
  deliberately not vendored: the viewer has no way to ask for a
  password, so it says so instead of pretending.
- Each animation frame is decoded through WIC as it is shown, so a very
  large GIF costs one decode per frame rather than reading from a cache
  of decoded frames. It is bounded by the frame rate, and no frame is
  decoded twice in a row, but a 4K animation may not keep pace on a slow
  machine. Watch for it in step 14 and report it if you see it.

## Recording the result

Note the Windows version and which steps passed. A failure in 1-4 points
at the archive wiring in `main.c`, since the ZIP reading itself is proven
by T014 and T034; 9-10 at the history wiring, proven by T033; 11 at the
WIC colour transform, which has no host coverage at all.

## Measured on the Windows 11 VM, 2026-09-24

The first run of this case. There was no comic on the machine, so the
archives were made on purpose (`Vol 01.cbz` with its pages named `1`,
`2`, `9`, `10`; `Vol 02.cbz`; `manga.cbz` carrying a `ComicInfo.xml` with
`YesAndRightToLeft` and a `FrontCover`; `cp949raw.cbz` whose names are
CP949 bytes with the UTF-8 flag clear, written by hand because Python's
own zip writer sets that flag).

| # | Result |
|---|---|
| 1 | PASS. `%TEMP%` holds nothing of ours while the archive is open or after it closes. |
| 4 | PASS. The fourth page is the one named `10`, so the order is 1, 2, 9, 10 and not 1, 10, 2, 9. |
| 5 | PASS. The status line reads `둘째장.png`: the host code page was applied. (The first attempt "failed" against a fixture that was wrong — Python had set the UTF-8 flag and encoded the mojibake, so the viewer was right to show what the file claimed. A fixture is evidence only when it is the thing it imitates.) |
| 6 | PASS. `Right` on the last page of `Vol 01` opens `Vol 02` at its first page. |
| 8 | PASS. `manga.cbz` opens right to left with no key press: the cover is alone, and the next spread has page 2 on the **right** and page 3 on the left. |
| 9 | PASS. Left on page 3 of 4 and reopened, the viewer offers `Resume page 3 / 4 (Enter)`. A two-page volume left on its last page offers nothing, which is right. |
| 10 | PASS, both halves. With no `settings.ini` beside the program, `history.ini` and `layout.ini` are written to `%APPDATA%\rubraview` — on a clean exit, not while running. With an empty `settings.ini` beside it, they are written **there** and the ones in `%APPDATA%` are left alone. |
| 12 | PASS: folders behave as they did. |
| 2, 3, 13 | NOT RUN: these need a volume of hundreds of pages, which this machine has not got. |
| 7 | NOT RUN: `Ctrl+]` cannot be sent through the in-session key path. |
| 11 | NOT RUN: no wide-gamut photo on the machine. |

Also seen: the window title is empty where an archive page's name would
go (`(2/2) - Rubraview 0.0.17`), while the status line names the page
properly. Noted in `BACKLOGS.md` as a small blemish, not a defect of the
archive path.
