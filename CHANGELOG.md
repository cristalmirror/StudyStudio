# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Known Issues
- Neither platform's `load_subject` extracts the archive back into files on
  disk: Linux returns the raw decompressed TAR stream, Windows returns the
  raw decompressed entry stream; parsing/unpacking is not implemented on
  either side yet.
- `save_subject`/`load_subject` are not wired into `main.c`/the UI yet.
- The internal Windows entry format has no magic value, version, or
  end-of-archive marker yet; it must not be treated as a stable archive
  format.
- The Windows entry format records only regular files, so empty directories
  cannot be restored.

### Planned
- Implement CSS custom styles for widgets
- Add `GtkListView` / `GtkColumnView` for high-performance lists
- Support for modal dialogs and secondary windows
- Automated script to package Windows `.exe` with its DLLs
- Unit tests with GLib Testing Framework
- Internal API documentation
- Complete functional testing for both Linux and Windows builds
- User documentation and `README.md` improvements

## [0.0.9] - 2026-09-17

### Fixed
- `src/main.c` did not compile: `on_load_clicked` passed a misspelled signal
  name (`"respose"` instead of `"response"`) and an undeclared, misspelled
  callback identifier (`on_load_dialog_resose` instead of
  `on_load_dialog_respose`) to `g_signal_connect`. Corrected both, and added a
  forward declaration for `on_load_dialog_respose` above `on_load_clicked`
  since the callback is referenced before its definition later in the file.
- `on_load_dialog_respose` called `g_file_path()`, which does not exist in
  GLib/GIO, and stored the result in a variable named `paht` while the rest of
  the function referenced `path`, leaving `path` undeclared. Replaced with
  `g_file_get_path(file)` assigned to `path`.
- Removed a stray `static int _walk_directory(...)` forward declaration from
  `include/subject.h` (Windows section). `_walk_directory` is a private
  implementation detail already defined in `src/subject.c`, but the
  declaration in the shared header caused every other translation unit that
  includes `subject.h` and does not define/use the function itself — namely
  `main.c` on the Windows build — to see a `static` function that is declared
  but never defined or used there, triggering a `-Wunused-function`-class
  warning. `subject.c` never needed this declaration in the first place: it
  defines `_walk_directory` before its only use (`new_subject`'s Windows
  branch).
- Silenced the `-Wunused-parameter` warning on `activate`'s `user_data` with
  `(void)user_data;`, matching the pattern already used in the other
  callbacks.

### Changed
- Bumped the project version to `0.0.9`: `Makefile`'s `NAME` and the window
  title in `interface.ui` (which had been left at a stale `StudyStudio-0.0.7`
  since before the 0.0.8 release) now both read `0.0.9`.

## [0.0.8] - 2026-09-13

### Fixed
- Fixed the Windows build, which did not compile at all before this version:
  - Missing semicolon and an invalid duplicate/`static` member declaration in
    the Windows section of the `Subject` struct (`include/subject.h`).
  - `_walk_directory`'s forward declaration didn't match its definition
    (missing the `Subject *self` parameter), causing a `conflicting types`
    error.
  - `include/subject.h` was not self-contained: it referenced `lzma_stream`
    and `HANDLE` without including `<lzma.h>`/`<windows.h>` itself, relying on
    `subject.c` having included them first. This broke any other translation
    unit including the header directly, such as `main.c`. The header now
    includes `<stdint.h>`, `<stdio.h>`, `<sys/types.h>`, and `<lzma.h>`
    unconditionally, and `<windows.h>` under `_WIN32`.
  - Replaced the non-portable `u_int32_t` with `uint32_t` in `write_u32_le`.
  - `WalkContext`'s `outFile` field was referenced as `outfile` (wrong case)
    in two places in `_walk_directory`.
  - `Subject.pid` was a single `pid_t` field shared by both platforms, but
    Windows process handles from the Win32 API are `HANDLE` (a pointer), not
    a `pid_t`. Split into `proc_handle` (`HANDLE`, Windows) and `pid`
    (`pid_t`, POSIX), and updated `_wait_pid_os_opt` and `new_subject`
    accordingly.
  - `new_subject`'s Windows branch referenced an undeclared `walk_directory`
    identifier (missing the leading underscore) and left `is_dot_or_dotdot`
    and `feed_bytes` unassigned.
- Translated three Spanish-language strings/comments left in the Windows code
  path to English (`_wait_pid_os_opt`'s two `fprintf` messages, and a comment
  in `_load_subject`).

### Added
- Connected `is_dot_or_dotdot`, `write_u32_le`, `feed_bytes`, and
  `walk_directory` as real methods on `Subject` for Windows: they are now
  assigned in `new_subject` and invoked internally through `self->...(...)`
  instead of calling the static functions directly.
- Implemented `_save_subject` for Windows: opens the destination file,
  initializes the LZMA encoder (preset `6 | LZMA_PRESET_EXTREME`, CRC64),
  splits the source directory into `(parent, basename)` with `_fullpath`
  (mirroring the Linux `tar -C parent basename` behavior so the archived
  paths include the root directory name), walks the tree through
  `walk_directory`/`feed_bytes`, and finishes the LZMA stream with
  `LZMA_FINISH` so the `.xz` file's final blocks and index are written
  (`walk_directory`/`feed_bytes` alone never finish the stream).
