# AI Prototype — Work session (2026-09-20)

Historical session log. Version numbers, commands, code samples, and pending tasks below describe their respective session dates, not the current release. For version 0.1.3, use the [repository README](../README.md), the [current documentation](README.md), and the [changelog](../CHANGELOG.md).

Log of an AI-assisted work session (Claude Code) on StudyStudio. Covers adding GDB debugging support to the `Makefile`, the associated documentation, and the analysis + design of real unpacking for `_load_subject`. The code in that last part was **designed by the AI but is still pending manual implementation** by the user, following this project's collaboration mode (the AI reviews/diagnoses/generates reference code; the user types it by hand to understand the code's structure).

---

## 1. GDB debugging support (`Makefile`)

A Linux compilation variant with debug symbols, unoptimized, was added to the `Makefile`:

- `CFLAGS_LINUX_DEBUG := -Wall -Wextra -g -O0 -DDEBUG $(INCLUDES) $(GTK_CFLAGS_LINUX)`
- Objects kept separate in `build/linux-debug/` (never mixed with `build/linux/`).
- `debug` target: builds `build/studystudio-0.0.9_linux_debug`.
- `gdb` target: depends on `debug` and launches `gdb` against the resulting binary.
- `setup` now also creates `build/linux-debug`.

There is no debug variant for Windows (`debug`/`gdb` are Linux-only).

### Build/debug workflow

The build still happens inside Docker (the host lacks `gtk4-devel`/`libarchive-devel`), but GDB runs on the **host**, the same way the release binary is already run, because the container has no `ptrace` or display forwarding configured:

```
docker run --rm \
    --user "$(id -u):$(id -g)" \
    -v "$(pwd)/src:/usr/src/app/src" \
    -v "$(pwd)/include:/usr/src/app/include" \
    -v "$(pwd)/build:/usr/src/app/build" \
    -v "$(pwd)/Makefile:/usr/src/app/Makefile:ro" \
    -v "$(pwd)/resources.xml:/usr/src/app/resources.xml:ro" \
    -v "$(pwd)/interface.ui:/usr/src/app/interface.ui:ro" \
    mi_app_builder \
    make debug

gdb ./build/studystudio-0.0.9_linux_debug
```

**Verified in session:** `make debug` compiled successfully (only pre-existing warnings from deprecated `gtk_file_chooser_native_new`/`gtk_file_chooser_get_file` and unused parameters in `subject.c`, unrelated to the change). GDB test was successful: breakpoint on `main`, stepping with `next` through the creation of `GtkApplication` and `g_application_run`, the GTK window started normally and the log `Cargados 10240 bytes desde /home/crisal/Descargas/archivo.tar.xz` was observed.

### Documentation updated

- `README.md`: new "🐞 Debugging with GDB" section (build in Docker + debug on host, basic GDB commands) and an update to the `Project Structure` tree to include `build/linux-debug/`.
- `docs/Makefile.md`: targets table with `debug`/`gdb`, explanation of `CFLAGS_LINUX_DEBUG` and `build/linux-debug` in "Inputs and dependencies", and a new bullet in "Current limitations" (no Windows variant, `gdb` intended for the host).

---

## 2. Analysis: does `_save_subject` compress an entire folder?

Yes, on both platforms, but with different implementations:

- **Linux** (`subject.c:599`, `#else` branch): does a real `fork`+`exec` of `tar -cf - -C <parent> <base>`, and its `stdout` is piped into the LZMA re-encoder. It inherits all of `tar`'s standard behavior (subdirectories, symlinks, permissions, empty folders).
- **Windows** (`subject.c:490`, `#ifdef _WIN32` branch): there is no `tar`, so `_walk_directory` (line 393) walks the tree with `FindFirstFileA`/`FindNextFileA` and recurses into subdirectories. For each file, `_write_file_entry_to_lzma` (line 300) writes its own header (`u32 path_len + path + u64 size + content`) fed into the same LZMA stream.

Limitations of the Windows version:
1. Does not support symlinks/reparse points — aborts the whole operation if it finds one (line 452-456).
2. Does not preserve empty folders (only file entries exist).
3. It is a custom format (custom header + raw LZMA), not a real `.tar` — only `_load_subject` itself can read it.

---

## 3. Analysis: does `_load_subject` unpack what `_save_subject` compressed?

**No, not currently.** `_load_subject` (Windows in `subject.c:106`, Linux in `subject.c:776`) only decompresses the full `.xz` into a dynamic in-memory buffer (`*out_buf`/`*out_size`) using `lzma_stream_decoder(..., LZMA_CONCATENATED)`. It does not interpret that content:

- **Linux:** the resulting buffer is an uncompressed `tar` in memory — it is never passed through `tar -xf` or anything that would unpack it.
- **Windows:** the resulting buffer is the raw concatenation of the `length + path + size + content` headers — they are never parsed to reconstruct individual files.

In `main.c:64-92` (`on_load_dialog_respose`), the result is only used to print `"Cargados %zu bytes desde %s"` and then `free(buf)`. Loading a subject today **does not reconstruct any folder or file on disk**, it only confirms that the `.xz` decompresses correctly.

This was already documented in `docs/subject.c.md` ("Loading" and "Known implementation limitations" sections): *"neither platform's `_load_subject` parses the per-entry archive format back into individual files, and neither `_save_subject` nor `_load_subject` is wired into `main.c`/the UI yet."*

**Minor detail found, not previously documented:** in the Linux version, `subject.c:815`, `uint8_t *acc = LZMA_RUN;` initializes the pointer with the `lzma_action` enum constant (worth `0`) instead of `NULL`. It works because `0` is a valid null pointer constant in C, but it is confusing to read and fragile if the enum's order ever changed (although `LZMA_RUN` is guaranteed to be `0` by the liblzma API).

---

## 4. Proposed design: new private unpacking method

**Goal:** make `_load_subject`, after decompressing the `.xz`, actually unpack the content to disk, without changing `load_subject`'s public signature (it still returns `out_buf`/`out_size` as before; unpacking is a side effect before the `return`).

**Destination convention agreed with the user:** the destination directory is the same directory where the original `.xz` lives, with the same base name (the last extension is stripped, e.g. `math.xz` → folder `math`; with a double extension like `math.tar.xz` it would become `math.tar`, pending a decision on whether `.tar` should also be stripped).

### 4.1 Missing include (Linux)

```c
#else
   #include <linux/limits.h>
   #include <sys/wait.h>
   #include <sys/stat.h>   /* <-- new: for mkdir() */
#endif
```

### 4.2 Computing the destination directory

**Windows** (inside `#ifdef _WIN32`, before `_load_subject`):

```c
/* "C:\subjects\math.xz" -> dest_dir = "C:\subjects\math" */
static int _derive_extract_dir(const char *path, char *dest_dir, size_t dest_dir_size) {
    char *full = _fullpath(NULL, path, 0);
    if (!full) return -1;

    char *last_slash = strrchr(full, '\\');
    char base[MAX_PATH];
    snprintf(base, sizeof(base), "%s", last_slash ? last_slash + 1 : full);

    char *dot = strrchr(base, '.');
    if (dot != NULL && dot != base) *dot = '\0';

    int n = last_slash
        ? snprintf(dest_dir, dest_dir_size, "%.*s\\%s", (int)(last_slash - full), full, base)
        : snprintf(dest_dir, dest_dir_size, "%s", base);

    free(full);
    return (n < 0 || (size_t)n >= dest_dir_size) ? -1 : 0;
}
```

**Linux** (inside `#else`, before `_load_subject`):

```c
static int _derive_extract_dir(const char *path, char *dest_dir, size_t dest_dir_size) {
    char *full = realpath(path, NULL);
    if (!full) return -1;

    char *last_slash = strrchr(full, '/');
    char base[PATH_MAX];
    snprintf(base, sizeof(base), "%s", last_slash ? last_slash + 1 : full);

    char *dot = strrchr(base, '.');
    if (dot != NULL && dot != base) *dot = '\0';

    int n = last_slash
        ? snprintf(dest_dir, dest_dir_size, "%.*s/%s", (int)(last_slash - full), full, base)
        : snprintf(dest_dir, dest_dir_size, "%s", base);

    free(full);
    return (n < 0 || (size_t)n >= dest_dir_size) ? -1 : 0;
}
```

### 4.3 Unpacking (different implementation per platform)

**Linux — reuses real `tar` via `fork`+`exec`**, symmetric to how `_save_subject` generates it, but with `tar -xf -` reading from the pipe:

```c
static int _unpack_tar_buffer(Subject *self, const uint8_t *buf, size_t size, const char *dest_dir) {
    if (mkdir(dest_dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "mkdir(%s) failed: %s\n", dest_dir, strerror(errno));
        return -1;
    }

    int pipefd[2];
    if (pipe(pipefd) == -1) self->fatal("pipe");

    pid_t pid = fork();
    if (pid == -1) self->fatal("fork");

    if (pid == 0) {
        close(pipefd[1]);
        if (dup2(pipefd[0], STDIN_FILENO) == -1) self->fatal("dup2");
        close(pipefd[0]);
        execlp("tar", "tar", "-xf", "-", "-C", dest_dir, (char *)NULL);
        perror("execlp tar -xf");
        _exit(127);
    }

    close(pipefd[0]);
    size_t written = 0;
    while (written < size) {
        ssize_t w = write(pipefd[1], buf + written, size - written);
        if (w == -1) {
            if (errno == EINTR) continue;
            fprintf(stderr, "write to tar pipe failed: %s\n", strerror(errno));
            close(pipefd[1]);
            return -1;
        }
        written += (size_t)w;
    }
    close(pipefd[1]);

    int status;
    if (waitpid(pid, &status, 0) == -1) {
        fprintf(stderr, "waitpid failed: %s\n", strerror(errno));
        return -1;
    }
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}
```

