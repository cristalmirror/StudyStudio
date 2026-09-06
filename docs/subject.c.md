# `src/subject.c`

Status: draft for user approval; version 0.0.7.

## Purpose and dependencies

Implements subject construction/destruction, diagnostic output, process waiting, directory archiving with external `tar`, and XZ compression/decompression through liblzma. Uses standard C I/O and allocation, POSIX process APIs, and a Windows-specific waiting branch.

## Lifecycle and helper operations

`new_subject` allocates the object, initializes `val` and `pid`, and assigns all operation pointers. `_close_subject` frees the object only. `_read_subject` prints its integer value. `_fatal` calls `perror` and `exit(EXIT_FAILURE)`.

`_wait_pid_os_opt` uses `waitpid` on POSIX, retrying interrupted waits, or `WaitForSingleObject` and optionally `GetExitCodeProcess` on Windows. It returns `-1` on API failure and `0` otherwise. When requested, it normalizes child status to `0` for success and `1` for failure.

## Saving

`_save_subject(self, msg, dir, outpath)` expects pointers to directory and destination path strings. It does not validate these arguments; `msg` is unused.

1. Creates a pipe and forks a child.
2. Resolves the directory and executes `tar -cf - -C <parent> <base>` in the child, sending output to the pipe.
3. Opens the destination and encodes pipe data with liblzma preset `6 | LZMA_PRESET_EXTREME` and CRC64.
4. Writes compressed bytes, releases resources on the normal path, waits for the child, and prints a completion message when successful.

The intended output is an XZ-compressed TAR stream. Errors are inconsistently handled through process termination, diagnostic output, or an early return. The function returns `void`, so callers cannot reliably detect success. Opening with `wb` can overwrite an existing file, and failures can leave a partial output.

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

## Known implementation limitations

- Saving uses POSIX `fork`, `pipe`, and related calls without a Windows implementation. The Windows wait branch alone does not make this module portable.
- The root-level directory path branch copies the base name into `parent` instead of `base`, leaving `base` uninitialized.
- The encoding loop can replace pending input before liblzma has consumed it all.
- Several save error paths leave the read side of the pipe open; some final I/O results are not checked.
- If initial load-buffer allocation fails, cleanup runs but execution continues with released resources instead of returning.
- Decoder errors are not all handled with an immediate loop exit, and final acceptance includes `LZMA_OK` instead of requiring `LZMA_STREAM_END`.
- Decoded data is accumulated without an application size limit, and capacity growth lacks overflow checks.

## SOLID development direction

Follow the [project SOLID guidelines](README.md#development-direction-solid). Separate subject data, archive storage, compression, and platform process handling into cohesive modules. Have application workflows consume a small storage contract and supply concrete implementations from the composition point. Keep platform handles private. Return structured or consistently defined errors to callers rather than terminating the application. Implementations must preserve the same ownership, completion, and error semantics when substituted.
