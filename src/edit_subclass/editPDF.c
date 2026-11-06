/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.3
 * License: GPLv3
 * Last edited: 2026-11-06
 */

/*
 * EditPDF: Poppler (poppler-glib) parses and paints the PDF with cairo,
 * creates / removes annotations and writes the file back.
 *
 * Poppler cannot change the page content (text, images), so a PDF is
 * read-only text: it can be selected and copied, and everything else is an
 * annotation stored in the file (highlight, shaded area, rectangle, note).
 * Inserting and reordering pages is delegated to qpdf (msc/pdfPages.c).
 * Poppler has no undo either: each change first keeps a copy of the whole
 * file in memory (undo / redo stacks of GBytes).
 *
 * Three coordinate systems are used:
 *   - widget: pixels, origin at the top-left corner of the page widget;
 *   - area:   points (1/72 inch), origin at the top-left corner (EditPDFArea);
 *   - PDF:    points, origin at the BOTTOM-left corner, y up.
 * widget -> area: divide by zoom;  area -> PDF: y_pdf = page_height - y
 */
#include <stdlib.h>
#include <math.h>
#include <glib/gstdio.h>       /* g_rename, g_remove, g_close */
#include <poppler.h>
#include "../../include/edit_subclass/editPDF.h"
#include "../../include/msc/pdfPages.h"

const char *const edit_pdf_suffixes[] = { "pdf", NULL };

#define PDF_ZOOM_MIN      0.25
#define PDF_ZOOM_MAX      4.0
#define PDF_ZOOM_STEP     1.25
#define PDF_NOTE_SIZE     20.0   /* sticky note icon, in points */
#define PDF_LINE_WIDTH    1.0    /* default PDF border width, in points */
#define PDF_SHADE_OPACITY 0.35   /* shaded areas must not hide the page */
#define PDF_REGION_SCALE  8.0    /* text regions come as ints: keep 1/8 pt */
#define PDF_HISTORY_MAX   20     /* snapshots kept for undo */

static const EditPDFToolInfo s_tools[EDIT_PDF_TOOL_COUNT] = {
    [EDIT_PDF_TOOL_VIEW]      = { "👁",  "Ver (sin editar)" },
    [EDIT_PDF_TOOL_SELECT]    = { "⬚",  "Seleccionar texto o área (arrastrar)" },
    [EDIT_PDF_TOOL_HIGHLIGHT] = { "🖍",  "Resaltar texto con el color elegido (arrastrar)" },
    [EDIT_PDF_TOOL_RECT]      = { "▭",  "Rectángulo con el color elegido (arrastrar)" },
    [EDIT_PDF_TOOL_NOTE]      = { "🗒",  "Nota (clic, usa el texto de la barra)" },
    [EDIT_PDF_TOOL_ERASE]     = { "🧽", "Borrar anotación (clic)" },
};

static PopplerColor _pdf_color(const GdkRGBA *c) {
    PopplerColor color = {
        (guint16)lround(CLAMP(c->red, 0.0, 1.0) * 65535),
        (guint16)lround(CLAMP(c->green, 0.0, 1.0) * 65535),
        (guint16)lround(CLAMP(c->blue, 0.0, 1.0) * 65535),
    };
    return color;
}

/* ---------- pages ---------- */

static void _pdf_drop_cache(EditPDFPage *view) {
    if (view->cache != NULL) {
        cairo_surface_destroy(view->cache);
        view->cache = NULL;
    }
}

/* the page changed: repaint it if it is on screen */
static void _pdf_repaint(EditPDFPage *view) {
    _pdf_drop_cache(view);
    if (view->area != NULL) gtk_widget_queue_draw(view->area);
}

/* frees the page array; the widgets belong to pages_box, not to it */
static void _pdf_free_pages(EditPDF *pdf) {
    for (int i = 0; i < pdf->n_pages && pdf->pages != NULL; i++) {
        EditPDFPage *view = &pdf->pages[i];
        _pdf_drop_cache(view);
        if (view->page != NULL) g_object_unref(view->page);
    }
    free(pdf->pages);
    pdf->pages = NULL;
    pdf->n_pages = 0;
}

static void _pdf_build_pages(EditPDF *pdf);
static void _pdf_apply_zoom(EditPDF *pdf);

static void _pdf_update_page_label(EditPDF *pdf) {
    if (pdf->page_label == NULL) return;
    char *text = pdf->current_page >= 0
        ? g_strdup_printf("Pág. %d / %d", pdf->current_page + 1, pdf->n_pages)
        : g_strdup_printf("%d págs.", pdf->n_pages);
    gtk_label_set_text(GTK_LABEL(pdf->page_label), text);
    g_free(text);
}

static void _pdf_redraw(EditPDF *pdf) {
    for (int i = 0; i < pdf->n_pages; i++) _pdf_repaint(&pdf->pages[i]);
}

/*
 * Replaces the document by the PDF in bytes. With the same number of pages
 * the page widgets are kept (their callbacks point into the array), so undo
 * of an annotation does not rebuild the view nor lose the scroll position.
 */