There is no deadlock from writing the whole buffer before waiting: these are two separate processes, `tar` keeps reading from the pipe while the parent keeps writing.

**Windows — manual parsing of the custom format** (`u32 path_len + path + u64 size + content`, repeated). Little-endian readers, twins of `_write_u32_le`/`_write_u64_le` (which exist today but nothing calls: `_write_file_entry_to_lzma` builds the headers by hand):

```c
static uint32_t _read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t _read_u64_le(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= ((uint64_t)p[i]) << (8 * i);
    return v;
}
```

Helper to create intermediate subfolders (a `relpath` can come as `notas\semana1\archivo.txt`):

```c
static int _win_mkdir_p(const char *dir_path) {
    char tmp[MAX_PATH];
    snprintf(tmp, sizeof(tmp), "%s", dir_path);

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '\\') {
            *p = '\0';
            if (!CreateDirectoryA(tmp, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return -1;
            *p = '\\';
        }
    }
    return (!CreateDirectoryA(tmp, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) ? -1 : 0;
}
```

Unpacker, walking the buffer record by record with bounds checking at every step:

```c
static int _unpack_windows_buffer(Subject *self, const uint8_t *buf, size_t size, const char *dest_dir) {
    (void)self;
    size_t offset = 0;

    if (!CreateDirectoryA(dest_dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        fprintf(stderr, "CreateDirectoryA(%s) failed: %lu\n", dest_dir, (unsigned long)GetLastError());
        return -1;
    }

    while (offset < size) {
        if (offset + 4 > size) { fprintf(stderr, "truncated archive: length header\n"); return -1; }
        uint32_t rel_len = _read_u32_le(buf + offset);
        offset += 4;

        if (rel_len == 0 || rel_len >= MAX_PATH || offset + rel_len > size) {
            fprintf(stderr, "truncated or invalid relpath\n");
            return -1;
        }
        char relpath[MAX_PATH];
        memcpy(relpath, buf + offset, rel_len);
        relpath[rel_len] = '\0';
        offset += rel_len;

        if (offset + 8 > size) { fprintf(stderr, "truncated archive: size header\n"); return -1; }
        uint64_t filesize = _read_u64_le(buf + offset);
        offset += 8;

        if (offset + filesize > size) { fprintf(stderr, "truncated archive: content\n"); return -1; }

        char fullpath[MAX_PATH];
        if (snprintf(fullpath, sizeof(fullpath), "%s\\%s", dest_dir, relpath) < 0) return -1;

        char *last_slash = strrchr(fullpath, '\\');
        if (last_slash != NULL) {
            char parent[MAX_PATH];
            snprintf(parent, sizeof(parent), "%.*s", (int)(last_slash - fullpath), fullpath);
            if (_win_mkdir_p(parent) != 0) {
                fprintf(stderr, "mkdir(%s) failed\n", parent);
                return -1;
            }
        }

        FILE *out = fopen(fullpath, "wb");
        if (!out) { fprintf(stderr, "fopen(%s) failed\n", fullpath); return -1; }
        if (filesize > 0 && fwrite(buf + offset, 1, (size_t)filesize, out) != (size_t)filesize) {
            fprintf(stderr, "fwrite(%s) failed\n", fullpath);
            fclose(out);
            return -1;
        }
        fclose(out);
        offset += filesize;
    }
    return 0;
}
```

### 4.4 Wiring it into `_load_subject`

In both versions, where it currently reads:

```c
*out_buf = acc;
*out_size = acc_size;
return 0;
```

**Windows:**

```c
*out_buf = acc;
*out_size = acc_size;

char dest_dir[MAX_PATH];
if (_derive_extract_dir(path, dest_dir, sizeof(dest_dir)) != 0 ||
    _unpack_windows_buffer(self, acc, acc_size, dest_dir) != 0) {
    fprintf(stderr, "failed to unpack %s\n", path);
    return -9;
}
return 0;
```

**Linux:**

```c
*out_buf = acc;
*out_size = acc_size;

char dest_dir[PATH_MAX];
if (_derive_extract_dir(path, dest_dir, sizeof(dest_dir)) != 0 ||
    _unpack_tar_buffer(self, acc, acc_size, dest_dir) != 0) {
    fprintf(stderr, "failed to unpack %s\n", path);
    return -9;
}
return 0;
```

`load_subject` keeps returning the decompressed buffer exactly as before (`main.c` needs no immediate changes). If unpacking fails but decompression succeeded, the new `-9` code is returned.

---

## 5. Status and next steps

- [ ] The user will hand-type the code from section 4 (to understand the structure before copying it).
- [ ] Verify it compiles on both platforms (`make linux`, `make win64`, `make debug`).
- [ ] Test the full flow with `make gdb`: set breakpoints inside `_unpack_windows_buffer`/`_unpack_tar_buffer` to inspect `relpath`/`filesize` record by record.
- [ ] Decide whether `_derive_extract_dir` should also strip `.tar` from names like `math.tar.xz`.
- [ ] Document the new `-9` error code in the table in `docs/subject.c.md` once implemented and tested.
- [ ] Update `docs/subject.c.md` ("Known implementation limitations") to reflect that unpacking is no longer pending, once it is implemented and verified.

---

### cristalmirror IA secion ### [2026-09-23]

Log of an AI-assisted session (Claude Code) on StudyStudio. Covers a review of the uncommitted `footer` added to `interface.ui` and the design of a logging mechanism so every message printed by `subject.c` is shown in that footer. As in previous sessions, **no files were edited by the AI**: all code below is reference code pending manual implementation by the user.

---

## 1. Review of `interface.ui`

The uncommitted change added this block as a second child of `main_window`:

```xml
<child>
  <object class="GtkBox" id="footer">
    <property name>
  </object>
</child>
```

Problems found:

1. **Invalid XML (line 48):** `<property name>` has no attribute value and is never closed. `glib-compile-resources` does not validate XML (no `preprocess="xml-stripblanks"` in `resources.xml`), so the build may succeed but `gtk_builder_new_from_resource()` (`src/main.c:110`) aborts at runtime with a parse error.
2. **Wrong placement:** in GTK4 a `GtkWindow` accepts **a single child** (`gtk_window_set_child`). A second `<child>` replaces `main_box` or triggers a warning, and the buttons and list disappear. The footer must go **inside `main_box`**, after `scrolled_window`; since `main_box` is vertical and `scrolled_window` has `vexpand`, the footer stays at the bottom.
3. **Minor:** the window title is still `StudyStudio-0.0.12` while the project is at 0.0.13.

Suggested structure:

```xml
        <child>
          <object class="GtkScrolledWindow" id="scrolled_window">
            ...
          </object>
        </child>
        <child>
          <object class="GtkBox" id="footer">
            <property name="orientation">horizontal</property>
            <property name="spacing">10</property>
            <!-- footer widgets here, e.g. a GtkLabel -->
          </object>
        </child>
      </object>   <!-- end of main_box -->
    </child>
  </object>       <!-- end of main_window -->
```

Validation before compiling: `xmllint --noout interface.ui` (syntax) and `gtk4-builder-tool validate interface.ui` (GTK4 structure, if installed).

---

## 2. Sending `subject.c` output to the footer

`subject.c` has one `printf` (line 91) and ~30 `fprintf(stderr, ...)` calls. Chosen design: a **logging callback**, so `subject.c` stays independent of GTK. `main.c` registers a function that writes to the footer; if no logger is registered, messages still go to `stderr` as before.

### Step 1 — Interface in `include/subject.h`

Before the `#endif`:

```c
/* function that receives each message from subject.c */
typedef void (*SubjectLogFunc)(const char *msg, void *user_data);

void subject_set_logger(SubjectLogFunc fn, void *user_data);
```

### Step 2 — Implementation in `src/subject.c`

Add `#include <stdarg.h>` with the other includes, then:

```c
static SubjectLogFunc g_log_fn = NULL;
static void *g_log_data = NULL;

void subject_set_logger(SubjectLogFunc fn, void *user_data) {
    g_log_fn = fn;
    g_log_data = user_data;
}

/* replaces printf/fprintf: same format, configurable destination */
static void subject_log(const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (g_log_fn) g_log_fn(buf, g_log_data);
    else fputs(buf, stderr);   /* no GUI: previous behaviour */
}
```

Replace every call, e.g.:

```c
fprintf(stderr, "truncated archive: length header\n");   // before
subject_log("truncated archive: length header\n");       // after
```

**Exception:** the `perror` calls after `fork()` (lines 776 and 945) run in the **child process**, which has its own memory copy, so the callback/widget are not really usable there. They must keep going to `stderr` (reaching the GUI would require reading them through a pipe in the parent).

### Step 3 — Widget in `interface.ui`

```xml
<object class="GtkBox" id="footer">
  <property name="orientation">horizontal</property>
  <child>
    <object class="GtkLabel" id="footer_label">
      <property name="label">Listo</property>
      <property name="xalign">0</property>
      <property name="ellipsize">end</property>
    </object>
  </child>
</object>
```

A `GtkLabel` shows only the **last** message (status bar style). For a full **history**, use a non-editable `GtkTextView` inside a fixed-height `GtkScrolledWindow` and append to its `GtkTextBuffer`.

### Step 4 — Wiring in `src/main.c`

