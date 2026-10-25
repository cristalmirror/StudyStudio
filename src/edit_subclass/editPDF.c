/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.3
 * License: GPLv3
 * Last edited: 2026-10-25
 */

/*
 * EditPDF: Poppler (poppler-glib) parses and paints the PDF with cairo,
 * creates / removes annotations and writes the file back.
 *
 * Two coordinate systems are used:
 *   - widget: pixels, origin at the top-left corner of the page widget;
 *   - PDF:    points (1/72 inch), origin at the BOTTOM-left corner, y up.
 * widget -> PDF:  x_pdf = x / zoom,  y_pdf = page_height - y / zoom
 */
#include <stdlib.h>
#include <math.h>
#include <glib/gstdio.h>       /* g_rename, g_remove */
#include <poppler.h>
#include "../../include/edit_subclass/editPDF.h"

const char *const edit_pdf_suffixes[] = { "pdf", NULL };

#define PDF_ZOOM_MIN   0.25
#define PDF_ZOOM_MAX   4.0
#define PDF_ZOOM_STEP  1.25
#define PDF_NOTE_SIZE  20.0    /* sticky note icon, in points */
#define PDF_LINE_WIDTH 1.0     /* default PDF border width, in points */

/* annotation colors, PopplerColor goes from 0 to 65535 per channel */
static const PopplerColor s_highlight_color = { 65535, 60000, 0 };   /* yellow */
static const PopplerColor s_rect_color      = { 60000, 0, 0 };       /* red */
static const PopplerColor s_note_color      = { 65535, 50000, 0 };   /* orange */

static const EditPDFToolInfo s_tools[EDIT_PDF_TOOL_COUNT] = {
    [EDIT_PDF_TOOL_VIEW]      = { "👁",  "Ver (sin editar)" },
    [EDIT_PDF_TOOL_HIGHLIGHT] = { "🖍",  "Resaltar texto (arrastrar)" },
    [EDIT_PDF_TOOL_RECT]      = { "▭",  "Rectángulo (arrastrar)" },
    [EDIT_PDF_TOOL_NOTE]      = { "🗒",  "Nota (clic, usa el texto de la barra)" },
    [EDIT_PDF_TOOL_ERASE]     = { "🧽", "Borrar anotación (clic)" },
};

/* ---------- pages ---------- */

static void _pdf_drop_cache(EditPDFPage *view) {
    if (view->cache != NULL) {
        cairo_surface_destroy(view->cache);
        view->cache = NULL;
    }
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
        return -1;
    }
    GBytes *bytes = g_bytes_new_take(contents, len);   /* bytes owns contents */
    PopplerDocument *doc = poppler_document_new_from_bytes(bytes, NULL, &error);
    g_bytes_unref(bytes);                              /* doc keeps its own ref */
    if (doc == NULL) {
        g_printerr("EditPDF: cannot open %s: %s\n", path, error->message);
        g_clear_error(&error);
        return -2;
    }

    int n = poppler_document_get_n_pages(doc);
    EditPDFPage *pages = calloc(n > 0 ? n : 1, sizeof *pages);
    if (pages == NULL) {
        g_object_unref(doc);
        return -3;
    }
    for (int i = 0; i < n; i++) {
        pages[i].pdf = pdf;
        pages[i].index = i;
        pages[i].page = poppler_document_get_page(doc, i);
        poppler_page_get_size(pages[i].page, &pages[i].width, &pages[i].height);
    }

    /* reopen: drop the previous document (its widgets are rebuilt by render) */
    _pdf_free_pages(pdf);
    if (pdf->doc != NULL) g_object_unref(pdf->doc);
    pdf->doc = doc;
    pdf->pages = pages;
    pdf->n_pages = n;
    if (self->widget != NULL) _pdf_build_pages(pdf);   /* reopen while shown */

    self->set_path(self, path);
    self->mark_modified(self, false);
    return 0;
}

