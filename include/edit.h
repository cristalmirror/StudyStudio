/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.0
 * License: GPLv3
 * Last edited: 2026-10-06
 */

/*
 * Abstract base "class" for every editor of StudyStudio.
 * Each subclass embeds Edit as its FIRST member, calls edit_init()
 * and assigns its own open/save/render/finalize.
 */
#ifndef EDIT_H
#define EDIT_H
#include <stdbool.h>
#include <gtk/gtk.h>


typedef struct Edit Edit;

struct Edit {
    
    char *path; /* owned copy of the file path */
    bool modified;/* true while there ar unsaved changes */
    GtkWidget *widget; /* root widget created by render (NULL until then) */

    /* Abstract: assigned by each subclass */
    int (*open)(Edit *self, const char *path);
    int (*save)(Edit *self);
    void (*render)(Edit *self, GtkWidget *container);
    void (*finalize)(Edit *self, GtkWidget *container);

    /* Common: implemented once in edit.c */
    void (*close)(Edit *self);
    bool (*is_modified)(Edit *self);
    void (*mark_modified)(Edit *self, bool modified);
    void (*set_path)(Edit *self,const char *path);

};

/* protected: only subclass constructors call it */
void edit_init(Edit *self);

/* factory: picks the subclass from the file extension and opens the file */
Edit *new_edit_for_path(const char *path);

#endif