static int _pdf_load(EditPDF *pdf, GBytes *bytes, GError **error) {
    PopplerDocument *doc = poppler_document_new_from_bytes(bytes, NULL, error);
    if (doc == NULL) return EDIT_ERR_INVALID;   /* doc keeps its own ref to bytes */

    int n = poppler_document_get_n_pages(doc);
    if (pdf->pages != NULL && n == pdf->n_pages) {
        for (int i = 0; i < n; i++) {
            EditPDFPage *view = &pdf->pages[i];
            _pdf_drop_cache(view);
            g_object_unref(view->page);
            view->page = poppler_document_get_page(doc, i);
            poppler_page_get_size(view->page, &view->width, &view->height);
        }
        g_object_unref(pdf->doc);
        pdf->doc = doc;
        if (pdf->pages_box != NULL) {
            _pdf_apply_zoom(pdf);
            _pdf_redraw(pdf);
        }
    } else {
        EditPDFPage *pages = calloc(n > 0 ? n : 1, sizeof *pages);
        if (pages == NULL) {
            g_object_unref(doc);
            return EDIT_ERR_BACKEND;
        }
        for (int i = 0; i < n; i++) {
            pages[i].pdf = pdf;
            pages[i].index = i;
            pages[i].page = poppler_document_get_page(doc, i);
            poppler_page_get_size(pages[i].page, &pages[i].width, &pages[i].height);
        }
        _pdf_free_pages(pdf);
        if (pdf->doc != NULL) g_object_unref(pdf->doc);
        pdf->doc = doc;
        pdf->pages = pages;
        pdf->n_pages = n;
        if (pdf->pages_box != NULL) _pdf_build_pages(pdf);   /* reload while shown */
    }

    pdf->sel_page = -1;
    if (pdf->current_page >= n) pdf->current_page = n - 1;
    _pdf_update_page_label(pdf);
    return EDIT_OK;
}

/* ---------- history ---------- */

/* the whole document, annotations included, as it would be saved */
static GBytes *_pdf_snapshot(EditPDF *pdf, GError **error) {
    char *tmp = NULL;
    int fd = g_file_open_tmp("studystudio-XXXXXX.pdf", &tmp, error);
    if (fd < 0) return NULL;
    g_close(fd, NULL);

    GBytes *bytes = NULL;
    char *uri = g_filename_to_uri(tmp, NULL, error);   /* Poppler wants a URI */
    if (uri != NULL && poppler_document_save(pdf->doc, uri, error)) {
        char *data = NULL;
        gsize len = 0;
        if (g_file_get_contents(tmp, &data, &len, error))
            bytes = g_bytes_new_take(data, len);
    }
    g_remove(tmp);
    g_free(uri);
    g_free(tmp);
    return bytes;
}

/* before: the snapshot taken before a change that did happen */
static void _pdf_commit(EditPDF *pdf, GBytes *before) {
    g_ptr_array_add(pdf->undo, before);
    if (pdf->undo->len > PDF_HISTORY_MAX) g_ptr_array_remove_index(pdf->undo, 0);
    g_ptr_array_set_size(pdf->redo, 0);
    pdf->base.mark_modified(&pdf->base, true);
    pdf->base.notify(&pdf->base, NULL);
}

static GBytes *_pdf_begin(EditPDF *pdf) {
    GError *error = NULL;
    GBytes *before = _pdf_snapshot(pdf, &error);
    if (before == NULL) {
        g_printerr("EditPDF: cannot keep the undo copy: %s\n", error ? error->message : "?");
        pdf->base.notify(&pdf->base, "Cannot keep an undo copy: change cancelled");
        g_clear_error(&error);
    }
    return before;
}

/* moves the current state to `to` and loads the top of `from` */
static int _pdf_step(EditPDF *pdf, GPtrArray *from, GPtrArray *to) {
    GError *error = NULL;
    if (from->len == 0) return EDIT_ERR_INVALID;

    GBytes *now = _pdf_snapshot(pdf, &error);
    if (now == NULL) {
        g_clear_error(&error);
        return EDIT_ERR_IO;
    }
    GBytes *target = g_ptr_array_steal_index(from, from->len - 1);
    int rc = _pdf_load(pdf, target, &error);
    if (rc != EDIT_OK) {
        g_printerr("EditPDF: cannot restore: %s\n", error ? error->message : "?");
        g_clear_error(&error);
        g_ptr_array_add(from, target);
        g_bytes_unref(now);
        return rc;
    }
    g_bytes_unref(target);
    g_ptr_array_add(to, now);
    pdf->base.mark_modified(&pdf->base, true);
    pdf->base.notify(&pdf->base, NULL);
    return EDIT_OK;
}

static bool _pdf_can_undo(Edit *self) { return ((EditPDF *)self)->undo->len > 0; }
static bool _pdf_can_redo(Edit *self) { return ((EditPDF *)self)->redo->len > 0; }
static int _pdf_undo(Edit *self) { EditPDF *p = (EditPDF *)self; return _pdf_step(p, p->undo, p->redo); }
static int _pdf_redo(Edit *self) { EditPDF *p = (EditPDF *)self; return _pdf_step(p, p->redo, p->undo); }

static const EditHistory s_history = { _pdf_can_undo, _pdf_can_redo, _pdf_undo, _pdf_redo };

/* ---------- coordinates ---------- */

static EditPDFArea _pdf_area_from_drag(EditPDFPage *view) {
    double z = view->pdf->zoom;
    EditPDFArea a = {
        MIN(view->x0, view->x1) / z, MIN(view->y0, view->y1) / z,
        MAX(view->x0, view->x1) / z, MAX(view->y0, view->y1) / z,
    };
    return a;
}

static PopplerRectangle _pdf_rect(EditPDFPage *view, const EditPDFArea *a) {
    PopplerRectangle r = { a->x1, view->height - a->y2, a->x2, view->height - a->y1 };
    return r;
}