```c
/* callback that subject.c calls with each message */
static void on_subject_log(const char *msg, void *user_data) {
    GtkLabel *label = GTK_LABEL(user_data);
    char *clean = g_strchomp(g_strdup(msg));   /* strip trailing \n */
    gtk_label_set_text(label, clean);
    g_free(clean);
}
```

In `activate()`, after fetching the other widgets:

```c
GtkWidget *footer_label = GTK_WIDGET(gtk_builder_get_object(builder, "footer_label"));
subject_set_logger(on_subject_log, footer_label);
```

The `g_print` calls in `main.c` (e.g. "Cargados %zu bytes…") can also be changed to update the label.

### Caveats

- **Threads:** `load_subject` currently runs on the GTK main thread (inside the dialog callback), so updating the label directly is safe. If loading is moved to a worker thread, the callback must use `g_idle_add()`, since GTK may only be touched from the main thread.
- **Redraw:** while `load_subject` is busy the window does not redraw, so the label only shows the last message once it finishes. A worker thread would also fix this.

---

## 3. Extra bug found in `main.c`

`state->window` is never assigned in `activate()`, but `on_load_clicked` uses `GTK_WINDOW(state->window)`. `g_malloc` does not zero memory, so it holds garbage. Fix: add `state->window = window;`.

---

## 4. Status and next steps

- [ ] Fix the `footer` XML and move it inside `main_box`.
- [ ] Update the window title to 0.0.13.
- [ ] Add `SubjectLogFunc` / `subject_set_logger` to `subject.h` and `subject_log` to `subject.c`.
- [ ] Replace `printf`/`fprintf(stderr, ...)` in `subject.c` with `subject_log` (except the post-`fork()` `perror` calls).
- [ ] Register the logger in `activate()` and add `state->window = window;`.
- [ ] Decide between `GtkLabel` (last message) and `GtkTextView` (history) for the footer.

---

### cristalmirror IA secion ### [2026-09-24]

Log of an AI-assisted session on StudyStudio. Covers the implementation of the footer designed in the 2026-09-23 session. Unlike previous sessions, part of the code was applied by the AI at the user's request; the rest was typed by the user following the review below.

---

## 1. Compilation review

The first build of the footer code failed in `src/subject.c`:

1. **Type name mismatch:** `include/subject.h` declared `SubjectLogFun`, while `src/subject.c` used `SubjectLogFunc` (unknown type, and every call through `g_log_fn` failed). Fixed by renaming the typedef in the header to `SubjectLogFunc`.
2. **Implicit declaration:** `subject_log` was called from line 58 onwards but defined `static` near the end of the file, so GCC created an implicit non-`static` declaration that conflicted with the definition. Fixed with a forward declaration right after the includes: `static void subject_log(const char *fmt, ...);`.

## 2. Runtime review

1. **Footer replaced the whole UI:** it was a second child of `main_window`; moved inside `main_box` after `scrolled_window` (applied by the AI).
2. **Builder used after release:** `activate()` fetched `footer_label` after `g_object_unref(builder)`. Fixed by moving the lookup and `subject_set_logger` before the `unref`.
3. **Footer always showed `Listo`:** `_load_subject` only called `subject_log` on the `-9` path; success and the other errors returned silently, and the messages in `main.c` use `g_print` (terminal only). A `subject_log` call was added before every `return` of both `_load_subject` implementations (applied by the AI; messages in English). The Linux version was also missing the `return -4` after a failed buffer allocation.

## 3. Status and next steps

- [x] Fix the `footer` XML and move it inside `main_box`.
- [x] Update the window title (now 0.0.14).
- [x] Add `SubjectLogFunc` / `subject_set_logger` to `subject.h` and `subject_log` to `subject.c`.
- [x] Route `subject.c` diagnostics through `subject_log` (except the post-`fork()` `perror` calls).
- [x] Register the logger in `activate()`.
- [ ] Add `state->window = window;` in `activate()` (use `g_new0` for `AppState`).
- [ ] Move the unreachable `subject_log` calls after `return -1` in `_unpack_windows_buffer` before the `return`.
- [ ] Linux `_save_subject`: the `"/foo"` branch copies into `parent` instead of `base`; replace the `strncpy` calls with `snprintf`.
- [ ] Decide between `GtkLabel` (last message) and `GtkTextView` (history) for the footer.

---

### cristalmirror IA secion ### [2026-10-06] — next 0.1.0

Log of an AI-assisted session on StudyStudio. Covers the review of the untracked `include/edit.h` / `include/edit_subclass/` headers and the design of the `Edit` hierarchy (editing tools) targeted for **version 0.1.0**. The code below was **designed by the AI and is pending manual implementation** by the user. It was compiled in an isolated copy of the project (`make linux win64` inside `mi_app_builder`): both targets built with no new warnings. It has **not** been tested at runtime yet.

---

## 1. Review of the current state

| File | Problem |
| --- | --- |
| `include/edit.h` | No include guard, and missing `<stdbool.h>` (`bool`) and `<gtk/gtk.h>` (`GtkWidget`), so it does not compile once included. No standard file header. `EditPDF` is declared here instead of in `editPDF.h`. |
| `include/edit.h` (design) | **No virtual destructor:** `close` is common, but every subclass owns resources (a `GtkTextBuffer`, a PDF document...) that the base cannot know about. There is also no field for the widget created by `render`, so `close` cannot remove it from the UI. |
| `editDOC.h` | `#include <edit.h>` works thanks to `-Iinclude`, but the project uses relative paths (`"../include/subject.h"`). Missing guard, header, constructor prototype and trailing newline. |
| `editPDF.h`, `editUML.h` | Empty. |
| `Makefile` | `SRCS := $(wildcard $(SRC_DIR)/*.c)` is not recursive, so `src/edit_subclass/*.c` would never be compiled. `HEADERS := $(wildcard $(INCLUDE_DIR)/.h)` is wrong (missing `*`) and unused: changing a `.h` does not rebuild the `.o` files. |
| `docs/class_diagram.md` | Names `EditCode` / `EditDocs`, while the files are `editDOC` / `editPDF` / `editUML`; to be aligned once the names are settled. |

> [!WARNING]
> Because header changes do not trigger a rebuild, changing the layout of `struct Edit` can leave a stale `editDOC.o` using the old layout and silently corrupt memory. Until the dependency tracking is fixed (e.g. `-MMD -MP`), run **`make clean` after every `.h` change**.

## 2. Design: inheritance in C

Same pattern as `Subject`, plus inheritance:

```
EditDOC in memory:
┌───────────────────────────┐  <- (EditDOC *)doc == (Edit *)doc  (same address)
│ Edit base                 │     path, modified, widget, open, save, render...
├───────────────────────────┤
│ GtkTextBuffer *buffer     │  <- subclass-specific fields
│ gulong modified_id        │
└───────────────────────────┘
```

- **Upcast** (`EditDOC*` → `Edit*`): always safe. The C standard guarantees that a pointer to a struct also points to its first member, so `base` **must** be the first field.
- **Downcast** (`Edit*` → `EditDOC*`): only safe inside `EditDOC`'s own methods, because they are only assigned to real `EditDOC` objects.
- **Abstract methods:** `edit_init` leaves them `NULL` and each subclass constructor assigns them. Calling a `NULL` method crashes, which is the desired outcome if a subclass forgot to implement it.
- **Virtual destructor:** the common `close` calls the subclass's `self->finalize(self)`, then frees `path` and calls `free(self)`. That frees the **whole** `EditDOC`, because `self` points to the start of the block allocated with `calloc(sizeof(EditDOC))`.

## 3. `include/edit.h`

```c
/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.0
 * License: GPLv3
 * Last edited: 2026-10-06
 */

/*
 * Abstract base "class" for every editor of StudyStudio.
 * Each subclass embeds Edit as its FIRST member, calls edit_init()
 * and assigns its own open/save/render/finalize.
 */

#ifndef EDIT_H
#define EDIT_H

#include <stdbool.h>
#include <gtk/gtk.h>

typedef struct Edit Edit;

struct Edit {
    char *path;          /* owned copy of the file path (NULL until open) */
    bool modified;       /* true while there are unsaved changes */
    GtkWidget *widget;   /* root widget created by render (NULL until then) */

    /* abstract: assigned by each subclass */
    int  (*open)(Edit *self, const char *path);
    int  (*save)(Edit *self);
    void (*render)(Edit *self, GtkWidget *container);
    void (*finalize)(Edit *self);   /* frees subclass fields, may be NULL */

    /* common: implemented once in edit.c */
    void (*close)(Edit *self);
    bool (*is_modified)(Edit *self);
    void (*mark_modified)(Edit *self, bool modified);
    void (*set_path)(Edit *self, const char *path);
};

/* protected: only subclass constructors call it */
void edit_init(Edit *self);

/* factory: picks the subclass from the file extension and opens the file */
Edit *new_edit_for_path(const char *path);

#endif
```

## 4. `src/edit.c`

