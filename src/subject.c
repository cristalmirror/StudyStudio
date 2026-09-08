/*
 * this archive have all operaticons needed to create and manipulation of a
 * subject and create the archive .xz . 
 */
#include <lzma.h>

/*OS macro definitions*/
#ifdef _WIN32
   #include <limits.h>
   #include <inttypes.h>
    /*
     * windows.h is used how altenative for windows OS
     * of sys/wait.h, beacuse is a POSIX standard, and
     * don't are ported for windows. 
     */
   #include <windows.h>
#else
   #include <linux/limits.h>
   #include <sys/wait.h>
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


/*
 * This function is to set the option
 * of waitpid correct according to whom
 * OS is compiled (Windows or GNU/Linux).
 */

/* Returns -1 on API failure; optional status is 0 for success, 1 otherwise. */
static int _wait_pid_os_opt(Subject *self, int *status) {
    #ifdef _WIN32
        if (WaitForSingleObject(self->pid, INFINITE) == WAIT_FAILED) {
            fprintf(stderr, "Error esperando: %lu\n", GetLastError());
            return -1;
        }
        if (status != NULL) {
            DWORD exit_code;
            if (!GetExitCodeProcess(self->pid, &exit_code)) {
                fprintf(stderr, "Error consultando salida: %lu\n", GetLastError());
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
}

/* 
 * To me this is more simple to implement the same 
 * function in two Operating System.
 * 
 * Else form is use the TAR comand for windows,
 * but isn't the idea is implement XZ algorithm
 * incide of code.
 * 
 */
#ifdef _WIN32
/* Module for windows code
 * 
 * This part of the code contente all code that is necesary 
 * to the algorithm can run in Windows and maintain the 
 * interface with code rest.
 * 
 * May be in the future this part of the code can be hun in our
 * .h and .c, but the moment is better that all are integrated
 * here.
 */

int _load_subject(Subject *self, const char *path, uint8_t **out_buf, size_t *out_size) {

}

/*
 *  ------- SO IMPORTANT!!! ------- 
 *  All functions and methods that has whited here
 *  are necesary to _save_subject() runing fine.
 *
 */

/* Write integer values in the archive's little-endian format. */
static int _write_u32_le(FILE *f, uint32_t v) {
    uint8_t b[4];
    b[0] = v & 0xFF;
    b[1] = (v >> 8) & 0xFF;
    b[2] = (v >> 16) & 0xFF;
    b[3] = (v >> 24) & 0xFF;
    return fwrite(b, 1, sizeof(b), f) == sizeof(b) ? 0 : -1;
}

static int _write_u64_le(FILE *f, uint64_t v) {
    uint8_t b[8];

    for (int i = 0; i < 8; i++) b[i] = (v >> (8 * i)) & 0xFF;
    return fwrite(b, 1, sizeof(b), f) == sizeof(b) ? 0 : -1;
}

/*
 * Make route to connect base + "\" + name int UTF-8 simple 
 * (ANSI -> UTF-8 not implemented here). 
 * 
 * For stability in platforms with Unicode usa WideCharToMultiByte
 * on the name wide.
 * 
 * Here we assume ANSI routes (or compile con UTF-8 in framework). 
 */
static int _is_dot_or_dotdot(Subject *self, const char *name) {
    (void)self;
    return name != NULL && name[0] == '.' &&
           (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

/* Devuelve 0 en éxito, != 0 en error. */

static int _feed_bytes(lzma_stream *strm,
                      uint8_t *outbuf, size_t out_buf_size,
                      FILE *outfile,
                      const uint8_t *data, size_t len)
{
    const uint8_t *p = data;
    size_t left = len;

    while (left > 0) {
        /* Alimentamos todo lo que nos queda; lzma_code actualizará next_in/avail_in. */
        strm->next_in = (uint8_t *)p;      /* liblzma API requiere uint8_t*, por eso el cast */
        strm->avail_in = left;

        do {
            strm->next_out = outbuf;
            strm->avail_out = out_buf_size;

            lzma_ret ret = lzma_code(strm, LZMA_RUN);

            /* Si se produjo salida, escribirla */
            if (strm->avail_out < out_buf_size) {
                size_t wrote = out_buf_size - strm->avail_out;
                if (fwrite(outbuf, 1, wrote, outfile) != wrote)
                    return -1;
            }

            if (ret != LZMA_OK) {
                if (ret == LZMA_STREAM_END) {
                    /* No es esperado durante LZMA_RUN (se usa en LZMA_FINISH),
                       pero tratamos como terminación segura. */
                    return 0;
                }
                fprintf(stderr, "lzma_code error: %d\n", (int)ret);
                return -1;
            }

            /* Repetir hasta que lzma consuma toda la entrada (avail_in == 0). */
        } while (strm->avail_in > 0);

        /* lzma consumió todo lo que le dimos; avanzar el puntero */
        /* strm->next_in fue incrementado internamente por liblzma,
           pero no confiamos en ello para nuestro pointer local; consumimos 'left' bytes. */
        p += left;
        left = 0;
    }

    return 0;
}
/*
 * Write a input (header + content) to encoder: return 0 Ok.
 */
static int _write_file_entry_to_lzma(Subject *self, const char *relpath, const char *fullpath, lzma_stream *strm, FILE *outfile) {
    (void)self;

    if (!relpath || !fullpath || !strm || !outfile) {
        return -1;
    }

    size_t relpath_size = strlen(relpath);
    if (relpath_size > UINT32_MAX) {
        fprintf(stderr, "Path too long: %s\n", relpath);
        return -1;
    }

    //Open binary mode
    FILE *f = fopen(fullpath, "rb");
    if (!f) {
        fprintf(stderr," fopen(%s) failed \n",fullpath);
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

    //Buffer I/O LMZA
    uint8_t inbuf[IN_BUF_SIZE];
    uint8_t outbuf[OUT_BUF_SIZE];
    uint8_t length_header[4];
    uint8_t size_header[8];

    /*
     * First is necesary send the bytes of handler without compress
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
     * Implement a little funtion inline to feed bytes
     *  (repit code header and archive).
     */
    if (_feed_bytes(strm, outbuf, OUT_BUF_SIZE, outfile,
                    length_header, sizeof(length_header)) != 0 ||
        _feed_bytes(strm, outbuf, OUT_BUF_SIZE, outfile,
                    (const uint8_t *)relpath, relpath_size) != 0 ||
        _feed_bytes(strm, outbuf, OUT_BUF_SIZE, outfile,
                    size_header, sizeof(size_header)) != 0) {
        fclose(f);
        return -1;
    }

    size_t read_count;
    while ((read_count = fread(inbuf, 1, sizeof(inbuf), f)) > 0) {
        if (_feed_bytes(strm, outbuf, OUT_BUF_SIZE, outfile,
                        inbuf, read_count) != 0) {
            fclose(f);
            return -1;
        }
    }

    if (ferror(f)) {
        fclose(f);
        return -1;
    }

    fclose(f);
    return 0;
}
static int _walk_directory(
      Subject *self,
      const char *base_path,
      const char *rel_prefix,
      WalkContext *ctx) {
      char search_path[MAX_PATH];
      WIN32_FIND_DATAA fd;

      if (!self || !base_path || !ctx || !ctx->strm || !ctx->outfile) {
          return -1;
      }

      if (rel_prefix && rel_prefix[0] != '\0') {
          if (snprintf(search_path, sizeof(search_path), "%s\\%s\\*",
                       base_path, rel_prefix) < 0) {
              return -1;
          }
      } else {
          if (snprintf(search_path, sizeof(search_path), "%s\\*",
                       base_path) < 0) {
              return -1;
          }
      }

      HANDLE hfind = FindFirstFileA(search_path, &fd);
      if (hfind == INVALID_HANDLE_VALUE) {
          DWORD error = GetLastError();

          if (error == ERROR_FILE_NOT_FOUND) {
              return 0;
          }

          fprintf(stderr, "FindFirstFileA(%s) failed: %lu\n",
                  search_path, (unsigned long)error);
          return -1;
      }

      do {
          const char *name = fd.cFileName;
          char relpath[MAX_PATH];

          if (_is_dot_or_dotdot(self, name)) {
              continue;
          }

          if (rel_prefix && rel_prefix[0] != '\0') {
              if (snprintf(relpath, sizeof(relpath), "%s\\%s",
                           rel_prefix, name) < 0) {
                  FindClose(hfind);
                  return -1;
              }
          } else {
              if (snprintf(relpath, sizeof(relpath), "%s", name) < 0) {
                  FindClose(hfind);
                  return -1;
              }
          }

          /*
           * No seguir enlaces/reparse points: evita ciclos durante
           * la recursión y mantiene el archivo generado predecible.
           */
          if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
              fprintf(stderr, "Reparse point not supported: %s\n", relpath);
              FindClose(hfind);
              return -1;
          }

          if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
              if (_walk_directory(self, base_path, relpath, ctx) != 0) {
                  FindClose(hfind);
                  return -1;
              }
          } else {
              char fullpath[MAX_PATH];

              if (snprintf(fullpath, sizeof(fullpath), "%s\\%s",
                           base_path, relpath) < 0) {
                  FindClose(hfind);
                  return -1;
              }

              if (_write_file_entry_to_lzma(
                      self, relpath, fullpath, ctx->strm, ctx->outfile) != 0) {
                  FindClose(hfind);
                  return -1;
              }
          }
      } while (FindNextFileA(hfind, &fd));

      DWORD error = GetLastError();
      FindClose(hfind);

      if (error != ERROR_NO_MORE_FILES) {
          fprintf(stderr, "FindNextFileA failed: %lu\n",
                  (unsigned long)error);
          return -1;
      }

      return 0;
  }

void _save_subject(Subject *self, char **msg, const char **dir, const char **outpath) {

}
#else
/*compres and save al archives and information*/
void _save_subject(Subject *self, char **msg, const char **dir, const char **outpath) {

    int pipefd[2];
    if (pipe(pipefd) == -1) self->fatal("pipe");
    if (self->pid == -1) self->fatal("fork");   
    if (self->pid == 0) {
      /*
       * son process: execute tar -cf - -C <pernt_of_dir> <basename>
       * redirect stdout of son process to the of write pipe 
       */
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) == -1) self->fatal("dup2");
        close(pipefd[1]);

        // tar execute
        //we use "tar -cf - -C <parent> <base>" to that the tar don't include absulete route
        char *dircopy = realpath(*dir, NULL);
        if (!dircopy) self->fatal("realpath");

        char *last_slash = strrchr(dircopy, '/');
        char parent[PATH_MAX];
        char base[PATH_MAX];
        if (last_slash == NULL) {
            // no slash, use "." like parent
            strcpy(parent, ".");
            strncpy(base,dircopy,PATH_MAX);
        } else if (last_slash == dircopy) {
            //path starts with "/" and is like "/foo"
            strncpy(parent, "/", PATH_MAX);
            strncpy(parent, last_slash + 1, PATH_MAX);
        } else {

            size_t p_len = last_slash - dircopy;
            if (p_len >= PATH_MAX) p_len = PATH_MAX - 1;
            strncpy(parent, dircopy, p_len);
            parent[p_len] = '\0';
            strncpy(base, last_slash + 1, PATH_MAX);
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
        } 
        fprintf(stderr, "lzma_easy_encoder failed: %d\n", ret);
        return;
    }
    /*Buffers (I/O)*/
    uint8_t inbuf[IN_BUF_SIZE];
    uint8_t outbuf[OUT_BUF_SIZE];

    /*readin archive to compress*/
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

    printf("Maked %s\n", *outpath);
    
}

/* This function load subject:
 *
 * This function decompress the .xz archive
 * and load al archives (.doc .pdf .txt .html, etc).
 *
 * `path` is the .xz archive.
 * `out_buf` pointer buffer with the data decompress
 * `out_size` buffer bytes size
 */

int _load_subject(Subject *self, const char *path, uint8_t **out_buf, size_t *out_size) {

    /* Set init and check if data are fine */
    if (!path || !out_buf || !out_size) return -1;

    *out_buf = NULL;
    *out_size = 0;

    FILE *f = fopen(path, "rb");
    if (!f) return -2;

    // init lzma_stream
    lzma_stream strm = LZMA_STREAM_INIT;
    lzma_ret ret  = lzma_stream_decoder(&strm, UINT64_MAX, LZMA_CONCATENATED);
    if (ret != LZMA_OK) {
        fclose(f);
        return -3;
    }


    /* Input/output Buffer */
    const size_t IN_CHUNK = 1 << 16;
    const size_t OUT_CHUNK = 1 << 16;

    /*
     * YES, this form of define is a shit, but is more easy
     * to write, pleace don't mistake with `**out_buf`
     */
    uint8_t *inbuf = malloc(IN_CHUNK);
    uint8_t *outbuf = malloc(OUT_CHUNK);

    if (!inbuf || !outbuf) {
        free(inbuf);
        free(outbuf);
        lzma_end(&strm);
        fclose(f);
    }

    /* Dinamic Buffer accumulators */
    uint8_t *acc = LZMA_RUN;
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
            /* resize and realalocation if is necesary
             * more capacity to accumulator (acc).
             */
            if (acc_size + produced > acc_cap) {
                size_t new_cap = new_cap = acc_cap ? acc_cap * 2 : produced;

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

        if (ret == LZMA_STREAM_END) {
            break;
        } else if (ret == LZMA_OK) {
            continue; // error
        }
        
        /*if haven't more input and don't produced output and EOF, finish  */
        if (feof(f) && strm.avail_in == 0 && strm.avail_out == OUT_CHUNK) break;

    } while (1);

    /* free mememory resources of decoder */
    lzma_end(&strm);
    free(inbuf);
    free(outbuf);
    fclose(f);

    /*
     * Error maps to return code
     * "Ah shit, here we go again!!!"
     */
    if (ret != LZMA_OK && ret != LZMA_STREAM_END) {
        free(acc);
        if (ret == LZMA_MEM_ERROR) return -5;
        if (ret == LZMA_FORMAT_ERROR) return -6;
        if (ret == LZMA_DATA_ERROR) return -7;
        return -8; // error lzma unknow
    }

    *out_buf = acc;
    *out_size = acc_size;
    return 0;
}

#endif
/*destructor*/
void _close_subject(Subject *self) {
    if (self != NULL) {
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
        new->walk_directory =walk_directory;
    #endif
    new->val = value;
    new->pid = 0;
    new->wait_pid_os_opt = _wait_pid_os_opt;
    new->save_subject = _save_subject;
    new->read_subject = _read_subject;
    new->close_subject = _close_subject;
    new->fatal = _fatal;
    new->load_subject = _load_subject;
    return new;
}
