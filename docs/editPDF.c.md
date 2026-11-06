# `include/edit_subclass/editPDF.h` and `src/edit_subclass/editPDF.c`

Status: draft for user approval; version 0.1.3 (selection, colors, undo and
page operations added 2026-11-06). Covered by `tests/test_edit_pdf.c` (see
[tests](tests.md)); the mouse tools still need a manual check on a desktop.

## Purpose

`EditPDF` is the PDF editor, a subclass of [`Edit`](edit.c.md). It uses
**Poppler** through **poppler-glib**: Poppler parses the file, paints each
page with cairo, adds or removes annotations and writes the document back.

Poppler cannot rewrite page content (text, images), so in a PDF the text is
read-only: it can be selected and copied, and every visible change is an
**annotation** stored in the file (highlight, shaded area, rectangle, note),
which other viewers (Evince, Okular, Firefox, Acrobat...) also show.
Inserting and reordering pages is done by qpdf ([pdfPages.c](pdfPages.c.md)).

## Dependencies and license

- **Linux:** `poppler-glib-devel` (Fedora 40), through `pkgconf poppler-glib`.
- **Windows:** `mingw64-poppler-glib`; its DLLs ship next to the `.exe`.
- **Optional:** libqpdf at run time for the page operations.
- **License:** Poppler is GPL-2.0-or-later and qpdf Apache-2.0, both
  compatible with StudyStudio's GPLv3.

`editPDF.h` only forward-declares `PopplerDocument` and `PopplerPage`.

## Fields

| Field | Description |
| --- | --- |
| `base` | Inherited `Edit`; must be the first member. |
| `doc` | `PopplerDocument`, loaded from memory; one reference owned. |
| `n_pages`, `pages` | Array of `EditPDFPage`, one per page. |
| `zoom` | Pixels per PDF point; 0.25 to 4, starts at 1. |
| `tool` | Current `EditPDFTool`. |
| `color` | Color of the interactive tools, set by the toolbar through `set_color`. |
| `current_page` | Last page clicked (-1: none); page operations act on it. |
| `sel_page`, `sel` | Page and area of the selection (`EditPDFArea`, points, top-left origin); `sel_page == -1`: nothing selected. |
| `undo`, `redo` | `GPtrArray` of `GBytes`: whole-file snapshots, at most 20. |
| `pages_box`, `zoom_label`, `page_label`, `note_entry` | Widgets of the view and of its contextual bar. |

`EditPDFPage` holds the `PopplerPage`, its size in points, its
`GtkDrawingArea` and the state of a drag (start/end points and a cached
render of the page, so a drag does not re-render the PDF on every motion).

`edit_pdf_select(edit, page, area)` selects an area (what the select tool
does); it is public so tests and future tools can use it.

## Tools (`EditPDFTool`, contextual bar)

| Tool | Gesture | Result |
| --- | --- | --- |
| `VIEW` | — | Nothing; the click is passed on (scrolling). |
| `SELECT` | drag | Selects the text under the area (or the area itself); a click clears it. Copy, highlight, shade, clear and "comment" act on it. |
| `HIGHLIGHT` | drag | Highlight annotation in the chosen color, one quadrilateral per text line; the area itself on pages without text. |
| `RECT` | drag | Square annotation outlined in the chosen color. |
| `NOTE` | click | Sticky note with the text of the bar's entry. |
| `ERASE` | click | Removes the topmost user annotation under the cursor (links, form fields and popups stay). |

## Capabilities

| Contract | Implementation |
| --- | --- |
| `EditHistory` | Snapshots: before each change the document (with its annotations) is saved to a temporary file and kept as `GBytes`; undo/redo reload a snapshot. With the same page count the page widgets are reused, so the view does not jump. |
| `EditClipboard` | `copy` puts `poppler_page_get_selected_text` on the clipboard. `cut`/`paste` are `NULL`. |
| `EditMarkup` | `highlight`: highlight annotation; `shade`: square with interior color and 35 % opacity; `clear`: removes highlights and filled squares touching the selection. |
| `EditInsert` | `annotation(text)`: note at the selection's top-left corner. |
| `EditCombine` | Only when qpdf is available. `merge_file`: every page of another PDF after `current_page` (or at the end); `move_page(±1)`: moves `current_page`, which follows it. |

A change that finds nothing to do (erase on empty space, clear without
markup) adds no undo step.

## Coordinates

Widget pixels → area points: divide by `zoom`. Area (top-left origin) → PDF
(bottom-left origin): `y_pdf = page_height - y`. Text regions are requested
at scale 8 (`PDF_REGION_SCALE`) because cairo returns integer rectangles.
`poppler_page_get_annot_mapping` returns **PDF** coordinates; the erase
code compared them with top-left coordinates until 2026-11-06, which only
worked near the vertical middle of the page.

## Behavior

- `open` loads the whole file into memory (Poppler never keeps it open, so
  saving can replace it on Windows too), clears the history and offers
  `combine` only if `pdf_pages_available()`.
- `save_to` writes `<path>.saving` and renames it over the target.
- The contextual bar holds the tool buttons, the note entry, the current
  page ("Pág. 2 / 9") and the zoom.

## Known issues

- Page rotation is ignored by the annotation coordinates.
- Snapshots cost one full save per change: fine for course notes, slow for
  very large PDFs.
- Merging copies pages, not the document outline (bookmarks) nor
  document-level form fields of the inserted file.
- Without qpdf (e.g. the Windows package, see [windows](windows.md)) page
  operations are hidden.
