/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.0
 * License: GPLv3
 * Last edited: 2026-10-12
 */

/* Plain-text document editor (.txt, .md) based on GtkTextView. */
#ifndef EDIT_TXT_H
#define EDIT_TXT_H

#include "../edit.h"

typedef struct {
    Edit base;               /* must be the first member */
    GtkTextBuffer *buffer;   /* document text, we own one reference */
    gulong modified_id;      /* "modified-changed" handler id */
} EditTXT;

/* file suffixes EditTXT opens, without the dot, NULL-terminated */
extern const char *const edit_txt_suffixes[];

Edit *new_edit_txt(void);

#endif