/* text under the area, as rectangles in points * PDF_REGION_SCALE with a
 * TOP-left origin. NULL or empty if there is no text (e.g. a scanned page). */
static cairo_region_t *_pdf_text_region(EditPDFPage *view, const EditPDFArea *a) {
    PopplerRectangle sel = { a->x1, a->y1, a->x2, a->y2 };
    return poppler_page_get_selected_region(view->page, PDF_REGION_SCALE,
                                            POPPLER_SELECTION_GLYPH, &sel);
}

/* ---------- annotations ---------- */

static void _pdf_add_annot(EditPDFPage *view, PopplerAnnot *annot) {
    poppler_page_add_annot(view->page, annot);
    g_object_unref(annot);                     /* the page keeps its own ref */
    _pdf_repaint(view);
}

/* the highlight follows the text lines; with no text, the whole area */
static void _pdf_add_highlight(EditPDFPage *view, const EditPDFArea *a, const GdkRGBA *color) {
    EditPDF *pdf = view->pdf;
    GArray *quads = g_array_new(FALSE, FALSE, sizeof(PopplerQuadrilateral));
    PopplerRectangle box = { G_MAXDOUBLE, G_MAXDOUBLE, -G_MAXDOUBLE, -G_MAXDOUBLE };

    cairo_region_t *region = _pdf_text_region(view, a);
    int n = region != NULL ? cairo_region_num_rectangles(region) : 0;
    for (int i = 0; i < n; i++) {
        cairo_rectangle_int_t r;
        cairo_region_get_rectangle(region, i, &r);
        double left = r.x / PDF_REGION_SCALE, right = (r.x + r.width) / PDF_REGION_SCALE;
        double top = view->height - r.y / PDF_REGION_SCALE;
        double bottom = view->height - (r.y + r.height) / PDF_REGION_SCALE;

        /* PDF order: top-left, top-right, bottom-left, bottom-right */
        PopplerQuadrilateral q = {
            { left, top }, { right, top }, { left, bottom }, { right, bottom }
        };
        g_array_append_val(quads, q);
        box.x1 = MIN(box.x1, left);   box.x2 = MAX(box.x2, right);
        box.y1 = MIN(box.y1, bottom); box.y2 = MAX(box.y2, top);
    }
    if (region != NULL) cairo_region_destroy(region);

    if (n == 0) {
        box = _pdf_rect(view, a);
        PopplerQuadrilateral q = {
            { box.x1, box.y2 }, { box.x2, box.y2 }, { box.x1, box.y1 }, { box.x2, box.y1 }
        };
        g_array_append_val(quads, q);
    }

    PopplerColor pc = _pdf_color(color);
    PopplerAnnot *annot = poppler_annot_text_markup_new_highlight(pdf->doc, &box, quads);
    poppler_annot_set_color(annot, &pc);
    _pdf_add_annot(view, annot);
    g_array_free(quads, TRUE);
}

/* square annotation; interior = true fills it (a shaded area) */
static void _pdf_add_square(EditPDFPage *view, const EditPDFArea *a, const GdkRGBA *color,
                            bool interior) {
    PopplerRectangle r = _pdf_rect(view, a);
    PopplerColor pc = _pdf_color(color);
    PopplerAnnot *annot = poppler_annot_square_new(view->pdf->doc, &r);
    poppler_annot_set_color(annot, &pc);
    if (interior) {
        poppler_annot_square_set_interior_color(POPPLER_ANNOT_SQUARE(annot), &pc);
        poppler_annot_markup_set_opacity(POPPLER_ANNOT_MARKUP(annot), PDF_SHADE_OPACITY);
    }
    _pdf_add_annot(view, annot);
}

static void _pdf_add_note(EditPDFPage *view, double x, double y, const char *text) {
    /* the icon's top-left corner goes to (x, y), in area points */
    EditPDFArea a = { x, y, x + PDF_NOTE_SIZE, y + PDF_NOTE_SIZE };
    PopplerRectangle r = _pdf_rect(view, &a);
    PopplerColor pc = _pdf_color(&view->pdf->color);
    PopplerAnnot *annot = poppler_annot_text_new(view->pdf->doc, &r);
    poppler_annot_set_contents(annot, text);
    poppler_annot_set_color(annot, &pc);
    poppler_annot_text_set_icon(POPPLER_ANNOT_TEXT(annot), POPPLER_ANNOT_TEXT_ICON_NOTE);
    _pdf_add_annot(view, annot);
}

/* user annotations only: links, form fields and popups stay */
static bool _pdf_is_user_annot(PopplerAnnot *annot) {
    PopplerAnnotType type = poppler_annot_get_annot_type(annot);
    return type != POPPLER_ANNOT_LINK && type != POPPLER_ANNOT_WIDGET &&
           type != POPPLER_ANNOT_POPUP;
}

/* highlight or filled square: what clear() removes */
static bool _pdf_is_markup(PopplerAnnot *annot) {
    PopplerAnnotType type = poppler_annot_get_annot_type(annot);
    if (type == POPPLER_ANNOT_HIGHLIGHT) return true;
    if (type != POPPLER_ANNOT_SQUARE) return false;
    PopplerColor *fill = poppler_annot_square_get_interior_color(POPPLER_ANNOT_SQUARE(annot));
    bool filled = fill != NULL;
    g_free(fill);
    return filled;
}

