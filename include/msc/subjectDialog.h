/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.3
 * License: GPLv3
 * Last edited: 2026-10-18
 */

/* Modal window that asks for the data of a new subject. */
#ifndef SUBJECT_DIALOG_H
#define SUBJECT_DIALOG_H

#include <stdbool.h>
#include <gtk/gtk.h>
#include "../edit.h"

/*
 * Called when the user presses "Crear". chosen[i] is true if edit_kinds[i]
 * was ticked. Return true to close the dialog, false to keep it open
 * (e.g. the subject already exists) so the user can fix the data.
 */
typedef bool (*SubjectDialogDone)(const char *name, const char *base_dir,
                                  const bool *chosen, gpointer user_data);

typedef struct {
    GtkWidget *window;
    GtkWidget *name_entry;
    GtkWidget *dir_label;         /* shows base_dir */
    GtkWidget *error_label;       /* hidden until a check fails */
    GtkWidget **checks;           /* one per edit_kinds[] */
    char *base_dir;               /* owned: folder that will hold the subject */
    GCancellable *cancellable;    /* stops the folder chooser if we die first */
    SubjectDialogDone on_done;
    gpointer user_data;           /* passed back to on_done */
} SubjectDialog;

/* builds and shows the dialog; it frees itself when its window is destroyed */
SubjectDialog *new_subject_dialog(GtkWindow *parent, SubjectDialogDone on_done,
                                  gpointer user_data);

#endif
