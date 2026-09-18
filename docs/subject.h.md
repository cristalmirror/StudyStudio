# `include/subject.h`

Status: draft for user approval; version 0.0.9.

## Purpose

Declares `Subject`, its public fields and operation pointers, and the `new_subject(int value)` constructor. Defines input/output buffer sizes of 65,536 bytes.

The header is self-contained as of this version: it includes `<stdint.h>`,
`<stdio.h>`, `<sys/types.h>`, and `<lzma.h>` unconditionally, and `<windows.h>`
under `_WIN32`. Previously it relied on `subject.c` having already included
those headers first, which broke any other translation unit (such as
`main.c`) that included `subject.h` on its own.

The Windows archive traversal helpers are deliberately not part of the public
*storage* interface, but on Windows they are exposed as operation pointers on
`Subject` itself (see below) so `subject.c` can invoke them as methods
(`self->walk_directory(...)`, etc.) instead of calling the static functions
directly. `WalkContext` is a private implementation detail of `src/subject.c`
used only to carry the output file and the active LZMA stream through the
recursive walk.

As of this version, `_walk_directory` itself is no longer forward-declared in
this header. It never needed to be: `src/subject.c` defines it before its
only use (`new_subject`'s Windows branch), and the declaration's only real
effect was leaking a private, `static` implementation detail into every other
translation unit that includes `subject.h` — including `main.c` on the
Windows build, which does not define or call it, and so saw it flagged as
declared but never defined.

## Current interface

| Member or function | Current contract |
| --- | --- |
| `val` | Integer initialized from the constructor argument; no domain meaning is established yet. |
| `proc_handle` (Windows) / `pid` (elsewhere) | Child-process identifier. As of this version these are two distinct struct fields selected by `#ifdef _WIN32`, not a single field holding either a `HANDLE` or a `pid_t`: `proc_handle` is a Win32 `HANDLE`, `pid` is a POSIX `pid_t`. |
| `is_dot_or_dotdot(self, name)` (Windows only) | Returns non-zero when `name` is `.` or `..`, to be skipped while walking a directory. |
| `write_u32_le(self, f, v)` (Windows only) | Writes a `uint32_t` to `f` in little-endian order. Currently assigned but not called from anywhere in `_save_subject`'s write path, which instead packs headers by hand before handing them to `feed_bytes`. |
| `feed_bytes(strm, outbuf, out_buf_size, outfile, data, len)` (Windows only) | Pushes `data` through the active LZMA encoder with `LZMA_RUN` and writes any produced compressed bytes to `outfile`. Does not finish the stream. |
| `walk_directory(self, base_path, rel_prefix, ctx)` (Windows only) | Recursively enumerates `base_path` with `FindFirstFileA`/`FindNextFileA` and writes each regular file's entry through `feed_bytes`. |
| `fatal(msg)` | Default implementation prints an error and terminates the process. |
| `read_subject(self)` | Prints `val`; does not load an archive. |
| `wait_pid_os_opt(self, status)` | Waits for the child; returns `0` on API success or `-1` on API failure. Optional status receives `0` for child success, `1` otherwise. Uses `self->proc_handle` on Windows and `self->pid` elsewhere. |
| `load_subject(self, path, out_buf, out_size)` | Decodes XZ data into an allocated memory buffer; returns an integer result. See [implementation details](subject.c.md). |
| `save_subject(self, msg, dir, outpath)` | Archives a directory and compresses it to the destination. Returns no success/failure value; `msg` is unused on both platforms. |
| `close_subject(self)` | Frees the object; accepts `NULL` in the default implementation. |
| `new_subject(value)` | Returns a heap-allocated object with default operations, or `NULL` on allocation failure. |

Callers must use `save_subject`/`load_subject`; they must not depend on the
internal archive format or `WalkContext`.

Although there is a forward typedef, the full structure is public: consumers can access and modify its state and operation pointers. The header still defines its feature-test macros (`_POSIX_C_SOURCE`, `_GNU_SOURCE`) after its own includes, which may be too late to affect those specific headers; other translation units that include system headers before `subject.h` are unaffected by this ordering.

## Ownership

The constructor transfers ownership to the caller. Destroy an object once and do not access it afterward. A successfully returned load buffer belongs to the caller and must be freed separately. Destroying a subject does not wait for a process or release an OS process handle.

## SOLID development direction

Follow the [project SOLID guidelines](README.md#development-direction-solid). Make subject data opaque, remove process and compression details from this interface, and separate storage operations from the domain model. Define consistent error and ownership contracts before supporting interchangeable implementations. Operation pointers alone do not establish SOLID compliance.
