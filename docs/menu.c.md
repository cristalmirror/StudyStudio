# `include/menu.h`, `src/menu.c` and the menu subclasses (`msc/`)

Status: draft for user approval; version 0.1.1.

## Purpose and dependencies

Defines `Menu`, the abstract base "class" of every drop-down menu of
StudyStudio, and its subclasses under `include/msc/` and `src/msc/` ("menu
subclasses"). `menu.h` depends only on GTK4 and `<stdbool.h>`. The only
subclass today is `MenuEditKind`, the subject menu, which also depends on
`include/edit.h` for `EditKind` and `edit_kinds[]` (see
[edit.c](edit.c.md#editor-kinds)). See the
[class diagram](class_diagram.md#menu-hierarchy) for the whole hierarchy.

## `Menu`

Inheritance follows the same pattern as [`Edit`](edit.c.md#inheritance-in-c):
the subclass embeds `Menu base` as its first member, allocates itself with
`calloc`, calls `menu_init(&obj->base)`, assigns `activate` (and `finalize`
if it owns memory) and returns `&obj->base`.

| Member | Kind | Description |
| --- | --- | --- |
| `popover` | field | Root widget (`GtkPopover`). Give it to a `GtkMenuButton` with `gtk_menu_button_set_popover`. |
| `list` | field | Vertical `GtkBox` inside the popover holding the entries. |
| `activate` | abstract | Called with the clicked entry's `item_data`, after the popover was closed. `NULL` after `menu_init`. |
| `finalize` | abstract, optional | Frees only what the subclass added (virtual destructor). May stay `NULL`. |
| `add_item` | common | Appends a frameless button with `label`, sensitive or greyed out, storing `item_data` as object data `"item"`. Returns the button. |
| `popdown` | common | Closes the popover (`gtk_popover_popdown`). |

Every entry's `"clicked"` signal goes to the private `_menu_on_item_clicked`,
which receives the `Menu *` as `user_data`, reads `"item"` from the button,
calls `popdown` and then `activate`: the base handles the widget, the
subclass decides what an entry means.

## Ownership

`Menu` has no `close()`: a menu has no natural moment to be closed by its
caller. `menu_init` ties the object to its popover with
`g_object_set_data_full(popover, "menu", self, _menu_free)`, so when GTK
destroys the popover (with its `GtkMenuButton`) `_menu_free` calls
`finalize` and `free(self)`. The subclass must therefore be allocated with
`malloc`/`calloc`. Callers keep the returned `Menu *` only as long as they
need `popover`.

`item_data` is a borrowed pointer: the menu never frees it. `MenuEditKind`
passes rows of the static `edit_kinds[]`, which live for the whole program.

## `MenuEditKind` (`msc/menuEditKind`)

The subject menu: "what do you want to edit?".

| Member | Description |
| --- | --- |
| `base` | The embedded `Menu`. |
| `on_chosen` | `MenuEditKindChosen` callback, `void (*)(const EditKind *kind, gpointer user_data)`. |
| `user_data` | Passed back to `on_chosen`; `main.c` uses the `SubjectPanel`. |

`new_menu_edit_kind(on_chosen, user_data)` adds one entry per `edit_kinds[]`
row, insensitive when the row's `suffixes` is `NULL` (the UML
placeholder). Its `activate` downcasts to `MenuEditKind *` and forwards the
chosen `const EditKind *` to `on_chosen`. It returns `NULL` if allocation
fails, and has no `finalize` because it owns no extra memory.

The menu does not know `SubjectPanel`, which is private to `main.c`: it only
reports the choice, and `main.c`'s `on_kind_chosen` decides what to do
(availability check, file dialog). See [main.c](main.c.md).

## Limitations

- Built for Linux and Windows but not run yet.
- Entries are plain buttons; there is no keyboard navigation between them
  beyond GTK's default focus handling, and no separators or submenus.
- `on_chosen` must not be `NULL`; `MenuEditKind` does not check it.
