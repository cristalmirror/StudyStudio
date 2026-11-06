/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.4
 * License: GPLv3
 * Last edited: 2026-11-06
 */

/* Office document editor (.docx, .odt, .xlsx, .pptx...) backed by LibreOfficeKit. */
#ifndef EDIT_DOC_H
#define EDIT_DOC_H

#include "../edit.h"

/* Forward declaration: whoever includes this header (edit.c, main.c)
 * does not need the LibreOfficeKit headers. */
typedef struct _LibreOfficeKitDocument LibreOfficeKitDocument;

/* shared with LibreOffice's thread, which sends the callbacks (editDOC.c) */
typedef struct EditDOCLink EditDOCLink;

typedef struct {
    Edit base;                    /* must be the first member */
    LibreOfficeKitDocument *lok;  /* document loaded by LibreOffice */
    int doc_type;                 /* LOK_DOCTYPE_TEXT, _SPREADSHEET, _PRESENTATION... */
    long width_twips;             /* size of the current part, 1 twip = 1/1440 inch */
    long height_twips;
    double zoom;                  /* pixels per twip */
    bool fit_width;               /* zoom follows the widget width */
    EditDOCLink *link;            /* we own one reference */

    GtkWidget *area;              /* GtkDrawingArea showing the visible part */
    GtkAdjustment *hadj, *vadj;   /* scroll position, in pixels; we own a ref */
    GtkWidget *hbar, *vbar;       /* their scrollbars, hidden when all fits */
    GtkWidget *zoom_label;
    GtkWidget *parts;             /* GtkDropDown: sheets / slides, NULL for text */
    GtkIMContext *im;             /* turns key presses into text (accents, compose) */
    cairo_surface_t *tile;        /* last painted view, kept until invalidated */
    bool tile_dirty;
    bool updating_parts;          /* we are filling the drop-down ourselves */
    bool follow_cursor;           /* scroll to the cursor once it moves */
    bool mouse_down;
    int pressed_key;              /* LOK key code of the key held down */

    /* LibreOffice's view state, from its callbacks, in twips */
    GdkRectangle cursor;
    bool cursor_visible;
    GArray *selection;            /* GdkRectangle: selected text / cells */
    GdkRectangle cell_cursor;     /* spreadsheets: the active cell */
    bool has_cell_cursor;
    GdkRectangle graphic;         /* selected image or shape */
    bool has_graphic;
    bool can_undo, can_redo;
    bool in_table;                /* Writer: the cursor is inside a table */
    bool expect_merge_dialog;     /* Calc asks before merging filled cells */
} EditDOC;

/* file suffixes EditDOC opens, without the dot, NULL-terminated */
extern const char *const edit_doc_suffixes[];

Edit *new_edit_doc(void);
bool edit_doc_available(void);    /* true if LibreOffice was found and started */
bool edit_doc_started(void);      /* true if LibreOffice is running (no lok_init attempt) */

#endif
