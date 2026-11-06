/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.4
 * License: GPLv3
 * Last edited: 2026-11-06
 */

/*
 * Toolbar of an open document. It only knows the Edit contracts
 * (EditHistory, EditClipboard, ...): it shows the tools the bound editor
 * offers and runs them as GActions ("doc.copy", "doc.undo"...). File
 * operations (save, save as, close) belong to the owner, which receives them
 * through DocToolbarOwner together with state changes and messages.
 */
#ifndef DOC_TOOLBAR_H
#define DOC_TOOLBAR_H

#include <gtk/gtk.h>
#include "../edit.h"

typedef struct {
    void (*save)(gpointer owner);
    void (*save_as)(gpointer owner);
    void (*close)(gpointer owner);
    void (*changed)(gpointer owner);                     /* modified flag, name... */
    void (*message)(gpointer owner, const char *text);   /* for the status bar */
} DocToolbarOwner;

typedef struct DocToolbar DocToolbar;

/* the toolbar lives as long as its widget; cancellable stops its file dialogs */
DocToolbar *doc_toolbar_new(GtkWindow *window, GCancellable *cancellable,
                            const DocToolbarOwner *owner, gpointer owner_data);

GtkWidget *doc_toolbar_widget(DocToolbar *bar);

/* makes the "doc" actions and their keyboard shortcuts (Ctrl+S, Ctrl+C...)
 * work anywhere inside scope, e.g. the whole document panel */
void doc_toolbar_attach(DocToolbar *bar, GtkWidget *scope);

/* shows the tools of edit (NULL: none) and becomes its listener */
void doc_toolbar_bind(DocToolbar *bar, Edit *edit);

#endif