/* the annotation mapping uses PDF coordinates (bottom-left origin) */
static bool _pdf_overlaps(const PopplerRectangle *m, const PopplerRectangle *r) {
    return m->x1 <= r->x2 && m->x2 >= r->x1 && m->y1 <= r->y2 && m->y2 >= r->y1;
}

/* removes the topmost user annotation under (x, y) in area points */
static bool _pdf_erase_at(EditPDFPage *view, double x, double y) {
    PopplerRectangle point = { x, view->height - y, x, view->height - y };
    GList *mapping = poppler_page_get_annot_mapping(view->page);
    PopplerAnnot *hit = NULL;

    /* the list follows the drawing order: the last match is on top */
    for (GList *l = mapping; l != NULL; l = l->next) {
        PopplerAnnotMapping *m = l->data;
        if (_pdf_is_user_annot(m->annot) && _pdf_overlaps(&m->area, &point)) hit = m->annot;
    }
    if (hit != NULL) {
        poppler_page_remove_annot(view->page, hit);
        _pdf_repaint(view);
    }
    poppler_page_free_annot_mapping(mapping);
    return hit != NULL;
}

/* removes every highlight / shaded area touching the area; how many */
static int _pdf_clear_area(EditPDFPage *view, const EditPDFArea *a) {
    PopplerRectangle r = _pdf_rect(view, a);
    GList *mapping = poppler_page_get_annot_mapping(view->page);
    int removed = 0;
    for (GList *l = mapping; l != NULL; l = l->next) {
        PopplerAnnotMapping *m = l->data;
        if (_pdf_is_markup(m->annot) && _pdf_overlaps(&m->area, &r)) {
            poppler_page_remove_annot(view->page, m->annot);
            removed++;
        }
    }
    poppler_page_free_annot_mapping(mapping);
    if (removed > 0) _pdf_repaint(view);
    return removed;
}

/* ---------- selection, clipboard and markup ---------- */

static void _pdf_set_current(EditPDF *pdf, int index) {
    pdf->current_page = index;
    _pdf_update_page_label(pdf);
}

static void _pdf_clear_selection(EditPDF *pdf) {
    int old = pdf->sel_page;
    pdf->sel_page = -1;
    if (old >= 0 && old < pdf->n_pages && pdf->pages[old].area != NULL)
        gtk_widget_queue_draw(pdf->pages[old].area);
    if (old >= 0) pdf->base.notify(&pdf->base, NULL);
}

bool edit_pdf_select(Edit *self, int page, const EditPDFArea *area) {
    EditPDF *pdf = (EditPDF *)self;
    if (page < 0 || page >= pdf->n_pages || area == NULL ||
        area->x2 - area->x1 < 1.0 || area->y2 - area->y1 < 1.0) return false;

    _pdf_clear_selection(pdf);
    pdf->sel_page = page;
    pdf->sel = *area;
    _pdf_set_current(pdf, page);
    if (pdf->pages[page].area != NULL) gtk_widget_queue_draw(pdf->pages[page].area);
    pdf->base.notify(&pdf->base, NULL);
    return true;
}

static bool _pdf_has_selection(Edit *self) {
    return ((EditPDF *)self)->sel_page >= 0;
}

static int _pdf_copy(Edit *self) {
    EditPDF *pdf = (EditPDF *)self;
    if (pdf->sel_page < 0) return EDIT_ERR_NO_SELECTION;

    EditPDFPage *view = &pdf->pages[pdf->sel_page];
    PopplerRectangle r = { pdf->sel.x1, pdf->sel.y1, pdf->sel.x2, pdf->sel.y2 };
    char *text = poppler_page_get_selected_text(view->page, POPPLER_SELECTION_GLYPH, &r);
    if (text == NULL || *text == '\0') {
        g_free(text);
        self->notify(self, "There is no text in the selection (scanned page?)");
        return EDIT_ERR_INVALID;
    }
    GdkClipboard *clipboard = view->area != NULL ? gtk_widget_get_clipboard(view->area)
        : (gdk_display_get_default() ? gdk_display_get_clipboard(gdk_display_get_default()) : NULL);
    if (clipboard == NULL) {
        g_free(text);
        return EDIT_ERR_BACKEND;
    }
    gdk_clipboard_set_text(clipboard, text);
    g_free(text);
    return EDIT_OK;
}

/* Poppler cannot change page content: no cut, no paste */
static const EditClipboard s_clipboard = { _pdf_has_selection, _pdf_copy, NULL, NULL };

typedef enum { PDF_MARK_HIGHLIGHT, PDF_MARK_SHADE } PdfMark;

static int _pdf_mark(Edit *self, const GdkRGBA *color, PdfMark kind) {
    EditPDF *pdf = (EditPDF *)self;
    if (pdf->sel_page < 0) return EDIT_ERR_NO_SELECTION;
    GBytes *before = _pdf_begin(pdf);
    if (before == NULL) return EDIT_ERR_IO;

    EditPDFPage *view = &pdf->pages[pdf->sel_page];
    if (kind == PDF_MARK_HIGHLIGHT) _pdf_add_highlight(view, &pdf->sel, color);
    else _pdf_add_square(view, &pdf->sel, color, true);
    _pdf_commit(pdf, before);
    return EDIT_OK;
}

static int _pdf_highlight(Edit *self, const GdkRGBA *color) {
    return _pdf_mark(self, color, PDF_MARK_HIGHLIGHT);
}

static int _pdf_shade(Edit *self, const GdkRGBA *color) {
    return _pdf_mark(self, color, PDF_MARK_SHADE);
}

