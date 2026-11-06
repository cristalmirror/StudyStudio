# `interface.ui`

Status: draft for user approval; version 0.1.3.

## Purpose and structure

Defines the GTK4 layout consumed by `GtkBuilder`. The main window is titled `StudyStudio-0.1.3`, with a default size of 1100 by 760. Since 2026-11-06 the window uses a `GtkHeaderBar` (title "StudyStudio") that holds the two main actions, and its look comes from `style.css` (see [below](#styles)).

| Object ID | Role |
| --- | --- |
| `main_window` | Application window. |
| `header_bar` | Title bar (`app-header` class) with the two buttons at the start. |
| `add_button` | "Nueva materia" (icon + label, `suggested-action`). |
| `load_button` | "Cargar materia" (icon + label). |
| `main_box` | Vertical container: content and footer. |
| `scrolled_window` | Vertically expanding scroll container (no horizontal scroll). |
| `target_box` | Vertical destination (`subject-list` class) for the subject cards created at runtime. |
| `empty_hint` | Text shown until the first subject is added; `main.c` hides it. |
| `footer` | Horizontal container at the bottom of `main_box` (`footer` class). |
| `footer_label` | Status label inside `footer`; starts as `Listo` and shows the last message (accessible name "Mensajes de estado"). |

The XML does not bind callbacks. `src/main.c` looks up `main_window`, `add_button`, `load_button`, `target_box`, `empty_hint` and `footer_label`, then connects signals and the footer logger in C. Renaming those IDs requires updating the corresponding lookups. The resource manifest embeds this file at `/org/studystudio/interface.ui`.

## Footer

As of version 0.0.14 the `footer` is the last child of `main_box`, after `scrolled_window`. A GTK4 `GtkWindow` accepts a single child; in 0.0.13 the footer was declared as a second child of `main_window` and replaced `main_box`, hiding the buttons and the list. Since `main_box` is vertical and `scrolled_window` has `vexpand`, the footer stays at the bottom of the window.

`footer_label` is a `GtkLabel` with `xalign` 0, `ellipsize` `end`, and `hexpand` true, so long messages are cut with an ellipsis instead of widening the window. It shows only the **last** message (status-bar style), not a history. `src/main.c` registers it with `subject_set_logger`, so every message passed to `subject_log` in `src/subject.c` replaces its text (see [main.c](main.c.md) and [subject.h](subject.h.md#logging)).

Check the file with `xmllint --noout interface.ui` or `gtk4-builder-tool validate interface.ui` before building: `glib-compile-resources` does not validate it, and a malformed file only fails at startup in `gtk_builder_new_from_resource()`.

## Styles

`style.css` (embedded at `/org/studystudio/style.css`, loaded by `load_css()`
in `main.c` with `GTK_STYLE_PROVIDER_PRIORITY_APPLICATION`) defines the
classes used by the window and the editors: `subject-list`, `subject-card`,
`doc-toolbar`, `tool-group`, `tool-menu`, `menu-item`, `editor-surface`,
`editor-text`, `pdf-surface`, `pdf-page`, `doc-canvas`, `context-bar`,
`tool-toggle`, `zoom-label`, `doc-status`, `doc-name`, `status-modified`,
`status-saved`, `empty-hint`, `footer`. Colors are the theme's named colors
(`@theme_base_color`, `@borders`, `@theme_selected_bg_color`,
`@warning_color`, ...), so light and dark themes both work. States are never
shown by color alone: the active PDF tool also gets an underline, focus an
outline, and the document state a text ("● Sin guardar" / "✓ Guardado").

## Current limitations

The layout contains no file chooser or archive-loading workflow. Labels (including the initial `Listo` of `footer_label`) remain in Spanish, while the messages written to the footer by `src/subject.c` are in English; English documentation does not change the application language.

## SOLID development direction

Follow the [project SOLID guidelines](README.md#development-direction-solid). Keep this file responsible for presentation structure. Implement application operations behind the UI boundary and keep storage decisions out of visual definitions.
