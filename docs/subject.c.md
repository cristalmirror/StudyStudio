# `src/subject.c`

Status: draft for user approval; version 0.0.11.

## Purpose and dependencies

Implements subject construction/destruction, diagnostic output, process waiting,
and XZ archive handling through liblzma. Linux archives and unpacks directories
with external `tar` (`tar -cf`/`tar -xf` through a pipe, in either direction);
Windows uses a native directory traversal and a custom entry serializer for
both directions, with no dependency on `tar`. The file opens with a standard
header comment (developer, repository, version, license, edit date), kept
above the existing purpose comment.

## Lifecycle and helper operations

`new_subject` allocates the object, initializes `val` and the platform process
field (`proc_handle` on Windows, `pid` elsewhere), and assigns all operation
pointers, including the Windows-only `is_dot_or_dotdot`, `write_u32_le`,
`feed_bytes`, and `walk_directory` methods. `_close_subject` frees the object
only. `_read_subject` prints its integer value. `_fatal` calls `perror` and
`exit(EXIT_FAILURE)`.

`_wait_pid_os_opt` uses `waitpid` on POSIX, retrying interrupted waits, or `WaitForSingleObject` and optionally `GetExitCodeProcess` on Windows. It returns `-1` on API failure and `0` otherwise. When requested, it normalizes child status to `0` for success and `1` for failure.

## Saving

`_save_subject(self, msg, dir, outpath)` expects pointers to directory and destination path strings. It does not validate these arguments; `msg` is unused.

Linux saving currently has the working implementation:

1. Creates a pipe and forks a child.
2. Resolves the directory and executes `tar -cf - -C <parent> <base>` in the child, sending output to the pipe.
3. Opens the destination and encodes pipe data with liblzma preset `6 | LZMA_PRESET_EXTREME` and CRC64.
4. Writes compressed bytes, releases resources on the normal path, waits for the child, and prints a completion message when successful.

The Linux output is an XZ-compressed TAR stream. Errors are inconsistently handled through process termination, diagnostic output, or an early return. The function returns `void`, so callers cannot reliably detect success. Opening with `wb` can overwrite an existing file, and failures can leave a partial output.

### Windows saving

As of version 0.0.8, Windows has a working, native `_save_subject` that does
not depend on `tar`:

1. Opens `*outpath` for writing and initializes an LZMA encoder with preset
   `6 | LZMA_PRESET_EXTREME` and CRC64, same as the Linux path.
