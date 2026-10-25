/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.3
 * License: GPLv3
 * Last edited: 2026-10-25
 */

/*
 * this archive have all operations needed to create and manipulation of a
 * subject and create the archive .xz .
 */
#include <lzma.h>

/*OS macro definitions*/
#ifdef _WIN32
   #include <limits.h>
   #include <inttypes.h>
    /*
     * windows.h is used how alternative for windows OS
     * of sys/wait.h, because is a POSIX standard, and
     * don't are ported for windows. 
     */
   #include <windows.h>
   #include <glib.h>
   #include <glib/gstdio.h>
#else
   #include <linux/limits.h>
   #include <sys/wait.h>
   #include <sys/stat.h>
#endif

/*standard definitions for all OS*/
#include <unistd.h> 
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <stdbool.h>
#include "../include/subject.h"
#include <stdarg.h>


static void subject_log(const char *fmt, ...); /* definition for GUI print */


/*
 * This function is to set the option
 * of waitpid correct according to whom
 * OS is compiled (Windows or GNU/Linux).
 */

/* Returns -1 on API failure; optional status is 0 for success, 1 otherwise. */
static int _wait_pid_os_opt(Subject *self, int *status) {
    #ifdef _WIN32
        if (WaitForSingleObject(self->proc_handle, INFINITE) == WAIT_FAILED) {
            fprintf(stderr, "Error waiting: %lu\n", GetLastError());
            subject_log("Error waiting: %lu\n", GetLastError());
            return -1;
        }
        if (status != NULL) {
            DWORD exit_code;
            if (!GetExitCodeProcess(self->proc_handle, &exit_code)) {
                fprintf(stderr, "Error querying exit code: %lu\n", GetLastError());
                subject_log("Error querying exit code: %lu\n", GetLastError());
                return -1;
            }
            *status = (exit_code == 0) ? 0 : 1;
        }
    #else
        int raw_status;
        pid_t result;
        do {
            result = waitpid(self->pid, status != NULL ? &raw_status : NULL, 0);
        } while (result == -1 && errno == EINTR);
        if (result == -1) {
            perror("waitpid");
            return -1;
        }
        if (status != NULL) {
            *status = (WIFEXITED(raw_status) && WEXITSTATUS(raw_status) == 0)
                  ? 0 : 1;
        }
    #endif
    return 0;
}

static void _fatal(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
} 

void _read_subject(Subject *self) {
    printf("Valor >> %i\n",self->val);
    subject_log("Valor >> %i\n",self->val);
}

/* 
 * To me this is more simple to implement the same 
 * function in two Operating System.
 * 
 * Else form is use the TAR command for windows,
 * but isn't the idea is implement XZ algorithm
 * inside of code.
 * 
 */
#ifdef _WIN32
/* Module for windows code
 * 
 * This part of the code contains all code that is necessary 
 * to the algorithm can run in Windows and maintain the 
 * interface with code rest.
 * 
 * May be in the future this part of the code can be split into its own
 * .h and .c, but the moment is better that all are integrated
 * here.
 */

/*
 *     ------------- SO IMPORTANT!!! -------------- 
 * The next functions are part important and necessary to
 * _load_subject run correctly and do the decompress 
 */
static int _win_path_format(char *dest, size_t size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(dest, size, fmt, args);
    va_end(args);
    return (n < 0 || (size_t)n >= size) ? -1 : n;
}

static uint32_t _read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t _read_u64_le(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= ((uint64_t)p[i]) << (8 * i);
    return v;
}

/*
 * Helper to create intermediate subfolders
 * (a `relpath` can come as `notas\semana1\archivo.txt`)
 */
static int _win_mkdir_p(const char *dir_path) {
    return g_mkdir_with_parents(dir_path, 0755);
}

static int _unpack_windows_buffer(Subject *self,const uint8_t *buf, size_t size, const char *dest_dir) {
    (void)self;
    size_t offset = 0;

    if (_win_mkdir_p(dest_dir) != 0) {
        subject_log("Could not create extraction directory: %s\n", dest_dir);
        return -1;
    }

    while (offset < size) {
        if (size - offset < 4) {
            fprintf(stderr, "truncated archive: length header\n");
            subject_log("truncated archive: length header\n");
            return -1;
        }
        uint32_t rel_len = _read_u32_le(buf + offset);
        offset += 4;
        
        if (rel_len == 0 || rel_len >= MAX_PATH || offset + rel_len > size) {
            fprintf(stderr,"truncated or invalid relpath\n");
            subject_log("truncated or invalid relpath\n");
            return -1;
        }
        char relpath[MAX_PATH];
        memcpy(relpath, buf + offset, rel_len);
        relpath[rel_len] = '\0';
        offset += rel_len;

        /* Entries are relative UTF-8 names, never drive paths or traversal. */
        if (memchr(relpath, '\0', rel_len) || !g_utf8_validate(relpath, -1, NULL)
            || relpath[0] == '/' || relpath[0] == '\\'
            || strpbrk(relpath, ":*?\"<>|") != NULL) return -1;
        char **parts = g_strsplit_set(relpath, "/\\", -1);
        bool valid = true;
        for (size_t i = 0; parts[i] != NULL; ++i) {
            size_t n = strlen(parts[i]);
            if (n == 0 || parts[i][n - 1] == '.' || parts[i][n - 1] == ' ')
                valid = false;
        }
        g_strfreev(parts);
        if (!valid) return -1;
        for (char *p = relpath; *p; ++p) if (*p == '/') *p = '\\';

        if (size - offset < 8) {
            subject_log("truncated archive: size header\n");
            return -1;
        }
        uint64_t filesize = _read_u64_le(buf + offset);
        offset += 8;

        if (filesize > size - offset) {
            subject_log("truncated archive: content\n");
            return -1;
        }

        char fullpath[MAX_PATH];
        int n = snprintf(fullpath, sizeof(fullpath), "%s\\%s", dest_dir, relpath);
        if (n < 0 || (size_t)n >= sizeof(fullpath)) return -1;

        char *last_slash = strrchr(fullpath, '\\');
        if (last_slash != NULL) {
            char parent[MAX_PATH];
            snprintf(parent, sizeof(parent), "%.*s", (int)(last_slash - fullpath), fullpath);
            if (_win_mkdir_p(parent) != 0) {
                fprintf(stderr, "mkdir(%s) failed\n", parent);
                subject_log("mkdir(%s) failed\n", parent);
                return -1;
            }
        }

        FILE *out = g_fopen(fullpath, "wb");
        if (!out) { fprintf(stderr, "fopen(%s) failed\n", fullpath); return -1; }
        if (filesize > 0 && fwrite(buf + offset, 1, (size_t)filesize, out) != (size_t)filesize) {
            fprintf(stderr, "fwrite(%s) failed\n", fullpath);
            subject_log("fwrite(%s) failed\n", fullpath);
            fclose(out);
            return -1;
        }
        if (fclose(out) != 0) return -1;
        offset += filesize;
    }
    return 0;
    
}

 /*
  * Make the name of the result of decompress the folder or archive
  * with the same name of the original archive tar.xz compress
  * Example: "C:\subjects\math.xz" -> dest_dir = "C:\subjects\math"
  */
