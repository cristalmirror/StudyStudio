# `interface.ui`

Status: draft for user approval; version 0.0.14.

## Purpose and structure

Defines the GTK4 layout consumed by `GtkBuilder`. The main window is titled `StudyStudio-0.0.14`, with a default size of 300 by 400.

| Object ID | Role |
| --- | --- |
| `main_window` | Application window. |
| `main_box` | Vertical container with spacing and margins. |
| `buttons_box` | Horizontal container with equally allocated button space. |
| `add_button` | Button labeled `➕ Agregar Elemento`. |
| `load_button` | Button labeled `📂 Cargar Materia`. |
| `scrolled_window` | Vertically expanding scroll container. |
| `target_box` | Vertical destination for buttons created at runtime. |
| `footer` | Horizontal container at the bottom of `main_box`, below `scrolled_window` (see below). |
| `footer_label` | Status label inside `footer`; starts as `Listo` and shows the last message reported by `src/subject.c`. |

The XML does not bind callbacks. `src/main.c` looks up `main_window`, `add_button`, `load_button`, `target_box`, and `footer_label`, then connects signals and the footer logger in C. Renaming those IDs requires updating the corresponding lookups. The resource manifest embeds this file at `/org/studystudio/interface.ui`.

## Footer

As of version 0.0.14 the `footer` is the last child of `main_box`, after `scrolled_window`. A GTK4 `GtkWindow` accepts a single child; in 0.0.13 the footer was declared as a second child of `main_window` and replaced `main_box`, hiding the buttons and the list. Since `main_box` is vertical and `scrolled_window` has `vexpand`, the footer stays at the bottom of the window.

`footer_label` is a `GtkLabel` with `xalign` 0, `ellipsize` `end`, and `hexpand` true, so long messages are cut with an ellipsis instead of widening the window. It shows only the **last** message (status-bar style), not a history. `src/main.c` registers it with `subject_set_logger`, so every message passed to `subject_log` in `src/subject.c` replaces its text (see [main.c](main.c.md) and [subject.h](subject.h.md#logging)).

Check the file with `xmllint --noout interface.ui` or `gtk4-builder-tool validate interface.ui` before building: `glib-compile-resources` does not validate it, and a malformed file only fails at startup in `gtk_builder_new_from_resource()`.

## Current limitations

The layout contains no file chooser or archive-loading workflow. Labels (including the initial `Listo` of `footer_label`) remain in Spanish, while the messages written to the footer by `src/subject.c` are in English; English documentation does not change the application language.

## SOLID development direction

Follow the [project SOLID guidelines](README.md#development-direction-solid). Keep this file responsible for presentation structure. Implement application operations behind the UI boundary and keep storage decisions out of visual definitions.