```c
/**
 * (standard header, same as edit.h)
 */

/*
 * Common operations of the Edit hierarchy and the factory that
 * chooses the right editor for a file.
 */
#include <stdlib.h>
#include <string.h>
#include "../include/edit.h"
#include "../include/edit_subclass/editDOC.h"

/* destructor: shared by every subclass */
static void _edit_close(Edit *self) {
    if (self == NULL) return;

    /* 1. take the widget out of the UI first, so no signal can reach us later */
    if (self->widget != NULL) {
        GtkWidget *parent = gtk_widget_get_parent(self->widget);
        if (parent != NULL && GTK_IS_BOX(parent)) {
            gtk_box_remove(GTK_BOX(parent), self->widget);
        }
        self->widget = NULL;
    }
    /* 2. let the subclass free its own fields (virtual destructor) */
    if (self->finalize != NULL) {
        self->finalize(self);
    }
    /* 3. free the base fields and the whole object */
    g_free(self->path);
    free(self);
}

static bool _edit_is_modified(Edit *self) {
    return self->modified;
}

static void _edit_mark_modified(Edit *self, bool modified) {
    self->modified = modified;
}

static void _edit_set_path(Edit *self, const char *path) {
    char *copy = g_strdup(path);   /* copy first: path may be self->path */
    g_free(self->path);
    self->path = copy;
}

void edit_init(Edit *self) {
    self->path = NULL;
    self->modified = false;
    self->widget = NULL;

    /* abstract: NULL until the subclass assigns them */
    self->open = NULL;
    self->save = NULL;
    self->render = NULL;
    self->finalize = NULL;

    /* common */
    self->close = _edit_close;
    self->is_modified = _edit_is_modified;
    self->mark_modified = _edit_mark_modified;
    self->set_path = _edit_set_path;
}

Edit *new_edit_for_path(const char *path) {
    const char *ext = strrchr(path, '.');
    Edit *edit = NULL;

    if (ext == NULL) return NULL;

    if (g_ascii_strcasecmp(ext, ".txt") == 0 ||
        g_ascii_strcasecmp(ext, ".md") == 0) {
        edit = new_edit_doc();
    }
    /* else if (... ".pdf") edit = new_edit_pdf(); */

    if (edit == NULL) return NULL;

    if (edit->open(edit, path) != 0) {
        edit->close(edit);
        return NULL;
    }
    return edit;
}
```

Notes:

- **`_edit_` / `_doc_` prefixes:** identifiers starting with `_` at file scope are reserved for the implementation in C, and MinGW already declares `_open` / `_close` in `<io.h>`. The prefix avoids cross-platform clashes.
- **`g_strdup` / `g_free` for `path`:** GLib is already linked, it is portable, and `g_strdup(NULL)` returns `NULL`. Rule: what is allocated with `g_*` is freed with `g_free`; what is allocated with `malloc` / `calloc` is freed with `free`.
- **Order in `close`:** widget first, then `finalize`. The other way round, a GTK signal could reach an already-freed `self`.

## 5. `include/edit_subclass/editDOC.h`

```c
/**
 * (standard header)
 */

/* Plain-text document editor (.txt, .md) based on GtkTextView. */

#ifndef EDIT_DOC_H
#define EDIT_DOC_H

#include "../edit.h"

typedef struct {
    Edit base;               /* must be the first member */
    GtkTextBuffer *buffer;   /* document text, we own one reference */
    gulong modified_id;      /* "modified-changed" handler id */
} EditDOC;

Edit *new_edit_doc(void);

#endif
```

The constructor returns `Edit *`, not `EditDOC *`, on purpose: the rest of the application should only know the `Edit` interface.

## 6. `src/edit_subclass/editDOC.c`

```c
/**
 * (standard header)
 */

/*
 * EditDOC: implementation of the abstract methods of Edit
 * for plain-text documents.
 */
#include <stdlib.h>
#include "../../include/edit_subclass/editDOC.h"

/* GTK calls this every time the buffer's "modified" flag flips */
static void _doc_on_modified_changed(GtkTextBuffer *buffer, gpointer user_data) {
    Edit *self = (Edit *)user_data;
    self->mark_modified(self, gtk_text_buffer_get_modified(buffer));
}

static int _doc_open(Edit *self, const char *path) {
    EditDOC *doc = (EditDOC *)self;   /* downcast: safe, base is first */
    char *contents = NULL;
    gsize len = 0;
    GError *error = NULL;

    if (!g_file_get_contents(path, &contents, &len, &error)) {
        g_printerr("EditDOC: cannot open %s: %s\n", path, error->message);
        g_clear_error(&error);
        return -1;
    }
    /* GtkTextBuffer only accepts valid UTF-8 and an int length */
    if (len > G_MAXINT || !g_utf8_validate(contents, (gssize)len, NULL)) {
        g_printerr("EditDOC: %s is not valid UTF-8 text\n", path);
        g_free(contents);
        return -2;
    }

    gtk_text_buffer_set_text(doc->buffer, contents, (int)len);
    g_free(contents);
    gtk_text_buffer_set_modified(doc->buffer, FALSE);  /* freshly loaded */
    self->set_path(self, path);
    return 0;
}

static int _doc_save(Edit *self) {
    EditDOC *doc = (EditDOC *)self;
    GtkTextIter start, end;
    GError *error = NULL;

    if (self->path == NULL) return -1;

    gtk_text_buffer_get_bounds(doc->buffer, &start, &end);
    char *text = gtk_text_buffer_get_text(doc->buffer, &start, &end, FALSE);

    /* atomic write: temp file + rename, never leaves a half-written file */
    gboolean ok = g_file_set_contents(self->path, text, -1, &error);
    g_free(text);

    if (!ok) {
        g_printerr("EditDOC: cannot save %s: %s\n", self->path, error->message);
        g_clear_error(&error);
        return -1;
    }
    gtk_text_buffer_set_modified(doc->buffer, FALSE);
    return 0;
}

static void _doc_render(Edit *self, GtkWidget *container) {
    EditDOC *doc = (EditDOC *)self;

    if (self->widget != NULL) return;          /* already rendered */
    if (!GTK_IS_BOX(container)) {
        g_printerr("EditDOC: container must be a GtkBox\n");
        return;
    }

    GtkWidget *view = gtk_text_view_new_with_buffer(doc->buffer);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), view);
    gtk_widget_set_vexpand(scroll, TRUE);

    gtk_box_append(GTK_BOX(container), scroll);
    self->widget = scroll;
}

/* virtual destructor: only frees what EditDOC added */
static void _doc_finalize(Edit *self) {
    EditDOC *doc = (EditDOC *)self;
    if (doc->buffer != NULL) {
        g_signal_handler_disconnect(doc->buffer, doc->modified_id);
        g_object_unref(doc->buffer);
        doc->buffer = NULL;
    }
}

/* constructor */
Edit *new_edit_doc(void) {
    EditDOC *doc = calloc(1, sizeof(EditDOC));  /* zeroed: every pointer NULL */
    if (doc == NULL) return NULL;

    edit_init(&doc->base);
    doc->base.open = _doc_open;
    doc->base.save = _doc_save;
    doc->base.render = _doc_render;
    doc->base.finalize = _doc_finalize;

    doc->buffer = gtk_text_buffer_new(NULL);
    doc->modified_id = g_signal_connect(doc->buffer, "modified-changed",
                                        G_CALLBACK(_doc_on_modified_changed), doc);
    return &doc->base;   /* upcast */
}
```

GTK concepts used:

- **Model / view:** `GtkTextBuffer` is the model (the text) and `GtkTextView` the view. The buffer is created in the constructor, so `open` works before `render` (a file can be loaded without being shown).
- **Reference counting:** `gtk_text_buffer_new` gives 1 reference and the view takes its own. When the view is destroyed it drops its reference; `g_object_unref` in `finalize` drops ours, and the buffer is freed when the count reaches 0.
- **`modified` without duplicated state:** `GtkTextBuffer` already tracks changes. Listening to `"modified-changed"` keeps `base.modified` in sync, and `save` resets it with `set_modified(FALSE)`.
- **`g_signal_handler_disconnect` in `finalize`:** the callback receives `doc` as `user_data`; if someone kept the buffer alive after `free(self)`, the next signal would use freed memory.

## 7. `Makefile`

```make
SRCS := $(wildcard $(SRC_DIR)/*.c) $(wildcard $(SRC_DIR)/edit_subclass/*.c)
```

And in the **4** rules `%.o: $(SRC_DIR)/%.c` (linux, linux-debug, win64, win64-debug), create the object's folder before compiling:

```make
$(BUILD_DIR)/linux/%.o: $(SRC_DIR)/%.c | setup
	@mkdir -p $(@D)
	$(CC_LINUX) $(CFLAGS_LINUX) -c $< -o $@
```

`%` matches `edit_subclass/editDOC`, so the object goes to `build/linux/edit_subclass/editDOC.o`. `gcc -o` does not create folders; `$(@D)` is the target's directory. The recipe line must start with a **TAB**.

## 8. Pending decisions: `EditPDF` and `EditUML`

- **PDF:** GTK4 cannot render PDF. The usual approach is **poppler-glib** (`poppler_document_new_from_file` + `poppler_page_render` on a `GtkDrawingArea` with cairo). It requires adding `poppler-glib-devel` and its MinGW equivalent to the `Dockerfile` (exact Fedora 40 MinGW package name not verified yet). Real PDF editing is out of scope; recommendation: a **viewer** (`save` returns "not supported", annotations at most).
- **UML:** (a) text-based UML (PlantUML / Mermaid) with `EditUML` **inheriting from `EditDOC`** (two-level inheritance, plus an optional preview) — recommended; or (b) a graphical editor with `GtkDrawingArea` + cairo + mouse gestures (much more work).
- **"DOC":** proposed as plain text / Markdown; `.docx` is not realistic without an external library.

## 9. Status and next steps (0.1.0)