static int _derive_extract_dir(const char *path, char *dest_dir, size_t dest_dir_size) {
    char *full = g_canonicalize_filename(path, NULL);
    if (!full) return -1;

    //filter backslash
    char *last_slash = strrchr(full, '\\');
    char base [MAX_PATH];
    if (_win_path_format(base, sizeof(base), "%s", last_slash ? last_slash + 1 : full) < 0) {
        g_free(full);
        return -1;
    }

    //filter point
    char *dot =strrchr(base, '.');
    if (dot != NULL && dot != base) *dot = '\0';

    int n = last_slash
        ? snprintf(dest_dir, dest_dir_size, "%.*s\\%s",
        (int) (last_slash - full), full, base)
        : snprintf(dest_dir, dest_dir_size, "%s", base);

    g_free(full);
    return (n < 0 || (size_t) n >= dest_dir_size) ? -1 : 0;
}


int _load_subject(Subject *self, const char *path, uint8_t **out_buf, size_t *out_size) {
    (void)self;

    if (!path || !out_buf || !out_size) {
        subject_log("Error: invalid arguments while loading subject\n");
        return -1;
    }

    *out_buf = NULL;
    *out_size = 0;

    subject_log("Loading %s...\n", path);

    FILE *f = g_fopen(path, "rb");
    if (!f) {
        subject_log("Error: could not open %s\n", path);
        return -2;
    }

        lzma_stream strm = LZMA_STREAM_INIT;
        lzma_ret ret = lzma_stream_decoder(&strm, UINT64_MAX, LZMA_CONCATENATED);
        if (ret != LZMA_OK) {
            fclose(f);
            subject_log("Error: could not initialize xz decoder (%d)\n", (int)ret);
            return -3;
        }

        uint8_t *inbuf = malloc(IN_BUF_SIZE);
        uint8_t *outbuf = malloc(OUT_BUF_SIZE);
        if (!inbuf || !outbuf) {
            free(inbuf);
            free(outbuf);
            lzma_end(&strm);
            fclose(f);
            subject_log("Error: out of memory allocating buffers\n");
            return -4;
        }

        uint8_t *acc = NULL;
        size_t acc_size = 0;
        size_t acc_cap = 0;

        lzma_action action = LZMA_RUN;
        strm.avail_in = 0;

        do {
            if (strm.avail_in == 0 && !feof(f)) {
                size_t r = fread(inbuf, 1, IN_BUF_SIZE, f);
                if (ferror(f)) {
                    ret = LZMA_DATA_ERROR;
                    break;
                }
                strm.next_in = inbuf;
                strm.avail_in = r;
                if (feof(f)) action = LZMA_FINISH;
            }

            strm.next_out = outbuf;
            strm.avail_out = OUT_BUF_SIZE;

            ret = lzma_code(&strm, action);

            size_t produced = OUT_BUF_SIZE - strm.avail_out;
            if (produced > 0) {
                  if (acc_size + produced > acc_cap) {
                    size_t new_cap = acc_cap ? acc_cap * 2 : produced;
                    while (new_cap < acc_size + produced) new_cap *= 2;
                    uint8_t *tmp = realloc(acc, new_cap);
                    if (!tmp) {
                        ret = LZMA_MEM_ERROR;
                        break;
                    }
                    acc = tmp;
                    acc_cap = new_cap;
                }
                memcpy(acc + acc_size, outbuf, produced);
                acc_size += produced;
            }

            /* LZMA_STREAM_END or any error (a truncated file gives
             * LZMA_BUF_ERROR): stop and keep ret */
            if (ret != LZMA_OK) break;

    } while (1);

    lzma_end(&strm);
    free(inbuf);
    free(outbuf);
    fclose(f);

    if (ret != LZMA_STREAM_END) {
        free(acc);
        if (ret == LZMA_MEM_ERROR) {
            subject_log("Error: out of memory while decompressing %s\n", path);
            return -5;
        }
        if (ret == LZMA_FORMAT_ERROR) {
            subject_log("Error: %s is not a valid .xz file\n", path);
            return -6;
        }
        if (ret == LZMA_DATA_ERROR) {
            subject_log("Error: corrupted data in %s\n", path);
            return -7;
        }
        if (ret == LZMA_BUF_ERROR) {
            subject_log("Error: %s is truncated (unexpected end of file)\n", path);
            return -10;
        }
        subject_log("Error: unknown lzma error (%d) in %s\n", (int)ret, path);
        return -8;
    }

    /* implementation of Windows directory factory and decompress result. */
    char dest_dir[MAX_PATH];
    if (_derive_extract_dir(path, dest_dir, sizeof(dest_dir)) != 0 ||
        _unpack_windows_buffer(self, acc, acc_size, dest_dir) != 0) {
        free(acc);   /* the caller only owns the buffer when we return 0 */
        fprintf(stderr, "failed to unpack %s\n", path);
        subject_log("Error: could not extract %s\n", path);
        return -9;
    }

    /* everything went fine: only now the buffer goes to the caller */
    *out_buf = acc;
    *out_size = acc_size;
    subject_log("Subject loaded: %s (%lu bytes) into %s\n", path, (unsigned long)acc_size, dest_dir);
    return 0;

}

