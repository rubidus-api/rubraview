# T037: Windows archive and caching smoke check (M4)

M4 gave the viewer comic archives, a pre-cache ring, reading history and
colour management. The decisions behind all of it are covered on the
host by T033-T036 and by the archive tests in T014 — including real
DEFLATE round trips through the vendored miniz. What the build machine
still cannot show is a CBZ actually rendering, because there is no
Windows here.

Run this after T025 (canvas) and T031 (reading UI) on the Windows target.

## Prerequisites

- `dist/rubraview.exe` from `make win64`.
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

## Known M4 limitations (not defects)

- CB7 (`.cb7`, 7-Zip) is not implemented. Owner decision D-3 approved
  vendoring the LZMA SDK for it (RV-052), but the solid-stream decoder
  §3.8.2 describes is a substantial piece of work in its own right and
  has not been started. A `.cb7` will not open.
- CBR (`.cbr`, RAR) is post-1.0 by the same decision — no pure-C
  permissively licensed reader was found.
- Animated GIF, WebP and APNG have their frame model, timing and
  stepping implemented and tested (T036), and the WIC backend can decode
  an individual frame, but the viewer does not yet run the animation
  clock against the canvas: an animated file shows its first frame.
- ICO opens at its first frame rather than its largest; the "largest
  mipmap" rule is implemented and tested but not yet wired to the
  loader.

## Recording the result

Note the Windows version and which steps passed. A failure in 1-4 points
at the archive wiring in `main.c`, since the ZIP reading itself is proven
by T014 and T034; 9-10 at the history wiring, proven by T033; 11 at the
WIC colour transform, which has no host coverage at all.