static int _pdf_save(Edit *self) {
    EditPDF *pdf = (EditPDF *)self;
    GError *error = NULL;
    int result = -1;

    if (self->path == NULL || pdf->doc == NULL) return -1;

    /* write next to the file and rename it over: never leaves a half-written PDF */
    char *tmp = g_strconcat(self->path, ".saving", NULL);
    char *uri = g_filename_to_uri(tmp, NULL, &error);   /* Poppler wants a URI */

    if (uri == NULL || !poppler_document_save(pdf->doc, uri, &error)) {
        g_printerr("EditPDF: cannot save %s: %s\n", self->path,
                   error ? error->message : "?");
    } else if (g_rename(tmp, self->path) != 0) {
        g_printerr("EditPDF: cannot replace %s\n", self->path);
    } else {
        self->mark_modified(self, false);
        result = 0;
    }
    if (result != 0) g_remove(tmp);

    g_clear_error(&error);
    g_free(uri);
    g_free(tmp);
    return result;
}

/* ---------- coordinates ---------- */

static double _pdf_x(EditPDFPage *view, double x) {
    return x / view->pdf->zoom;
}

static double _pdf_y(EditPDFPage *view, double y) {
    return view->height - y / view->pdf->zoom;
}

/* rectangle in PDF coordinates from two widget points, in any order */
static PopplerRectangle _pdf_rect(EditPDFPage *view, double xa, double ya, double xb, double yb) {
    PopplerRectangle r;
    r.x1 = _pdf_x(view, MIN(xa, xb));
    r.x2 = _pdf_x(view, MAX(xa, xb));
    r.y1 = _pdf_y(view, MAX(ya, yb));   /* bottom: the larger widget y */
    r.y2 = _pdf_y(view, MIN(ya, yb));
    return r;
}

/* text under the drag, as rectangles in points with a TOP-left origin.
 * NULL or empty if there is no text there (e.g. a scanned page). */
static cairo_region_t *_pdf_text_region(EditPDFPage *view) {
    PopplerRectangle sel = {
        _pdf_x(view, view->x0), view->y0 / view->pdf->zoom,
        _pdf_x(view, view->x1), view->y1 / view->pdf->zoom,
    };
    return poppler_page_get_selected_region(view->page, 1.0, POPPLER_SELECTION_GLYPH, &sel);
}

/* ---------- annotations ---------- */

static void _pdf_add_annot(EditPDFPage *view, PopplerAnnot *annot) {
    poppler_page_add_annot(view->page, annot);
    g_object_unref(annot);                     /* the page keeps its own ref */
    view->pdf->base.mark_modified(&view->pdf->base, true);
    _pdf_drop_cache(view);
    gtk_widget_queue_draw(view->area);
}

static void _pdf_add_highlight(EditPDFPage *view) {
    EditPDF *pdf = view->pdf;
    GArray *quads = g_array_new(FALSE, FALSE, sizeof(PopplerQuadrilateral));
    PopplerRectangle box = { G_MAXDOUBLE, G_MAXDOUBLE, -G_MAXDOUBLE, -G_MAXDOUBLE };

    /* one quadrilateral per text rectangle, so the highlight follows the lines */
    cairo_region_t *region = _pdf_text_region(view);
    int n = region != NULL ? cairo_region_num_rectangles(region) : 0;
    for (int i = 0; i < n; i++) {
        cairo_rectangle_int_t r;
        cairo_region_get_rectangle(region, i, &r);
        double left = r.x, right = r.x + r.width;
        double top = view->height - r.y, bottom = view->height - (r.y + r.height);

        /* PDF order: top-left, top-right, bottom-left, bottom-right */
        PopplerQuadrilateral q = {
            { left, top }, { right, top }, { left, bottom }, { right, bottom }
        };
        g_array_append_val(quads, q);
        box.x1 = MIN(box.x1, left);   box.x2 = MAX(box.x2, right);
        box.y1 = MIN(box.y1, bottom); box.y2 = MAX(box.y2, top);
    }
    if (region != NULL) cairo_region_destroy(region);

    if (n == 0) {   /* no text under the drag: highlight the dragged rectangle */
        box = _pdf_rect(view, view->x0, view->y0, view->x1, view->y1);
        PopplerQuadrilateral q = {
            { box.x1, box.y2 }, { box.x2, box.y2 }, { box.x1, box.y1 }, { box.x2, box.y1 }
        };
        g_array_append_val(quads, q);
    }

    if (box.x2 - box.x1 >= 1.0 && box.y2 - box.y1 >= 1.0) {
        PopplerAnnot *annot = poppler_annot_text_markup_new_highlight(pdf->doc, &box, quads);
        poppler_annot_set_color(annot, (PopplerColor *)&s_highlight_color);
        _pdf_add_annot(view, annot);
    }
    g_array_free(quads, TRUE);
}

