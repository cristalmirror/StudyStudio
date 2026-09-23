# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Known Issues
- Windows's `load_subject` still does not extract the archive back into
  files on disk: `_derive_extract_dir`'s malformed `strrchr` call and a
  `stderr`/`stderrm` typo in `_unpack_windows_buffer` mean the Windows
  extraction path does not compile yet, even though it is wired into
  `_load_subject` the same way as Linux, which does compile and correctly
  unpacks the decoded TAR bytes via `_unpack_tar_buffer` (see
  `docs/subject.c.md#extraction`).
- Neither `_save_subject` nor `_load_subject` is explicitly wired into
  `main.c`/the UI as a dedicated call, but since `main.c` already calls
  `load_subject` for the "Load Subject" button, Linux extraction now runs
  as a side effect of that existing button.
- The internal Windows entry format has no magic value, version, or
  end-of-archive marker yet; it must not be treated as a stable archive
  format.
- The Windows entry format records only regular files, so empty directories
  cannot be restored.
- `interface.ui`: the in-progress `footer` is declared as a second child of
  `main_window` (a GTK4 `GtkWindow` accepts only one) and contains a
  malformed `<property name>` line, so `gtk_builder_new_from_resource()`
  aborts at startup until it is fixed (see `docs/interface.ui.md#footer-work-in-progress`).

### Planned
- Implement CSS custom styles for widgets
- Add `GtkListView` / `GtkColumnView` for high-performance lists
- Support for modal dialogs and secondary windows
- Automated script to package Windows `.exe` with its DLLs
- Unit tests with GLib Testing Framework
- Internal API documentation
- Complete functional testing for both Linux and Windows builds
- User documentation and `README.md` improvements

## [0.0.13] - 2026-09-22

### Added
- `Makefile`: `win64-debug` target, mirroring the existing Linux `debug`
  target for Windows — new `CFLAGS_WIN_DEBUG` (`-g -O0 -DDEBUG`),
  `LDFLAGS_WIN_DEBUG`, and `OBJS_WIN64_DEBUG` build `build/studystudio-
  0.0.13_win64_debug.exe` from objects kept in `build/win64-debug/`, so they
  never mix with the optimized `build/win64/` objects. Unlike `win64`,
  `LDFLAGS_WIN_DEBUG` omits `-mwindows`, so the debug `.exe` keeps its
  console window for debug output. `setup` now also creates
  `build/win64-debug`. There is no `gdb`-equivalent shortcut for it: this
  toolchain does not set up `winedbg`/Wine for cross-debugging a MinGW-w64
  binary (see `docs/Makefile.md`).
- `.github/workflows/build.yml`: new "Compile Windows binary (debug)" step
  running `make win64-debug` with the same container/volume layout as the
  other steps. "Verify binaries were produced" now also checks for
  `build/studystudio-*_win64_debug.exe`, and "Upload binaries as artifacts"
  now includes it alongside the other three binaries.
