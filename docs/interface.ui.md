# `interface.ui`

Status: draft for user approval; version 0.0.13.

## Purpose and structure

Defines the GTK4 layout consumed by `GtkBuilder`. The main window is titled `StudyStudio-0.0.13`, with a default size of 300 by 400.

| Object ID | Role |
| --- | --- |
| `main_window` | Application window. |
| `main_box` | Vertical container with spacing and margins. |
| `buttons_box` | Horizontal container with equally allocated button space. |
| `add_button` | Button labeled `➕ Agregar Elemento`. |
| `load_button` | Button labeled `📂 Cargar Materia`. |
| `scrolled_window` | Vertically expanding scroll container. |
| `target_box` | Vertical destination for buttons created at runtime. |
| `footer` | Work in progress: container intended to show the messages printed by `src/subject.c` (see below). |

The XML does not bind callbacks. `src/main.c` looks up `main_window`, `add_button`, `load_button`, and `target_box`, then connects signals in C. Renaming those IDs requires updating the corresponding lookups. The resource manifest embeds this file at `/org/studystudio/interface.ui`.

## Footer (work in progress)

A `footer` `GtkBox` is being added so that every message printed by `src/subject.c` is shown in the window instead of only on the terminal. The current draft is not usable yet:

- It is declared as a second `<child>` of `main_window`. A GTK4 `GtkWindow` accepts a single child, so the footer must move inside `main_box`, after `scrolled_window`; the vertical orientation of `main_box` and the `vexpand` of `scrolled_window` then keep it at the bottom.
- Its `<property name>` line is malformed XML. `glib-compile-resources` does not validate it, so the build succeeds but `gtk_builder_new_from_resource()` in `src/main.c` aborts at startup. Check the file with `xmllint --noout interface.ui` or `gtk4-builder-tool validate interface.ui` before building.

Planned design: the footer holds a `GtkLabel` with ID `footer_label` (last message, status-bar style) or a non-editable `GtkTextView` in a fixed-height `GtkScrolledWindow` (full history). `src/subject.c` exposes a logging callback (`subject_set_logger`) instead of depending on GTK, and `src/main.c` registers a callback that writes each message to the footer widget. The `perror` calls in the child processes created by `fork()` stay on `stderr`. See the 2026-09-23 session in [prototype_ia.md](prototype_ia.md) for the reference code.

## Current limitations

The layout contains no file chooser or archive-loading workflow. Labels remain in Spanish; English documentation does not change the application language.

## SOLID development direction

Follow the [project SOLID guidelines](README.md#development-direction-solid). Keep this file responsible for presentation structure. Implement application operations behind the UI boundary and keep storage decisions out of visual definitions.