static void _pdf_add_rect(EditPDFPage *view) {
    PopplerRectangle r = _pdf_rect(view, view->x0, view->y0, view->x1, view->y1);
    if (r.x2 - r.x1 < 2.0 || r.y2 - r.y1 < 2.0) return;   /* just a click */

    PopplerAnnot *annot = poppler_annot_square_new(view->pdf->doc, &r);
    poppler_annot_set_color(annot, (PopplerColor *)&s_rect_color);
    _pdf_add_annot(view, annot);
}

static void _pdf_add_note(EditPDFPage *view, double x, double y) {
    const char *text = gtk_editable_get_text(GTK_EDITABLE(view->pdf->note_entry));
    if (text == NULL || *text == '\0') {
        g_printerr("EditPDF: write the note text in the toolbar first\n");
        return;
    }

    /* the icon's top-left corner goes where the user clicked */
    PopplerRectangle r = { _pdf_x(view, x), _pdf_y(view, y) - PDF_NOTE_SIZE,
                           _pdf_x(view, x) + PDF_NOTE_SIZE, _pdf_y(view, y) };
    PopplerAnnot *annot = poppler_annot_text_new(view->pdf->doc, &r);
    poppler_annot_set_contents(annot, text);
    poppler_annot_set_color(annot, (PopplerColor *)&s_note_color);
    poppler_annot_text_set_icon(POPPLER_ANNOT_TEXT(annot), POPPLER_ANNOT_TEXT_ICON_NOTE);
    _pdf_add_annot(view, annot);
    gtk_editable_set_text(GTK_EDITABLE(view->pdf->note_entry), "");
}

