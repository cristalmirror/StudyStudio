# `include/msc/docToolbar.h` and `src/msc/docToolbar.c`

Status: draft for user approval; version 0.1.3 (added 2026-11-06).

## Purpose

`DocToolbar` is the toolbar of an open document. It depends only on the
[`Edit` capability contracts](edit.c.md#capability-contracts), never on a
concrete editor (dependency inversion): it shows the tools the bound editor
offers and runs them. File operations (save, save as, close) belong to the
owner (`SubjectPanel` in [main.c](main.c.md)), which receives them through
`DocToolbarOwner` together with state changes and messages.

## Interface

| Function | Description |
| --- | --- |
| `doc_toolbar_new(window, cancellable, owner, owner_data)` | Builds the toolbar. It lives as long as its widget (`g_object_set_data_full`); `cancellable` stops its file dialogs when the window closes. |
| `doc_toolbar_widget(bar)` | Root widget (a `GtkFlowBox`, so groups wrap on narrow windows). |
| `doc_toolbar_attach(bar, scope)` | Inserts the `doc` action group in `scope` and adds the keyboard shortcuts there. |
| `doc_toolbar_bind(bar, edit)` | Shows the tools of `edit` (`NULL`: none) and becomes its listener. |

`DocToolbarOwner`: `save`, `save_as`, `close`, `changed` (modified flag or
name changed) and `message` (text for the status bar).

## Actions

One `GSimpleAction` per tool in the `doc` group: `save`, `save-as`, `close`,
`undo`, `redo`, `copy`, `cut`, `paste`, `paste-plain`, `highlight`, `shade`,
`clear-markup`, `insert-image`, `insert-table`, `insert-row`,
`insert-column`, `insert-page`, `insert-note`, `merge-file`, `merge-cells`,
`split-cells`, `page-up`, `page-down`.

Each action is described by a `ToolAction`: `available(edit)` (the editor
offers it: its function pointer is not `NULL`) and `ready(edit)` (it can
run now: e.g. something is selected, undo is possible). Buttons use
`action-name`, so GTK greys them out when the action is disabled; tools not
available are hidden (and a group with no visible tool too). The listener
refreshes the enabled state on every `notify` of the editor.

If an operation fails and the editor did not explain why through `notify`,
the toolbar reports `edit_result_message(rc)`.

## Layout

| Group | Contents |
| --- | --- |
| File | Guardar, Guardar como, Cerrar |
| History | Deshacer, Rehacer |
| Clipboard | Cortar, Copiar, Pegar, Pegar sin formato |
| Markup | `GtkColorDialogButton` (also sent to `Edit.set_color`), Resaltar, Sombrear, Quitar |
| Insertar ▾ | Imagen…, filas × columnas + Tabla, Fila debajo, Columna después, page item (label from `EditInsert.page_label`), comment entry + Comentar |
| Combinar ▾ | Insertar desde otro archivo…, Combinar celdas, Separar celdas, Mover página antes/después |

Icons are Adwaita symbolic icons (shipped in the Windows package); every
button has a tooltip that is also its accessible name.

## Shortcuts

`doc_toolbar_attach` adds two `GtkShortcutController`s to the scope:

- capture phase: Ctrl+S, Ctrl+Shift+S (save wins over the focused widget);
- bubble phase: Ctrl+C/X/V, Ctrl+Shift+V, Ctrl+Z, Ctrl+Shift+Z, Ctrl+Y.
  The text view and the office canvas handle these first; the panel only
  receives them when nothing inside did (e.g. a PDF page).

## Ownership

The toolbar does not own the `Edit`. When the toolbar is freed it removes
itself as the editor's listener. File dialogs check `bar->edit` again when
they finish, because the document may have been closed meanwhile.
