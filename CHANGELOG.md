# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Known Issues
- `EditPDF` has only been compiled, not run. Pending runtime checks:
  annotations created with Poppler on real PDFs (highlight quads),
  saving over the opened file (on Windows too) and the Poppler DLLs, which
  must be shipped next to the Windows `.exe`. Page rotation is ignored by
  the annotation coordinates, there is no undo, and notes take their text
  from the toolbar entry (no popover). No pen tool: Poppler 24.02's GLib API
  (Fedora 40) has no ink annotations nor border width.
- `EditDOC` is view-only (no keyboard/mouse input yet), targets Writer
  rendering, and has only been compiled, not run. Pending runtime checks:
  clean exit without `office->destroy()` (it crashed in the spike),
  `saveAs` over the opened file, and LibreOffice on Windows.
- The subject menu (`MenuEditKind`) has not been run yet (built for Linux
  and Windows only).
  Closing a document with unsaved changes is refused with a footer message;
  there is no Save/Discard dialog. The first office document blocks the UI
  while LibreOffice starts.
- The "Nueva materia" dialog (`SubjectDialog`) and `make_subject` have only
  been compiled, not run. Pending runtime checks: the subject button must not
  stay pressed after the dialog opens (`gtk_menu_button_set_active(FALSE)`
  in `on_subject_button_activated`), the folders on disk, the "already
  exists" error, and accented names on Windows (`CreateDirectoryW`).
- "📂 Cargar Materia" does not create a subject panel yet: a loaded subject
  never gets a button nor an edit menu, and `_load_subject` does not fill
  `Subject.name`/`Subject.dir`.
- `make_subject` does not roll back: if a subfolder cannot be created, the
  folders already made stay on disk.
- The subfolders reuse `EditKind.filter_name` ("Texto", "Documentos de
  oficina", "PDF"), a UI string with spaces; renaming a filter would break
  the folder lookup in `on_kind_chosen` for existing subjects.
- Windows's `load_subject` extraction path (`_derive_extract_dir` +
  `_unpack_windows_buffer`) now compiles, but it has not been tested at
  runtime on Windows yet (see `docs/subject.c.md#extraction`).
- Neither `_save_subject` nor `_load_subject` is explicitly wired into
  `main.c`/the UI as a dedicated call, but since `main.c` already calls
  `load_subject` for the "Load Subject" button, extraction runs as a side
  effect of that existing button.
- The internal Windows entry format has no magic value, version, or
  end-of-archive marker yet; it must not be treated as a stable archive
  format.
- The Windows entry format records only regular files, so empty directories
  cannot be restored.
- `src/subject.c`: in `_unpack_windows_buffer`, the `subject_log` calls for
  the truncated size header/content errors come after `return -1` and are
  never reached.
- `src/subject.c`: the footer only shows the final message of a load, because
  `load_subject` runs on the GTK main thread and the window is not redrawn
  until it returns.

### Planned
- Implement CSS custom styles for widgets
- Add `GtkListView` / `GtkColumnView` for high-performance lists
- Support for modal dialogs and secondary windows
- Automated script to package Windows `.exe` with its DLLs
- Unit tests with GLib Testing Framework
- Internal API documentation
- Complete functional testing for both Linux and Windows builds
- User documentation and `README.md` improvements

## [0.1.3] - 2026-10-18

### Added
- `include/msc/subjectDialog.h`, `src/msc/subjectDialog.c`: `SubjectDialog`,
  the modal "Nueva materia" window (a `GtkWindow`, since `GtkDialog` is
  deprecated in GTK 4.10). Asks for the subject name, its location (default
  `~/StudyStudio`, changeable with an asynchronous folder chooser) and one
  check per `EditKind`; validates that the name is not empty and that at
  least one kind is ticked, and reports the result through a
  `SubjectDialogDone` callback that decides whether the window closes (see
  `docs/subjectDialog.c.md`).
