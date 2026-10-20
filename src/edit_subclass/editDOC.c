/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.3
 * License: GPLv3
 * Last edited: 2026-10-18
 */

/*
 * EditDOC: LibreOffice does the real work through LibreOfficeKit (LOK).
 * It loads the file, paints it into a pixel buffer and saves it back.
 */
#define LOK_USE_UNSTABLE_API   /* paintTile, postKeyEvent... are behind it */
#include <stdlib.h>
#include <stdbool.h>           /* LibreOfficeKit.h uses bool without including it */
#include <LibreOfficeKit/LibreOfficeKitInit.h>   /* lok_init(): include in ONE .c only */
#include <LibreOfficeKit/LibreOfficeKitEnums.h>  /* LOK_CALLBACK_*, LOK_KEYEVENT_*... */
#include "../../include/edit_subclass/editDOC.h"

const char *const edit_doc_suffixes[] = {
    "docx", "doc", "odt", "rtf",    /* Writer */
    "xlsx", "xls", "ods",           /* Calc */
    "pptx", "ppt", "odp",           /* Impress */
    NULL
};

/* One LibreOffice per process: lok_init() must run only once. */
static LibreOfficeKit *s_office = NULL;
static bool s_office_tried = false;

static const char *const s_lo_paths[] = {
#ifdef _WIN32
    "C:\\Program Files\\LibreOffice\\program",
#else
    "/usr/lib64/libreoffice/program",   /* Fedora */
    "/usr/lib/libreoffice/program",     /* Debian, Ubuntu */
    "/opt/libreoffice/program",
#endif
    NULL
};

static LibreOfficeKit *_doc_office(void) {
    if (s_office_tried) return s_office;
    s_office_tried = true;

    const char *env = g_getenv("STUDYSTUDIO_LO_PATH");
    if (env != NULL) s_office = lok_init(env);

    for (int i = 0; s_office == NULL && s_lo_paths[i] != NULL; i++) {
        if (g_file_test(s_lo_paths[i], G_FILE_TEST_IS_DIR))
            s_office = lok_init(s_lo_paths[i]);
    }
    if (s_office == NULL)
        g_printerr("EditDOC: LibreOffice not found (set STUDYSTUDIO_LO_PATH)\n");
    return s_office;
}

bool edit_doc_available(void) {
    return _doc_office() != NULL;
}

/* unlike edit_doc_available(), never starts LibreOffice */
bool edit_doc_started(void) {
    return s_office != NULL;
}

static int _doc_open(Edit *self, const char *path) {
    EditDOC *doc = (EditDOC *)self;
    LibreOfficeKit *office = _doc_office();
    if (office == NULL) return -1;

    LibreOfficeKitDocument *lok = office->pClass->documentLoad(office, path);
    if (lok == NULL) {
        char *err = office->pClass->getError(office);
        g_printerr("EditDOC: cannot open %s: %s\n", path, err ? err : "?");
        office->pClass->freeError(err);   /* allocated by LO: LO frees it */
        return -2;
    }

    if (doc->lok != NULL) doc->lok->pClass->destroy(doc->lok);   /* reopen */
    doc->lok = lok;
    lok->pClass->initializeForRendering(lok, NULL);
    lok->pClass->getDocumentSize(lok, &doc->width_twips, &doc->height_twips);

    self->set_path(self, path);
    self->mark_modified(self, false);
    return 0;
}

static int _doc_save(Edit *self) {
    EditDOC *doc = (EditDOC *)self;
    if (self->path == NULL || doc->lok == NULL) return -1;

    /* format NULL: LibreOffice picks the filter from the extension */
    if (!doc->lok->pClass->saveAs(doc->lok, self->path, NULL, NULL)) {
        g_printerr("EditDOC: cannot save %s\n", self->path);
        return -1;
    }
    self->mark_modified(self, false);
    return 0;
}

