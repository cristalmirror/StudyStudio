/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.3
 * License: GPLv3
 * Last edited: 2026-11-06
 */
/*
 * Common operations of the Edit hierarchy and the factory that
 * chooses the right editor for a file.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../include/edit.h"
#include "../include/edit_subclass/editTXT.h"
#include "../include/edit_subclass/editDOC.h"
#include "../include/edit_subclass/editPDF.h"

const EditKind edit_kinds[] = {
    { "📝 Texto (.txt, .md)", "Texto", edit_txt_suffixes, new_edit_txt, NULL },
    { "📄 Documento (.docx, .odt, .xlsx, .pptx...)", "Documentos de oficina",
      edit_doc_suffixes, new_edit_doc, edit_doc_available },
    { "📕 PDF (.pdf)", "PDF", edit_pdf_suffixes, new_edit_pdf, NULL },
    { "📐 UML (próximamente)", NULL, NULL, NULL, NULL },
};
const size_t edit_kinds_count = G_N_ELEMENTS(edit_kinds);

/* destructor: shared by every subclass */
static void _edit_close(Edit *self) {
    if (self == NULL) return;
    self->listener = NULL;      /* the owner is letting us go: stop reporting */

    /* Take the widget out of the UI first,
     * so no signal can reach it
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


const char *edit_result_message(int result) {
    switch (result) {
    case EDIT_OK:               return "Done";
    case EDIT_ERR_UNSUPPORTED:  return "Not supported for this document";
    case EDIT_ERR_NO_SELECTION: return "Select something first";
    case EDIT_ERR_IO:           return "Cannot read or write the file";
    case EDIT_ERR_BACKEND:      return "The document engine reported an error";
    case EDIT_ERR_INVALID:      return "Invalid or incompatible input";
    default:                    return "Unknown error";
    }
}

guint32 edit_color_rgb(const GdkRGBA *color) {
    guint32 r = (guint32)lround(CLAMP(color->red, 0.0, 1.0) * 255.0);
    guint32 g = (guint32)lround(CLAMP(color->green, 0.0, 1.0) * 255.0);
    guint32 b = (guint32)lround(CLAMP(color->blue, 0.0, 1.0) * 255.0);
    return (r << 16) | (g << 8) | b;
}

static bool _edit_is_modified(Edit *self) {
    return self->modified;
}

static void _edit_notify(Edit *self, const char *message) {
    if (self->listener != NULL)
        self->listener(self, message, self->listener_data);
}

static void _edit_mark_modified(Edit *self, bool modified) {
    if (self->modified == modified) return;
    self->modified = modified;
    self->notify(self, NULL);
}

static void _edit_set_listener(Edit *self, EditListener listener, gpointer user_data) {
    self->listener = listener;
    self->listener_data = user_data;
}

static int _edit_save(Edit *self) {
    if (self->path == NULL || self->save_to == NULL) return EDIT_ERR_INVALID;
    int rc = self->save_to(self, self->path);
    if (rc == EDIT_OK) self->mark_modified(self, false);
    return rc;
}

/* the document keeps its format: the new path must use one of its suffixes */
static int _edit_save_as(Edit *self, const char *path) {
    if (path == NULL || self->save_to == NULL) return EDIT_ERR_INVALID;
    int rc = self->save_to(self, path);
    if (rc != EDIT_OK) return rc;
    self->set_path(self, path);
    self->mark_modified(self, false);
    self->notify(self, NULL);      /* the name shown by the UI changed */
    return EDIT_OK;
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
    self->history = NULL;
    self->clipboard = NULL;
    self->markup = NULL;
    self->insert = NULL;
    self->combine = NULL;
    self->listener = NULL;
    self->listener_data = NULL;

    /* abstract: NULL until the subclass assigns them */
    self->open = NULL;
    self->save_to = NULL;
    self->render = NULL;
    self->finalize = NULL;
    self->focus = NULL;
    self->set_color = NULL;

    /* common: */
    self->save = _edit_save;
    self->save_as = _edit_save_as;
    self->close = _edit_close;
    self->is_modified = _edit_is_modified;
    self->mark_modified = _edit_mark_modified;
    self->set_path = _edit_set_path;
    self->set_widget = _edit_set_widget;
    self->set_listener = _edit_set_listener;
    self->notify = _edit_notify;
}

/* true if suffix (without the dot) is in the NULL-terminated list */
static bool _suffix_in(const char *suffix, const char *const *list) {
    for (int i = 0; list[i] != NULL; i++) {
        if (g_ascii_strcasecmp(suffix, list[i]) == 0) return true;
    }
    return false;
}

bool edit_path_has_suffix(const char *path, const char *const *suffixes) {
    const char *ext = (path != NULL) ? strrchr(path, '.') : NULL;
    if (ext == NULL || suffixes == NULL) return false;
    return _suffix_in(ext + 1, suffixes);
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
    } else if (_suffix_in(ext, edit_pdf_suffixes)) {
        edit = new_edit_pdf();
    }

    if (edit == NULL) return NULL;

    if (edit->open(edit, path) != 0) {
        edit->close(edit);
        return NULL;
    }
    return edit;

}