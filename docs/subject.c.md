# `src/subject.c`

Status: draft for user approval; version 0.0.10.

## Purpose and dependencies

Implements subject construction/destruction, diagnostic output, process waiting,
and XZ archive handling through liblzma. Linux archives directories with
external `tar`; Windows uses a native directory traversal and a custom entry
serializer, with no dependency on `tar`. The file opens with a standard header
comment (developer, repository, version, license, edit date), kept above the
existing purpose comment.

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

`_load_subject(self, path, out_buf, out_size)` reads an XZ file, decodes it with concatenated streams enabled, and accumulates decompressed bytes in a dynamically allocated buffer. `self` is unused. This operation does not extract TAR entries (Linux) or per-entry archive records (Windows), nor construct a subject from the decoded bytes.

As of version 0.0.8, Windows has its own native `_load_subject`, functionally
equivalent to the Linux one: it decompresses the full `.xz` file into a single
heap-allocated buffer with `lzma_stream_decoder`, growing the accumulator
geometrically, and includes a safety exit for a truncated stream that never
reaches `LZMA_STREAM_END`. It does not parse the `length + path + size +
content` records written by `_save_subject`; unpacking that format into
individual files is not implemented on either platform yet.

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

These return codes describe the current implementation, not a complete or reliable error contract for every failure path. The Windows implementation returns the same codes for the same conditions; it does not introduce Windows-specific ones.

## Extraction (in progress)

Both platforms have a new private helper, `_derive_extract_dir(path, dest_dir, dest_dir_size)`, added as groundwork for making `_load_subject` unpack its decoded bytes into real files instead of only returning them in memory (see [Loading](#loading) and [prototype_ia.md](prototype_ia.md) for the full design this is based on). It is not called from `_load_subject` yet, and does not compile as currently written on either platform (see below).

Intended behavior, mirroring `_save_subject`'s directory/basename split: resolve the archive path to an absolute path (`_fullpath` on Windows, `realpath` on Linux), take its last path component, and drop everything after the last `.` in that filename to get the extraction directory's name, placed next to the archive. Example: `C:\subjects\math.xz` → `C:\subjects\math`. A double extension such as `math.tar.xz` currently yields `math.tar`, not `math`, since only the last `.` is stripped.

Current defects (source review only, not yet built):

- Windows: `strrchr(base, sizeof(base), "%s", '.')` passes four arguments to `strrchr`, which takes two (`const char *`, `int`); this does not compile. It should read `strrchr(base, '.')`.
- Linux: `strrchar(full, '/')` — `strrchar` does not exist; this should be `strrchr`.
- Linux: `snpritf(dest_dir, dest_dir_size, ...)` — `snpritf` does not exist; this should be `snprintf`.
- Linux: the function definition omits a return type (`static _derive_extract_dir(...)` instead of `static int _derive_extract_dir(...)`), relying on implicit `int`.

Fixed since the previous review: the Linux truncation check now reads `(size_t) n >= dest_dir_size`, correctly treating an `snprintf` result equal to the buffer size (output truncated, no room for the terminator) as failure instead of success.

## Known implementation limitations

- Windows saving and loading now run natively (no `tar` dependency), but
  neither platform's `_load_subject` parses the per-entry archive format back
  into individual files, and neither `_save_subject` nor `_load_subject` is
  wired into `main.c`/the UI yet.
- The Windows entry format currently records only files, so empty directories
  cannot be restored.
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