- `Subject.make_subject(self, base_dir, name, folders)` (`_make_subject` in
  `src/subject.c`): creates `<base_dir>/<name>/` plus one subfolder per entry
  of the NULL-terminated `folders`, refuses invalid names and existing
  subjects, and stores the owned `Subject.name` / `Subject.dir`. Folders are
  created with `mkdir` on Linux and `CreateDirectoryW` (UTF-8 → UTF-16) on
  Windows, so accented names work.
- `docs/subjectDialog.c.md`.

### Changed
- `src/main.c`: the "Materia Num #x" button starts without a popover and
  opens `SubjectDialog` through `gtk_menu_button_set_create_popup_func`.
  Once the subject is created, `subject_panel_set_subject` renames the button
  after the subject and gives it the edit menu. `SubjectPanel` gains
  `number` and the owned `subject`; the file dialog of a kind starts in the
  subject's folder for that kind.
- `new_menu_edit_kind(on_chosen, user_data, enabled)`: the new `enabled`
  array hides the kinds a subject does not use (`NULL` shows them all).
- `_close_subject` frees `name` and `dir`; `new_subject` sets them to `NULL`.
- Bumped the project version to `0.1.3`: `Makefile`'s `NAME`, the window
  title in `interface.ui`, the version badge and early-development banner in
  `README.md`, the `Version` field of every file header, the "Status" /
  baseline version lines and binary names across `docs/*.md`, and the commit
  example in `CLAUDE.md`.

## [0.1.2] - 2026-10-16

### Added
- `include/edit_subclass/editPDF.h`, `src/edit_subclass/editPDF.c`:
  `EditPDF`, the PDF editor as an `Edit` subclass backed by Poppler
  (poppler-glib). Shows every page in a scrolled column with zoom, and a
  toolbar with tools (`EditPDFTool`) to highlight text, draw rectangles,
  add sticky notes and erase annotations. `save`
  writes the annotations back to the file (see `docs/editPDF.c.md`).
  Every data structure (`EditPDF`, `EditPDFPage`, `EditPDFTool`,
  `EditPDFToolInfo`) is in `editPDF.h`; `editPDF.c` only has the operations
  and constant tables.
- `docs/editPDF.c.md`.

### Changed
- `Dockerfile`: installs `poppler-glib-devel` and `mingw64-poppler-glib`
  (Poppler is GPL-2.0-or-later, compatible with GPLv3).
- `Makefile`: `pkgconf` asks for `gtk4 poppler-glib` on both targets, and
  every target links `-lm` (`editPDF.c` uses `<math.h>`).
- `src/edit.c`: the PDF row of `edit_kinds[]` and the factory now create
  `EditPDF` for `.pdf` files.
- Bumped the project version to `0.1.2`: `Makefile`'s `NAME`, the window
  title in `interface.ui`, the version badge and early-development banner in
  `README.md`, the `Version` field of every file header, the "Status" /
  baseline version lines and binary names across `docs/*.md`, and the commit
  example in `CLAUDE.md`.

## [0.1.1] - 2026-10-13

### Added
- `include/menu.h`, `src/menu.c`: abstract `Menu` base "class" for
  drop-down menus (same inheritance pattern as `Edit`): `popover` and `list`
  widgets, abstract `activate`/`finalize`, common `add_item`/`popdown` and
  `menu_init()`. The menu is freed together with its popover through
  `g_object_set_data_full`, so callers never free it (see `docs/menu.c.md`).
