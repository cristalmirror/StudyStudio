# `include/edit_subclass/editDOC.h`, `editDOCOps.h`, `src/edit_subclass/editDOC.c`, `editDOCOps.c`

Status: draft for user approval; version 0.1.4. Since 2026-11-06 an editor
(keyboard, mouse, clipboard, tools), no longer view-only. Covered by
`tests/test_edit_doc.c` against a real LibreOffice (see [tests](tests.md)).

## Purpose

`EditDOC` is the office document editor, a subclass of [`Edit`](edit.c.md).
It embeds LibreOffice through **LibreOfficeKit (LOK)**: LibreOffice loads
the file, paints any rectangle of it, receives our key and mouse events,
runs the editing commands and saves the file back. **All the LOK code of the
project lives in these files.**

| File | Responsibility |
| --- | --- |
| `editDOC.c` | LibreOffice process, document lifecycle, view (painting, zoom, parts, scroll), input (keys, IM, mouse) and LOK callbacks. |
| `editDOCOps.c` | The capability tables: each tool is a UNO command, with different tables for Writer, Calc and Impress. |
| `editDOCOps.h` | Internal interface between both (`edit_doc_command`, `edit_doc_key`, `edit_doc_refresh`, link helpers...). |

## Dependencies

- **Build time:** only the vendored LOK headers (see [below](#vendored-headers)).
  `lok_init()` loads LibreOffice with `dlopen` / `LoadLibraryExW` at run time.
- **Run time:** an installed LibreOffice, found through `STUDYSTUDIO_LO_PATH`
  or the usual install folders (Fedora, Debian, `/opt`, `C:\Program Files`).

`editDOC.c` is the only file that includes `LibreOfficeKitInit.h` (it defines
`static` functions); `editDOCOps.c` uses `LibreOfficeKit.h` only.

## Threads and callbacks

LibreOffice runs its own thread and reports its state through a callback
called **from that thread** (verified on LibreOffice 26.8). GTK may only be
used from the main thread, so `_doc_lok_callback` copies the message and
queues it with `g_idle_add_full`; `_doc_dispatch` handles it in the main
thread. The queued message holds a reference to a small `EditDOCLink`
(atomic reference count), not to the `EditDOC`: `finalize` sets
`link->doc = NULL`, so a message arriving after the document was closed is
dropped instead of touching freed memory.

| Callback | Use |
| --- | --- |
| `INVALIDATE_TILES` | The cached view (`tile`) is stale: repaint. |
| `INVALIDATE_VISIBLE_CURSOR` | Cursor rectangle; after typing, the view scrolls to it. Also repaints (see below). |
| `CURSOR_VISIBLE` | Hides/shows the cursor. `cursor_visible` starts `true`. |
| `TEXT_SELECTION`, `CELL_CURSOR`, `GRAPHIC_SELECTION` | Selection overlay, active cell, selected image/shape; `has_selection`. The first two also repaint. |
| `STATE_CHANGED` | `.uno:Undo` / `.uno:Redo` availability and `.uno:StateTableCell` (cursor in a Writer table). Also repaints. |
| `DOCUMENT_SIZE_CHANGED`, `SET_PART` | Re-read size and the sheet/slide list. |
| `WINDOW` | A dialog LibreOffice opened (see below). |

**Repainting without `INVALIDATE_TILES`.** LibreOffice 26.8 (desktop LOK)
never sends `INVALIDATE_TILES` after an edit, nor `CURSOR_VISIBLE "true"`:
typing only moves the cursor, Delete/Backspace only send `STATE_CHANGED`.
`paintTile` does return the new content, so the cursor, text selection,
active cell and state callbacks also mark `tile` as stale
(`_doc_invalidate`). It is cheap: it only sets a flag and queues a draw, and
GTK merges every queued draw of a frame into one `_doc_draw`, so a burst of
~150 `STATE_CHANGED` messages costs a single `paintTile` of the visible area.
For the same reason `cursor_visible` starts `true` (as in LibreOffice's own
`LOKDocView`); only `CURSOR_VISIBLE "false"` hides it.

**Dialogs.** LibreOffice cannot show its dialogs inside StudyStudio and a
pending modal dialog blocks the document. Calc's "Merge Cells" question is
expected and answered with Enter (default: keep the hidden cells' content);
any other dialog is closed and the user is told the action was cancelled.

## View and input

- `render` builds a contextual bar (sheet/slide drop-down for Calc and
  Impress, zoom −, fit-width, +), and a `GtkGrid` with the `GtkDrawingArea`
  and two scrollbars (hidden when everything fits). No `GtkScrolledWindow`:
  the area is only as large as the visible space.
- `_doc_draw` keeps the last `paintTile` result in `tile` and only asks
  LibreOffice again when invalidated (see "Repainting without
  `INVALIDATE_TILES`"), scrolled, zoomed or resized; cursor and
  selections are painted on top. Text and slides start fit-to-width; sheets
  at 100 % (15 twips per pixel). Ctrl + wheel zooms.
- Keyboard: a `GtkEventControllerKey` with a `GtkIMMulticontext`. Text
  (including accents and compose sequences) arrives through the IM's
  `commit` and is sent as characters; other keys are mapped to LibreOffice
  key codes (`com.sun.star.awt.Key`) with Shift/Ctrl/Alt modifiers, so
  Ctrl+B, Shift+arrows... work natively. Ctrl+C/X/V/Shift+V/Z/Y/A go to our
  clipboard and history operations instead (the LOK clipboard is not the
  system one).
- Mouse: a click gesture (with the press count, so double/triple click
  select words/paragraphs) and a motion controller for drag selection.
- `modified` is set by our own input and tools. LibreOffice's
  `.uno:ModifiedStatus` was not reliable in the probes and is not used.

## Capabilities (`editDOCOps.c`)

| Operation | Writer | Calc | Impress |
| --- | --- | --- | --- |
| Undo / redo | `.uno:Undo`/`.uno:Redo` | same | same |
| Copy / cut / paste | `getTextSelection` as `text/html` + text → `GdkClipboard`; paste reads HTML, PNG or text and calls `paste()`; plain paste reads text only | same | same |
| Highlight | `.uno:CharBackColor` | — (use shade) | `.uno:CharBackColor` (text being edited) |
| Shade | `.uno:BackgroundColor` (paragraph) | `.uno:BackgroundColor` (cells) | `.uno:FillColor` (selected shape) |
| Clear | both colors → `-1` (transparent) | cell background → `-1` | text highlight → `-1` |
| Image | `.uno:InsertGraphic` | same | same |
| Table | `.uno:InsertTable` | — | `.uno:InsertTable` |
| Row / column | `.uno:InsertRowsAfter`/`ColumnsAfter`, only inside a table | same, always | — |
| Page | `.uno:InsertPagebreak` | — | `.uno:InsertPage` ("Nueva diapositiva") |
| Comment | `.uno:InsertAnnotation` | same | — (needs a dialog) |
| Insert file | `.uno:InsertDoc` (`odt docx doc rtf txt`) | — | — (needs a dialog) |
| Merge / split cells | `.uno:MergeCells` (table) / — (needs a dialog) | `.uno:MergeCells` (+ dialog answer) / `.uno:SplitCell` | — |
| Move page | — | — | `.uno:MovePageUp`/`Down` |

Every command was checked against LibreOffice 26.8 with a standalone LOK
probe and then by the tests, which save as ODF and inspect `content.xml`.
`UNO_COMMAND_RESULT` reports `success: false` for some commands that did
work (`CharBackColor`, `BackgroundColor`, `InsertTable` in Impress), so it is
not used to decide success.

## Saving

`save_to` asks `saveAs` for a copy at `.~studystudio-<name>` in the same
folder (same extension, so the same filter) and renames it over the target:
a failed save leaves the original intact. If the rename fails (Windows
refuses to replace a file LibreOffice keeps open) it saves in place.

## Vendored headers

`include/third_party/LibreOfficeKit/` holds the LOK headers from LibreOffice
**24.2** (MPL-2.0), vendored because MinGW has no LOK package. Older headers
work with newer LibreOffice: the function tables only grow at the end. Two
C++ constructs are patched for C (marked `StudyStudio:`): the
`lokCallbackTypeToString()` body in `LibreOfficeKitEnums.h` and two
`reinterpret_cast` in `LibreOfficeKitInit.h`.

## Known limitations and risks

- Only one view per document; no LibreOffice dialogs (features that need one
  are not offered, unexpected ones are closed).
- Selected images/shapes are not copied to the clipboard as images (only
  text/HTML of the selection).
- Dragging selected text with the mouse moves it without setting `modified`.
- `s_office` is never destroyed: LibreOffice's static destructors crash
  inside `exit()`, so `main()` leaves with `_Exit` when it was started
  (see [main.c](main.c.md)).
- The first office document blocks the UI while `lok_init` starts LibreOffice
  (the window shows a spinner and "Opening the document…" first).
- Windows: LibreOffice on Windows and the in-place save fallback have not
  been run.
