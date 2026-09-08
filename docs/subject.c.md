# `src/subject.c`

Status: draft for user approval; version 0.0.7.

## Purpose and dependencies

Implements subject construction/destruction, diagnostic output, process waiting,
and XZ archive handling through liblzma. Linux currently archives directories
with external `tar`; Windows has an in-progress native directory traversal and
custom entry serializer.

## Lifecycle and helper operations

`new_subject` allocates the object, initializes `val` and `pid`, and assigns all operation pointers. `_close_subject` frees the object only. `_read_subject` prints its integer value. `_fatal` calls `perror` and `exit(EXIT_FAILURE)`.

`_wait_pid_os_opt` uses `waitpid` on POSIX, retrying interrupted waits, or `WaitForSingleObject` and optionally `GetExitCodeProcess` on Windows. It returns `-1` on API failure and `0` otherwise. When requested, it normalizes child status to `0` for success and `1` for failure.

## Saving

`_save_subject(self, msg, dir, outpath)` expects pointers to directory and destination path strings. It does not validate these arguments; `msg` is unused.

Linux saving currently has the working implementation:

1. Creates a pipe and forks a child.
2. Resolves the directory and executes `tar -cf - -C <parent> <base>` in the child, sending output to the pipe.
3. Opens the destination and encodes pipe data with liblzma preset `6 | LZMA_PRESET_EXTREME` and CRC64.
4. Writes compressed bytes, releases resources on the normal path, waits for the child, and prints a completion message when successful.

The Linux output is an XZ-compressed TAR stream. Errors are inconsistently handled through process termination, diagnostic output, or an early return. The function returns `void`, so callers cannot reliably detect success. Opening with `wb` can overwrite an existing file, and failures can leave a partial output.

### Windows saving: in progress

Windows does not invoke a complete save workflow yet. The private helpers in
`subject.c` are being prepared for a native implementation that does not depend
on `tar`:

- `_walk_directory(self, base_path, rel_prefix, ctx)` recursively enumerates
  files with `FindFirstFileA` and `FindNextFileA`.
- `_write_file_entry_to_lzma(...)` serializes one regular file and feeds it to
  the active LZMA encoder.
- `_feed_bytes(self, ...)` is the private method responsible for passing input
  bytes to liblzma and writing generated compressed bytes to the destination.

Each file entry uses this private binary representation before compression:

```text
u32 little-endian route length
route bytes
u64 little-endian file size
file bytes
```

Directories themselves are not serialized; they are represented only through
the relative paths of their files. Reparse points are rejected to prevent
recursive cycles.

This is not yet a stable archive format: it has no magic value, version,
end-of-archive marker, or matching Windows loader.
### Warning
This operation have two implementations, one for linux system, other for Windows. The windows implementatons are defined, but not maked logics.

## Loading

`_load_subject(self, path, out_buf, out_size)` reads an XZ file, decodes it with concatenated streams enabled, and accumulates decompressed bytes in a dynamically allocated buffer. `self` is unused. This operation does not extract TAR entries or construct a subject from the decoded bytes.

For valid output pointers, output values are initialized to `NULL` and zero before opening the file. On success, the caller owns the returned buffer and must use `free` after consumption. Inputs and path strings remain caller-owned.

| Result | Meaning in the current implementation |
| --- | --- |
| `0` | Accepted decoding result; output buffer and length are assigned. |
| `-1` | Missing path or output pointer. |
| `-2` | Input file could not be opened. |
| `-3` | Decoder initialization failed. |
| `-5` | Final mapped decoder/allocation memory error. |
| `-6` | XZ format error. |
| `-7` | Data error, also used for an input read failure. |
| `-8` | Other decoder error. |

These return codes describe the current implementation, not a complete or reliable error contract for every failure path.
### Warning
This operation have two implementations, one for linux system, other for Windows. The windows implementatons are defined, but not maked logics.
## Known implementation limitations

- Windows saving and loading are incomplete. The native Windows traversal and
  entry writer are private implementation work and are not called by
  `_save_subject` yet.
- The Windows entry format currently records only files, so empty directories
  cannot be restored.
- `MAX_PATH` and `FindFirstFileA` limit Windows paths and do not provide full
  Unicode-path support.
- The root-level directory path branch copies the base name into `parent` instead of `base`, leaving `base` uninitialized.
- The encoding loop can replace pending input before liblzma has consumed it all.
- Several save error paths leave the read side of the pipe open; some final I/O results are not checked.
- If initial load-buffer allocation fails, cleanup runs but execution continues with released resources instead of returning.
- Decoder errors are not all handled with an immediate loop exit, and final acceptance includes `LZMA_OK` instead of requiring `LZMA_STREAM_END`.
- Decoded data is accumulated without an application size limit, and capacity growth lacks overflow checks.

## SOLID development direction

Follow the [project SOLID guidelines](README.md#development-direction-solid). Separate subject data, archive storage, compression, and platform process handling into cohesive modules. Have application workflows consume a small storage contract and supply concrete implementations from the composition point. Keep platform handles private. Return structured or consistently defined errors to callers rather than terminating the application. Implementations must preserve the same ownership, completion, and error semantics when substituted.
