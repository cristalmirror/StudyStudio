# `include/edit_subclass/editPDF.h` and `src/edit_subclass/editPDF.c`

Status: draft for user approval; version 0.1.2. Compiled, not run yet.

## Purpose

`EditPDF` is the PDF editor, a subclass of [`Edit`](edit.c.md). It uses
**Poppler** through its GLib/cairo binding, **poppler-glib**: Poppler parses
the file, paints each page with cairo (the same 2D library GTK4 draws with),
adds or removes annotations and writes the document back. **All the Poppler
code of the project lives in these two files.**

"Editing" a PDF here means **annotating** it, which is what a student needs:
highlight text, draw rectangles, stick notes and erase them. The page content itself (text, images) is not rewritten; Poppler has
no API for that, and annotations are what other viewers (Evince, Okular,
Firefox, Acrobat...) show and edit too.

## Dependencies and license

- **Linux:** `poppler-glib-devel` (Fedora 40), linked through
  `pkgconf poppler-glib`.
- **Windows:** `mingw64-poppler-glib`; the Poppler DLLs (and their own
  dependencies) must ship next to the `.exe`, like GTK's.
- **License:** Poppler is GPL-2.0-or-later, so it can be combined with
  StudyStudio's GPLv3. (MuPDF, the other common choice, is AGPL-3.0, which
  would impose its network clause; Poppler avoids that.)

`editPDF.h` only forward-declares `PopplerDocument` and `PopplerPage`, so `edit.c` and `main.c`
do not see the Poppler headers (same idea as `EditDOC` with LibreOfficeKit).

## Fields

| Field | Description |
| --- | --- |
| `base` | Inherited `Edit`; must be the first member. |
| `doc` | `PopplerDocument`, loaded from memory; `EditPDF` owns one reference. |
| `n_pages`, `pages` | Array of `EditPDFPage`, one per page. |
| `zoom` | Pixels per PDF point (1 point = 1/72 inch); 0.25 to 4, starts at 1. |
| `tool` | Current `EditPDFTool`, chosen in the toolbar. |
| `pages_box` | Vertical `GtkBox` with one `GtkDrawingArea` per page. |
| `zoom_label`, `note_entry` | Toolbar widgets: zoom percentage and text of the next sticky note. |

The header holds every data structure and `editPDF.c` only the operations
(the `static _pdf_*` functions) and the constant tables (`s_tools[]`, colors,
`PDF_*` macros). `EditPDF` and `EditPDFPage` point to each other, so
`typedef struct EditPDFPage EditPDFPage;` comes first and the struct is
defined after `EditPDF`. `EditPDFToolInfo` (`label`, `tooltip`) describes a
tool button; `s_tools[]` has one per `EditPDFTool`.

`EditPDFPage` holds the `PopplerPage` (one reference), its size in
points, its `GtkDrawingArea`, and the state of a drag in progress: start/end
points and a cached render of the page.

`edit_pdf_suffixes` (`pdf`) is exported for the factory and the subject menu.

## Tools (`EditPDFTool`)

| Tool | Gesture | Result |
| --- | --- | --- |
| `VIEW` | — | Nothing; the click is passed on (scrolling, etc.). |
| `HIGHLIGHT` | drag over text | Highlight annotation (yellow). It follows the text lines: `poppler_page_get_selected_region` gives one rectangle per line, and each becomes one quadrilateral. With no text under the drag (a scanned page), the dragged rectangle is highlighted. |
| `RECT` | drag | Square annotation, red border (default 1 pt width). |
| `NOTE` | click | Text ("sticky note") annotation with the toolbar entry's text; refused if the entry is empty. |
| `ERASE` | click | Removes the topmost annotation under the cursor. Links, form fields and popups are never removed. |

## Coordinates

Two systems meet in this file:

- **widget:** pixels, origin at the top-left of the page widget, y down;
- **PDF:** points, origin at the **bottom-left** of the page, y up.

`x_pdf = x / zoom`, `y_pdf = page_height - y / zoom` (`_pdf_x`, `_pdf_y`,
`_pdf_rect`). `poppler_page_get_selected_region` and
`poppler_page_get_annot_mapping` return rectangles with a top-left origin, so
they are flipped before building annotations. Page rotation (`/Rotate`) is
not taken into account yet.

## Behavior

- `open` reads the whole file with `g_file_get_contents` and loads it with
  `poppler_document_new_from_bytes`. Poppler then never holds the file open,
  so `save` can replace it (Windows cannot rename over an open file). It
  creates the page array; reopening while shown rebuilds the page widgets.
- `save` calls `poppler_document_save` on `<path>.saving` (Poppler wants a
  URI, made with `g_filename_to_uri`) and renames it over the original with
  `g_rename`, so a failed save never leaves a half-written PDF. The
  temporary file is removed on failure.
- `render` builds a toolbar (tool toggle buttons in one group, note entry,
  zoom −/+) and a `GtkScrolledWindow` with the page column. Each page is a
  `GtkDrawingArea` sized `width × zoom` by `height × zoom`, with a
  `GtkGestureDrag` for the tools. GTK only calls a draw function when the
  widget needs it, so scrolling does not re-render pages.
- Drawing paints a white background (PDF pages have none), scales by `zoom`
  and calls `poppler_page_render`, which also paints the annotations. While
  dragging, the page is rendered **once** into a cairo image surface at
  `drag-begin`; every `drag-update` only paints that cache plus the preview.
- Adding or removing an annotation calls `mark_modified(true)`; `save`
  clears it.
- `finalize` frees the page array (each `PopplerPage` reference) and the
  document. The widgets are destroyed with `self->widget`, which `close()`
  removes before calling `finalize`.

## Known issues

- Not run yet; to verify on real PDFs: highlight quads, saving over the
  opened file, and the Windows build with its DLLs.
- No pen (ink) tool and no border width: Fedora 40 ships Poppler 24.02,
  whose GLib API has neither `poppler_annot_ink_new` nor
  `poppler_annot_set_border_width`. Emulating a stroke with many line
  annotations was rejected (erasing it would take one click per segment).
- No undo; page rotation is ignored; notes take their text from the toolbar
  (no popover to edit an existing note); no text selection/copy.
- Encrypted PDFs fail to open (no password prompt).
