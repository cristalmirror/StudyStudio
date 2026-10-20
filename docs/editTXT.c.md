# `include/edit_subclass/editTXT.h` and `src/edit_subclass/editTXT.c`

Status: draft for user approval; version 0.1.3.

## Purpose

`EditTXT` is the plain-text editor (`.txt`, `.md`), a subclass of
[`Edit`](edit.c.md) based on `GtkTextView`. Until 2026-10-12 it was called
`EditDOC` (`editDOC.h`/`editDOC.c`); it was renamed so that `EditDOC` could
become the LibreOfficeKit office editor ([editDOC.c](editDOC.c.md)). Only
names changed (`EditDOC` → `EditTXT`, `new_edit_doc` → `new_edit_txt`,
`_doc_*` → `_txt_*`, `EDIT_DOC_H` → `EDIT_TXT_H`); the behavior is the same.

## Fields

| Field | Description |
| --- | --- |
| `base` | Inherited `Edit`; must be the first member. |
| `buffer` | `GtkTextBuffer` holding the text; `EditTXT` owns one reference. |
| `modified_id` | Handler id of the buffer's `"modified-changed"` signal. |

`edit_txt_suffixes` (`"txt"`, `"md"`, `NULL`) is exported for the factory and
the subject menu.

## Behavior

- `open` reads the whole file with `g_file_get_contents`, rejects content that
  is not valid UTF-8 or is longer than `G_MAXINT`, loads it into the buffer and
  clears the buffer's modified flag.
- `save` writes the buffer with `g_file_set_contents` (temporary file +
  rename, so a failed save never leaves a half-written file).
- `render` appends a monospace, word-wrapping `GtkTextView` inside a
  `GtkScrolledWindow` to a `GtkBox` container and stores it with
  `set_widget` (weak pointer).
- The buffer's `"modified-changed"` signal keeps `Edit.modified` in sync.
- `finalize` disconnects that handler and drops the buffer reference.
