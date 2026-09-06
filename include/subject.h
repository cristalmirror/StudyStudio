/*
 * this archive have all operaticons needed to create and manipulation of a
 * subject and create the archive .xz . 
 */

#ifndef SUBJECT_H
#define SUBJECT_H

#include <stddef.h>
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#endif

/*input & output buffer size*/
#define IN_BUF_SIZE 65536
#define OUT_BUF_SIZE 65536

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
typedef struct Subject Subject;

struct Subject {
    int val;
#ifdef _WIN32
    HANDLE pid;
#else
    pid_t pid;
#endif
    void (*fatal)(const char *msg);
    void (*read_subject)(Subject *self);
    /* Optional status: 0 on child success, 1 otherwise; returns -1 on API error. */
    int (*wait_pid_os_opt)(Subject *self, int *status);
    int (*load_subject)(Subject *self, const char *path, uint8_t **out_buf, size_t *out_size);
    void (*save_subject)(Subject *self, char **msg, const char **dir, const char **outpath);
    void (*close_subject)(Subject *self);
};


Subject *new_subject(int value);

#endif
