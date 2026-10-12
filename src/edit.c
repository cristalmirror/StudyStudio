/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.0
 * License: GPLv3
 * Last edited: 2026-10-12
 */
/*
 * Common operations of the Edit hierarchy and the factory that
 * chooses the right editor for a file.
 */
#include <stdlib.h>
#include <string.h>
#include "../include/edit.h"
#include "../include/edit_subclass/editTXT.h"
#include "../include/edit_subclass/editDOC.h"

/* destructor: shared by every subclass */
static void _edit_close(Edit *self) {
    if (self == NULL) return;

    /* Take the widget out of the UI first,
     * so no signal can reach 
     */

    if (self->widget != NULL) {
        GtkWidget *widget = self->widget;
        self->set_widget(self, NULL);   /* drop the weak pointer before free(self) */
        GtkWidget *parent = gtk_widget_get_parent(widget);
        if (parent != NULL && GTK_IS_BOX(parent)) {
            gtk_box_remove(GTK_BOX(parent), widget);
        }
    }

    /* Let the subclass free its own fields (virtual destructor) */
    if (self->finalize != NULL) {
        self->finalize(self);
    }

    /* Free the base fields and the whole object */
    g_free(self->path); 
    free(self);
}


static bool _edit_is_modified(Edit *self) {
    return self->modified;
}

static void _edit_mark_modified(Edit *self, bool modified) {
    self->modified = modified;
}

static void _edit_set_path(Edit *self, const char *path) {
    char *copy = g_strdup(path); /* copy first: path may be self->path */
    g_free(self->path);
    self->path = copy;
}

/* GTK sets self->widget to NULL by itself if the widget is destroyed
 * (e.g. the window closes) before close() runs: weak pointer */
static void _edit_set_widget(Edit *self, GtkWidget *widget) {
    if (self->widget != NULL)
        g_object_remove_weak_pointer(G_OBJECT(self->widget), (gpointer *)&self->widget);
    self->widget = widget;
    if (widget != NULL)
        g_object_add_weak_pointer(G_OBJECT(widget), (gpointer *)&self->widget);
}

void edit_init(Edit *self) {
    self->path = NULL;
    self->modified = false;
    self->widget = NULL;

    /* abstract: NULL until the subclass assigns them */
    self->open = NULL;
    self->save = NULL;
    self->render = NULL;
    self->finalize = NULL;
    
    /* common: */
    self->close = _edit_close;
    self->is_modified = _edit_is_modified;
    self->mark_modified = _edit_mark_modified;
    self->set_path = _edit_set_path;
    self->set_widget = _edit_set_widget;

}

/* true if suffix (without the dot) is in the NULL-terminated list */
static bool _suffix_in(const char *suffix, const char *const *list) {
    for (int i = 0; list[i] != NULL; i++) {
        if (g_ascii_strcasecmp(suffix, list[i]) == 0) return true;
    }
    return false;
}

Edit *new_edit_for_path(const char *path) {
    const char *ext = strrchr(path, '.');
    Edit *edit = NULL;
    if (ext == NULL) return NULL;

    ext++;   /* skip the dot: the suffix lists have none */

    if (_suffix_in(ext, edit_txt_suffixes)) {
        edit = new_edit_txt();
    } else if (_suffix_in(ext, edit_doc_suffixes) && edit_doc_available()) {
        edit = new_edit_doc();
    }
    /* else if (... ".pdf") edit = new_edit_pdf(); */

    if (edit == NULL) return NULL;

    if (edit->open(edit, path) != 0) {
        edit->close(edit);
        return NULL;
    }
    return edit;

}