/*
 *  ------- SO IMPORTANT!!! ------- 
 *  All functions and methods that are written here
 *  are necessary to _save_subject() running fine.
 *
 */

/* Directory enumeration converts native UTF-16 names to UTF-8 entries. */
static int _is_dot_or_dotdot(Subject *self, const char *name) {
    (void)self;
    return name != NULL && name[0] == '.' &&
           (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

/** Returns 0 on success and a nonzero value on error. */

static int _feed_bytes(lzma_stream *strm,
                      uint8_t *outbuf, size_t out_buf_size,
                      FILE *outfile,
                      const uint8_t *data, size_t len)
{
    const uint8_t *p = data;
    size_t left = len;

    while (left > 0) {
        /** Feed all remaining input; lzma_code updates next_in and avail_in. */
        strm->next_in = (uint8_t *)p;      /** The liblzma API requires uint8_t*, hence the cast. */
        strm->avail_in = left;

        do {
            strm->next_out = outbuf;
            strm->avail_out = out_buf_size;

            lzma_ret ret = lzma_code(strm, LZMA_RUN);

            /** If output was produced, write it. */
            if (strm->avail_out < out_buf_size) {
                size_t wrote = out_buf_size - strm->avail_out;
                if (fwrite(outbuf, 1, wrote, outfile) != wrote)
                    return -1;
            }

            if (ret != LZMA_OK) {
                if (ret == LZMA_STREAM_END) {
                    /**
                     * This is not expected during LZMA_RUN (it is used with
                     * LZMA_FINISH), but handle it as a safe termination.
                     */
                    return 0;
                }
                fprintf(stderr, "lzma_code error: %d\n", (int)ret);
                subject_log("lzma_code error: %d\n", (int)ret);
                return -1;
            }

            /** Repeat until LZMA consumes all input (avail_in == 0). */
        } while (strm->avail_in > 0);

        /** LZMA consumed all provided input; advance the pointer. */
        /**
         * strm->next_in was incremented internally by liblzma, but do not rely
         * on it for the local pointer; consume `left` bytes.
         */
        p += left;
        left = 0;
    }

    return 0;
}
/*
 * Write a input (header + content) to encoder: return 0 Ok.
 */
static int _write_file_entry_to_lzma(Subject *self, const char *relpath, const char *fullpath, lzma_stream *strm, FILE *outfile) {
    if (!self || !relpath || !fullpath || !strm || !outfile) {
        return -1;
    }

    size_t relpath_size = strlen(relpath);
    if (relpath_size > UINT32_MAX) {
        fprintf(stderr, "Path too long: %s\n", relpath);
        subject_log("Path too long: %s\n", relpath);
        return -1;
    }

    //Open binary mode
    FILE *f = g_fopen(fullpath, "rb");
    if (!f) {
        fprintf(stderr," fopen(%s) failed \n",fullpath);
        subject_log("fopen(%s) failed \n",fullpath);
        return -1;
    } 

    //to fetch size
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long pos = ftell(f);
    if (pos < 0) {
        fclose(f); 
        return -1;
    }
    uint64_t filesize = (uint64_t)pos;
    rewind(f);

    //Buffer I/O LZMA
    uint8_t inbuf[IN_BUF_SIZE];
    uint8_t outbuf[OUT_BUF_SIZE];
    uint8_t length_header[4];
    uint8_t size_header[8];

    /*
     * First is necessary send the bytes of handler without compress
     * to the encoder.
     *
     * Header: u32(uint32_t)strlen(relpath) + relpath bytes + u64(filesize)
     */
    uint32_t rel_len = (uint32_t)relpath_size;

    length_header[0] = (uint8_t)(rel_len & 0xFF);
    length_header[1] = (uint8_t)((rel_len >> 8) & 0xFF);
    length_header[2] = (uint8_t)((rel_len >> 16) & 0xFF);
    length_header[3] = (uint8_t)((rel_len >> 24) & 0xFF);

    for (int i = 0; i < 8; ++i) {
        size_header[i] = (uint8_t)((filesize >> (8 * i)) & 0xFF);
    }
    /*
     * For put the bytes to the compress flow, we use 
     * lzma_code() with LZMA_RUN.
     * 
     * Implement a little function inline to feed bytes
     *  (repeat code header and archive).
     */
    if (self->feed_bytes(strm, outbuf, OUT_BUF_SIZE, outfile,
                    length_header, sizeof(length_header)) != 0 ||
        self->feed_bytes(strm, outbuf, OUT_BUF_SIZE, outfile,
                    (const uint8_t *)relpath, relpath_size) != 0 ||
        self->feed_bytes(strm, outbuf, OUT_BUF_SIZE, outfile,
                    size_header, sizeof(size_header)) != 0) {
        fclose(f);
        return -1;
    }

    /*
     * Read and cout size in bytes.
     */
    size_t read_count;
    while ((read_count = fread(inbuf, 1, sizeof(inbuf), f)) > 0) {
        if (self->feed_bytes(strm, outbuf, OUT_BUF_SIZE, outfile,
                        inbuf, read_count) != 0) {
            fclose(f);
            return -1;
        }
    }

    /* file error return error */
    if (ferror(f)) {
        fclose(f);
        return -1;
    }
    /* Close file */
    fclose(f);
    return 0;
}


static int _walk_directory(Subject *self, const char *base_path, const char *rel_prefix, WalkContext *ctx) {
    char search_path[MAX_PATH];
    WIN32_FIND_DATAW fd;

      /* check that parameters aren't NULL*/  
    if (!self || !base_path || !ctx || !ctx->strm || !ctx->outFile) {
        return -1;
    }

    if (rel_prefix && rel_prefix[0] != '\0') {
        if (_win_path_format(search_path, sizeof(search_path), "%s\\%s\\*", base_path, rel_prefix) < 0) {
            return -1;
        }
    } else {
        if (_win_path_format(search_path, sizeof(search_path), "%s\\*", base_path) < 0) {
            return -1;
        }
    }

    /* 
     * List the content in base_path
     * (with )
     */
    gunichar2 *wide_search = g_utf8_to_utf16(search_path, -1, NULL, NULL, NULL);
    if (wide_search == NULL) return -1;
    HANDLE hfind = FindFirstFileW((wchar_t *)wide_search, &fd);
    g_free(wide_search);
    if (hfind == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();

        if (error == ERROR_FILE_NOT_FOUND) {
            return 0;
        }

        fprintf(stderr, "FindFirstFileW(%s) failed: %lu\n", search_path, (unsigned long)error);
        subject_log("FindFirstFileW(%s) failed: %lu\n", search_path, (unsigned long)error);
        return -1;
    }
    /* Go to directory with . and .. by self->is_dot_or_dotdot in loop*/
    do {
        char name[MAX_PATH * 4];
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, fd.cFileName, -1,
                                name, sizeof(name), NULL, NULL) == 0) {
            FindClose(hfind);
            return -1;
        }
        char relpath[MAX_PATH];

        if (self->is_dot_or_dotdot(self, name)) {
            continue;
        }

        if (rel_prefix && rel_prefix[0] != '\0') {
            if (_win_path_format(relpath, sizeof(relpath), "%s\\%s", rel_prefix, name) < 0) {
                FindClose(hfind);
                return -1;
            }
        } else {
            if (_win_path_format(relpath, sizeof(relpath), "%s", name) < 0) {
                FindClose(hfind);
                return -1;
            }
        }

          /**
           * Do not follow links or reparse points: this prevents recursion
           * cycles and keeps the generated archive predictable.
           */
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            fprintf(stderr, "Reparse point not supported: %s\n", relpath);
            subject_log("Reparse point not supported: %s\n", relpath);
            FindClose(hfind);
            return -1;
        }
        /* recursive call if the self directory */
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (self->walk_directory(self, base_path, relpath, ctx) != 0) {
                FindClose(hfind);
                return -1;
            }
        } else {
            char fullpath[MAX_PATH];

            if (_win_path_format(fullpath, sizeof(fullpath), "%s\\%s", base_path, relpath) < 0) {
                FindClose(hfind);
                return -1;
            }

            if (_write_file_entry_to_lzma(self, relpath, fullpath, ctx->strm, ctx->outFile) != 0) {
                FindClose(hfind);
                return -1;
            }
        }
    } while (FindNextFileW(hfind, &fd));

    DWORD error = GetLastError();
    FindClose(hfind);

    /* Error system message */
    if (error != ERROR_NO_MORE_FILES) {
        fprintf(stderr, "FindNextFileW failed: %lu\n", (unsigned long)error);
        subject_log("FindNextFileW failed: %lu\n", (unsigned long)error);
        return -1;
    }

    return 0;
}
/* compress and save all archives and information */
void _save_subject(Subject *self, char **msg, const char **dir, const char **outpath) {
    (void)msg;

    /* check that parameters isn't NULL */
    if (!self || !dir || !*dir || !outpath) {
        return;
    }

    /* Open file */
    FILE *outf = g_fopen(*outpath, "wb");
    if (!outf) {
        fprintf(stderr,"fopen(%s) failed \n", *outpath);
        subject_log("fopen(%s) failed \n", *outpath);
        return;
    }

    /*
     * LZMA encoder maker
     */
    lzma_stream strm = LZMA_STREAM_INIT;
    lzma_ret ret = lzma_easy_encoder(&strm, 6 | LZMA_PRESET_EXTREME, LZMA_CHECK_CRC64);
    if (ret != LZMA_OK) {
        fprintf(stderr, "lzma_easy_encoder failed: %d\n", ret);
        subject_log("lzma_easy_encoder failed: %d\n", ret);
        fclose(outf);
        return;
    }

    /*
     * Is equivalent to "tar -C parent basename" of Linux side:
     * waiting *dir in (parent, basename) to the name of the root
     * directory was written inside the archive.
     */

    char *dircopy = g_canonicalize_filename(*dir, NULL);
    if (dircopy != NULL && strlen(dircopy) >= MAX_PATH) {
        subject_log("Error: subject path too long\n");
        g_free(dircopy);
        lzma_end(&strm);
        fclose(outf);
        return;
    }
    if (!dircopy) {
        fprintf(stderr,"_fullpath(%s) filed\n", *dir);
        subject_log("_fullpath(%s) filed\n", *dir);
        lzma_end(&strm);
        fclose(outf);
        return;
    }

    char *last_slash = strrchr(dircopy, '\\');
    char parent[MAX_PATH];
    char base[MAX_PATH];

    if (last_slash == NULL) {
        snprintf(parent, sizeof(parent), ".");
        snprintf(base, sizeof(base), "%s", dircopy);
    } else if (last_slash == dircopy + 2 && dircopy[1] == ':') {
        size_t root_len = (size_t)(last_slash - dircopy) + 1;
        snprintf(parent, sizeof(parent), "%.*s",(int)root_len, dircopy);
        snprintf(base, sizeof(base), "%s", last_slash + 1);
    } else {
        size_t p_len = (size_t)(last_slash - dircopy);
        snprintf(parent, sizeof(parent), "%.*s",(int)p_len, dircopy);
        snprintf(base, sizeof(base), "%s", last_slash + 1);
    }

    g_free(dircopy);

    WalkContext ctx;
    ctx.outFile = outf;
    ctx.strm = &strm;

    if (self->walk_directory(self, parent, base, &ctx) != 0) {
        fprintf(stderr, "walk_directory failed for %s\n", *dir);
        subject_log("walk_directory failed for %s\n", *dir);
        lzma_end(&strm);
        fclose(outf);
        return;
    }

    /*
     * walk_directory / feed_bytes only use LZMA_RUN:
     *
     * they are releasing what they already have * compressed,
     * but the stream is still "open".
     * Here we close it with * LZMA_FINISH for XZ to write the
     * final blocks + the index.
     */

    uint8_t outbuf[OUT_BUF_SIZE];
    lzma_ret fret;
    do {
        strm.next_in = NULL;
        strm.avail_in = 0;
        strm.next_out = outbuf;
        strm.avail_out = OUT_BUF_SIZE;

        fret = lzma_code(&strm, LZMA_FINISH);
        size_t wrote = OUT_BUF_SIZE - strm.avail_out;
        if (wrote > 0 && fwrite(outbuf, 1, wrote, outf) != wrote) {
            fprintf(stderr, "fwrite failed while finishing xz stream\n");
            subject_log("fwrite failed while finishing xz stream\n");
            lzma_end(&strm);
            fclose(outf);
            return;
        }
    } while(fret == LZMA_OK);

    lzma_end(&strm);
    fclose(outf);

    if (fret != LZMA_STREAM_END) {
        fprintf(stderr, "lzma_code error while finishing: %d\n",(int)fret);
        subject_log("lzma_code error while finishing: %d\n",(int)fret);
        return;
    }

    printf("Created %s\n", *outpath);
}
#else
/* compress and save all archives and information */
void _save_subject(Subject *self, char **msg, const char **dir, const char **outpath) {

    int pipefd[2];
    if (pipe(pipefd) == -1) self->fatal("pipe");
    self->pid = fork();
    if (self->pid == -1) self->fatal("fork");
    
    if (self->pid == 0) {
      /**
       * Child process: execute `tar -cf - -C <parent_of_dir> <basename>`.
       * Redirect its standard output to the write end of the pipe.
       */
        signal(SIGPIPE, SIG_DFL);   /* main() ignores it: see _unpack_tar_buffer */
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) == -1) self->fatal("dup2");
        close(pipefd[1]);

        // tar execute
        //we use "tar -cf - -C <parent> <base>" to that the tar don't include absolute route
        char *dircopy = realpath(*dir, NULL);
        if (!dircopy) self->fatal("realpath");

        char *last_slash = strrchr(dircopy, '/');
        char parent[PATH_MAX];
        char base[PATH_MAX];
        /*
         * ============= CONTEXT =============
         * This code has removed in 0.0.15 because
         * strncpy() isn't secure to ensure the '\0' 
         * in the end and has changed for snprintf()
         * ====================================
         *if (last_slash == NULL) {
         *   // no slash, use "." like parent
         *   strcpy(parent, ".");
         *   strncpy(base,dircopy,PATH_MAX);
         *} else if (last_slash == dircopy) {
         *   //path starts with "/" and is like "/foo"
         *   strncpy(parent, "/", PATH_MAX);
         *   strncpy(parent, last_slash + 1, PATH_MAX);
         *} else {
         *
         *   size_t p_len = last_slash - dircopy;
         *   if (p_len >= PATH_MAX) p_len = PATH_MAX - 1;
         *   strncpy(parent, dircopy, p_len);
         *   parent[p_len] = '\0';
         *   strncpy(base, last_slash + 1, PATH_MAX);
         *}
         */
        if (last_slash == NULL) {
            // no slash, use "." like parent
            snprintf(parent, sizeof(parent), ".");
            snprintf(base, sizeof(base), "%s", dircopy);
        } else if (last_slash == dircopy) {
            //path starts with "/" and is like "/foo"
            snprintf(parent, sizeof(parent), "/");
            snprintf(base, sizeof(base), "%s", last_slash + 1);
        } else {
            size_t p_len = last_slash - dircopy;
            if (p_len >= PATH_MAX) p_len = PATH_MAX - 1;
            memcpy(parent, dircopy, p_len);
            parent[p_len] = '\0';
            snprintf(base, sizeof(base), "%s", last_slash + 1);
        }
        free(dircopy);
        execlp("tar", "tar", "-cf", "-", "-C", parent, base, (char *)NULL);
        perror("execlp tar");
        _exit(127);
    }

    close(pipefd[1]);
    // archive manipulation code...
    FILE *outf = fopen(*outpath, "wb");
    if (!outf) {
        int save_errno = errno;
        kill(self->pid, SIGTERM);
        if (self->wait_pid_os_opt(self,NULL) == -1) {
            fprintf(stderr,"Error in the request %d\n",-1);
            subject_log("Error in the request %d\n",-1);
        } 
        errno = save_errno;
        self->fatal("fopen output");
    }
    /*check CRc64 integrity*/
    lzma_stream strm = LZMA_STREAM_INIT;
    lzma_ret ret = lzma_easy_encoder(&strm, 6 | LZMA_PRESET_EXTREME, LZMA_CHECK_CRC64);
    if (ret != LZMA_OK) {
        fclose(outf);
        kill(self->pid, SIGTERM);
        if (self->wait_pid_os_opt(self,NULL) == -1) {
            fprintf(stderr,"Error in the request %d\n",-1);
            subject_log("Error in the request %d\n",-1);
        } 
        fprintf(stderr, "lzma_easy_encoder failed: %d\n", ret);
        subject_log("lzma_easy_encoder failed: %d\n", ret);
        return;
    }
    /*Buffers (I/O)*/
    uint8_t inbuf[IN_BUF_SIZE];
    uint8_t outbuf[OUT_BUF_SIZE];

    /*reading archive to compress*/
    ssize_t r;
    bool done_reading = false;
    while (1) {
        if (!done_reading) {
            r = read(pipefd[0], inbuf, IN_BUF_SIZE);
            if (r < 0) {
                perror("read form tar pipe");
                lzma_end(&strm);
                fclose(outf);
                kill(self->pid, SIGTERM);
                if (self->wait_pid_os_opt(self,NULL) == -1) {
                    fprintf(stderr,"Error in the request %d\n",-1);
                    subject_log("Error in the request %d\n",-1);
                } 
                return;
            } else if (r == 0) {
                // EOF of tar stream
                done_reading = true;
                strm.next_in = NULL;
                strm.avail_in = 0;
            } else {
                strm.next_in = inbuf;
                strm.avail_in = (size_t)r;
            }
        }
        /*
         * lzma finish read
         */
        lzma_action action = done_reading ? LZMA_FINISH : LZMA_RUN;
        strm.next_out = outbuf;
        strm.avail_out = OUT_BUF_SIZE;
        ret = lzma_code(&strm, action);

        /*
         * Write the bytes compressed that was
         * generated in outbuf
         */

        size_t wrote = OUT_BUF_SIZE - strm.avail_out;
        if (wrote > 0) {
            if (fwrite(outbuf, 1, wrote, outf) != wrote) {
                perror("fwrite exit xz");
                lzma_end(&strm);
                fclose(outf);
                kill(self->pid, SIGTERM);
                if (self->wait_pid_os_opt(self,NULL) == -1) {
                    fprintf(stderr,"Error in the request %d\n",-1);
                    subject_log("Error in the request %d\n",-1);
                } 
                return;
            }
        }

        if (ret == LZMA_STREAM_END) {
            //successful finish
            break;
        } else if (ret != LZMA_OK) {
            fprintf(stderr,"lzma_code error: %d\n", ret);
            lzma_end(&strm);
            fclose(outf);
            kill(self->pid, SIGTERM);
            if (self->wait_pid_os_opt(self,NULL) == -1) {
                fprintf(stderr,"Error in the request %d\n",-1);
                subject_log("Error in the request %d\n",-1);
            }   
            return;
        }

        /*
         * If haven't more input and don't make output
         * or finish, repeat read
         */
        if (!done_reading) continue;

        /*
         * if done_reading and still LZMA_OK,
         * loop again for to drain until LZMA_STREAM_END
         */
    }

    lzma_end(&strm);
    fclose(outf);
    close(pipefd[0]);

    int status;
    if (self->wait_pid_os_opt(self, &status) == -1) {
        return;
    }
    if (status != 0) {
        fprintf(stderr, "tar failed\n");
        return;
    }

    printf("Created %s\n", *outpath);
    subject_log("Created %s\n", *outpath);
}