2. Resolves `*dir` to an absolute path with `_fullpath` and splits it into
   `(parent, base)` at the last `\`, mirroring the Linux `tar -C parent base`
   behavior so the archived paths include the root directory name (a drive
   root such as `C:\Materia1` is handled as a special case).
3. Builds a `WalkContext { outFile, strm }` and calls
   `self->walk_directory(self, parent, base, &ctx)`, which recursively feeds
   every regular file's header and content to the encoder via
   `self->feed_bytes` (see below).
4. Drains the encoder with `LZMA_FINISH` in a loop until `LZMA_STREAM_END`,
   writing the final compressed blocks and the XZ index/footer. This step is
   required because `walk_directory`/`feed_bytes` only ever call
   `lzma_code` with `LZMA_RUN`; without the explicit finish here the `.xz`
   file would be left truncated/invalid.

The private helpers backing this workflow:

- `_walk_directory(self, base_path, rel_prefix, ctx)` recursively enumerates
  files with `FindFirstFileA` and `FindNextFileA`, calling itself through
  `self->walk_directory(...)` for subdirectories.
- `_write_file_entry_to_lzma(...)` serializes one regular file and feeds it to
  the active LZMA encoder through `self->feed_bytes`.
- `_feed_bytes` (exposed as `self->feed_bytes`) is responsible for passing
  input bytes to liblzma and writing generated compressed bytes to the
  destination file.

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

This is not yet a stable archive format: it has no magic value, version, or
end-of-archive marker, and `_load_subject` does not parse it back into
entries (see Loading below). `msg` is not used by the Windows implementation
either, matching the Linux side.

## Loading

`_load_subject(self, path, out_buf, out_size)` reads an XZ file, decodes it with concatenated streams enabled, and accumulates decompressed bytes in a dynamically allocated buffer, same as before. On Linux, `self` is used now: after decoding, it derives an extraction directory next to the archive (`_derive_extract_dir`) and feeds the decoded TAR bytes to `tar -xf -` through a pipe (`_unpack_tar_buffer`), writing real files to disk as a side effect before returning. `out_buf`/`out_size` still carry the full decoded buffer, unchanged; the extraction is additional, not a replacement for it. `main.c` was not modified, so this side effect now happens automatically the next time `on_load_dialog_respose` calls `load_subject` on Linux (see [main.c](main.c.md)).

As of version 0.0.8, Windows has its own native `_load_subject`, functionally
equivalent to the Linux one for decoding: it decompresses the full `.xz` file into a single
heap-allocated buffer with `lzma_stream_decoder`, growing the accumulator
geometrically, and includes a safety exit for a truncated stream that never
reaches `LZMA_STREAM_END`. It now also attempts to parse the `length + path +
size + content` records written by `_save_subject` and write them to disk
(`_derive_extract_dir` + `_unpack_windows_buffer`, mirroring the Linux wiring),
but as written this does not compile on Windows (see
[Extraction](#extraction) below), so this path remains unverified.

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
| `-9` | Decoding succeeded, but deriving the extraction directory or unpacking to disk failed. `*out_buf`/`*out_size` are still assigned at this point; the caller still owns a valid decoded buffer even though extraction failed. |

These return codes describe the current implementation, not a complete or reliable error contract for every failure path. The Windows implementation returns the same codes for the same conditions, plus the same new `-9` for its own (currently non-compiling) unpacking attempt.

## Extraction

Both platforms have `_derive_extract_dir(path, dest_dir, dest_dir_size)`, resolving the archive path to an absolute path (`_fullpath` on Windows, `realpath` on Linux), taking its last path component, and dropping everything after the last `.` in that filename to get the extraction directory's name, placed next to the archive. Example: `C:\subjects\math.xz` → `C:\subjects\math`. A double extension such as `math.tar.xz` currently yields `math.tar`, not `math`, since only the last `.` is stripped. See [prototype_ia.md](prototype_ia.md) for the full design this is based on.

### Linux: compiles and is wired in

The Linux `_derive_extract_dir` no longer has the defects from the previous review (`strrchr`/`snprintf` typos, missing `static int` return type all fixed) and now reads correctly. A new helper, `_unpack_tar_buffer(self, buf, size, dest_dir)`, `mkdir`s the destination and forks `tar -xf - -C <dest_dir>`, writing the decoded TAR bytes to it through a pipe — the load-side mirror of the existing `tar -cf -` save path. `_load_subject` calls both after a successful decode (see [Loading](#loading)); this is the only extraction path currently expected to compile and run.

### Windows: still does not compile

`_derive_extract_dir` still has the previously documented defect: `strrchr(base, sizeof(base), "%s", '.')` passes four arguments to `strrchr`, which takes two (`const char *`, `int`); this does not compile. It should read `strrchr(base, '.')`.

A new helper, `_unpack_windows_buffer(self, buf, size, dest_dir)`, parses the `length + path + size + content` records written by `_save_subject` and writes each one to disk (creating intermediate subfolders with `_win_mkdir_p`). It has its own new defect: `fprintf(stderrm "CreateDirectoryA(%s) failed: %lu\n", dest_dir, (unsigned long)GetLastError());` is missing the comma after `stderr` and instead runs it together into a single, undeclared identifier `stderrm`; this does not compile. It should read `fprintf(stderr, "CreateDirectoryA(%s) failed: %lu\n", ...)`.

Both defects mean the Windows build of `_load_subject` will not compile as currently written, even though it is now wired the same way as the Linux side.

## Known implementation limitations

- Windows saving and loading now run natively (no `tar` dependency). On
  Linux, `_load_subject` now unpacks the decoded TAR bytes back into
  individual files/directories through `_unpack_tar_buffer` (see
  [Extraction](#extraction)); the equivalent Windows unpacking
  (`_unpack_windows_buffer`) is wired the same way but does not compile yet.
  Neither `_save_subject` nor `_load_subject` is explicitly wired into
  `main.c`/the UI, but since `main.c` already calls `load_subject`, the
  Linux extraction now runs as a side effect of the existing "Load Subject"
  button (see [main.c](main.c.md)).
- The Windows entry format currently records only files, so empty directories
  cannot be restored.
- Linux only, pre-existing and not previously documented here: in
  `_load_subject`, `uint8_t *acc = LZMA_RUN;` initializes the decode
  accumulator with the `lzma_action` enum constant (value `0`) instead of
  `NULL`. This happens to work because `0` is a valid null pointer constant
  in C and `LZMA_RUN` is guaranteed to be `0` by the liblzma API, but it is
  confusing to read and fragile if that guarantee ever changed. It should
  read `uint8_t *acc = NULL;`.
- `MAX_PATH` and `FindFirstFileA` limit Windows paths and do not provide full
  Unicode-path support.
- Windows only: `_save_subject`'s parent/basename split does not handle a
  source directory that already ends with a trailing backslash, and a bare
  drive root (e.g. `*dir` equal to `C:\` itself, with nothing after it) is
  not specially handled.
- Linux only: the root-level directory path branch copies the base name into
  `parent` instead of `base`, leaving `base` uninitialized.
- Linux only: the encoding loop can replace pending input before liblzma has
  consumed it all.
- Linux only: several save error paths leave the read side of the pipe open;
  some final I/O results are not checked.
- Linux only: if initial load-buffer allocation fails in `_load_subject`,
  cleanup runs but execution continues with released resources instead of
  returning. The Windows `_load_subject` added the missing `return` for this
  case.
- Linux only: decoder errors are not all handled with an immediate loop exit,
  and final acceptance includes `LZMA_OK` instead of requiring
  `LZMA_STREAM_END`. The Windows `_load_subject` requires `LZMA_STREAM_END`
  strictly.
- Both platforms: decoded data is accumulated without an application size
  limit, and capacity growth lacks overflow checks.

## SOLID development direction

Follow the [project SOLID guidelines](README.md#development-direction-solid). Separate subject data, archive storage, compression, and platform process handling into cohesive modules. Have application workflows consume a small storage contract and supply concrete implementations from the composition point. Keep platform handles private. Return structured or consistently defined errors to callers rather than terminating the application. Implementations must preserve the same ownership, completion, and error semantics when substituted.