- Implemented `_load_subject` for Windows: decompresses a full `.xz` file
  into a heap-allocated buffer, functionally equivalent to the Linux
  implementation, including a truncated-archive safety exit. Does not parse
  the per-entry format written by `_save_subject` (see Known Issues).
- `build/studystudio-0.0.8_win64.exe` now builds and links successfully
  alongside the Linux target.

## [0.0.7] - 2026-08-27

## Added
- The in `subject.c` and `subject.h` the funtion `load_subject()`
## [0.0.6] - 2026-08-27

### Changed 
- `include/subject.h` and `src/subject.c` can compres in lmza/xz now
- `xz-devel` library has add in the dockerfile

## [0.0.5] - 2026-08-15

### Added
- `include/subject.h` and `src/subject.c` has created to manager the subject archive

### Changed 
- Makefike has modify to can compile `include/subject.h` and `src/subject.c` 


---
## [0.0.4] - 2026-08-06

### Added
- New **"📂 Load Subject"** button placed next to the "➕ Add Element" button.
- `on_load_clicked()` callback function in `main.c` to handle the new button's click event.
- Horizontal `GtkBox` (`buttons_box`) in `interface.ui` to arrange both buttons side by side.
- Connected the `load_button` signal to its callback in the `activate()` function.

### Changed
- Reorganized the top layout in `interface.ui`: buttons are now grouped inside a horizontal `GtkBox` with `homogeneous` spacing.


## [0.0.3] - 2026-07-26

### Added
- **GLib GResources support**: Embedded `interface.ui` directly into the executable using `resources.xml` and `glib-compile-resources`.
- Automated `Makefile` rules to generate `resources.c` and compile it into `resources.o` for both Linux and Windows targets.
- Explicit dependency tracking in `Makefile` to ensure resources are generated before compilation starts.

### Changed
- Updated `src/main.c` to use `gtk_builder_new_from_resource("/org/studystudio/interface.ui")` instead of `gtk_builder_new_from_file()`.
- Improved `Makefile` robustness by replacing variable-based resource rules with explicit file targets to prevent "No rule to make target" errors.
- Executable naming convention now reflects the version (e.g., `studystudio-0.0.2_linux`).

### Removed
- Runtime dependency on the external `interface.ui` file. The binary is now truly self-contained regarding its UI layout.

### Fixed
- Build failure where `make` could not find the rule to create `build/linux/resources.o`.
- Runtime crash (`Gtk-ERROR: failed to add UI from file`) caused by incorrectly passing a resource path to a file-loading function.
- Tabulation issues in `Makefile` recipes that caused silent parsing failures in some environments.

### Known Issues
- Subject buttons only print to console; no persistent state or navigation yet.
- Windows executable still requires manual DLL copying for distribution (static linking GTK4 remains highly complex).
- No CSS styling for widgets.
- No unit tests implemented.

## [0.0.2] - 2026-07-26

### Added
- Dynamic button creation in `on_add_clicked`: each click on "Add Subject" now creates a new interactive button instead of a static label.
- New callback function `on_materia_clicked` to handle clicks on dynamically created subject buttons.
- Each subject button prints its label to the console when clicked.
- Makefile now mounted as a Docker volume to avoid rebuilding the image when build rules change.

### Changed
- Renamed executable from `mi_app` to `studystudio` in the `Makefile` (`NAME := studystudio`).
- Updated window title in `interface.ui` from "Agregador con XML" to **"StudyStudio 0.0.2"**.
- Improved Docker workflow: `Makefile` is now bind-mounted, so changes to build rules are reflected immediately without `docker build`.


## [0.0.1] - 2026-07-24

> [!WARNING]
> **This is a pre-release development version.** The build system is functional, but the application itself is not yet complete or fully tested. Use for development and testing purposes only.

### Added
- Cross-compilation setup for Linux and Windows using a single Docker environment (Fedora 40)
- GTK4 support for both platforms (native Linux + MinGW-w64 for Windows)
- Docker volume system to persist compiled binaries on the host
- `interface.ui` file with UI layout separated from C code using `GtkBuilder`
- Dynamic element addition functionality via button click
- Scrollable area (`GtkScrolledWindow`) to handle long lists of elements
- `AppState` structure to share state between GTK callbacks

### Changed
- Complete migration from GTK3 to GTK4, including:
  - Replacement of `gtk_container_add()` with `gtk_window_set_child()`
  - Removal of `gtk_widget_show_all()` in favor of `gtk_window_present()`
  - Use of `gtk_box_append()` to add widgets to containers
- Refactored visual design from hardcoded C to XML-based `.ui` files
- Separation of concerns: C code for logic, XML for presentation

### Fixed
- `PKG_CONFIG_PATH` conflict between Linux and Windows targets in Makefile
- Incorrect use of `GTK_WIDGET()` macro instead of `GTK_WINDOW()` for `gtk_window_set_application()`
- Inconsistency in `AppState` struct member names (`contenedor_destino` vs `target_box`)
- Docker creating fake directories when mounting non-existent files with `-v`
- "Device or resource busy" error when running `make clean` with mounted volumes
- "Failed to open display" error when running GTK applications inside Docker containers

### Known Issues
- Application is not yet fully functional
- Windows executable requires manual DLL copying for distribution
- No automated packaging system for Windows deployment
- Missing CSS styling for widgets
- No unit tests implemented

---

## Project Structure
