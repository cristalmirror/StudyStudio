# AI Prototype — Work session (2026-09-20)

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