- [ ] Write `include/edit.h` and `src/edit.c` (base class, `edit_init`, `new_edit_for_path`).
- [ ] Write `include/edit_subclass/editDOC.h` and `src/edit_subclass/editDOC.c`.
- [ ] Move `EditPDF` out of `edit.h` into `editPDF.h`.
- [ ] `Makefile`: include `src/edit_subclass/*.c` and add `@mkdir -p $(@D)` to the 4 object rules.
- [ ] `Makefile`: fix `HEADERS` / add header dependency tracking (`-MMD -MP`).
- [ ] Wire a button in `main.c` to open a `.txt` with `new_edit_for_path` and test at runtime.
- [ ] Decide the scope of `EditPDF` (poppler-glib viewer?) and `EditUML` (text-based, inheriting from `EditDOC`?).
- [ ] Align `docs/class_diagram.md` with the final class names.
- [ ] Bump to 0.1.0 when released: `Makefile` (`NAME`), headers, `README.md`, `CHANGELOG.md`.


---

### cristalmirror IA secion ### [2026-10-11] — next 0.1.0

Log of an AI-assisted session on StudyStudio. Covers the review of the first manual implementation of the `Edit` hierarchy (2026-10-06 design), the `Makefile` changes to compile `src/edit_subclass/`, and the design of a **drop-down editing panel per subject** in `main.c` with a new **`EditDOCX`** subclass (text-only `.docx` editing). The `Makefile` change was **applied by the AI at the user's request**; everything else is **reference code pending manual implementation**. That code was compiled in an isolated copy (`make linux win64` inside `studystudio-builder`, no new warnings) and `EditDOCX` was tested at runtime with a headless harness (open → edit → save → reopen). The GUI panel has **not** been tested by hand yet.

---

## 1. Compilation review of the `Edit` hierarchy

`src/edit.c` was already part of the build (`SRCS := $(wildcard src/*.c)`), so `main` did not compile:

| Error | Cause | Fix |
| --- | --- | --- |
| `too few arguments to function 'self->finalize'` | `edit.h` declared `finalize(Edit *, GtkWidget *)` | `void (*finalize)(Edit *self);` — a destructor needs no container |
| `implicit declaration of function 'strrr'` (+ `int` → pointer) | Typo; C89 implicit `int strrr()` | `strrchr(path, '.')` (last `.`: `notes.v2.md` → `.md`) |
| `'edit' undeclared` | Missing local | `Edit *edit = NULL;` (must be `NULL` if no extension matches) |
| `implicit declaration of function 'new_edit_doc'` | No prototype in `editDOC.h` | `Edit *new_edit_doc(void);` — `(void)` means "no arguments", `()` means "unspecified" |

Also: `editDOC.h` got an include guard, the relative include `"../edit.h"` (relative to the header itself) and the standard file header. Pending cosmetics: blank first line and missing trailing newline in `editDOC.h`, missing standard header in `editDOC.c`.

> [!NOTE]
> Without the prototype, the compiler assumes `new_edit_doc` returns `int`. On x86-64 that is a real bug, not just a warning: a pointer is 64 bits and an `int` 32, so the upper half of the address would be lost.

## 2. `Makefile`: compiling `src/edit_subclass/` (applied)

```make
SRCS := $(wildcard $(SRC_DIR)/*.c) $(wildcard $(SRC_DIR)/edit_subclass/*.c)
```

And in the 4 rules `%.o: $(SRC_DIR)/%.c` (linux, linux-debug, win64, win64-debug):

```make
$(BUILD_DIR)/linux/%.o: $(SRC_DIR)/%.c | setup
	@mkdir -p $(@D)
	$(CC_LINUX) $(CFLAGS_LINUX) -c $< -o $@
```

- `wildcard` is not recursive, hence the second pattern.
- `%` matches `edit_subclass/editDOC`, so the object goes to `build/linux/edit_subclass/editDOC.o`; `$(@D)` is the target's directory and `gcc -o` does not create folders.

Result: `make clean && make linux win64` builds both targets; only the two pre-existing `unused parameter` warnings in `subject.c` remain. `HEADERS` is still broken, so **`make clean` after every `.h` change** still applies.

## 3. Format decision: `.doc` / `.docx`

GTK4 cannot read Word files. `.docx` is a ZIP with XML inside (`word/document.xml`); `.doc` is a binary OLE format, not realistic to implement by hand. Options considered: open in an external app (`GtkFileLauncher`), **native text-only `.docx`**, convert through LibreOffice, or plain text only. **Chosen: native text-only `.docx`** with libarchive (already linked, reads/writes ZIP) + libxml2 (already installed as a GTK4 dependency, Linux and MinGW).

> [!WARNING]
> Saving rewrites the paragraphs as plain text: bold, styles and tables are **lost** (tables become plain paragraphs). Test with copies of real documents. `.doc` is not supported (the factory returns `NULL`).

## 4. Design: two-level inheritance `Edit → EditDOC → EditDOCX`

A `.docx` is edited exactly like a `.txt` (a `GtkTextBuffer` in a `GtkTextView`); only reading/writing the file differs. `EditDOCX` therefore inherits `render`, `finalize` and the buffer from `EditDOC` and overrides only `open` / `save`.

```
EditDOCX in memory:
┌───────────────────────────┐  <- (EditDOCX *) == (EditDOC *) == (Edit *)
│ EditDOC doc               │
│ ┌───────────────────────┐ │
│ │ Edit base             │ │     path, modified, widget, methods...
│ ├───────────────────────┤ │
│ │ GtkTextBuffer *buffer │ │
│ │ gulong modified_id    │ │
│ └───────────────────────┘ │
└───────────────────────────┘
```

Problem: `new_edit_doc()` allocates `sizeof(EditDOC)`, but `EditDOCX` needs its own size. Solution (same as `edit_init`): a **protected initializer** that fills already-allocated memory.

### `include/edit_subclass/editDOC.h` — before `new_edit_doc`

```c
/* protected: subclasses of EditDOC (e.g. EditDOCX) call it on their memory */
void edit_doc_init(EditDOC *doc);
```

### `src/edit_subclass/editDOC.c` — constructor split in two

```c
/* protected: fills an EditDOC that the caller already allocated */
void edit_doc_init(EditDOC *doc) {
    edit_init(&doc->base);
    doc->base.open = _doc_open;
    doc->base.save = _doc_save;
    doc->base.render = _doc_render;
    doc->base.finalize = _doc_finalize;

    doc->buffer = gtk_text_buffer_new(NULL);
    doc->modified_id = g_signal_connect(doc->buffer, "modified-changed",
                                        G_CALLBACK(_doc_on_modified_changed), doc);
}

/* constructor */
Edit *new_edit_doc(void) {
    EditDOC *doc = calloc(1, sizeof(EditDOC));  /* zeroed: every pointer NULL */
    if (doc == NULL) return NULL;

    edit_doc_init(doc);
    return &doc->base;   /* upcast */
}
```

## 5. Weak pointer for `Edit.widget`

When the window closes, GTK destroys every widget, including the editor's `GtkScrolledWindow`; `edit->widget` would then dangle and `_edit_close` would call `gtk_widget_get_parent()` on freed memory. `g_object_add_weak_pointer` makes GObject set the pointer to `NULL` when the object dies.

In `_doc_render`, after `self->widget = scroll;`:

```c
    /* GTK sets self->widget to NULL by itself if the widget is destroyed
     * (e.g. the window closes) before close() runs */
    g_object_add_weak_pointer(G_OBJECT(scroll), (gpointer *)&self->widget);
```

In `_edit_close` (`src/edit.c`), first line inside `if (self->widget != NULL) {`:

```c
        g_object_remove_weak_pointer(G_OBJECT(self->widget), (gpointer *)&self->widget);
```

It must be removed before `free(self)`, otherwise GObject would later write `NULL` into freed memory.

## 6. `include/edit_subclass/editDOCX.h`

```c
/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.0
 * License: GPLv3
 * Last edited: 2026-10-11
 */

/*
 * Word .docx editor (text only). Inherits from EditDOC: reuses its
 * GtkTextBuffer, render and finalize, and only replaces open/save.
 * Saving rewrites the paragraphs as plain text: formatting is lost.
 */
#ifndef EDIT_DOCX_H
#define EDIT_DOCX_H

#include "editDOC.h"

typedef struct {
    EditDOC doc;   /* must be the first member: Edit -> EditDOC -> EditDOCX */
} EditDOCX;

Edit *new_edit_docx(void);

#endif
```

## 7. `src/edit_subclass/editDOCX.c`

Structure of `word/document.xml`:

```
<w:body> <w:p> <w:r> <w:t>text</w:t> </w:r> </w:p> ... <w:sectPr/> </w:body>
         paragraph  run (piece with one format)        page setup (must stay last)
```

