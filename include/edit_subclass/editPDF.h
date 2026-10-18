/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.2
 * License: GPLv3
 * Last edited: 2026-10-16
 */

/* PDF editor (.pdf) backed by Poppler: view, annotate and save. */
#ifndef EDIT_PDF_H
#define EDIT_PDF_H

#include "../edit.h"

/* Forward declarations: whoever includes this header (edit.c, main.c)
 * does not need the Poppler headers. */
typedef struct _PopplerDocument PopplerDocument;
typedef struct _PopplerPage PopplerPage;

/* what a click / drag on a page does */
typedef enum {
    EDIT_PDF_TOOL_VIEW,        /* nothing: only read and scroll */
    EDIT_PDF_TOOL_HIGHLIGHT,   /* drag over text: highlight annotation */
    EDIT_PDF_TOOL_RECT,        /* drag: rectangle (square) annotation */
    EDIT_PDF_TOOL_NOTE,        /* click: sticky note with the toolbar text */
    EDIT_PDF_TOOL_ERASE,       /* click: removes the annotation under the cursor */
    EDIT_PDF_TOOL_COUNT
} EditPDFTool;

/* how a tool looks in the toolbar */
typedef struct {
    const char *label;         /* button text (an emoji) */
    const char *tooltip;
} EditPDFToolInfo;

/* one page and its widget: EditPDF and EditPDFPage point to each other,
 * so the name is declared here and the struct after EditPDF */
typedef struct EditPDFPage EditPDFPage;

typedef struct {
    Edit base;                 /* must be the first member */
    PopplerDocument *doc;      /* document loaded from memory, we own one reference */
    int n_pages;
    EditPDFPage *pages;        /* array of n_pages, NULL until open */
    double zoom;               /* pixels per PDF point (1 point = 1/72 inch) */
    EditPDFTool tool;          /* tool chosen in the toolbar */
    GtkWidget *pages_box;      /* vertical GtkBox with one GtkDrawingArea per page */
    GtkWidget *zoom_label;     /* "100 %" */
    GtkWidget *note_entry;     /* text of the next sticky note */
} EditPDF;

struct EditPDFPage {
    EditPDF *pdf;              /* owner, for the callbacks */
    int index;
    PopplerPage *page;         /* we own one reference */
    double width, height;      /* page size in points */
    GtkWidget *area;           /* GtkDrawingArea (owned by pages_box) */

    /* drag in progress, in widget coordinates */
    bool dragging;
    double x0, y0, x1, y1;
    cairo_surface_t *cache;    /* page rendered once at drag-begin, so the
                                * preview does not re-render the PDF per motion */
};

/* file suffixes EditPDF opens, without the dot, NULL-terminated */
extern const char *const edit_pdf_suffixes[];

Edit *new_edit_pdf(void);

#endif
