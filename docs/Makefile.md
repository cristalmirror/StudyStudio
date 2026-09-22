# `Makefile`

Status: draft for user approval; version 0.0.13.

## Purpose

Defines Linux and Windows build recipes, generates embedded resource code, and stores artifacts under `build`. The executable base name is `studystudio-0.0.13`.

| Target | Behavior |
| --- | --- |
| `all` | Requests both Linux and Windows builds; default target. |
| `linux` | Builds `build/studystudio-0.0.13_linux` with GCC. |
| `win64` | Builds `build/studystudio-0.0.13_win64.exe` with MinGW-w64. |
| `debug` | Builds `build/studystudio-0.0.13_linux_debug` with GCC, unoptimized and with debug symbols, for use with GDB. |
| `gdb` | Depends on `debug`; launches GDB against the resulting debug binary. |
| `win64-debug` | Builds `build/studystudio-0.0.13_win64_debug.exe` with MinGW-w64, unoptimized and with debug symbols, mirroring `debug` for Windows. |
| `setup` | Creates platform object directories, including both debug ones. |
| `clean` | Empties the contents of `build` and removes the root-level `resources.c`, leaving the `build` directory itself in place. |

## Inputs and dependencies

Discovers `src/*.c`, compiles separate platform objects, and links GTK4 and liblzma. Resource generation uses `glib-compile-resources`, `resources.xml`, and `interface.ui` to produce `build/resources.c`. GTK flags come from `pkgconf`; Windows lookup uses the configured MinGW pkg-config directory. Compilation enables `-Wall -Wextra -O2`; Windows linking also sets `-mwindows -static-libgcc`.

The `debug` target reuses the same source discovery and GTK4/liblzma linking as `linux`, but compiles with `-g -O0 -DDEBUG` instead of `-O2`, and places its objects in `build/linux-debug` so they never mix with the optimized `build/linux` objects. This keeps a `make linux` release build and a `make debug` build independently cacheable side by side. `win64-debug` follows the same pattern for Windows: same GTK4/liblzma linking as `win64`, `-g -O0 -DDEBUG` instead of `-O2`, objects in `build/win64-debug`, and no `-mwindows` in its link flags (unlike `win64`) so the resulting `.exe` keeps its console window for debug output.

`clean` uses `rm -rf $(BUILD_DIR)/*` rather than removing `$(BUILD_DIR)` itself. When `build/` is bind-mounted as its own Docker volume (rather than mounting the whole repository), it is an active mount point inside the container, and a non-root process cannot `rmdir` it — only empty it. Removing the directory itself would fail with "Device or resource busy" under that mount layout.

Use `make linux`, `make win64`, or `make all` inside an environment with the required tools and libraries. These are configured build targets, not evidence that both platforms currently compile successfully. `make debug`/`make gdb`/`make win64-debug` have the same toolchain requirement as `linux`/`win64`; see [README.md's GDB debugging section](../README.md#-debugging-with-gdb) for the recommended Docker-build/host-debug workflow. `win64-debug` has no `gdb`-equivalent shortcut, since cross-debugging a MinGW-w64 binary needs `winedbg`/Wine, which this toolchain does not set up.

## Current limitations

- Source discovery is limited to C files directly inside `src`; future nested modules require updated rules.
- `HEADERS` uses `include/.h` rather than `include/*.h` and is not used as a prerequisite. Header changes alone do not reliably rebuild dependent objects.
- There is no test target or compiler-generated header dependency tracking.
- As of version 0.0.8 both `linux` and `win64` build and link successfully; `include/subject.h` now guards its Windows/POSIX-specific fields and includes correctly, so `subject.c` compiles cleanly on both targets. Version 0.0.9 fixed `main.c` build errors (see [main.c](main.c.md)) and removed a stray declaration from `subject.h` that produced a `-Wunused-function` warning on the Windows build (see [subject.h](subject.h.md)). See [subject.c](subject.c.md) for the remaining implementation limitations.
- As of version 0.0.13, `win64-debug` gives Windows a debug build target, but there is still no way to actually attach a debugger to it from this toolchain — `gdb` itself only runs against the native Linux `debug` binary. Running `gdb` inside the Docker build container is also not supported out of the box (no `ptrace` capability or display forwarding is configured), so `make gdb` is intended to be run on the host after the object files exist.

## SOLID development direction

Follow the [project SOLID guidelines](README.md#development-direction-solid). Keep build configuration responsible for assembling modules and selecting platform implementations. As modules are separated, update source discovery and dependency tracking so their boundaries remain practical to build and verify. SOLID describes the software design; the existence of separate build targets does not establish compliance.
