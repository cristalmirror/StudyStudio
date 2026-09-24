# `src/main.c`

Status: draft for user approval; version 0.0.14.

## Purpose and dependencies

Starts the GTK4 application, loads the embedded interface, and connects UI events. Depends on GTK4/GLib, `include/subject.h`, and the resource path `/org/studystudio/interface.ui`. The file now opens with a standard header comment (developer, repository, version, license, edit date).

## Current behavior

| Element | Responsibility |
| --- | --- |
| `AppState` | Holds the destination widget and the counter for added buttons. |
| `main` | Creates application `org.ejemplo.app`, connects activation, runs the event loop, and releases the application. |
| `activate` | Loads the builder resource, retrieves widgets, associates the window with the application, allocates state, connects signals, presents the window, fetches `footer_label` and registers it with `subject_set_logger(on_subject_log, footer_label)`, and finally releases the builder. |
| `on_subject_log` | Logger callback registered with `subject_set_logger`. Copies the message, strips the trailing newline with `g_strchomp`, and sets it as the text of `footer_label`. |
| `on_add_clicked` | Increments the counter and appends a button labeled `Materia Num #N`. |
| `on_subject_clicked` | Prints the clicked button's label. |
| `on_load_clicked` | Opens a `GtkFileChooserNative` ("Cargar Materia") and connects `on_load_dialog_respose` to its `"response"` signal. A forward declaration above `on_load_clicked` makes the callback visible before its definition later in the file. |
| `on_load_dialog_respose` | On `GTK_RESPONSE_ACCEPT`, resolves the chosen `GFile` to a local path with `g_file_get_path`, constructs a throwaway `Subject` with `new_subject(state->counter)`, and calls `mat->load_subject(mat, path, &buf, &size)`. Prints either the decoded byte count or the numeric error code, frees the decoded buffer and the path string, and destroys the temporary subject. |

Added buttons do not currently represent persisted subject objects, and the object constructed in `on_load_dialog_respose` only exists to call `load_subject`; it does not become part of the UI state (`state->counter` is not tied to the loaded subject in any way). `load_subject` returns the raw decompressed bytes (see [subject.c](subject.c.md)); this callback itself still only prints the byte count or the numeric error code and frees the buffer. On Linux, though, `load_subject` now also unpacks the decoded archive to disk as an internal side effect (writing files under a directory derived from the archive's name), so clicking "Load Subject" already extracts real files even though this callback was not changed to do anything with that side effect or report it. The Windows build of the equivalent extraction path does not currently compile (see [subject.c](subject.c.md#extraction)), so this behavior is Linux-only for now. As of version 0.0.14, every result of `load_subject` (start, each error code, and success) is also reported by `src/subject.c` through `subject_log`, so it appears in the window footer (see [interface.ui](interface.ui.md#footer)). The callback's own `g_print` calls still go to the terminal only.

The footer lookup must happen before `g_object_unref(builder)`: `gtk_builder_get_object` on a released builder is undefined behavior. `load_subject` runs on the GTK main thread, so `on_subject_log` can update the label directly; if loading moves to a worker thread, the callback must defer the update with `g_idle_add()`. While `load_subject` runs the window is not redrawn, so only the final message of a load is visible.

## Ownership and limitations

Temporary label strings are freed after widget creation. The builder and application references are released. The temporary subject in `on_load_dialog_respose` is destroyed after use, and its decoded buffer is freed once printed. `AppState` is heap allocated, but no cleanup is registered. Builder objects are used without explicit checks for missing IDs. `AppState.window` is never assigned (and `g_malloc` does not zero the structure), yet `on_load_clicked` passes `GTK_WINDOW(state->window)` as the parent of the file chooser; it should be allocated with `g_new0` and set to `window` in `activate`.

## SOLID development direction

Follow the [project SOLID guidelines](README.md#development-direction-solid). Keep GTK rendering and event translation here or in a dedicated UI module. Move subject workflows into application operations as they develop, and supply their dependencies during startup. Keep compression and operating-system operations outside callbacks. Give application state an explicit cleanup lifecycle.
