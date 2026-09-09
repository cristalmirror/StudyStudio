/*
 * this archive have all operaticons needed to create and manipulation of a
 * subject and create the archive .xz . 
 */

#ifndef SUBJECT_H
#define SUBJECT_H

/*input & output buffer size*/
#define IN_BUF_SIZE 65536
#define OUT_BUF_SIZE 65536

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#ifdef _WIN32 //windows flags, structs and machro
    #define _CRT_SECURE_NO_WARNINGS
    typedef struct WalkContext WalkContext;
    /*
     * Recursive route: write file be file to encoder.
     * Base_path must be route without final bar.
     * rel_prefix is the route relative accumulated.
     */
    struct WalkContext {
        FILE *outFile;
        lzma_stream *strm;
    };

    //Foward
    static int _walk_directory(const char *base_path, const char *rel_prefix, WalkContext *ctx);
#endif

typedef struct Subject Subject;

struct Subject {
    int val;
    pid_t pid;
    #ifdef _WIN32
        int (*is_dot_or_dotdot)(Subject *self, const char *name)
        int (*write_u32_le)(Subject *self, FILE *f, u_int32_t v);
        static int (*_is_dot_or_dotdot)(Subject *self, const char *name);
        int (*feed_bytes)(lzma_stream *strm,
                      uint8_t *outbuf, size_t out_buf_size,
                      FILE *outfile,
                      const uint8_t *data, size_t len);
        int (*walk_directory)(const char *base_path, const char *rel_prefix, WalkContext *ctx);

    #endif
    void (*fatal)(const char *msg);
    void (*read_subject)(Subject *self);
    int (*wait_pid_os_opt)(Subject *self, int *status);
    int (*load_subject)(Subject *self, const char *path, uint8_t **out_buf, size_t *out_size);
    void (*save_subject)(Subject *self, char **msg, const char **dir, const char **outpath);
    void (*close_subject)(Subject *self);
};

Subject *new_subject(int value);

#endif
