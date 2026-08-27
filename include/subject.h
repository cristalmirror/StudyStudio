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
typedef struct Subject Subject;

struct Subject {
    int val;
    void (*fatal)(const char *msg);
    void (*read_subject)(Subject *self);
    void (*save_subject)(Subject *self, char **msg, const char **dir, const char **outpath);
    void (*close_subject)(Subject *self);
};


Subject *new_subject(int value);

#endif
