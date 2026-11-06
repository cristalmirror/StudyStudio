# `resources.xml`

Status: draft for user approval; version 0.1.4.

## Purpose and integration

Defines the GLib resource collection with prefix `/org/studystudio` and two inputs: `interface.ui` (runtime path `/org/studystudio/interface.ui`, passed by `activate` in `src/main.c` to `gtk_builder_new_from_resource`) and, since 2026-11-06, `style.css` (`/org/studystudio/style.css`, loaded by `load_css()`).

The Makefile runs `glib-compile-resources --generate-source --target=build/resources.c resources.xml`. It declares this manifest, `interface.ui` and `style.css` as prerequisites. Changing either triggers regeneration when building the resource target.

When adding resources, update this manifest and ensure the build tracks their input files as prerequisites. Changing the prefix or file name requires updating consumers of the runtime path.

## SOLID development direction

Follow the [project SOLID guidelines](README.md#development-direction-solid). Keep this file limited to resource declarations. Resource packaging must not contain subject workflows or storage logic.