/*
  * Make the name of the result of decompress the folder or archive
  * with the same name of the original archive tar.xz compress
  */
static int _derive_extract_dir(const char *path, char *dest_dir, size_t dest_dir_size) {
    char *full = realpath(path, NULL);
    if (!full) return -1;

    char *last_slash = strrchr(full,'/');
    char base[PATH_MAX];
    snprintf(base,sizeof(base), "%s", last_slash ? last_slash + 1 : full);

    char *dot = strrchr(base, '.');
    if (dot != NULL && dot != base) *dot = '\0';

    int n = last_slash
        ? snprintf(dest_dir, dest_dir_size, "%.*s/%s",
        (int) (last_slash - full), full, base)
        : snprintf(dest_dir, dest_dir_size, "%s", base);

    free(full);
    return (n < 0 || (size_t) n >= dest_dir_size) ? -1 : 0;
}

/* 
 * unpacking decompress archive in memory:
 *
 * the buffer have the total of bytes of
 * folder and LZMA decompress these buffer,
 * finaly _unpack_tar_buffer() remake the 
 * folder using tar comand.
 */
static int _unpack_tar_buffer(Subject *self, const uint8_t *buf, size_t size, const char *dest_dir) {
    if (mkdir(dest_dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "mkdir(%s) failed: %s\n", dest_dir, strerror(errno));
        subject_log("mkdir(%s) failed: %s\n", dest_dir, strerror(errno));
        return -1;
    }    

    (void)self;
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        subject_log("pipe failed: %s\n", strerror(errno));
        return -1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        subject_log("fork failed: %s\n", strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        /* main() ignores SIGPIPE; ignored signals survive exec, so give tar
         * back the default. _exit, not exit/fatal: the child must not run
         * the parent's atexit handlers (GTK, LibreOffice...). */
        signal(SIGPIPE, SIG_DFL);
        close(pipefd[1]);
        if (dup2(pipefd[0], STDIN_FILENO) == -1) _exit(127);
        close(pipefd[0]);
        execlp("tar","tar","-xf","-","-C", dest_dir,(char *) NULL);
        perror("execlp tar -xf");
        _exit(127);
    }

    close(pipefd[0]);
    int rc = 0;
    size_t written = 0;
    while (written < size) {
        ssize_t w = write(pipefd[1], buf + written, size - written);
        if (w == -1) {
            if (errno == EINTR) continue;
            /* EPIPE: tar is gone (not installed, or it rejected the data) */
            fprintf(stderr, "write to tar pipe failed: %s\n", strerror(errno));
            subject_log("write to tar pipe failed: %s\n", strerror(errno));
            rc = -1;
            break;   /* don't return yet: the child must be reaped */
        }
        written += (size_t)w;
    }
    close(pipefd[1]);   /* EOF for tar */

    int status;
    while (waitpid(pid, &status, 0) == -1) {
        if (errno == EINTR) continue;
        fprintf(stderr,"waitpid failed: %s\n", strerror(errno));
        subject_log("waitpid failed: %s\n", strerror(errno));
        return -1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) rc = -1;
    return rc;
}

