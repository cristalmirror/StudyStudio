/*
 * this archive have all operaticons needed to create and manipulation of a
 * subject and create the archive .xz . 
 */

#ifndef SUBJECT_H
#define SUBJECT_H

typedef struct Subject Subject;

struct Subject {
    int val;
    void (*read_subject)(Subject *self);
    void (*close_subject)(Subject *self);
};


Subject *new_subject(int value);

#endif
