/*
 * this archive have all operaticons needed to create and manipulation of a
 * subject and create the archive .xz . 
 */
#include <lzma.h>
#include <error.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "../include/subject.h"

static void _fatal(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
} 

void _read_subject(Subject *self) {
    printf("Valor >> %i\n",self->val);
}
/*compres and save al archives and information*/
void _save_subject(Subject *self, char **msg, const char **dir, const char **outpath) {

    int pipefd[2];
    if (pipe(pipefd) == -1) self->fatal("pipe");

    pid_t pid = fork();
    if (pid == -1) self->fatal("fork");

    if (pid == 0) {
      /*
       * son process: execute tar -cf - -C <pernt_of_dir> <basename>
       * redirect stdout of son process to the of write pipe 
       */
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) == -1) self->fatal("dup2");
        close(pipefd[1]);

        // tar execute
        //we use "tar -cf - -C <parent> <base>" to that the tar don't include absulete route
        char *dircopy = realpath(dir, NULL);
        if (!dircopy) self->fatal("realpath");

        char *last_slash = strrchr(dircopy, NULL);
        char parent[PATH_MAX];
        char base[PATH_MAX];
        if (last_slash == NULL) {
            // no slash, use "." like parent
            strcpy(parent, ".");
            strcpy(base,dircopy,PATH_MAX);
        } else if (last_slash == dircopy) {
            //path starts with "/" and is like "/foo"
            strcpy(parent, "/", PATH_MAX);
            strcpy(parent, last_slash + 1, PATH_MAX);
        } else {

            size_t p_len = last_slash - dircopy;
            if (p_len >= PATH_MAX) p_len = PATH_MAX - 1;
            strcpy(parent, dircopy, p_len);
            parent[p_len] = '\0';
            strcpy(base, last_slash + 1, PATH_MAX);
        }
        free(dircopy);
        execlp("tar", "tar", "-cf", "-", "-C", parent, base, (char *)NULL);
        perror("execlp tar");
        _exit(127);
    }

    close(pipefd[1]);
    //archive manipulation code...
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
    new->fatal = _fatal;
    return new;
}

