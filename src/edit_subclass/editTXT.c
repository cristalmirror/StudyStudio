/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.2
 * License: GPLv3
 * Last edited: 2026-10-12
 */

/*
 * EditTXT: implementation of the abstract methods of Edit
 * for plain-text documents (.txt, .md).
 */
#include <stdlib.h>
#include "../../include/edit_subclass/editTXT.h"

const char *const edit_txt_suffixes[] = { "txt", "md", NULL };

/* GTK calls this every time the buffer's "modified" flag flips */
static void _txt_on_modified_changed(GtkTextBuffer *buffer, gpointer user_data) {
    Edit *self = (Edit *)user_data;
    self->mark_modified(self, gtk_text_buffer_get_modified(buffer));
}

static int _txt_open(Edit *self, const char *path) {
    EditTXT *doc = (EditTXT *)self;   /* downcast: safe, base is first */
    char *contents = NULL;
    gsize len = 0;
    GError *error = NULL;

    if (!g_file_get_contents(path, &contents, &len, &error)) {
        g_printerr("EditTXT: cannot open %s: %s\n", path, error->message);
        g_clear_error(&error);
        return -1;
    }
    /* GtkTextBuffer only accepts valid UTF-8 and an int length */
    if (len > G_MAXINT || !g_utf8_validate(contents, (gssize)len, NULL)) {
        g_printerr("EditTXT: %s is not valid UTF-8 text\n", path);
        g_free(contents);
        return -2;
    }

    gtk_text_buffer_set_text(doc->buffer, contents, (int)len);
    g_free(contents);
    gtk_text_buffer_set_modified(doc->buffer, FALSE);  /* freshly loaded */
    self->set_path(self, path);
    return 0;
}

static int _txt_save(Edit *self) {
    EditTXT *doc = (EditTXT *)self;
    GtkTextIter start, end;
    GError *error = NULL;

    if (self->path == NULL) return -1;

    gtk_text_buffer_get_bounds(doc->buffer, &start, &end);
    char *text = gtk_text_buffer_get_text(doc->buffer, &start, &end, FALSE);

    /* atomic write: temp file + rename, never leaves a half-written file */
    gboolean ok = g_file_set_contents(self->path, text, -1, &error);
    g_free(text);

    if (!ok) {
        g_printerr("EditTXT: cannot save %s: %s\n", self->path, error->message);
        g_clear_error(&error);
        return -1;
    }
    gtk_text_buffer_set_modified(doc->buffer, FALSE);
    return 0;
}

static void _txt_render(Edit *self, GtkWidget *container) {
    EditTXT *doc = (EditTXT *)self;

    if (self->widget != NULL) return;          /* already rendered */
    if (!GTK_IS_BOX(container)) {
        g_printerr("EditTXT: container must be a GtkBox\n");
        return;
    }

    GtkWidget *view = gtk_text_view_new_with_buffer(doc->buffer);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), view);
    gtk_widget_set_vexpand(scroll, TRUE);

    gtk_box_append(GTK_BOX(container), scroll);
    self->set_widget(self, scroll);
}

/* virtual destructor: only frees what EditTXT added */
static void _txt_finalize(Edit *self) {
    EditTXT *doc = (EditTXT *)self;
    if (doc->buffer != NULL) {
        g_signal_handler_disconnect(doc->buffer, doc->modified_id);
        g_object_unref(doc->buffer);
        doc->buffer = NULL;
    }
}

/* constructor */
Edit *new_edit_txt(void) {
    EditTXT *doc = calloc(1, sizeof(EditTXT));  /* zeroed: every pointer NULL */
    if (doc == NULL) return NULL;

    edit_init(&doc->base);
    doc->base.open = _txt_open;
    doc->base.save = _txt_save;
    doc->base.render = _txt_render;
    doc->base.finalize = _txt_finalize;

    doc->buffer = gtk_text_buffer_new(NULL);
    doc->modified_id = g_signal_connect(doc->buffer, "modified-changed",
                                        G_CALLBACK(_txt_on_modified_changed), doc);
    return &doc->base;   /* upcast */
}
