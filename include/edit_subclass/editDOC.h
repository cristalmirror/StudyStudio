
/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.0
 * License: GPLv3
 * Last edited: 2026-10-11
 */

/* Plain-text document editor (.txt, .md) based on GtkTextView. */
#ifndef EDIT_DOC_H
#define EDIT_DOC_H


#include "../edit.h"

typedef struct {
    Edit base;          /* must be the first member */
    GtkTextBuffer *buffer;   /* document text, we own one reference */
    gulong modified_id;      /* "modified-changed" handler id */
    /* DOC-specific fields */
} EditDOC;

Edit *new_edit_doc(void);

#endif