# `include/edit_subclass/editDOC.h` and `src/edit_subclass/editDOC.c`

Status: draft for user approval; version 0.1.3. Phase 1 of
the LibreOfficeKit integration: view and save.

## Purpose

`EditDOC` is the office document editor, a subclass of [`Edit`](edit.c.md).
It embeds LibreOffice through **LibreOfficeKit (LOK)**: LibreOffice loads the
file, paints any rectangle of it into a pixel buffer and saves it back, so
every format LibreOffice understands (Writer, Calc, Impress) is available
without StudyStudio parsing it. **All the LOK code of the project lives in
these two files.**

## Dependencies

- **Build time:** only the LOK headers, vendored in
  `include/third_party/LibreOfficeKit/` (see [below](#vendored-headers)).
  Nothing is linked: `lok_init()` loads LibreOffice with `dlopen` (Linux) or
  `LoadLibraryA` (Windows) at run time.
- **Run time:** an installed LibreOffice. `_doc_office()` tries, in order, the
  `STUDYSTUDIO_LO_PATH` environment variable and then:
  - Linux: `/usr/lib64/libreoffice/program` (Fedora),
    `/usr/lib/libreoffice/program` (Debian, Ubuntu),
    `/opt/libreoffice/program`;
  - Windows: `C:\Program Files\LibreOffice\program`.

`editDOC.h` only forward-declares `LibreOfficeKitDocument`, so `edit.c` and
`main.c` do not see the LOK headers. `editDOC.c` defines
`LOK_USE_UNSTABLE_API` (it hides `paintTile`, `postKeyEvent`, ...) and is the
only file that may include `LibreOfficeKitInit.h`, because that header defines
`static` functions.

## Fields

| Field | Description |
| --- | --- |
| `base` | Inherited `Edit`; must be the first member. |
| `lok` | Document loaded by LibreOffice; destroyed in `finalize`. |
| `width_twips`, `height_twips` | Document size in twips (1/1440 inch), from `getDocumentSize`. |
| `area` | `GtkDrawingArea` that shows the visible part of the document. |
| `vadj` | `GtkAdjustment` with the vertical scroll position in pixels; `EditDOC` holds its own reference. |

`edit_doc_suffixes` (`docx`, `doc`, `odt`, `rtf`, `xlsx`, `xls`, `ods`,
`pptx`, `ppt`, `odp`) is exported for the factory and the subject menu.

`s_office` (a `static` global in `editDOC.c`) is the process-wide
`LibreOfficeKit` instance: `lok_init()` may run only once per process, so it
is created lazily by the first `edit_doc_available()` / `open()` call and
shared by every `EditDOC`.

## Behavior

- `edit_doc_available()` starts LibreOffice if needed and reports whether it
  was found. The factory checks it before creating an `EditDOC`.
- `open` calls `documentLoad`, then `initializeForRendering` and
  `getDocumentSize`. On failure it prints LibreOffice's error (released with
  `freeError`, because LibreOffice allocated it).
- `save` calls `saveAs(path, NULL, NULL)`: with a `NULL` format, LibreOffice
  picks the filter from the extension, so the file keeps its format.
- `render` builds `GtkBox(horizontal) = GtkDrawingArea + GtkScrollbar` and
  stores it with `set_widget` (weak pointer). A
  `GtkScrolledWindow` is not used: a long document would make the drawing area
  tens of thousands of pixels tall. The area is only as large as the visible
  space and `vadj` stores the scroll offset.
- `_doc_draw` paints only the visible rectangle. The zoom fits the page width
  (`zoom = width / width_twips`, pixels per twip). `paintTile` writes
  premultiplied BGRA straight into a `CAIRO_FORMAT_ARGB32` surface, which has
  the same byte layout on little-endian CPUs; the code checks that the stride
  is `width * 4`.
- `_doc_on_resize` recomputes the scroll range, because it depends on the
  width. The mouse wheel (`GtkEventControllerScroll`) moves `vadj`, and every
  change of `vadj` queues a redraw.
- `finalize` disconnects and releases `vadj`, forgets `area` (it is destroyed
  with `Edit.widget`) and destroys `lok`.

## Vendored headers

`include/third_party/LibreOfficeKit/` holds `LibreOfficeKit.h`,
`LibreOfficeKitInit.h`, `LibreOfficeKitTypes.h` and `LibreOfficeKitEnums.h`
from LibreOffice **24.2** (the version packaged in Fedora 40), under the
Mozilla Public License 2.0 (kept in each file header). They are vendored
because MinGW has no LOK package, and the `Makefile` adds
`-Iinclude/third_party`. Older headers work with newer LibreOffice releases:
the LOK function tables only grow at the end.

They contain C++ code that does not compile as C, patched and marked with a
`StudyStudio:` comment:

| File | Patch |
| --- | --- |
| `LibreOfficeKitEnums.h` | `lokCallbackTypeToString()` (uses `static_cast` and `nullptr`) is wrapped in `#ifdef __cplusplus`. |
| `LibreOfficeKitInit.h` | The two `reinterpret_cast` of the Windows branch (`lok_dlerror`, `lok_dlsym`) are replaced by C casts. Linux gcc never sees them; MinGW failed. |

`LibreOfficeKit.h` uses `bool` without including `<stdbool.h>`, so
`editDOC.c` includes it first (MinGW reported `unknown type name 'bool'`).

## Known limitations and risks

- **Read-only view:** keyboard and mouse input, the `LOK_CALLBACK_*` callback
  (e.g. `INVALIDATE_TILES`) and the toolbar (`postUnoCommand`) are phases 2
  and 3. The enums are already included for them.
- Rendering targets Writer. Calc and Impress load, but spreadsheets and slides
  need `setPart` / part handling to be shown properly.
- `paintTile` runs on every redraw, on the GTK main thread, with no tile
  cache.
- `s_office` is never destroyed: in the spike, `office->destroy()` crashed
  with SIGSEGV in an `atexit` handler of `libswlo`. It must still be verified
  that the application exits cleanly without it.
- `saveAs` over the same path that was opened has not been tested; the
  fallback is `postUnoCommand(".uno:Save")`.
- Windows: the install path and `lok_init` on Windows have not been tested at
  run time.
- The first use blocks the UI while `lok_init` starts LibreOffice on the GTK
  thread.