static int _pdf_clear(Edit *self) {
    EditPDF *pdf = (EditPDF *)self;
    if (pdf->sel_page < 0) return EDIT_ERR_NO_SELECTION;
    GBytes *before = _pdf_begin(pdf);
    if (before == NULL) return EDIT_ERR_IO;

    if (_pdf_clear_area(&pdf->pages[pdf->sel_page], &pdf->sel) > 0) {
        _pdf_commit(pdf, before);
    } else {
        g_bytes_unref(before);              /* nothing changed: no undo step */
    }
    return EDIT_OK;
}

static const EditMarkup s_markup = { _pdf_highlight, _pdf_shade, _pdf_clear };

/* note at the top-left corner of the selection */
static int _pdf_annotation(Edit *self, const char *text) {
    EditPDF *pdf = (EditPDF *)self;
    if (text == NULL || *text == '\0') return EDIT_ERR_INVALID;
    if (pdf->sel_page < 0) return EDIT_ERR_NO_SELECTION;
    GBytes *before = _pdf_begin(pdf);
    if (before == NULL) return EDIT_ERR_IO;

    _pdf_add_note(&pdf->pages[pdf->sel_page], pdf->sel.x1, pdf->sel.y1, text);
    _pdf_commit(pdf, before);
    return EDIT_OK;
}

static const EditInsert s_insert = { .annotation = _pdf_annotation };

/* ---------- combine: pages (qpdf) ---------- */

/* runs a qpdf operation on the current file and loads its result */
static int _pdf_restructure(EditPDF *pdf, GBytes *(*op)(GBytes *, gpointer, GError **),
                            gpointer data) {
    GBytes *before = _pdf_begin(pdf);
    if (before == NULL) return EDIT_ERR_IO;

    GError *error = NULL;
    GBytes *after = op(before, data, &error);
    int rc = EDIT_ERR_BACKEND;
    if (after != NULL) {
        rc = _pdf_load(pdf, after, &error);
        g_bytes_unref(after);
    }
    if (rc != EDIT_OK) {
        g_printerr("EditPDF: %s\n", error ? error->message : "page operation failed");
        pdf->base.notify(&pdf->base, error ? error->message : "Page operation failed");
        g_clear_error(&error);
        g_bytes_unref(before);
        return rc;
    }
    _pdf_commit(pdf, before);
    return EDIT_OK;
}

typedef struct { GBytes *other; int after; } PdfInsertArgs;
typedef struct { int from, to; } PdfMoveArgs;

static GBytes *_pdf_op_insert(GBytes *doc, gpointer data, GError **error) {
    PdfInsertArgs *args = data;
    return pdf_pages_insert(doc, args->other, args->after, error);
}

static GBytes *_pdf_op_move(GBytes *doc, gpointer data, GError **error) {
    PdfMoveArgs *args = data;
    return pdf_pages_move(doc, args->from, args->to, error);
}

/* every page of another PDF, after the current page (or at the end) */
static int _pdf_merge_file(Edit *self, const char *path) {
    EditPDF *pdf = (EditPDF *)self;
    char *contents = NULL;
    gsize len = 0;
    if (!g_file_get_contents(path, &contents, &len, NULL)) return EDIT_ERR_IO;

    PdfInsertArgs args = {
        g_bytes_new_take(contents, len),
        pdf->current_page >= 0 ? pdf->current_page : pdf->n_pages - 1,
    };
    int before_pages = pdf->n_pages;
    int rc = _pdf_restructure(pdf, _pdf_op_insert, &args);
    g_bytes_unref(args.other);
    if (rc == EDIT_OK) {
        char *msg = g_strdup_printf("%d pages inserted after page %d",
                                    pdf->n_pages - before_pages, args.after + 1);
        self->notify(self, msg);
        g_free(msg);
    }
    return rc;
}

static int _pdf_move_page(Edit *self, int delta) {
    EditPDF *pdf = (EditPDF *)self;
    if (pdf->current_page < 0) {
        self->notify(self, "Click the page to move first");
        return EDIT_ERR_INVALID;
    }
    PdfMoveArgs args = { pdf->current_page, pdf->current_page + delta };
    if (args.to < 0 || args.to >= pdf->n_pages) return EDIT_ERR_INVALID;

    int rc = _pdf_restructure(pdf, _pdf_op_move, &args);
    if (rc == EDIT_OK) _pdf_set_current(pdf, args.to);   /* follow the page */
    return rc;
}

static const EditCombine s_combine = {
    edit_pdf_suffixes, _pdf_merge_file, NULL, NULL, _pdf_move_page,
};

/* ---------- open / save ---------- */

static int _pdf_open(Edit *self, const char *path) {
    EditPDF *pdf = (EditPDF *)self;
    GError *error = NULL;
    char *contents = NULL;
    gsize len = 0;

    /* load the whole file in memory: Poppler then never keeps the file open,
     * so save can replace it (Windows cannot rename over an open file) */
    if (!g_file_get_contents(path, &contents, &len, &error)) {
        g_printerr("EditPDF: cannot open %s: %s\n", path, error->message);
        g_clear_error(&error);
        return EDIT_ERR_IO;
    }
    GBytes *bytes = g_bytes_new_take(contents, len);   /* bytes owns contents */
    int rc = _pdf_load(pdf, bytes, &error);
    g_bytes_unref(bytes);
    if (rc != EDIT_OK) {
        g_printerr("EditPDF: cannot open %s: %s\n", path, error ? error->message : "?");
        g_clear_error(&error);
        return rc;
    }

    g_ptr_array_set_size(pdf->undo, 0);
    g_ptr_array_set_size(pdf->redo, 0);
    self->combine = pdf_pages_available() ? &s_combine : NULL;
    self->set_path(self, path);
    self->mark_modified(self, false);
    return EDIT_OK;
}

