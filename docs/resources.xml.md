# `resources.xml`

Status: draft for user approval; version 0.0.7.

## Purpose and integration

Defines the GLib resource collection with prefix `/org/studystudio` and a single input, `interface.ui`. The resulting runtime path is `/org/studystudio/interface.ui`, which `activate` in `src/main.c` passes to `gtk_builder_new_from_resource`.

The Makefile runs `glib-compile-resources --generate-source --target=build/resources.c resources.xml`. It declares both this manifest and `interface.ui` as prerequisites. Changing either triggers regeneration when building the resource target.

When adding resources, update this manifest and ensure the build tracks their input files as prerequisites. Changing the prefix or file name requires updating consumers of the runtime path.

## SOLID development direction

Follow the [project SOLID guidelines](README.md#development-direction-solid). Keep this file limited to resource declarations. Resource packaging must not contain subject workflows or storage logic.