```c
/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.0
 * License: GPLv3
 * Last edited: 2026-10-11
 */

/*
 * EditDOCX: open/save of .docx files.
 * A .docx is a ZIP; the text lives in word/document.xml as
 *   <w:body> <w:p> <w:r> <w:t>text</w:t> </w:r> </w:p> ... <w:sectPr/> </w:body>
 * libarchive reads/writes the ZIP and libxml2 parses the XML.
 */
#include <stdlib.h>
#include <string.h>
#include <archive.h>
#include <archive_entry.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <glib/gstdio.h>
#include "../../include/edit_subclass/editDOCX.h"

#define DOCX_BODY_XML "word/document.xml"
#define DOCX_W_NS     "http://schemas.openxmlformats.org/wordprocessingml/2006/main"

/* true if node is the element <w:name> of the WordprocessingML namespace */
static bool _docx_is_w(xmlNode *node, const char *name) {
    return node->type == XML_ELEMENT_NODE && node->ns != NULL &&
           xmlStrcmp(node->ns->href, BAD_CAST DOCX_W_NS) == 0 &&
           xmlStrcmp(node->name, BAD_CAST name) == 0;
}

/* reads the data of the current archive entry; NULL on error */
static GByteArray *_docx_read_data(struct archive *a) {
    GByteArray *bytes = g_byte_array_new();
    char chunk[16384];
    la_ssize_t n;

    while ((n = archive_read_data(a, chunk, sizeof(chunk))) > 0) {
        g_byte_array_append(bytes, (const guint8 *)chunk, (guint)n);
    }
    if (n < 0) {
        g_byte_array_free(bytes, TRUE);
        return NULL;
    }
    return bytes;
}

/* returns a NUL-terminated copy of one entry of the zip (free with g_free) */
static char *_docx_read_entry(const char *zip_path, const char *name, size_t *out_len) {
    struct archive *a = archive_read_new();
    struct archive_entry *entry;
    char *data = NULL;

    archive_read_support_format_zip(a);
    if (archive_read_open_filename(a, zip_path, 10240) != ARCHIVE_OK) {
        g_printerr("EditDOCX: %s: %s\n", zip_path, archive_error_string(a));
        archive_read_free(a);
        return NULL;
    }
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (strcmp(archive_entry_pathname(entry), name) != 0) {
            archive_read_data_skip(a);
            continue;
        }
        GByteArray *bytes = _docx_read_data(a);
        if (bytes != NULL) {
            *out_len = bytes->len;
            g_byte_array_append(bytes, (const guint8 *)"", 1);   /* NUL */
            data = (char *)g_byte_array_free(bytes, FALSE);
        }
        break;
    }
    archive_read_free(a);
    return data;
}

/* parses word/document.xml of the zip; NULL if it is not a valid .docx */
static xmlDoc *_docx_load_xml(const char *zip_path, xmlNode **body_out) {
    size_t len = 0;
    char *text = _docx_read_entry(zip_path, DOCX_BODY_XML, &len);
    if (text == NULL || len > G_MAXINT) {
        g_printerr("EditDOCX: %s has no %s\n", zip_path, DOCX_BODY_XML);
        g_free(text);
        return NULL;
    }
    xmlDoc *xml = xmlReadMemory(text, (int)len, DOCX_BODY_XML, NULL, XML_PARSE_NONET);
    g_free(text);
    if (xml == NULL) return NULL;

    /* <w:document> -> <w:body> */
    xmlNode *root = xmlDocGetRootElement(xml);
    if (root != NULL && _docx_is_w(root, "document")) {
        for (xmlNode *n = root->children; n != NULL; n = n->next) {
            if (_docx_is_w(n, "body")) {
                *body_out = n;
                return xml;
            }
        }
    }
    g_printerr("EditDOCX: %s has no <w:body>\n", zip_path);
    xmlFreeDoc(xml);
    return NULL;
}

/* walks the XML tree: one line per <w:p>, also inside tables */
static void _docx_collect_text(xmlNode *node, GString *out) {
    for (xmlNode *n = node; n != NULL; n = n->next) {
        if (_docx_is_w(n, "pPr") || _docx_is_w(n, "rPr")) {
            continue;   /* properties: their <w:tab> are tab stops, not text */
        } else if (_docx_is_w(n, "t")) {
            xmlChar *t = xmlNodeGetContent(n);
            g_string_append(out, (const char *)t);
            xmlFree(t);
        } else if (_docx_is_w(n, "tab")) {
            g_string_append_c(out, '\t');
        } else if (_docx_is_w(n, "br") || _docx_is_w(n, "cr")) {
            g_string_append_c(out, '\n');
        } else {
            _docx_collect_text(n->children, out);
            if (_docx_is_w(n, "p")) g_string_append_c(out, '\n');
        }
    }
}

/* replaces the content of <w:body> by one plain <w:p> per line of text */
static void _docx_rebuild_body(xmlNode *body, const char *text) {
    xmlNs *w = body->ns;
    xmlNode *sect = NULL;   /* page setup: must stay as the last child */

    xmlNode *n = body->children;
    while (n != NULL) {
        xmlNode *next = n->next;   /* save it: n is freed below */
        if (_docx_is_w(n, "sectPr")) {
            sect = n;
        } else {
            xmlUnlinkNode(n);
            xmlFreeNode(n);
        }
        n = next;
    }

    char **lines = g_strsplit(text, "\n", -1);
    for (int i = 0; lines[i] != NULL; i++) {
        xmlNode *p = xmlNewNode(w, BAD_CAST "p");
        xmlNode *r = xmlNewChild(p, w, BAD_CAST "r", NULL);

        /* "a\tb" -> <w:t>a</w:t><w:tab/><w:t>b</w:t> */
        char **parts = g_strsplit(lines[i], "\t", -1);
        for (int j = 0; parts[j] != NULL; j++) {
            if (j > 0) xmlNewChild(r, w, BAD_CAST "tab", NULL);
            if (parts[j][0] != '\0') {
                /* xmlNewTextChild escapes &, < and > */
                xmlNode *t = xmlNewTextChild(r, w, BAD_CAST "t", BAD_CAST parts[j]);
                xmlNodeSetSpacePreserve(t, 1);   /* keep leading/trailing spaces */
            }
        }
        g_strfreev(parts);

        if (sect != NULL) xmlAddPrevSibling(sect, p);
        else xmlAddChild(body, p);
    }
    g_strfreev(lines);
}

/* copies the zip to path.tmp replacing one entry, then renames it over path */
static int _docx_write_zip(const char *path, const char *replace_name,
                           const void *data, size_t len) {
    char *tmp = g_strconcat(path, ".tmp", NULL);
    struct archive *in = archive_read_new();
    struct archive *out = archive_write_new();
    struct archive_entry *entry;
    int rc = 0;

    archive_read_support_format_zip(in);
    archive_write_set_format_zip(out);

    if (archive_read_open_filename(in, path, 10240) != ARCHIVE_OK ||
        archive_write_open_filename(out, tmp) != ARCHIVE_OK) {
        rc = -1;
    }

    int r = ARCHIVE_EOF;
    while (rc == 0 && (r = archive_read_next_header(in, &entry)) == ARCHIVE_OK) {
        GByteArray *bytes = _docx_read_data(in);
        if (bytes == NULL) {
            rc = -2;
            break;
        }
        const void *buf = bytes->data;
        size_t size = bytes->len;
        if (strcmp(archive_entry_pathname(entry), replace_name) == 0) {
            buf = data;   /* the new document.xml */
            size = len;
        }

        struct archive_entry *copy = archive_entry_clone(entry);
        archive_entry_set_size(copy, (la_int64_t)size);
        if (archive_write_header(out, copy) != ARCHIVE_OK ||
            (size > 0 && archive_write_data(out, buf, size) != (la_ssize_t)size)) {
            rc = -3;
        }
        archive_entry_free(copy);
        g_byte_array_free(bytes, TRUE);
    }
    if (rc == 0 && r != ARCHIVE_EOF) rc = -2;   /* broken input zip */

    if (rc != 0) {
        g_printerr("EditDOCX: cannot save %s: %s\n", path,
                   archive_error_string(rc == -3 ? out : in));
    }
    archive_read_free(in);
    if (archive_write_close(out) != ARCHIVE_OK && rc == 0) rc = -3;
    archive_write_free(out);

    if (rc == 0) {
        /* replace the original only now that the copy is complete */
        GFile *src = g_file_new_for_path(tmp);
        GFile *dst = g_file_new_for_path(path);
        GError *error = NULL;
        if (!g_file_move(src, dst, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error)) {
            g_printerr("EditDOCX: cannot replace %s: %s\n", path, error->message);
            g_clear_error(&error);
            rc = -4;
        }
        g_object_unref(src);
        g_object_unref(dst);
    }
    if (rc != 0) g_remove(tmp);
    g_free(tmp);
    return rc;
}

static int _docx_open(Edit *self, const char *path) {
    EditDOC *doc = (EditDOC *)self;   /* downcast to the parent class */
    xmlNode *body = NULL;

    xmlDoc *xml = _docx_load_xml(path, &body);
    if (xml == NULL) return -1;

    GString *text = g_string_new(NULL);
    _docx_collect_text(body->children, text);
    xmlFreeDoc(xml);

    /* the last <w:p> also added a '\n': drop it, or every save adds a paragraph */
    if (text->len > 0 && text->str[text->len - 1] == '\n') {
        g_string_truncate(text, text->len - 1);
    }
    if (text->len > G_MAXINT) {
        g_string_free(text, TRUE);
        return -2;
    }
    gtk_text_buffer_set_text(doc->buffer, text->str, (int)text->len);
    g_string_free(text, TRUE);

    gtk_text_buffer_set_modified(doc->buffer, FALSE);
    self->set_path(self, path);
    return 0;
}

static int _docx_save(Edit *self) {
    EditDOC *doc = (EditDOC *)self;
    xmlNode *body = NULL;
    GtkTextIter start, end;

    if (self->path == NULL) return -1;

    /* re-read the original document.xml to keep its namespaces and <w:sectPr> */
    xmlDoc *xml = _docx_load_xml(self->path, &body);
    if (xml == NULL) return -1;

    gtk_text_buffer_get_bounds(doc->buffer, &start, &end);
    char *text = gtk_text_buffer_get_text(doc->buffer, &start, &end, FALSE);
    _docx_rebuild_body(body, text);
    g_free(text);

    xmlChar *out = NULL;
    int out_len = 0;
    xmlDocDumpMemoryEnc(xml, &out, &out_len, "UTF-8");
    xmlFreeDoc(xml);
    if (out == NULL) return -1;

    int rc = _docx_write_zip(self->path, DOCX_BODY_XML, out, (size_t)out_len);
    xmlFree(out);

    if (rc == 0) gtk_text_buffer_set_modified(doc->buffer, FALSE);
    return rc;
}

/* constructor: build an EditDOC, then override what changes */
Edit *new_edit_docx(void) {
    EditDOCX *docx = calloc(1, sizeof(EditDOCX));
    if (docx == NULL) return NULL;

    edit_doc_init(&docx->doc);          /* buffer, render, finalize from EditDOC */
    docx->doc.base.open = _docx_open;   /* override */
    docx->doc.base.save = _docx_save;   /* override */
    return &docx->doc.base;             /* upcast to Edit */
}
```

