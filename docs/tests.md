# `tests/`

Status: draft for user approval; version 0.1.4 (added 2026-11-06).

## Purpose

Automated tests of the editors with the GLib testing framework (`g_test_*`,
TAP output). They exercise the real `Edit` contracts against real backends
(GtkTextBuffer, Poppler, qpdf, LibreOffice) and check the saved files, so a
passing test means the change is really in the document, not only on screen.

| File | Covers |
| --- | --- |
| `test_common.h` | Main-loop waiting, system clipboard read/write, temporary files. |
| `test_edit_txt.c` | Typing + undo/redo, save/save as, clipboard copy/cut/paste, insert file (and non-UTF-8 refusal), Markdown highlight/clear/table/row/column/image/comment with reopen, invalid and missing files, failed save. |
| `test_edit_pdf.c` | Text selection + copy, highlight/shade/clear with colors, snapshot undo/redo, note, save + reopen (colors kept), qpdf insert/move with undo and annotation preservation, broken files. |
| `test_edit_doc.c` | Writer (paste, copy, highlight, paragraph shade, insert file, page break, table, row/column, comment, save as, reopen), Calc (merge filled cells through the dialog, shade, comment, row), Impress (new slides, table, move slide), and the UI input path: rendered canvas, IM text with accents, Backspace, Shift+Home, click, Ctrl+Z, save over the same file, typing in a reopened file. |

## Running

```bash
# build (Docker, like the application)
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD":/usr/src/app \
  -w /usr/src/app mi_app_builder:latest make tests

# run on a machine with the GTK4/Poppler runtime and a display;
# headless with Broadway:
gtk4-broadwayd :5 &
GDK_BACKEND=broadway BROADWAY_DISPLAY=:5 make check
```

`make check` runs every test binary; exit code 77 means skipped. The
LibreOffice tests skip when LibreOffice is not installed, the page tests
when libqpdf is missing. `test_edit_doc` leaves with `_Exit`, like `main()`,
because LibreOffice crashes in its static destructors.

## Results (2026-11-06)

Run on the developer's Debian host (GTK 4.24.1, Poppler 26.07,
LibreOffice 26.8.2, qpdf 12.4.2 / libqpdf 30) with binaries built in the Fedora 40 image, under
Broadway: 13/13 pass. The same tests built with AddressSanitizer report no
invalid accesses; the only leaks reported come from fontconfig's caches,
none through project code.

Not covered: the Windows build at run time, popovers and file dialogs
(interactive), and the PDF mouse tools (drag gestures).