/* Paints only the visible part: LOK renders any rectangle of the document. */
static void _doc_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    EditDOC *doc = user_data;
    (void)area;
    if (doc->lok == NULL || doc->width_twips <= 0) return;

    double zoom = (double)width / doc->width_twips;   /* fit width: px per twip */
    double top = gtk_adjustment_get_value(doc->vadj); /* px scrolled */

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (cairo_image_surface_get_stride(surface) != width * 4) {
        cairo_surface_destroy(surface);
        return;
    }

    /* LOK paints premultiplied BGRA: the same bytes as CAIRO_FORMAT_ARGB32
     * on little-endian CPUs (x86, ARM). */
    cairo_surface_flush(surface);
    doc->lok->pClass->paintTile(doc->lok, cairo_image_surface_get_data(surface),
                                width, height,
                                0, (int)(top / zoom),
                                (int)(width / zoom), (int)(height / zoom));
    cairo_surface_mark_dirty(surface);

    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_paint(cr);
    cairo_surface_destroy(surface);
}

/* The scroll range depends on the width, because of the fit-to-width zoom. */
static void _doc_on_resize(GtkDrawingArea *area, int width, int height, gpointer user_data) {
    EditDOC *doc = user_data;
    (void)area;
    if (width <= 0 || doc->width_twips <= 0) return;

    double doc_height = doc->height_twips * (double)width / doc->width_twips;
    gtk_adjustment_configure(doc->vadj, gtk_adjustment_get_value(doc->vadj),0, doc_height,/*->lower, upper*/ height * 0.1, height * 0.9, height);
}

static void _doc_on_scrolled(GtkAdjustment *adj, gpointer user_data) {
    EditDOC *doc = user_data;
    (void)adj;
    gtk_widget_queue_draw(doc->area);
}

static gboolean _doc_on_wheel(GtkEventControllerScroll *ctrl,
                              double dx, double dy, gpointer user_data) {
    EditDOC *doc = user_data;
    (void)ctrl; (void)dx;
    gtk_adjustment_set_value(doc->vadj, gtk_adjustment_get_value(doc->vadj)
                             + dy * gtk_adjustment_get_step_increment(doc->vadj));
    return TRUE;   /* handled */
}

static void _doc_render(Edit *self, GtkWidget *container) {
    EditDOC *doc = (EditDOC *)self;

    if (self->widget != NULL) return;          /* already rendered */
    if (!GTK_IS_BOX(container)) {
        g_printerr("EditDOC: container must be a GtkBox\n");
        return;
    }

    /* we keep our own reference: we use vadj in callbacks and in finalize */
    doc->vadj = g_object_ref_sink(gtk_adjustment_new(0, 0, 0, 0, 0, 0));
    g_signal_connect(doc->vadj, "value-changed", G_CALLBACK(_doc_on_scrolled), doc);

    doc->area = gtk_drawing_area_new();
    gtk_widget_set_hexpand(doc->area, TRUE);
    gtk_widget_set_vexpand(doc->area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(doc->area), _doc_draw, doc, NULL);
    g_signal_connect(doc->area, "resize", G_CALLBACK(_doc_on_resize), doc);

    GtkEventController *wheel =
        gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(wheel, "scroll", G_CALLBACK(_doc_on_wheel), doc);
    gtk_widget_add_controller(doc->area, wheel);   /* area owns the controller */

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append(GTK_BOX(box), doc->area);
    gtk_box_append(GTK_BOX(box), gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, doc->vadj));
    gtk_widget_set_vexpand(box, TRUE);

    gtk_box_append(GTK_BOX(container), box);
    self->set_widget(self, box);
}

/* virtual destructor: only frees what EditDOC added.
 * s_office is never destroyed: office->destroy() crashed at exit in the spike. */
static void _doc_finalize(Edit *self) {
    EditDOC *doc = (EditDOC *)self;
    if (doc->vadj != NULL) {
        g_signal_handlers_disconnect_by_data(doc->vadj, doc);
        g_object_unref(doc->vadj);
        doc->vadj = NULL;
    }
    doc->area = NULL;              /* destroyed with self->widget */
    if (doc->lok != NULL) {
        doc->lok->pClass->destroy(doc->lok);
        doc->lok = NULL;
    }
}

/* constructor */
Edit *new_edit_doc(void) {
    EditDOC *doc = calloc(1, sizeof(EditDOC));  /* zeroed: every pointer NULL */
    if (doc == NULL) return NULL;

    edit_init(&doc->base);
    doc->base.open = _doc_open;
    doc->base.save = _doc_save;
    doc->base.render = _doc_render;
    doc->base.finalize = _doc_finalize;
    return &doc->base;   /* upcast */
}