- `include/msc/menuEditKind.h`, `src/msc/menuEditKind.c`: `MenuEditKind`,
  the subject menu as a `Menu` subclass. `new_menu_edit_kind(on_chosen,
  user_data)` adds one entry per `edit_kinds[]` row and reports the chosen
  `EditKind` through the `MenuEditKindChosen` callback. New `msc/` ("menu
  subclasses") directories for future menus.
- `docs/menu.c.md`.

### Changed
- Bumped the project version to `0.1.1`: `Makefile`'s `NAME`, the window
  title in `interface.ui`, the version badge and early-development banner in
  `README.md`, the `Version` field of every file header (and `Last edited`
  of the files changed in this release), the "Status"/baseline version lines
  and `studystudio-0.1.x` mentions across `docs/*.md`, and the commit example
  in `CLAUDE.md`.
- `EditorKind` / `s_editor_kinds` moved from `src/main.c` to
  `include/edit.h` / `src/edit.c` as `EditKind` / `edit_kinds[]`, with
  `edit_kinds_count` exported for code outside `edit.c`. It describes the
  `Edit` subclasses, so it belongs to that hierarchy, not to the menu.
- `src/main.c`: the subject popover is now built with
  `new_menu_edit_kind(on_kind_chosen, panel)`. `subject_menu_new` was
  removed, and `on_kind_clicked(GtkButton *, gpointer)` became
  `on_kind_chosen(const EditKind *, gpointer)` (the popdown and the
  `"kind"` object data are handled by `Menu`). `main.c` no longer includes
  `editTXT.h`/`editDOC.h`.
- `Makefile`: `SRCS` also compiles `src/msc/*.c`.
- `src/edit_subclass/editDOC.c`: `_doc_draw`, `_doc_on_resize` and the
  `cairo_image_surface_create` call joined onto single lines.
- `docs/main.c.md`, `docs/edit.c.md`, `docs/class_diagram.md`,
  `docs/Makefile.md`, `docs/README.md`, `README.md`: updated for `Menu`,
  `MenuEditKind`, `EditKind` and the `msc/` directories.

### Fixed
- `Makefile`: `HEADERS` used `$(INCLUDE_DIR)/.h` and matched nothing; it is
  now `$(INCLUDE_DIR)/*.h` (still not used as a prerequisite).

## [0.1.0] - 2026-10-12

### Added
- `include/edit.h`, `src/edit.c`: abstract `Edit` base "class" for every
  editor (inheritance in C: the subclass embeds `Edit` as its first member),
  with abstract `open`/`save`/`render`/`finalize`, common
  `close`/`is_modified`/`mark_modified`/`set_path`, `edit_init()` and the
  `new_edit_for_path()` factory (see `docs/edit.c.md`).
- `include/edit_subclass/editDOC.h`, `src/edit_subclass/editDOC.c`: new
  `EditDOC`, an office document editor embedding LibreOffice through
  LibreOfficeKit (phase 1: view and save). It finds LibreOffice through
  `STUDYSTUDIO_LO_PATH` or the usual install paths, keeps one process-wide
  `LibreOfficeKit` instance, paints only the visible part of the document
  with `paintTile` into a `GtkDrawingArea` (fit-to-width zoom, own
  `GtkAdjustment` and scrollbar) and saves with `saveAs` in the original
  format. New `edit_doc_available()` (see `docs/editDOC.c.md`).
- `include/third_party/LibreOfficeKit/`: LibreOfficeKit 24.2 headers
  (MPL-2.0), patched to compile as C: `lokCallbackTypeToString()` wrapped in
  `#ifdef __cplusplus`, and the two Windows `reinterpret_cast` in
  `LibreOfficeKitInit.h` replaced by C casts.
- `src/edit.c`: the factory opens `.docx`, `.doc`, `.odt`, `.rtf`, `.xlsx`,
  `.xls`, `.ods`, `.pptx`, `.ppt` and `.odp` with `EditDOC` when LibreOffice
  is available.
- `docs/edit.c.md`, `docs/editTXT.c.md`, `docs/editDOC.c.md`.
- `src/main.c`: each subject button is now a `GtkMenuButton` whose popover
  asks which kind of document to edit: text (`EditTXT`), office document
  (`EditDOC`), and greyed-out "(próximamente)" PDF and UML entries. The
  choice opens a `GtkFileDialog` filtered to that kind, and the editor is
  rendered in a per-subject `GtkRevealer` with `💾 Guardar` / `✖ Cerrar`
  buttons. New `EditorKind` table (`s_editor_kinds`), `SubjectPanel`,
  `app_status()` and `AppState.footer_label`.
- `Edit.set_widget()`: stores the editor's root widget as a GObject weak
  pointer, so `close()` is safe after GTK destroyed the widget (window
  closed). `EditTXT` and `EditDOC` use it in `render`.
- `edit_txt_suffixes` / `edit_doc_suffixes`: each editor exports the file
  suffixes it opens; the factory and the menu filters share them.
- `src/subject.c`: `_make_subject(self)` stub, intended to create the
  resources a subject needs (folders, tools, edits). Not implemented yet.

### Changed
- Bumped the project version to `0.1.0`: `Makefile`'s `NAME`, the window
  title in `interface.ui`, the version badge and early-development banner in
  `README.md`, the `Version`/`Last edited` fields of the file headers, the
  "Status"/baseline version lines and `studystudio-0.0.x` mentions across
  `docs/*.md`, and the commit example in `CLAUDE.md`.
- `Makefile`: compiles `src/edit_subclass/*.c` and creates the object
  subdirectories (`@mkdir -p $(@D)`).
- `editDOC.h`/`editDOC.c` (the `GtkTextView` plain-text editor) renamed to
  `editTXT.h`/`editTXT.c`: `EditDOC` → `EditTXT`, `new_edit_doc` →
  `new_edit_txt`. Both files now have the standard header; `editTXT.h` lost
  its leading blank line and both end with a newline.
- `Makefile`: `INCLUDES` adds `-Iinclude/third_party`.
- `src/edit.c`: the factory searches the subclasses' suffix lists through
  `_suffix_in()` instead of its own extension list.
- `interface.ui`: default window size 900×700 (was 300×400).
- `src/main.c`: `on_subject_clicked` (which only printed the label) removed.
- `docs/class_diagram.md`: the `Edit` hierarchy now matches the code (`widget`,
  `finalize`, `set_path`, `mark_modified(bool)`, `edit_init(Edit*)`, factory)
  and shows `EditTXT`, `EditDOC` and the LibreOfficeKit types; the planned
  `EditDocs` was removed.

## [0.0.15] - 2026-09-29

### Changed
- `src/main.c`: the "Load Subject" file picker now uses `GtkFileDialog`
  (`gtk_file_dialog_open` / `gtk_file_dialog_open_finish`) instead of the
  deprecated `GtkFileChooserNative` and `gtk_file_chooser_get_file`, which
  removes the `-Wdeprecated-declarations` warnings. The
  `on_load_dialog_respose` callback was replaced by `on_load_dialog_finish`;
  closing the dialog (`GTK_DIALOG_ERROR_DISMISSED`) is ignored and any other
  error is printed (see `docs/main.c.md`).
- `src/subject.c`: the Linux `_save_subject` builds `parent` and `base` with
  `snprintf` (and `memcpy` for the fixed-length parent prefix) instead of
  `strcpy`/`strncpy`, so both strings are always NUL-terminated. This removes
  the `-Wstringop-truncation` warnings.
- `Makefile`: build and clean messages translated to English.
- Bumped the project version to `0.0.15`: `Makefile`'s `NAME`, the window
  title in `interface.ui`, the version badge and early-development banner in
  `README.md`, the `Version`/`Last edited` fields of the file headers in
  `src/main.c`, `src/subject.c`, and `include/subject.h`, the
  "Status"/baseline version lines and `studystudio-0.0.x` mentions across
  `docs/*.md`, and the commit example in `CLAUDE.md`.
- `docs/main.c.md`, `docs/subject.c.md`, `docs/subject.h.md`,
  `docs/class_diagram.md`: updated for `on_load_dialog_finish`, the
  `AppState` allocation, and the removed `write_u32_le` method.

### Removed
- `src/subject.c`, `include/subject.h`: the unused Windows helpers
  `_write_u32_le` and `_write_u64_le`, and the `write_u32_le` method of
  `Subject`. They wrote directly to a `FILE *`, so using them inside
  `walk_directory` would have bypassed the LZMA encoder and corrupted the
  archive; the entry headers are built inline and sent through `feed_bytes`.

### Fixed
- `src/subject.c`: in the Linux `_save_subject`, for a directory directly
  under `/` (for example `/foo`) the basename was copied into `parent`
  instead of `base`, leaving `base` uninitialized and passing garbage to
  `tar -C <parent> <base>`.
- `src/main.c`: `AppState.window` was never assigned (and `g_malloc` left it
  as garbage), but it was passed as the parent of the file picker. `activate`
  now allocates `AppState` with `g_new0` and sets `state->window = window`.
- `src/main.c`: the success message `Cargados %zu bytes desde %s` now ends
  with a newline.

### Added
- `.gitignore`: ignore the `.ccls-cache/` directory created by the ccls
  language server.

## [0.0.14] - 2026-09-24

### Added
- `include/subject.h`: `SubjectLogFunc` callback type and
  `subject_set_logger(fn, user_data)`, so the GUI can receive every message
  from `src/subject.c` without `subject.c` depending on GTK (see
  `docs/subject.h.md#logging`).
- `src/subject.c`: `subject_set_logger` and the private `subject_log(fmt,
  ...)`, which formats the message (with a heap fallback for messages longer
  than 1024 bytes) and forwards it to the registered callback, or to `stderr`
  when none is registered. The existing `fprintf`/`printf` diagnostics are
  now also sent through `subject_log`.
- `src/subject.c`: both `_load_subject` implementations report every result
  through `subject_log`: `Loading <path>...`, one English error message per
  return code (`-1` to `-9`), and `Subject loaded: <path> (<N> bytes) into
  <dir>` on success (see `docs/subject.c.md#loading`).
- `src/main.c`: `on_subject_log` callback, which writes each message to
  `footer_label`; `activate` registers it with `subject_set_logger`.
- `interface.ui`: `footer_label` (`GtkLabel`, initial text `Listo`) inside
  the `footer` box.

### Fixed
- `interface.ui`: the `footer` was declared as a second child of
  `main_window`; a GTK4 `GtkWindow` accepts a single child, so it replaced
  `main_box` and hid the buttons and the list. It is now the last child of
  `main_box`, below `scrolled_window`.
- `src/main.c`: `footer_label` is fetched before `g_object_unref(builder)`,
  not after it (use of a released builder).
- `src/subject.c`: the Linux `_load_subject` now returns `-4` when its
  input/output buffers cannot be allocated, instead of continuing with
  released resources.
- `interface.ui`: the window title still read `StudyStudio-0.0.12`.

### Changed
- Bumped the project version to `0.0.14`: `Makefile`'s `NAME`, the window
  title in `interface.ui`, the version badge and early-development banner in
  `README.md`, the `Version`/`Last edited` fields of the file headers in
  `src/main.c`, `src/subject.c`, and `include/subject.h`, and the
  "Status"/baseline version lines and `studystudio-0.0.x` mentions across
  `docs/*.md`.
- `docs/interface.ui.md`: replaced "Footer (work in progress)" with a
  "Footer" section describing the final layout and `footer_label`.
- `docs/main.c.md`: documented `on_subject_log`, the footer registration in
  `activate`, and the unassigned `AppState.window`.
- `docs/subject.h.md`, `docs/subject.c.md`: new "Logging" sections; added the
  `-4` code and the per-code footer messages to the loading tables; the
  Windows extraction section now reflects that it compiles.
- `docs/README.md`: the overview mentions the footer and the current state of
  Windows extraction.
- `docs/prototype_ia.md`: added the 2026-09-24 session log.

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
- The in `subject.c` and `subject.h` the function `load_subject()`
## [0.0.6] - 2026-08-27

### Changed 
- `include/subject.h` and `src/subject.c` can compress in lzma/xz now
- `xz-devel` library has add in the dockerfile

## [0.0.5] - 2026-08-15

### Added
- `include/subject.h` and `src/subject.c` has created to manager the subject archive

### Changed 
- Makefile has modify to can compile `include/subject.h` and `src/subject.c` 


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