static int _pdf_save_to(Edit *self, const char *path) {
    EditPDF *pdf = (EditPDF *)self;
    GError *error = NULL;
    int result = EDIT_ERR_IO;

    if (pdf->doc == NULL) return EDIT_ERR_INVALID;

    /* write next to the file and rename it over: never leaves a half-written PDF */
    char *tmp = g_strconcat(path, ".saving", NULL);
    char *uri = g_filename_to_uri(tmp, NULL, &error);   /* Poppler wants a URI */

    if (uri == NULL || !poppler_document_save(pdf->doc, uri, &error)) {
        g_printerr("EditPDF: cannot save %s: %s\n", path, error ? error->message : "?");
    } else if (g_rename(tmp, path) != 0) {
        g_printerr("EditPDF: cannot replace %s\n", path);
    } else {
        result = EDIT_OK;
    }
    if (result != EDIT_OK) g_remove(tmp);

    g_clear_error(&error);
    g_free(uri);
    g_free(tmp);
    return result;
}

/* ---------- drawing ---------- */

static void _pdf_render_page(EditPDFPage *view, cairo_t *cr) {
    cairo_save(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);      /* PDF pages have no background */
    cairo_paint(cr);
    cairo_scale(cr, view->pdf->zoom, view->pdf->zoom);
    poppler_page_render(view->page, cr);
    cairo_restore(cr);
}

/* text rectangles of an area, or the area itself, filled with the source */
static void _pdf_fill_area(EditPDFPage *view, cairo_t *cr, const EditPDFArea *a) {
    double k = view->pdf->zoom / PDF_REGION_SCALE;
    cairo_region_t *region = _pdf_text_region(view, a);
    int n = region != NULL ? cairo_region_num_rectangles(region) : 0;
    for (int i = 0; i < n; i++) {
        cairo_rectangle_int_t r;
        cairo_region_get_rectangle(region, i, &r);
        cairo_rectangle(cr, r.x * k, r.y * k, r.width * k, r.height * k);
    }
    if (n == 0) {
        double z = view->pdf->zoom;
        cairo_rectangle(cr, a->x1 * z, a->y1 * z, (a->x2 - a->x1) * z, (a->y2 - a->y1) * z);
    }
    cairo_fill(cr);
    if (region != NULL) cairo_region_destroy(region);
}

/* preview of the drag, in widget coordinates */
static void _pdf_draw_preview(EditPDFPage *view, cairo_t *cr) {
    EditPDF *pdf = view->pdf;
    EditPDFArea a = _pdf_area_from_drag(view);
    const GdkRGBA *c = &pdf->color;

    switch (pdf->tool) {
    case EDIT_PDF_TOOL_SELECT:
        cairo_set_source_rgba(cr, 0.21, 0.52, 0.89, 0.30);
        _pdf_fill_area(view, cr, &a);
        break;
    case EDIT_PDF_TOOL_HIGHLIGHT:
        cairo_set_source_rgba(cr, c->red, c->green, c->blue, 0.4);
        _pdf_fill_area(view, cr, &a);
        break;
    case EDIT_PDF_TOOL_RECT:
        cairo_set_source_rgb(cr, c->red, c->green, c->blue);
        cairo_set_line_width(cr, PDF_LINE_WIDTH * pdf->zoom);
        cairo_rectangle(cr, MIN(view->x0, view->x1), MIN(view->y0, view->y1),
                        fabs(view->x1 - view->x0), fabs(view->y1 - view->y0));
        cairo_stroke(cr);
        break;
    default:
        break;
    }
}

static void _pdf_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    EditPDFPage *view = user_data;
    EditPDF *pdf = view->pdf;
    (void)area; (void)width; (void)height;

    if (view->cache != NULL) {
        cairo_set_source_surface(cr, view->cache, 0, 0);
        cairo_paint(cr);
    } else {
        _pdf_render_page(view, cr);
    }
    if (pdf->sel_page == view->index && !view->dragging) {
        cairo_set_source_rgba(cr, 0.21, 0.52, 0.89, 0.30);
        _pdf_fill_area(view, cr, &pdf->sel);
    }
    if (view->dragging) _pdf_draw_preview(view, cr);
}

/* ---------- mouse ---------- */

