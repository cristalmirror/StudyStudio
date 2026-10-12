# `include/edit.h` and `src/edit.c`

Status: draft for user approval; version 0.1.0.

## Purpose and dependencies

Defines `Edit`, the abstract base "class" of every StudyStudio editor, and the
factory that chooses the right editor for a file. It depends only on GTK4
(`<gtk/gtk.h>`) and `<stdbool.h>`; `src/edit.c` also includes the subclass
headers (`editTXT.h`, `editDOC.h`) because the factory constructs them. See
the [class diagram](class_diagram.md#edit-hierarchy) for the whole hierarchy.

## Inheritance in C

Each subclass declares a struct whose **first member** is `Edit base`, so a
pointer to the subclass can be cast to `Edit *` (upcast) and back (downcast)
safely. The constructor of each subclass:

1. allocates the subclass with `calloc` (every pointer starts as `NULL`),
2. calls `edit_init(&obj->base)`,
3. assigns its own `open`, `save`, `render` and `finalize`,
4. returns `&obj->base`.

| Member | Kind | Description |
| --- | --- | --- |
| `path` | field | Owned copy of the file path; replaced through `set_path`. |
| `modified` | field | `true` while there are unsaved changes. |
| `widget` | field | Root widget created by `render`; `NULL` until then. |
| `open`, `save`, `render`, `finalize` | abstract | `NULL` after `edit_init`; each subclass must assign them. `finalize` frees only what the subclass added (virtual destructor). |
| `close` | common | Shared destructor: drops the weak pointer (`set_widget(NULL)`, it must go before `free(self)` or GObject would later write into freed memory), removes the widget from its parent `GtkBox`, calls `finalize`, frees `path` and the object. |
| `is_modified`, `mark_modified` | common | Read and write `modified`. |
| `set_path` | common | Replaces `path` with a copy; safe when the argument is `self->path`. |
| `set_widget` | common | Sets `widget` as a **weak pointer** (`g_object_add_weak_pointer`): if GTK destroys the widget first (e.g. the window closes), GObject writes `NULL` into `self->widget`. Every `render` must store its root widget through it. Passing `NULL` removes the weak pointer. |

## Factory

`new_edit_for_path(path)` looks at the extension (case-insensitive, without
the dot) and searches the suffix list each subclass exports, with the private
helper `_suffix_in()`:

| Suffix list | Contents | Editor |
| --- | --- | --- |
| `edit_txt_suffixes` | `txt`, `md` | `EditTXT` ([editTXT.c](editTXT.c.md)) |
| `edit_doc_suffixes` | `docx`, `doc`, `odt`, `rtf`, `xlsx`, `xls`, `ods`, `pptx`, `ppt`, `odp` | `EditDOC` ([editDOC.c](editDOC.c.md)), only if `edit_doc_available()` returns `true` |
| — | anything else | `NULL` |

It then calls `open()`. If opening fails, the editor is destroyed with
`close()` and the factory returns `NULL`, so callers get either a fully
opened editor or nothing.

The same lists feed the file-dialog filters of the subject menu in
[main.c](main.c.md), so each format is declared once, next to its editor.
The menu does not use the factory: the user picks the editor kind, and
`main.c` calls that kind's constructor directly.

## Limitations

- When LibreOffice is missing, office formats return `NULL` with only a
  `g_printerr` message; there is no fallback editor.
- `close()` only detaches `widget` when its parent is a `GtkBox`.