/* removes the topmost user annotation under (x, y); links and form fields stay */
static void _pdf_erase_at(EditPDFPage *view, double x, double y) {
    double px = x / view->pdf->zoom, py = y / view->pdf->zoom;   /* top-left origin */
    GList *mapping = poppler_page_get_annot_mapping(view->page);
    PopplerAnnot *hit = NULL;

    /* the list follows the drawing order: the last match is on top */
    for (GList *l = mapping; l != NULL; l = l->next) {
        PopplerAnnotMapping *m = l->data;
        PopplerAnnotType type = poppler_annot_get_annot_type(m->annot);
        if (type == POPPLER_ANNOT_LINK || type == POPPLER_ANNOT_WIDGET ||
            type == POPPLER_ANNOT_POPUP) continue;
        if (px >= m->area.x1 && px <= m->area.x2 && py >= m->area.y1 && py <= m->area.y2)
            hit = m->annot;
    }
    if (hit != NULL) {
        poppler_page_remove_annot(view->page, hit);
        view->pdf->base.mark_modified(&view->pdf->base, true);
        gtk_widget_queue_draw(view->area);
    }
    poppler_page_free_annot_mapping(mapping);
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

/* preview of the drag, in widget coordinates */
static void _pdf_draw_preview(EditPDFPage *view, cairo_t *cr) {
    double zoom = view->pdf->zoom;

    switch (view->pdf->tool) {
    case EDIT_PDF_TOOL_HIGHLIGHT: {
        cairo_region_t *region = _pdf_text_region(view);
        int n = region != NULL ? cairo_region_num_rectangles(region) : 0;
        cairo_set_source_rgba(cr, 1, 0.9, 0, 0.4);
        for (int i = 0; i < n; i++) {
            cairo_rectangle_int_t r;
            cairo_region_get_rectangle(region, i, &r);
            cairo_rectangle(cr, r.x * zoom, r.y * zoom, r.width * zoom, r.height * zoom);
        }
        if (n == 0)
            cairo_rectangle(cr, MIN(view->x0, view->x1), MIN(view->y0, view->y1),
                            fabs(view->x1 - view->x0), fabs(view->y1 - view->y0));
        cairo_fill(cr);
        if (region != NULL) cairo_region_destroy(region);
        break;
    }
    case EDIT_PDF_TOOL_RECT:
        cairo_set_source_rgb(cr, 0.9, 0, 0);
        cairo_set_line_width(cr, PDF_LINE_WIDTH * zoom);
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
    (void)area; (void)width; (void)height;

    if (view->cache != NULL) {
        cairo_set_source_surface(cr, view->cache, 0, 0);
        cairo_paint(cr);
    } else {
        _pdf_render_page(view, cr);
    }
    if (view->dragging) _pdf_draw_preview(view, cr);
}

/* ---------- mouse ---------- */

static void _pdf_on_drag_begin(GtkGestureDrag *gesture, double x, double y, gpointer user_data) {
    EditPDFPage *view = user_data;
    EditPDF *pdf = view->pdf;

    switch (pdf->tool) {
    case EDIT_PDF_TOOL_NOTE:
        _pdf_add_note(view, x, y);
        return;
    case EDIT_PDF_TOOL_ERASE:
        _pdf_erase_at(view, x, y);
        return;
    case EDIT_PDF_TOOL_HIGHLIGHT:
    case EDIT_PDF_TOOL_RECT:
        break;
    default:   /* view: let the scrolled window have the event */
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
    (void)gesture;
    if (!view->dragging) return;

    view->x1 = view->x0 + dx;
    view->y1 = view->y0 + dy;
    view->dragging = false;

    switch (view->pdf->tool) {
    case EDIT_PDF_TOOL_HIGHLIGHT: _pdf_add_highlight(view); break;
    case EDIT_PDF_TOOL_RECT:      _pdf_add_rect(view);      break;
    default: break;
    }
    _pdf_drop_cache(view);
    gtk_widget_queue_draw(view->area);
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
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *group = NULL;   /* first tool button: the others join its group */

    for (int t = 0; t < EDIT_PDF_TOOL_COUNT; t++) {
        GtkWidget *button = gtk_toggle_button_new_with_label(s_tools[t].label);
        gtk_widget_set_tooltip_text(button, s_tools[t].tooltip);
        g_object_set_data(G_OBJECT(button), "tool", GINT_TO_POINTER(t));
        if (group != NULL)
            gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(button), GTK_TOGGLE_BUTTON(group));
        else
            group = button;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), t == (int)pdf->tool);
        g_signal_connect(button, "toggled", G_CALLBACK(_pdf_on_tool_toggled), pdf);
        gtk_box_append(GTK_BOX(bar), button);
    }

    pdf->note_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(pdf->note_entry), "Texto de la nota...");
    gtk_widget_set_hexpand(pdf->note_entry, TRUE);
    gtk_box_append(GTK_BOX(bar), pdf->note_entry);

    GtkWidget *out = gtk_button_new_with_label("−");
    GtkWidget *in = gtk_button_new_with_label("+");
    pdf->zoom_label = gtk_label_new(NULL);
    gtk_widget_set_tooltip_text(out, "Alejar");
    gtk_widget_set_tooltip_text(in, "Acercar");
    g_signal_connect(out, "clicked", G_CALLBACK(_pdf_on_zoom_out), pdf);
    g_signal_connect(in, "clicked", G_CALLBACK(_pdf_on_zoom_in), pdf);
    gtk_box_append(GTK_BOX(bar), out);
    gtk_box_append(GTK_BOX(bar), pdf->zoom_label);
    gtk_box_append(GTK_BOX(bar), in);
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

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
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
    pdf->pages_box = NULL;
    pdf->zoom_label = NULL;
    pdf->note_entry = NULL;
}

/* constructor */
Edit *new_edit_pdf(void) {
    EditPDF *pdf = calloc(1, sizeof(EditPDF));  /* zeroed: every pointer NULL */
    if (pdf == NULL) return NULL;

    edit_init(&pdf->base);
    pdf->base.open = _pdf_open;
    pdf->base.save = _pdf_save;
    pdf->base.render = _pdf_render;
    pdf->base.finalize = _pdf_finalize;

    pdf->zoom = 1.0;
    pdf->tool = EDIT_PDF_TOOL_VIEW;
    return &pdf->base;   /* upcast */
}
