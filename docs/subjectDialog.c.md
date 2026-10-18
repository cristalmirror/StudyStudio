# `include/msc/subjectDialog.h`, `src/msc/subjectDialog.c`

Status: draft for user approval; version 0.1.3. Compiled, not run yet.

## Purpose and dependencies

`SubjectDialog` is the modal "Nueva materia" window that asks for the data
of a new subject: its name, the folder that will hold it and the kinds of
file it will use. It only collects and validates input; creating the folders
is the job of [`Subject.make_subject`](subject.c.md#making-a-subject), which
`main.c` calls from the dialog's callback. Depends on GTK4/GLib and
`include/edit.h` (`edit_kinds[]`, `edit_kinds_count`). It lives in `msc/`
next to the menu subclasses, but it is not a [`Menu`](menu.c.md).

`GtkDialog` is deprecated since GTK 4.10, so the dialog is a plain
`GtkWindow` that is modal, transient for the main window, destroyed with it
and not resizable.

## Layout

| Row | Widget |
| --- | --- |
| Nombre de la materia | `GtkEntry` with the placeholder `p. ej. Álgebra`; Enter activates "Crear" (`gtk_entry_set_activates_default`). |
| Ubicación | `GtkLabel` with `base_dir` (ellipsized at the start) and a `📁 Cambiar…` button that opens `gtk_file_dialog_select_folder`. The default is `g_build_filename(g_get_home_dir(), "StudyStudio")`. |
| Tipos de archivo | One `GtkCheckButton` per `edit_kinds[]` row with its `label`, insensitive when `suffixes` is `NULL` (UML). None is ticked by default. |
| Error | `GtkLabel` with the `error` CSS class, hidden until a check fails. |
| Actions | `Cancelar` and `Crear` (`suggested-action`, default widget), aligned to the end. |

## Interface

```c
typedef bool (*SubjectDialogDone)(const char *name, const char *base_dir,
                                  const bool *chosen, gpointer user_data);

SubjectDialog *new_subject_dialog(GtkWindow *parent, SubjectDialogDone on_done,
                                  gpointer user_data);
```

`new_subject_dialog` builds the window, presents it and returns the
`SubjectDialog *`. The caller does not need to keep it: see
[Ownership](#ownership).

| Field | Description |
| --- | --- |
| `window` | The modal `GtkWindow`. |
| `name_entry` | Name entry. |
| `dir_label` | Shows `base_dir`. |
| `error_label` | Validation and failure messages. |
| `checks` | Array of `edit_kinds_count` check buttons, same order as `edit_kinds[]`. |
| `base_dir` | Owned copy of the chosen location. |
| `cancellable` | `GCancellable` given to the folder chooser. |
| `on_done`, `user_data` | Result callback and its data (`main.c` passes the `SubjectPanel`). |

## "Crear"

`_on_create_clicked` copies the entry text and strips the leading and
trailing spaces (`g_strstrip` works in place, so it works on a copy, never
on the entry's buffer). It fills a `bool chosen[edit_kinds_count]` from the
checks and then:

- empty name: shows `Escribe el nombre de la materia` and focuses the entry;
- no kind ticked: shows `Elige al menos un tipo de archivo`;
- otherwise calls `on_done(name, base_dir, chosen, user_data)`. If it
  returns `true` the window is destroyed; if it returns `false` the window
  stays open with `No se pudo crear la materia (ver barra inferior)`, since
  the detailed reason is written to the footer by `subject_log`.

`name`, `base_dir` and `chosen` are only valid during the callback: the
callback must copy whatever it keeps. `Cancelar` and the window's close
button destroy the window without calling `on_done`.

## Folder chooser

`_on_choose_dir_clicked` opens `gtk_file_dialog_select_folder`, parented to
the dialog and starting in `base_dir` when that folder exists. The call is
asynchronous, so the dialog could be destroyed before the chooser answers.
For that reason it is started with `self->cancellable`, which the destructor
cancels: `_on_folder_chosen` then gets `NULL` from
`gtk_file_dialog_select_folder_finish` and returns **before** using
`user_data`. A dismissed chooser takes the same path. On success the chosen
path replaces `base_dir` and the label.

## Ownership

Same pattern as `Menu`: `new_subject_dialog` ties the struct to its window
with `g_object_set_data_full(window, "dialog", self, _subject_dialog_free)`.
When GTK finalizes the window, `_subject_dialog_free` cancels and releases
`cancellable` and frees `checks`, `base_dir` and the struct. The check
widgets belong to the window. After `gtk_window_destroy` the struct may
already be freed, so no code touches `self` after destroying the window.

## Limitations

- Compiled for Linux and Windows, not run yet.
- The base folder's parent must exist (`make_subject` does not create
  intermediate folders); the folder chooser only offers existing folders.
- The name is only checked for emptiness here; invalid characters and
  existing subjects are rejected later by `make_subject`, with the reason in
  the footer instead of in the dialog.
- No keyboard shortcut for "Cancelar" besides closing the window (Escape is
  not bound).