static void _pdf_on_drag_begin(GtkGestureDrag *gesture, double x, double y, gpointer user_data) {
    EditPDFPage *view = user_data;
    EditPDF *pdf = view->pdf;
    double z = pdf->zoom;

    if (pdf->tool != EDIT_PDF_TOOL_VIEW) _pdf_set_current(pdf, view->index);

    switch (pdf->tool) {
    case EDIT_PDF_TOOL_NOTE: {
        const char *text = gtk_editable_get_text(GTK_EDITABLE(pdf->note_entry));
        if (text == NULL || *text == '\0') {
            pdf->base.notify(&pdf->base, "Write the note text in the PDF toolbar first");
            return;
        }
        GBytes *before = _pdf_begin(pdf);
        if (before == NULL) return;
        _pdf_add_note(view, x / z, y / z, text);
        _pdf_commit(pdf, before);
        gtk_editable_set_text(GTK_EDITABLE(pdf->note_entry), "");
        return;
    }
    case EDIT_PDF_TOOL_ERASE: {
        GBytes *before = _pdf_begin(pdf);
        if (before == NULL) return;
        if (_pdf_erase_at(view, x / z, y / z)) _pdf_commit(pdf, before);
        else g_bytes_unref(before);
        return;
    }
    case EDIT_PDF_TOOL_SELECT:
    case EDIT_PDF_TOOL_HIGHLIGHT:
    case EDIT_PDF_TOOL_RECT:
        break;
    default:   /* view: let the scrolled window have the event */
        _pdf_set_current(pdf, view->index);
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
        return;
    }

    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    view->dragging = true;
    view->x0 = view->x1 = x;
    view->y0 = view->y1 = y;

    /* render the page once; every motion only paints the cache + preview */
    int w = gtk_widget_get_width(view->area), h = gtk_widget_get_height(view->area);
    _pdf_drop_cache(view);
    view->cache = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t *cr = cairo_create(view->cache);
    _pdf_render_page(view, cr);
    cairo_destroy(cr);
}

static void _pdf_on_drag_update(GtkGestureDrag *gesture, double dx, double dy, gpointer user_data) {
    EditPDFPage *view = user_data;
    (void)gesture;
    if (!view->dragging) return;

    view->x1 = view->x0 + dx;
    view->y1 = view->y0 + dy;
    gtk_widget_queue_draw(view->area);
}

static void _pdf_on_drag_end(GtkGestureDrag *gesture, double dx, double dy, gpointer user_data) {
    EditPDFPage *view = user_data;
    EditPDF *pdf = view->pdf;
    (void)gesture;
    if (!view->dragging) return;

    view->x1 = view->x0 + dx;
    view->y1 = view->y0 + dy;
    view->dragging = false;
    _pdf_drop_cache(view);
    gtk_widget_queue_draw(view->area);

    EditPDFArea a = _pdf_area_from_drag(view);
    bool is_click = a.x2 - a.x1 < 2.0 || a.y2 - a.y1 < 2.0;

    if (pdf->tool == EDIT_PDF_TOOL_SELECT) {
        if (is_click) _pdf_clear_selection(pdf);
        else edit_pdf_select(&pdf->base, view->index, &a);
        return;
    }
    if (is_click) return;

    GBytes *before = _pdf_begin(pdf);
    if (before == NULL) return;
    if (pdf->tool == EDIT_PDF_TOOL_HIGHLIGHT) _pdf_add_highlight(view, &a, &pdf->color);
    else _pdf_add_square(view, &a, &pdf->color, false);
    _pdf_commit(pdf, before);
}

/* ---------- toolbar ---------- */

static void _pdf_apply_zoom(EditPDF *pdf) {
    for (int i = 0; i < pdf->n_pages; i++) {
        EditPDFPage *view = &pdf->pages[i];
        if (view->area == NULL) continue;
        gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(view->area),
                                           (int)ceil(view->width * pdf->zoom));
        gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(view->area),
                                            (int)ceil(view->height * pdf->zoom));
    }
    char *text = g_strdup_printf("%d %%", (int)lround(pdf->zoom * 100));
    gtk_label_set_text(GTK_LABEL(pdf->zoom_label), text);
    g_free(text);
}

static void _pdf_on_zoom_in(GtkButton *button, gpointer user_data) {
    EditPDF *pdf = user_data;
    (void)button;
    pdf->zoom = MIN(pdf->zoom * PDF_ZOOM_STEP, PDF_ZOOM_MAX);
    _pdf_apply_zoom(pdf);
}

static void _pdf_on_zoom_out(GtkButton *button, gpointer user_data) {
    EditPDF *pdf = user_data;
    (void)button;
    pdf->zoom = MAX(pdf->zoom / PDF_ZOOM_STEP, PDF_ZOOM_MIN);
    _pdf_apply_zoom(pdf);
}

static void _pdf_on_tool_toggled(GtkToggleButton *button, gpointer user_data) {
    EditPDF *pdf = user_data;
    if (gtk_toggle_button_get_active(button))
        pdf->tool = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "tool"));
}

static GtkWidget *_pdf_toolbar(EditPDF *pdf) {
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *tools = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *group = NULL;   /* first tool button: the others join its group */
    gtk_widget_add_css_class(bar, "context-bar");
    gtk_widget_add_css_class(tools, "linked");

    for (int t = 0; t < EDIT_PDF_TOOL_COUNT; t++) {
        GtkWidget *button = gtk_toggle_button_new_with_label(s_tools[t].label);
        gtk_widget_set_tooltip_text(button, s_tools[t].tooltip);
        gtk_widget_add_css_class(button, "tool-toggle");
        g_object_set_data(G_OBJECT(button), "tool", GINT_TO_POINTER(t));
        if (group != NULL)
            gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(button), GTK_TOGGLE_BUTTON(group));
        else
            group = button;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), t == (int)pdf->tool);
        g_signal_connect(button, "toggled", G_CALLBACK(_pdf_on_tool_toggled), pdf);
        gtk_box_append(GTK_BOX(tools), button);
    }
    gtk_box_append(GTK_BOX(bar), tools);

    pdf->note_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(pdf->note_entry), "Texto de la nota...");
    gtk_widget_set_hexpand(pdf->note_entry, TRUE);
    gtk_box_append(GTK_BOX(bar), pdf->note_entry);

    pdf->page_label = gtk_label_new(NULL);
    gtk_widget_add_css_class(pdf->page_label, "dim-label");
    gtk_box_append(GTK_BOX(bar), pdf->page_label);

    GtkWidget *zoom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(zoom, "linked");
    GtkWidget *out = gtk_button_new_with_label("−");
    GtkWidget *in = gtk_button_new_with_label("+");
    pdf->zoom_label = gtk_label_new(NULL);
    gtk_widget_add_css_class(pdf->zoom_label, "zoom-label");
    gtk_widget_set_tooltip_text(out, "Alejar");
    gtk_widget_set_tooltip_text(in, "Acercar");
    g_signal_connect(out, "clicked", G_CALLBACK(_pdf_on_zoom_out), pdf);
    g_signal_connect(in, "clicked", G_CALLBACK(_pdf_on_zoom_in), pdf);
    gtk_box_append(GTK_BOX(zoom), out);
    gtk_box_append(GTK_BOX(zoom), pdf->zoom_label);
    gtk_box_append(GTK_BOX(zoom), in);
    gtk_box_append(GTK_BOX(bar), zoom);
    return bar;
}

