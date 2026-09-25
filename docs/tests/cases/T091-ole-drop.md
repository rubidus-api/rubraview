# T091 — Dropping from other programs (OLE)

Covers: RFC-0001 §3.19.2, RV-073, D-36

The window is an OLE drop target. Files that exist (Explorer, file
managers, archive managers that extract first) arrive as paths, as with
WM_DROPFILES. Virtual files — a browser's dragged picture, a mail
attachment: a name and a stream, no file on disk — are written to a folder
of this process under `%TEMP%` (`rubraview-drop-<pid>\<n>\`), opened from
there, and the folder is removed when the viewer closes. The drop is always
a copy; the source keeps its file.

Host coverage: `tests/test_path.c` — the offered name is made safe to
create (no `..\` walks, no reserved characters or device names, cut on a
character boundary).

## Steps and expected

1. Drag three pictures from Explorer onto the viewer: it shows `(1/3)` of
   exactly those files.
2. Drag a picture out of a browser onto the viewer: it opens, titled with
   the picture's name.
3. A virtual file named `..\..\evil:name.png` arrives as `evil_name.png`
   inside the drop folder; nothing is written outside it.
4. Close the viewer: `%TEMP%\rubraview-drop-<pid>` is gone.
5. Dragging text or a link (no file) shows the "no" cursor and does nothing.

## Measured on the Windows 11 VM, 2026-09-25

The browser was stood in for by `vm_win11_dev_kd_tools/rubraview/vdrag.ps1`,
a WinForms drag source offering only FileGroupDescriptorW + FileContents.

| # | Result |
|---|---|
| 1 | PASS: three files → `page (2).png (1/3)`; two → `page (10).png (1/2)` (vmdrop.sh, a real mouse drag). |
| 2 | PASS (stand-in): `dragged picture.png (1/1)`, the file in `Temp\rubraview-drop-5804\1\`, 3583 bytes like its source. A step trace showed DragEnter → Drop → descriptor and contents read. |
| 3 | PASS: `evil_name.png (1/1)`, in `...\2\`; no `evil*` anywhere else. |
| 4 | PASS: after Esc, no rubraview process and no `rubraview-drop-*` folder. |
| 5 | NOT RUN. A real browser drag: NOT RUN (the VM has no page to drag from). |

Two false starts, both the tool's: the injector's multi-line command
broke (the drag never began), and the task's console window sat over the
viewer and took the drop. Both are in the VM tools' README.