Notes:

- **Namespaces:** elements are matched by `ns->href` (the URL), not by the `w:` prefix, which is chosen by whoever wrote the file.
- **`pPr` / `rPr` skipped:** inside `<w:tabs>` there are `<w:tab>` elements that are tab-stop positions, not text.
- **Trailing `\n`:** every `<w:p>` adds a newline; the last one is dropped on open, or each save would add an empty paragraph.
- **Save:** re-reads the original `document.xml` (keeps namespaces and `<w:sectPr>`), replaces the body, and copies every other ZIP entry (images, styles, `[Content_Types].xml`) unchanged.
- **Atomic write:** writes `file.docx.tmp` and only then moves it over the original with `g_file_move(..., G_FILE_COPY_OVERWRITE)`; plain `rename()` fails on Windows if the target exists.
- **Escaping:** `xmlNewTextChild` escapes `&`, `<`, `>`; `xmlNodeSetSpacePreserve` adds `xml:space="preserve"` so leading/trailing spaces survive.

## 8. Factory and build flags

`src/edit.c`:

```c
#include "../include/edit_subclass/editDOCX.h"
...
    else if (g_ascii_strcasecmp(ext, ".docx") == 0) {
        edit = new_edit_docx();
    }
```

`Makefile`: add `libxml-2.0` after `gtk4` in the 4 `pkgconf` lines (`GTK_CFLAGS_LINUX`, `GTK_LIBS_LINUX`, `GTK_CFLAGS_WIN`, `GTK_LIBS_WIN`), e.g.:

```make
GTK_LIBS_LINUX   := $(shell PKG_CONFIG_PATH="" pkgconf --libs gtk4 libxml-2.0)
```

`Dockerfile`: list `libxml2-devel` and `mingw64-libxml2` explicitly (today they only arrive as GTK4 dependencies). The CI builds from the `Dockerfile`, so no workflow change is needed.

## 9. `src/main.c`: drop-down editing panel per subject

Each "Materia Num" becomes a vertical box:

```
panel_box
├── [Materia Num #1]          <- click toggles the revealer
└── GtkRevealer (hidden)
    └── tools
        ├── [📄 Abrir] [💾 Guardar] [✖ Cerrar]
        └── editor_box        <- the Edit renders here
```

```c
#include "../include/edit.h"

/*this structure save the state of application*/
typedef struct {
    GtkWidget *destiny_content; //save the element
    GtkWidget *window; 
    GtkWidget *footer_label; /* status messages */
    int counter; //number of element(index)
} AppState;

/* one per "Materia Num" button: its drop-down editing tools */
typedef struct {
    AppState *state;
    GtkWidget *revealer;     /* shows / hides the tools */
    GtkWidget *editor_box;   /* where the open Edit renders itself */
    Edit *edit;              /* open document, NULL if none */
} SubjectPanel;
```

Replace `on_subject_clicked` and `on_add_clicked` with:

```c
/* writes a message in the footer */
static void app_status(AppState *state, const char *msg) {
    gtk_label_set_text(GTK_LABEL(state->footer_label), msg);
}

/* subject button: unfold / fold its editing tools */
static void on_subject_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    SubjectPanel *panel = (SubjectPanel *)user_data;
    GtkRevealer *revealer = GTK_REVEALER(panel->revealer);
    gtk_revealer_set_reveal_child(revealer, !gtk_revealer_get_reveal_child(revealer));
}

/* the file dialog finished: open the document inside the panel */
static void on_open_doc_finish(GObject *source, GAsyncResult *res, gpointer user_data) {
    SubjectPanel *panel = (SubjectPanel *)user_data;
    GError *error = NULL;

    GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), res, &error);
    if (file == NULL) {
        g_clear_error(&error);   /* cancelled */
        return;
    }
    char *path = g_file_get_path(file);
    g_object_unref(file);

    Edit *edit = new_edit_for_path(path);   /* picks EditDOCX, EditDOC... */
    if (edit == NULL) {
        app_status(panel->state, "Cannot open the document");
    } else {
        edit->render(edit, panel->editor_box);
        panel->edit = edit;
        app_status(panel->state, path);
    }
    g_free(path);
}

static void on_open_doc_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    SubjectPanel *panel = (SubjectPanel *)user_data;

    if (panel->edit != NULL) {
        app_status(panel->state, "Close the current document first");
        return;
    }

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Documents (.docx, .txt, .md)");
    gtk_file_filter_add_suffix(filter, "docx");
    gtk_file_filter_add_suffix(filter, "txt");
    gtk_file_filter_add_suffix(filter, "md");

    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Abrir documento");
    gtk_file_dialog_set_default_filter(dialog, filter);
    gtk_file_dialog_open(dialog, GTK_WINDOW(panel->state->window), NULL,
                         on_open_doc_finish, panel);
    g_object_unref(filter);
    g_object_unref(dialog);
}

static void on_save_doc_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    SubjectPanel *panel = (SubjectPanel *)user_data;

    if (panel->edit == NULL) return;
    if (panel->edit->save(panel->edit) == 0) {
        app_status(panel->state, "Document saved");
    } else {
        app_status(panel->state, "Error saving the document");
    }
}

static void on_close_doc_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    SubjectPanel *panel = (SubjectPanel *)user_data;

    if (panel->edit == NULL) return;
    if (panel->edit->is_modified(panel->edit)) {
        app_status(panel->state, "Unsaved changes: save before closing");
        return;
    }
    panel->edit->close(panel->edit);   /* removes its widget and frees it */
    panel->edit = NULL;
    app_status(panel->state, "Document closed");
}

/* runs when the panel's box is finalized (e.g. the window closes) */
static void subject_panel_free(gpointer data) {
    SubjectPanel *panel = (SubjectPanel *)data;
    if (panel->edit != NULL) panel->edit->close(panel->edit);
    g_free(panel);
}

/* helper: a button connected to a SubjectPanel callback */
static GtkWidget *panel_button(const char *label, GCallback cb, SubjectPanel *panel) {
    GtkWidget *button = gtk_button_new_with_label(label);
    g_signal_connect(button, "clicked", cb, panel);
    return button;
}

/*
  this function is a callback that is executed
  when you press 'Add' button.
*/
static void on_add_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    // search our state in usaer_data
    AppState *state = (AppState *)user_data;
    state->counter++;

    SubjectPanel *panel = g_new0(SubjectPanel, 1);
    panel->state = state;

    /* toolbar: open / save / close */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(toolbar), panel_button("📄 Abrir", G_CALLBACK(on_open_doc_clicked), panel));
    gtk_box_append(GTK_BOX(toolbar), panel_button("💾 Guardar", G_CALLBACK(on_save_doc_clicked), panel));
    gtk_box_append(GTK_BOX(toolbar), panel_button("✖ Cerrar", G_CALLBACK(on_close_doc_clicked), panel));

    /* the Edit appends its own widget here; fixed height because we are
     * already inside a GtkScrolledWindow */
    panel->editor_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(panel->editor_box, -1, 300);

    GtkWidget *tools = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_append(GTK_BOX(tools), toolbar);
    gtk_box_append(GTK_BOX(tools), panel->editor_box);

    panel->revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(panel->revealer),
                                     GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_child(GTK_REVEALER(panel->revealer), tools);

    // Make the new element: the subject button + its hidden tools
    char *text_label = g_strdup_printf("Materia Num #%d", state->counter);
    GtkWidget *new_subject = gtk_button_new_with_label(text_label);
    g_signal_connect(new_subject, "clicked", G_CALLBACK(on_subject_clicked), panel);
    g_free(text_label);

    GtkWidget *panel_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_append(GTK_BOX(panel_box), new_subject);
    gtk_box_append(GTK_BOX(panel_box), panel->revealer);
    /* the panel lives as long as its box: GTK frees it for us */
    g_object_set_data_full(G_OBJECT(panel_box), "panel", panel, subject_panel_free);

    // add the element to the destiny box
    gtk_box_append(GTK_BOX(state->destiny_content), panel_box);
}
```

And in `activate()`, right after fetching `footer_label`:

```c
    state->footer_label = footer_label;
```

GTK concepts:

- **`GtkRevealer`:** single-child container that shows/hides it with an animation (`SLIDE_DOWN`); this is the "drop-down".
- **One `SubjectPanel` per button:** each subject has its own state (which document is open), so `panel` — not the global `AppState` — is the `user_data`.
- **`g_object_set_data_full`:** ties the panel's lifetime to `panel_box`; when GTK finalizes the box (window closed) it calls `subject_panel_free`. By then `edit->widget` is already `NULL` thanks to the weak pointer (section 5), so `close` does not touch dead memory.
- **`gtk_widget_set_size_request(editor_box, -1, 300)`:** the editor's `GtkScrolledWindow` lives inside another one (`scrolled_window`); without a fixed height it gets its minimum height (~0) and is invisible.
- **Closing with unsaved changes** is refused with a footer message; a future `GtkAlertDialog` (Save / Discard / Cancel) would be nicer.