/* This function load subject:
 *
 * This function decompress the .xz archive
 * and load all archives (.doc .pdf .txt .html, etc).
 *
 * `path` is the .xz archive.
 * `out_buf` pointer buffer with the data decompress
 * `out_size` buffer bytes size
 */

int _load_subject(Subject *self, const char *path, uint8_t **out_buf, size_t *out_size) {

    /* Set init and check if data are fine */
    if (!path || !out_buf || !out_size) {
        subject_log("Error: invalid arguments while loading subject\n");
        return -1;
    }

    *out_buf = NULL;
    *out_size = 0;

    subject_log("Loading %s...\n", path);

    FILE *f = fopen(path, "rb");
    if (!f) {
        subject_log("Error: could not open %s\n", path);
        return -2;
    }

    // init lzma_stream
    lzma_stream strm = LZMA_STREAM_INIT;
    lzma_ret ret  = lzma_stream_decoder(&strm, UINT64_MAX, LZMA_CONCATENATED);
    if (ret != LZMA_OK) {
        fclose(f);
        subject_log("Error: could not initialize xz decoder (%d)\n", (int)ret);
        return -3;
    }


    /* Input/output Buffer */
    const size_t IN_CHUNK = 1 << 16;
    const size_t OUT_CHUNK = 1 << 16;

    /*
     * YES, this form of define is a shit, but is more easy
     * to write, please don't mistake with `**out_buf`
     */
    uint8_t *inbuf = malloc(IN_CHUNK);
    uint8_t *outbuf = malloc(OUT_CHUNK);

    if (!inbuf || !outbuf) {
        free(inbuf);
        free(outbuf);
        lzma_end(&strm);
        fclose(f);
        subject_log("Error: out of memory allocating buffers\n");
        return -4;
    }

    /* Dynamic Buffer accumulators */
    uint8_t *acc = NULL;
    size_t acc_size = 0;
    size_t acc_cap = 0;

    lzma_action action = LZMA_RUN;

    do {
        /* Read the archive if haven't input data pending */
        if (strm.avail_in == 0 && !feof(f)) {
            size_t r = fread(inbuf, 1, IN_CHUNK, f);

            if (ferror(f)) {
                ret = LZMA_DATA_ERROR;
                break;
            }
            strm.next_in = inbuf;
            strm.avail_in = r;

            if (feof(f)) {
                action = LZMA_FINISH;
            }
        }

        /* decompress info and size */
        strm.next_out = outbuf;
        strm.avail_out = OUT_CHUNK;

        /* Decompress execute:
         *
         * this call can consume part or all
         * input data and put it in `outbuf`.
         *
         * In the call be used:
         *
         * - strm.next_in
         * - strm.avail_in
         * - strm.next_out
         * - strm.avail_out
         */
        ret = lzma_code(&strm, action);

        // copy the result product to accumulator
        
        size_t produced = OUT_CHUNK - strm.avail_out;
        if (produced > 0) {
            /* resize and reallocation if is necessary
             * more capacity to accumulator (acc).
             */
            if (acc_size + produced > acc_cap) {
                size_t new_cap = acc_cap ? acc_cap * 2 : produced;

                while (new_cap < acc_size + produced) new_cap *= 2;
                uint8_t *tmp = realloc(acc, new_cap);
                if (!tmp) {
                    ret = LZMA_MEM_ERROR;
                    break;
                }
                acc = tmp;
                acc_cap = new_cap;
            }
            memcpy(acc + acc_size, outbuf, produced);
            acc_size += produced;
        }

        /*
         * LZMA_OK: keep going. LZMA_STREAM_END or any error: stop here and
         * keep ret. Calling lzma_code again after an error never recovers
         * (it loops forever or replaces the real error with another one).
         * A truncated file ends here too: with LZMA_FINISH and no progress
         * liblzma returns LZMA_BUF_ERROR.
         */
        if (ret != LZMA_OK) break;

    } while (1);

    /* free memory resources of decoder */
    lzma_end(&strm);
    free(inbuf);
    free(outbuf);
    fclose(f);

    /*
     * Error maps to return code
     * "Ah shit, here we go again!!!"
     */
    if (ret != LZMA_STREAM_END) {
    free(acc);

        switch (ret) {
        case LZMA_MEM_ERROR:
            subject_log("Error: out of memory while decompressing %s\n", path);
            return -5;

        case LZMA_FORMAT_ERROR:
            subject_log("Error: %s is not a valid .xz file\n", path);
            return -6;

        case LZMA_DATA_ERROR:
            subject_log("Error: corrupted data in %s\n", path);
            return -7;

        case LZMA_BUF_ERROR:
            subject_log("Error: %s is truncated (unexpected end of file)\n", path);
            return -10;

        default:
            subject_log("Error: unknown lzma error (%d) in %s\n", (int)ret, path);
            return -8; // error lzma unknown
    }
}

    /* implementation of GNU/Linux directory factory and decompress result. */
    char dest_dir[PATH_MAX];
    if (_derive_extract_dir(path, dest_dir, sizeof(dest_dir)) != 0 ||
        _unpack_tar_buffer(self, acc, acc_size, dest_dir) != 0) {
        free(acc);   /* the caller only owns the buffer when we return 0 */
        fprintf(stderr, "failed to unpack %s\n", path);
        subject_log("Error: could not extract %s\n", path);
        return -9;
    }

    /* everything went fine: only now the buffer goes to the caller */
    *out_buf = acc;
    *out_size = acc_size;
    subject_log("Subject loaded: %s (%zu bytes) into %s\n", path, acc_size, dest_dir);
    return 0;
}

