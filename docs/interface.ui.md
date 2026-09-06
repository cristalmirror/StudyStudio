# `interface.ui`

Status: draft for user approval; version 0.0.7.

## Purpose and structure

Defines the GTK4 layout consumed by `GtkBuilder`. The main window is titled `StudyStudio-0.0.7`, with a default size of 300 by 400.

| Object ID | Role |
| --- | --- |
| `main_window` | Application window. |
| `main_box` | Vertical container with spacing and margins. |
| `buttons_box` | Horizontal container with equally allocated button space. |
| `add_button` | Button labeled `➕ Agregar Elemento`. |
| `load_button` | Button labeled `📂 Cargar Materia`. |
| `scrolled_window` | Vertically expanding scroll container. |
| `target_box` | Vertical destination for buttons created at runtime. |

The XML does not bind callbacks. `src/main.c` looks up `main_window`, `add_button`, `load_button`, and `target_box`, then connects signals in C. Renaming those IDs requires updating the corresponding lookups. The resource manifest embeds this file at `/org/studystudio/interface.ui`.

## Current limitations

The layout contains no file chooser or archive-loading workflow. Labels remain in Spanish; English documentation does not change the application language.

## SOLID development direction

Follow the [project SOLID guidelines](README.md#development-direction-solid). Keep this file responsible for presentation structure. Implement application operations behind the UI boundary and keep storage decisions out of visual definitions.
