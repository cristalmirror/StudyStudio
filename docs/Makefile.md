# `Makefile`

Status: draft for user approval; version 0.0.9.

## Purpose

Defines Linux and Windows build recipes, generates embedded resource code, and stores artifacts under `build`. The executable base name is `studystudio-0.0.9`.

| Target | Behavior |
| --- | --- |
| `all` | Requests both Linux and Windows builds; default target. |
| `linux` | Builds `build/studystudio-0.0.9_linux` with GCC. |
| `win64` | Builds `build/studystudio-0.0.9_win64.exe` with MinGW-w64. |
| `setup` | Creates platform object directories. |
| `clean` | Deletes `build` and the root-level `resources.c`. |

## Inputs and dependencies

Discovers `src/*.c`, compiles separate platform objects, and links GTK4 and liblzma. Resource generation uses `glib-compile-resources`, `resources.xml`, and `interface.ui` to produce `build/resources.c`. GTK flags come from `pkgconf`; Windows lookup uses the configured MinGW pkg-config directory. Compilation enables `-Wall -Wextra -O2`; Windows linking also sets `-mwindows -static-libgcc`.

Use `make linux`, `make win64`, or `make all` inside an environment with the required tools and libraries. These are configured build targets, not evidence that both platforms currently compile successfully.

## Current limitations

- Source discovery is limited to C files directly inside `src`; future nested modules require updated rules.
- `HEADERS` uses `include/.h` rather than `include/*.h` and is not used as a prerequisite. Header changes alone do not reliably rebuild dependent objects.
- There is no test target or compiler-generated header dependency tracking.
- As of version 0.0.8 both `linux` and `win64` build and link successfully; `include/subject.h` now guards its Windows/POSIX-specific fields and includes correctly, so `subject.c` compiles cleanly on both targets. Version 0.0.9 fixed `main.c` build errors (see [main.c](main.c.md)) and removed a stray declaration from `subject.h` that produced a `-Wunused-function` warning on the Windows build (see [subject.h](subject.h.md)). See [subject.c](subject.c.md) for the remaining implementation limitations.

## SOLID development direction

Follow the [project SOLID guidelines](README.md#development-direction-solid). Keep build configuration responsible for assembling modules and selecting platform implementations. As modules are separated, update source discovery and dependency tracking so their boundaries remain practical to build and verify. SOLID describes the software design; the existence of separate build targets does not establish compliance.