#endif

/*
 * Implementation of the interface to the abstraction
 * SubjectLogFunc. 
 * 
 * Really I know that it is so confusing using
 * this method, but I think that this bullshit is necessary
 * to not break SOLID arch.
 */

static SubjectLogFunc g_log_fn = NULL;
static void *g_log_data = NULL;

void subject_set_logger(SubjectLogFunc fn, void *user_data) {
    /* Si multithreading: usar atomic_store o lock aquí */
    g_log_fn = fn;
    g_log_data = user_data;
}

static void subject_log(const char *fmt, ...) {
    char stack_buf[1024];
    char *buf = stack_buf;
    va_list args;
    int needed;

    va_start(args, fmt);
    needed = vsnprintf(stack_buf, sizeof(stack_buf), fmt, args);
    va_end(args);

    if (needed < 0) return;                      /* format error */

    if ((size_t)needed >= sizeof(stack_buf)) {
        buf = malloc((size_t)needed + 1);
        if (!buf) {                              /* OOM: */
            if (g_log_fn) g_log_fn("[log truncated: OOM]", g_log_data);
            else fputs("[log truncated: OOM]\n", stderr);
            return;
        }
        va_start(args, fmt);                     /* re-exec with big buffer */
        vsnprintf(buf, (size_t)needed + 1, fmt, args);
        va_end(args);
    }

    if (g_log_fn) g_log_fn(buf, g_log_data);
    else fputs(buf, stderr);

    if (buf != stack_buf) free(buf);
}
#ifdef _WIN32
    #define SUBJECT_SEP '\\'
    #define SUBJECT_PATH_MAX MAX_PATH
