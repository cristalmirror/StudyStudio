/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.3
 * License: GPLv3
 * Last edited: 2026-10-12
 */

/* Office document editor (.docx, .odt, .xlsx, .pptx...) backed by LibreOfficeKit. */
#ifndef EDIT_DOC_H
#define EDIT_DOC_H

#include "../edit.h"

/* Forward declaration: whoever includes this header (edit.c, main.c)
 * does not need the LibreOfficeKit headers. */
typedef struct _LibreOfficeKitDocument LibreOfficeKitDocument;

typedef struct {
    Edit base;                    /* must be the first member */
    LibreOfficeKitDocument *lok;  /* document loaded by LibreOffice */
    long width_twips;             /* document size, 1 twip = 1/1440 inch */
    long height_twips;
    GtkWidget *area;              /* GtkDrawingArea showing the visible part */
    GtkAdjustment *vadj;          /* vertical scroll position, in pixels */
} EditDOC;

/* file suffixes EditDOC opens, without the dot, NULL-terminated */
extern const char *const edit_doc_suffixes[];

Edit *new_edit_doc(void);
bool edit_doc_available(void);    /* true if LibreOffice was found and started */

#endif