## 10. Verification

Headless harness linked against `edit.o` and `edit_subclass/*.o`, with a generated `.docx` containing bold text, a `<w:tabs>` tab stop, `&`, `ñ`, a `<w:tab/>`, a table, an empty paragraph, `<w:sectPr>` and a 25 600-byte binary entry:

```
--- read ---
[Hola mundo & ñandú
col1	col2
celda
]
after insert modified=1
save rc=0 modified=0
--- reread ---
[Hola mundo & ñandú
col1	col2
celda

linea nueva <x> & "y"]
```

The saved ZIP passed `zipfile.testzip()`, the binary entry was byte-identical and `<w:sectPr>` stayed as the last child of `<w:body>`.

> [!NOTE]
> A first run printed `save rc=0 modified=1`: the test did `printf("...", e->save(e), e->is_modified(e))`, and **the evaluation order of function arguments is unspecified in C**, so `is_modified` ran before `save`. Sequencing the calls (`int rc = e->save(e);` first) gave `modified=0`. The bug was in the test, not in `EditDOCX`.

## 11. Status and next steps (0.1.0)

- [x] Write `include/edit.h` and `src/edit.c` (base class, `edit_init`, `new_edit_for_path`).
- [x] Write `include/edit_subclass/editDOC.h` and `src/edit_subclass/editDOC.c`.
- [x] `Makefile`: include `src/edit_subclass/*.c` and add `@mkdir -p $(@D)` to the 4 object rules.
- [ ] `editDOC.h` / `editDOC.c`: standard header, remove blank first line, trailing newline.
- [ ] `edit_doc_init` + weak pointer in `editDOC.c` / `edit.c` (sections 4–5).
- [ ] Write `editDOCX.h` / `editDOCX.c` and add `.docx` to the factory.
- [ ] `Makefile`: `libxml-2.0` in the 4 `pkgconf` lines; `Dockerfile`: `libxml2-devel`, `mingw64-libxml2`.
- [ ] `main.c`: `SubjectPanel`, `GtkRevealer` toolbar, `footer_label` in `AppState`; test the GUI by hand on Linux and Windows.
- [ ] `interface.ui`: raise `default-width` / `default-height` (e.g. 700×600) so the editor fits.
- [ ] `Makefile`: fix `HEADERS` / add `-MMD -MP` dependency tracking.
- [ ] Optional: `GtkAlertDialog` for closing with unsaved changes.
- [ ] Move `EditPDF` out of `edit.h` into `editPDF.h`; decide the scope of `EditPDF` / `EditUML`.
- [ ] Align `docs/class_diagram.md` with the final class names (`EditDOC`, `EditDOCX`, ...).
- [ ] Bump to 0.1.0 when released: `Makefile` (`NAME`), headers, `README.md`, `CHANGELOG.md`.

---

### cristalmirror IA secion ### [2026-10-12] — next 0.1.0

Log of an AI-assisted session on StudyStudio. Decision: LibreOffice is embedded through **LibreOfficeKit (LOK)** as the engine for every office format, and all the LOK code lives in `editDOC.h` / `editDOC.c`. At the user's explicit request ("implementa los cambios") **the AI implemented this session's changes directly**. `make clean && make linux win64` inside `mi_app_builder` builds and links both targets with no new warnings. Nothing has been run yet.

---

## 1. LOK spike (2026-10-11, scratchpad, not in the repo)

A standalone C program against the host's LibreOffice 26.8 (`/usr/lib/libreoffice/program`) proved that:

- `lok_init` + `documentLoad` of a `.docx` work from pure C.
- `paintTile` renders the page correctly (BGRA buffer converted to PNG).
- `saveAs(..., "docx", NULL)` keeps the formatting.

Problems found:

| Problem | Resolution |
| --- | --- |
| `lokdocview` (LibreOffice's ready-made widget) is GTK3, unusable with GTK4. | Own widget in `EditDOC`: `GtkDrawingArea` + `paintTile` of the visible rectangle. |
| `LibreOfficeKitEnums.h` does not compile as C (`static_cast`, `nullptr`). | Vendored, patched headers (section 3). |
| SIGSEGV at exit after `office->destroy()`, in an `atexit` handler of `libswlo` (`desktop_LOKClipboard_get_implementation`). | `s_office` is never destroyed. To verify at run time. |
| Key events are asynchronous: `saveAs` ran before the typed key was applied. | Phase 2: drive the UI from LOK callbacks instead of assuming synchronous edits. |
| Windows LOK untested. | Phase 4. |

## 2. Alternatives considered

- **Rewrite the UI in C++/Qt6:** rejected for LOK alone. C++ only removes the header problem: there is no Qt LOK widget either, the exit crash does not depend on the toolkit, and the project goal is learning C + GTK4.
- **C++ glue file (`lok_glue.cpp`) with an `extern "C"` API:** unnecessary once the three C++ spots in the headers are patched.
- **Text-only `EditDOCX` (libarchive + libxml2), designed on 2026-10-11:** superseded by `EditDOC`; kept in this log only as a possible fallback when LibreOffice is missing.

## 3. Changes made

- `editDOC.h`/`editDOC.c` (the `GtkTextView` editor) renamed to `editTXT.h`/`editTXT.c` (`EditTXT`, `new_edit_txt`); standard header added, leading blank line removed, trailing newline added.
- New `EditDOC` (phase 1, view + save): see [editDOC.c](editDOC.c.md).
- `include/third_party/LibreOfficeKit/`: LOK 24.2 headers (MPL-2.0) with `StudyStudio:` patches: `lokCallbackTypeToString()` under `#ifdef __cplusplus`; C casts instead of the two Windows `reinterpret_cast` in `LibreOfficeKitInit.h`. `editDOC.c` includes `<stdbool.h>` first because `LibreOfficeKit.h` uses `bool` without it (MinGW error).
- `Makefile`: `INCLUDES := -I$(INCLUDE_DIR) -I$(INCLUDE_DIR)/third_party`. No link flags needed.
- `src/edit.c`: factory maps `.txt`/`.md` → `EditTXT` and office extensions → `EditDOC` (only if `edit_doc_available()`).
- Docs: `class_diagram.md` reviewed against the code and updated; new `edit.c.md`, `editTXT.c.md`, `editDOC.c.md`; `docs/README.md`, `docs/Makefile.md`, `README.md`, `CHANGELOG.md`.

## 4. LOK phases

1. **View + save** — done (compiles; not run).
2. **Input:** `postKeyEvent` / `postMouseEvent` from GTK controllers; `registerCallback` with `LOK_CALLBACK_INVALIDATE_TILES` and `DOCUMENT_SIZE_CHANGED`, forwarded to the GTK thread with `g_idle_add`; `mark_modified` from callbacks.
3. **Toolbar:** `postUnoCommand` (`.uno:Bold`, `.uno:Undo`, ...), save via `.uno:Save` if `saveAs` over the opened file fails.
4. **Windows:** install path detection and runtime test; Calc/Impress parts (`setPart`).

## 5. Status and next steps (0.1.0)

- [x] `editDOC.h` / `editDOC.c` → `editTXT`: standard header, blank first line, trailing newline.
- [x] `EditDOC` phase 1 on LibreOfficeKit; vendored headers; factory; `Makefile`.
- [x] `docs/class_diagram.md` aligned with the code.
- [x] `main.c` subject menu + panel; weak pointer (`set_widget`) — see section 6.
- [ ] Runtime test of `EditDOC`: open a `.docx`, scroll, save, close the app (exit crash?).
- [ ] LOK phase 2 (input + callbacks).
- [x] `interface.ui`: larger default size (900×700).
- [ ] `Makefile`: `-MMD -MP`.
- [x] Bump to 0.1.0 (2026-10-12).

## 6. Subject menu: choose the kind of document to edit

Requested by the user: pressing a subject button opens a menu to pick what to edit (PDF, .doc, .txt, .uml) and loads the matching editor, starting with the two that exist. Designed in chat, then **implemented by the AI at the user's request ("hazlo tú")**. `make clean && make linux win64` builds both targets with no new warnings; the class diagram parses with Mermaid 11.4.1. Not run yet.

```
[Materia Num #1 ▾]                 GtkMenuButton
   ├ 📝 Texto (.txt, .md)          -> EditTXT
   ├ 📄 Documento (.docx, ...)     -> EditDOC (if LibreOffice is found)
   ├ 📕 PDF (próximamente)         greyed out
   └ 📐 UML (próximamente)         greyed out
GtkRevealer: [💾 Guardar] [✖ Cerrar] + editor_box (500 px)
```

Design points:

- **`EditorKind` table** (`s_editor_kinds` in `main.c`): label, filter name, suffix list, constructor, optional `available()`. Adding `EditPDF` means filling its row (open/closed).
- **`edit_txt_suffixes` / `edit_doc_suffixes`**, exported by each subclass without the dot (the form `gtk_file_filter_add_suffix` expects): one list per format, shared by the factory and the menu.
- **Popover of frameless buttons instead of `GMenu` + `GAction`:** each entry calls a C callback directly; the row pointer travels as object data `"kind"`.
- **`available()` is checked on click**, not when building the menu, because `lok_init` takes a second or two.
- **`Edit.set_widget()`** (common method, weak pointer) replaces the per-subclass weak pointer of the 2026-10-11 section 5: the panel is freed through `g_object_set_data_full` after GTK has destroyed the editor's widget, so `close()` must not touch it.

