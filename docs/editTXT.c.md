# `include/edit_subclass/editTXT.h` and `src/edit_subclass/editTXT.c`

Status: draft for user approval; version 0.1.4 (editing tools added
2026-11-06). Covered by `tests/test_edit_txt.c` (see [tests](tests.md)).

## Purpose

`EditTXT` is the plain-text editor (`.txt`, `.md`), a subclass of
[`Edit`](edit.c.md) based on `GtkTextView`. Until 2026-10-12 it was called
`EditDOC`; it was renamed so that `EditDOC` could become the LibreOfficeKit
office editor ([editDOC.c](editDOC.c.md)).

`GtkTextView` already provides typing, cursor movement, mouse/keyboard
selection and the usual shortcuts; `GtkTextBuffer` keeps the undo history.
Plain text has no attributes, so a `.txt` file only gets history, clipboard
and "insert another file". Markdown stores its extras as text the format
itself defines, so they survive in any other editor or renderer.

## Fields

| Field | Description |
| --- | --- |
| `base` | Inherited `Edit`; must be the first member. |
| `buffer` | `GtkTextBuffer` holding the text; `EditTXT` owns one reference. |
| `view` | The `GtkTextView` once rendered (weak pointer, `NULL` before). |
| `markdown` | `true` for `.md`: markup and insert tools apply. |

`edit_txt_suffixes` (`"txt"`, `"md"`, `NULL`) is exported for the factory and
the subject menu.

## Capabilities

| Contract | `.txt` | `.md` |
| --- | --- | --- |
| `EditHistory` | `gtk_text_buffer_undo/redo`; `can-undo`/`can-redo` notify the toolbar. | same |
| `EditClipboard` | `gtk_text_buffer_copy/cut/paste_clipboard` on the widget's (or display's) `GdkClipboard`. Paste is asynchronous; plain and formatted paste are the same. | same |
| `EditMarkup` | — | `highlight` wraps the selection in `<mark style="background-color: #rrggbb">…</mark>`; `clear` removes `<mark>` tags in the selection, growing it to the enclosing tags when the selection or bare cursor is inside a highlight. No `shade`. |
| `EditInsert` | — | `table(rows, cols)`: GitHub pipe table, preceded by a blank line when needed; `row`: empty row below the cursor's row (below the rule when on the header); `column`: new last column on every line of the table around the cursor; `image`: `![name](<path>)`, relative to the `.md` folder when possible; `annotation`: `<!-- text -->` (`--` collapsed, comments cannot contain it). No `page`. |
| `EditCombine` | `merge_file`: inserts a UTF-8 `.txt`/`.md` file at the cursor. | same |

Every change made by a tool is one user action, so a single undo reverts it.
`row`/`column` outside a table return `EDIT_ERR_INVALID` and notify
"Put the cursor inside a Markdown table".

## Behavior

- `open` reads the whole file, rejects content that is not valid UTF-8 or is
  longer than `G_MAXINT` (`EDIT_ERR_INVALID`; a missing file is
  `EDIT_ERR_IO`), loads it inside an *irreversible action* (loading is not
  undoable) and clears the modified flag. It also picks the Markdown tables.
- `save_to` writes the buffer with `g_file_set_contents` (temporary file +
  rename, so a failed save never leaves a half-written file) and clears the
  buffer's modified flag.
- `render` appends a monospace, word-wrapping `GtkTextView` with margins
  inside a `GtkScrolledWindow` and stores it with `set_widget`.
- The buffer's `"modified-changed"` keeps `Edit.modified` in sync; its
  `notify::has-selection`, `notify::can-undo` and `notify::can-redo` call
  `notify` so the toolbar refreshes.
- `finalize` disconnects every handler and drops the buffer reference.

## Limitations

- `<mark>` is HTML inside Markdown: renderers that strip HTML show plain text.
  The editor shows the tags, not a coloured preview.
- No encodings other than UTF-8.