- `README.md`: new "📝 Commit Conventions" section documenting the
  [Conventional Commits](https://www.conventionalcommits.org/) types used in
  this repository (`feat`, `fix`, `docs`, `refactor`, `test`, `chore`).

### Fixed
- `Dockerfile`: added `RUN chown -R 1000:1000 /usr/src/app` after `COPY`.
  `WORKDIR`/`COPY` run as root during `docker build`, so `/usr/src/app`
  itself was root-owned in the image regardless of what gets bind-mounted
  over its subdirectories later. Removing or creating an entry directly
  inside `/usr/src/app` needs write permission on `/usr/src/app` itself, not
  on the entry, so running the container as a non-root user
  (`--user "$(id -u):$(id -g)"`) while bind-mounting individual
  subdirectories (`-v "$(pwd)/build:/usr/src/app/build"`, etc., rather than
  the whole repository at once) failed with `Permission denied` on `make
  clean` and `make setup` before this fix (see `docs/Dockerfile.md`).
- `Makefile`: `clean` now runs `rm -rf $(BUILD_DIR)/*` instead of
  `rm -rf $(BUILD_DIR)`, emptying `build/`'s contents rather than removing
  the directory itself. When `build/` is bind-mounted as its own Docker
  volume, it is an active mount point inside the container, and no process,
  root or not, can `rmdir` an active mount point — only empty it. `clean`
  failed with `Device or resource busy` under that mount layout before this
  fix (see `docs/Makefile.md`).
- Completed the `0.0.13` version bump listed under "Changed", which had
  missed some files: the window title in `interface.ui` and the "Status"/
  baseline lines of `docs/interface.ui.md`, `docs/main.c.md`,
  `docs/subject.c.md`, `docs/subject.h.md`, `docs/resources.c.md`,
  `docs/resources.xml.md`, and `docs/README.md` (including its SOLID
  section) still read `0.0.12`.

### Changed
- Bumped the project version to `0.0.13`: `Makefile`'s `NAME`, the window
  title in `interface.ui`, the version badge and early-development banner in
  `README.md`, the `Version` field in the standard file header comment in
  `src/main.c`, `src/subject.c`, and `include/subject.h`, and the
  "Status"/baseline version lines and embedded `studystudio-0.0.x`/
  `StudyStudio-0.0.x` mentions across `docs/*.md` now read `0.0.13`.
- `README.md`: documented `make win64-debug` alongside the existing GDB
  section, and added `build/win64-debug/` to the "Project Structure" tree.
- `docs/Makefile.md`: added the `win64-debug` target row, updated the
  `clean` and `setup` descriptions to match the fixes above, and replaced
  the "no Windows debug variant" limitation with a note that `win64-debug`
  exists but still has no attached debugger.
- `docs/build.yml.md`: added the "Compile Windows binary (debug)" step to
  the jobs/steps table, updated "Verify binaries were produced" and "Upload
  binaries as artifacts" to mention all four binaries, and replaced the "no
  Windows debug job" limitation (obsolete now that the step above exists)
  with a note that neither debug step attaches a debugger.
- `docs/Dockerfile.md`: documented the new `chown` step and why it is
  needed for the per-subdirectory bind-mount style specifically (mounting
  the whole repository at `/usr/src/app` sidesteps the issue on its own).
- `docs/interface.ui.md`: added the `footer` object to the ID table and a
  "Footer (work in progress)" section describing its current defects and the
  planned design (a `footer_label` widget fed by a `subject_set_logger`
  callback, so `src/subject.c` stays independent of GTK).
- `docs/prototype_ia.md`: added the 2026-09-23 session log (review of
  `interface.ui` and design of the footer logging callback).

## [0.0.12] - 2026-09-22

### Added
- `.github/workflows/build.yml`: GitHub Actions workflow that builds the
  project on every push/PR to `main` by reusing the existing Docker
  toolchain — `make all` for the Linux/Windows release binaries and
  `make debug` for the Linux debug binary — verifies all three binaries
  were produced, and uploads them as workflow artifacts.
- `docs/build.yml.md`: documents the new workflow's steps, triggers, and
  current limitations (no Windows debug job, no test step, no Docker layer
  caching).

### Changed
- Bumped the project version to `0.0.12`: `Makefile`'s `NAME`, the window
  title in `interface.ui`, the version badge in `README.md`, the `Version`/
  `Last edited` fields in the standard file header comment in `src/main.c`,
  `src/subject.c`, and `include/subject.h`, and the "Status"/baseline
  version lines across `docs/*.md` now read `0.0.12`.
- `README.md`: the early-development warning banner had been left reading
  `v0.0.5` since before the `0.0.6` release; it now reads `v0.0.12`,
  matching the version badge above it.
- `docs/Dockerfile.md`, `docs/resources.c.md`, `docs/resources.xml.md`: the
  "Status" baseline lines had been left at a stale `0.0.7` since before the
  `0.0.8` release; they now read `0.0.12` along with the rest of `docs/*.md`.

## [0.0.11] - 2026-09-21

### Added
- `src/subject.c`: `_load_subject` now calls the new `_unpack_tar_buffer`
  after a successful decode on Linux, actually unpacking the decoded TAR
  bytes back into individual files/directories on disk via `tar -xf -`,
  the load-side mirror of the existing `tar -cf -` save path. Windows is
  wired the same way through `_unpack_windows_buffer`, but two defects
  (`_derive_extract_dir`'s malformed `strrchr` call, and a typo turning
  `stderr` into an undeclared `stderrm` identifier) mean the Windows build
  still does not compile (see `docs/subject.c.md#extraction`). Since
  `main.c` already calls `load_subject`, Linux extraction now runs as a
  side effect of the existing "Load Subject" button.

### Changed
- Bumped the project version to `0.0.11`: `Makefile`'s `NAME`, the window
  title in `interface.ui`, the version badge in `README.md`, the `Version`
  field in the standard file header comment in `src/main.c`,
  `src/subject.c`, and `include/subject.h`, and the "Status"/baseline
  version lines across `docs/*.md` now read `0.0.11`.
- `docs/README.md`, `docs/main.c.md`, `docs/subject.c.md`: updated to
  describe the Linux extraction wiring above and the remaining Windows
  compile failure.

## [0.0.10] - 2026-09-20

### Added
- `Makefile`: `debug` target, building `build/studystudio-0.0.10_linux_debug`
  with `-g -O0 -DDEBUG` (objects kept separate in `build/linux-debug/` so they
  never mix with the optimized `build/linux/` objects), and a `gdb` target
  that depends on `debug` and launches GDB against the resulting binary.
  `setup` now also creates `build/linux-debug`.
- `README.md`: new "🐞 Debugging with GDB" section documenting the workflow
  (compile the debug binary inside the Docker toolchain, then run GDB
  directly on the host, the same way the release binary is already run) with
  basic GDB usage examples.
- `docs/prototype_ia.md`: new document logging this AI-assisted working
  session — the GDB setup above, an analysis of how `_save_subject`/
  `_load_subject` (de)compress a full directory tree on each platform, and a
  proposed (not yet implemented) design for a private method that would make
  `_load_subject` unpack its decompressed buffer back into real files/folders
  on disk.
- `src/main.c`, `src/subject.c`, `include/subject.h`: standard file header
  (developer, repository, version, license, edit date).
- `src/subject.c`: in-progress `_derive_extract_dir` helper (Windows and
  Linux variants) and the `<sys/stat.h>` include it will need, groundwork for
  making `_load_subject` unpack archives back to disk. Not yet wired in, and
  not yet compiling as written (see `docs/subject.c.md`).

### Changed
- Bumped the project version to `0.0.10`: `Makefile`'s `NAME`, the window
  title in `interface.ui`, the version badge in `README.md`, and the
  "Status"/baseline version lines across `docs/*.md` now read `0.0.10`.
- `docs/Makefile.md`: documented the new `debug`/`gdb` targets in the targets
  table and "Inputs and dependencies", and added a "Current limitations"
  note that there is no Windows debug variant and that `gdb` is intended to
  run on the host rather than inside the Docker build container (no `ptrace`
  capability or display forwarding is configured there).
- `README.md`: updated the "Project Structure" tree to include the new
  `build/linux-debug/` directory.
- Translated the remaining Spanish-language comments in `Makefile` and
  `Dockerfile` to English.

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