#else
    #define SUBJECT_SEP '/'
    #define SUBJECT_PATH_MAX PATH_MAX
#endif

/*
 * Creates one folder: 0 created, 1 already existed, -1 error.
 * GTK gives UTF-8 paths; on Windows CreateDirectoryA would read them in the
 * ANSI code page and break names like "Física", so we go through UTF-16.
 */
static int _mkdir_utf8(const char *path) {
    #ifdef _WIN32
        wchar_t wpath[MAX_PATH];
        if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH) == 0) return -1;
        if (CreateDirectoryW(wpath, NULL)) return 0;
        return (GetLastError() == ERROR_ALREADY_EXISTS) ? 1 : -1;
    #else
        if (mkdir(path, 0755) == 0) return 0;
        return (errno == EEXIST) ? 1 : -1;
    #endif
}

/* the name becomes ONE folder: no separators nor characters Windows rejects */
static bool _is_valid_subject_name(const char *name) {
    if (name == NULL || name[0] == '\0') return false;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;
    return strpbrk(name, "/\\:*?\"<>|") == NULL;
}

/* 
 * make the container of all resources necesary to create 
 * a subject: <base_dir>/<name>/ with one subfolder per entry of
 * folders (NULL-terminated). Returns 0 on success, -1 on error.
 */
