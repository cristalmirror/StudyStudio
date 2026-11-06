# `include/msc/pdfPages.h` and `src/msc/pdfPages.c`

Status: draft for user approval; version 0.1.3 (added 2026-11-06). Covered
by `tests/test_edit_pdf.c` (`/edit-pdf/pages`).

## Purpose

Structural PDF operations for [`EditPDF`](editPDF.c.md): insert every page of
another PDF and move a page. Poppler only renders and annotates, so these use
**qpdf** (Apache-2.0), whose page copy brings along everything a page uses
(fonts, images, annotations).

## Loading

Like LibreOffice, qpdf is not linked: its C API (`qpdf-c.h`) is resolved with
`GModule` the first time `pdf_pages_available()` is called. Tried in order:
`STUDYSTUDIO_QPDF_PATH` (full path to the library), then
`libqpdf.so.30/.29/.28/libqpdf.so` on Linux or
`qpdf30.dll`/`libqpdf30.dll`/... on Windows. If it is missing,
`EditPDF` does not offer the page operations. The few qpdf types used are
declared locally, so the build needs no qpdf headers.

## Interface

| Function | Description |
| --- | --- |
| `pdf_pages_available()` | `true` if the library and all needed symbols were found. |
| `pdf_pages_insert(doc, other, after, &error)` | New PDF: every page of `other` inserted after page `after` (`-1`: before the first; out of range: at the end). |
| `pdf_pages_move(doc, from, to, &error)` | New PDF with page `from` moved to index `to`. |

Inputs and outputs are complete PDF files in `GBytes`; the inputs are not
modified. Errors come back as `GError` with qpdf's message (qpdf's own
printing is silenced). The input buffers are kept alive while qpdf reads
them, because `qpdf_read_memory` does not copy.

## Limitations

- Only pages are copied: the inserted file's outline and document-level form
  fields are not merged.
- Fedora 40 has no `mingw64-qpdf` package, so the Windows package does not
  include qpdf (see [windows](windows.md)).
