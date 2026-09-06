# `src/main.c`

Status: draft for user approval; version 0.0.7.

## Purpose and dependencies

Starts the GTK4 application, loads the embedded interface, and connects UI events. Depends on GTK4/GLib, `include/subject.h`, and the resource path `/org/studystudio/interface.ui`.

## Current behavior

| Element | Responsibility |
| --- | --- |
| `AppState` | Holds the destination widget and the counter for added buttons. |
| `main` | Creates application `org.ejemplo.app`, connects activation, runs the event loop, and releases the application. |
| `activate` | Loads the builder resource, retrieves widgets, associates the window with the application, allocates state, connects signals, and presents the window. |
| `on_add_clicked` | Increments the counter and appends a button labeled `Materia Num #N`. |
| `on_subject_clicked` | Prints the clicked button's label. |
| `on_load_clicked` | Prints the counter, creates `new_subject(2)`, prints its value through `read_subject`, and destroys it. |

The load button is a demonstration: it does not open a file or invoke `load_subject`. Added buttons do not currently represent persisted subject objects.

## Ownership and limitations

Temporary label strings are freed after widget creation. The builder and application references are released. The temporary subject is destroyed if construction succeeds. `AppState` is heap allocated, but no cleanup is registered. Builder objects are used without explicit checks for missing IDs.

## SOLID development direction

Follow the [project SOLID guidelines](README.md#development-direction-solid). Keep GTK rendering and event translation here or in a dedicated UI module. Move subject workflows into application operations as they develop, and supply their dependencies during startup. Keep compression and operating-system operations outside callbacks. Give application state an explicit cleanup lifecycle.