static int _make_subject(Subject *self, const char *base_dir, const char *name,
                         const char *const *folders) {
    char path[SUBJECT_PATH_MAX];
    char sub[SUBJECT_PATH_MAX];
    int n;

    if (self == NULL || base_dir == NULL) return -1;
    if (!_is_valid_subject_name(name)) {
        subject_log("Invalid subject name: '%s'\n", name != NULL ? name : "");
        return -1;
    }

    /* 1. base folder (e.g. ~/StudyStudio): it may already exist */
    if (_mkdir_utf8(base_dir) < 0) {
        subject_log("Cannot create %s\n", base_dir);
        return -1;
    }

    /* 2. subject folder: must be new, never mix two subjects */
    n = snprintf(path, sizeof(path), "%s%c%s", base_dir, SUBJECT_SEP, name);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        subject_log("Path too long for subject '%s'\n", name);
        return -1;
    }
    switch (_mkdir_utf8(path)) {
        case 0:  break;
        case 1:  subject_log("Subject already exists: %s\n", path); return -1;
        default: subject_log("Cannot create %s\n", path);           return -1;
    }

    /* 3. one subfolder per kind of file */
    for (size_t i = 0; folders != NULL && folders[i] != NULL; i++) {
        n = snprintf(sub, sizeof(sub), "%s%c%s", path, SUBJECT_SEP, folders[i]);
        if (n < 0 || (size_t)n >= sizeof(sub) || _mkdir_utf8(sub) < 0) {
            subject_log("Cannot create folder '%s' in %s\n", folders[i], path);
            return -1;
        }
    }

    /* 4. remember who we are */
    free(self->name);
    free(self->dir);
    self->name = strdup(name);
    self->dir = strdup(path);
    if (self->name == NULL || self->dir == NULL) {
        subject_log("Out of memory\n");
        return -1;
    }

    subject_log("Subject created: %s\n", path);
    return 0;
}

/*destructor*/
void _close_subject(Subject *self) {
    if (self != NULL) {
        free(self->name);
        free(self->dir);
        free(self);
    }
}

/*constructor*/
Subject *new_subject(int value) {
    //allocate memory for the object 
    Subject *new = (Subject *)malloc(sizeof(Subject));

    if (new == NULL) {
        return NULL;
    }

    #ifdef _WIN32
        new->proc_handle = NULL;
        new->is_dot_or_dotdot = _is_dot_or_dotdot;
        new->feed_bytes = _feed_bytes;
        new->walk_directory = _walk_directory;
    #else
    new->pid = 0;
    #endif
    new->val = value;
    new->name = NULL;
    new->dir = NULL;
    new->make_subject = _make_subject;
    new->wait_pid_os_opt = _wait_pid_os_opt;
    new->save_subject = _save_subject;
    new->read_subject = _read_subject;
    new->close_subject = _close_subject;
    new->fatal = _fatal;
    new->load_subject = _load_subject;
    return new;
}
