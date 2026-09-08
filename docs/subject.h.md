# `include/subject.h`

Status: draft for user approval; version 0.0.7.

## Purpose

Declares `Subject`, its public fields and operation pointers, and the `new_subject(int value)` constructor. Defines input/output buffer sizes of 65,536 bytes.

The Windows archive traversal helpers are deliberately not part of this public
interface. `WalkContext`, `_walk_directory`, `_write_file_entry_to_lzma`, and
`_feed_bytes` are private implementation details of `src/subject.c`.

## Current interface

| Member or function | Current contract |
| --- | --- |
| `val` | Integer initialized from the constructor argument; no domain meaning is established yet. |
| `pid` | Child-process identifier: `HANDLE` on Windows, `pid_t` elsewhere. |
| `fatal(msg)` | Default implementation prints an error and terminates the process. |
| `read_subject(self)` | Prints `val`; does not load an archive. |
| `wait_pid_os_opt(self, status)` | Waits for the child; returns `0` on API success or `-1` on API failure. Optional status receives `0` for child success, `1` otherwise. |
| `load_subject(self, path, out_buf, out_size)` | Decodes XZ data into an allocated memory buffer; returns an integer result. See [implementation details](subject.c.md). |
| `save_subject(self, msg, dir, outpath)` | Attempts to archive a directory and compress it to the destination. Returns no success/failure value; `msg` is unused. |
| `close_subject(self)` | Frees the object; accepts `NULL` in the default implementation. |
| `new_subject(value)` | Returns a heap-allocated object with default operations, or `NULL` on allocation failure. |

No public Windows-specific archive traversal method is currently exposed.
Callers must use `save_subject`; they must not depend on the internal archive
format or its helper structures.

Although there is a forward typedef, the full structure is public: consumers can access and modify its state and operation pointers. The header also exposes platform headers and defines feature-test macros after other includes, which may be too late to affect declarations.

## Ownership

The constructor transfers ownership to the caller. Destroy an object once and do not access it afterward. A successfully returned load buffer belongs to the caller and must be freed separately. Destroying a subject does not wait for a process or release an OS process handle.

## SOLID development direction

Follow the [project SOLID guidelines](README.md#development-direction-solid). Make subject data opaque, remove process and compression details from this interface, and separate storage operations from the domain model. Define consistent error and ownership contracts before supporting interchangeable implementations. Operation pointers alone do not establish SOLID compliance.
