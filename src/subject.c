/*
 * this archive have all operaticons needed to create and manipulation of a
 * subject and create the archive .xz . 
 */

#include <stdio.h>
#include <stdlib.h>
#include "../include/subject.h"

void _read_subject(Subject *self) {
    printf("Valor >> %i\n",self->val);
}

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

    new->val = value;
    new->read_subject = _read_subject;
    new->close_subject = _close_subject;

    return new;
}