/* ---------- render ---------- */

/* one GtkDrawingArea per page, inside pages_box */
static void _pdf_build_pages(EditPDF *pdf) {
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(pdf->pages_box)) != NULL)
        gtk_box_remove(GTK_BOX(pdf->pages_box), child);

    for (int i = 0; i < pdf->n_pages; i++) {
        EditPDFPage *view = &pdf->pages[i];
        view->area = gtk_drawing_area_new();
        gtk_widget_set_halign(view->area, GTK_ALIGN_CENTER);
        gtk_widget_add_css_class(view->area, "pdf-page");
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(view->area), _pdf_draw, view, NULL);

        GtkGesture *drag = gtk_gesture_drag_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_PRIMARY);
        g_signal_connect(drag, "drag-begin", G_CALLBACK(_pdf_on_drag_begin), view);
        g_signal_connect(drag, "drag-update", G_CALLBACK(_pdf_on_drag_update), view);
        g_signal_connect(drag, "drag-end", G_CALLBACK(_pdf_on_drag_end), view);
        gtk_widget_add_controller(view->area, GTK_EVENT_CONTROLLER(drag));   /* area owns it */

        gtk_box_append(GTK_BOX(pdf->pages_box), view->area);
    }
    _pdf_apply_zoom(pdf);
    _pdf_update_page_label(pdf);
}

static void _pdf_render(Edit *self, GtkWidget *container) {
    EditPDF *pdf = (EditPDF *)self;

    if (self->widget != NULL) return;          /* already rendered */
    if (!GTK_IS_BOX(container)) {
        g_printerr("EditPDF: container must be a GtkBox\n");
        return;
    }

    pdf->pages_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);   /* gap between pages */
    gtk_widget_set_margin_top(pdf->pages_box, 12);
    gtk_widget_set_margin_bottom(pdf->pages_box, 12);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), pdf->pages_box);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_add_css_class(scroll, "editor-surface");
    gtk_widget_add_css_class(scroll, "pdf-surface");

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(box), _pdf_toolbar(pdf));
    gtk_box_append(GTK_BOX(box), scroll);
    gtk_widget_set_vexpand(box, TRUE);

    _pdf_build_pages(pdf);
    gtk_box_append(GTK_BOX(container), box);
    self->set_widget(self, box);
}

/* virtual destructor: only frees what EditPDF added.
 * The widgets are destroyed with self->widget (close() removes it first). */
static void _pdf_finalize(Edit *self) {
    EditPDF *pdf = (EditPDF *)self;
    _pdf_free_pages(pdf);
    if (pdf->doc != NULL) {
        g_object_unref(pdf->doc);
        pdf->doc = NULL;
    }
    g_ptr_array_unref(pdf->undo);
    g_ptr_array_unref(pdf->redo);
    pdf->pages_box = NULL;
    pdf->zoom_label = NULL;
    pdf->page_label = NULL;
    pdf->note_entry = NULL;
}

static void _pdf_set_color(Edit *self, const GdkRGBA *color) {
    ((EditPDF *)self)->color = *color;
}

/* constructor */
Edit *new_edit_pdf(void) {
    EditPDF *pdf = calloc(1, sizeof(EditPDF));  /* zeroed: every pointer NULL */
    if (pdf == NULL) return NULL;

    edit_init(&pdf->base);
    pdf->base.open = _pdf_open;
    pdf->base.save_to = _pdf_save_to;
    pdf->base.render = _pdf_render;
    pdf->base.finalize = _pdf_finalize;
    pdf->base.history = &s_history;
    pdf->base.clipboard = &s_clipboard;
    pdf->base.markup = &s_markup;
    pdf->base.insert = &s_insert;
    pdf->base.set_color = _pdf_set_color;

    pdf->zoom = 1.0;
    pdf->tool = EDIT_PDF_TOOL_VIEW;
    pdf->color = (GdkRGBA){ 1.0, 0.92, 0.0, 1.0 };   /* yellow */
    pdf->current_page = -1;
    pdf->sel_page = -1;
    pdf->undo = g_ptr_array_new_with_free_func((GDestroyNotify)g_bytes_unref);
    pdf->redo = g_ptr_array_new_with_free_func((GDestroyNotify)g_bytes_unref);
    return &pdf->base;   /* upcast */
}
