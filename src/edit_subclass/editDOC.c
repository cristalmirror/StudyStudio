/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.4
 * License: GPLv3
 * Last edited: 2026-11-06
 */

/*
 * EditDOC: LibreOffice does the real work through LibreOfficeKit (LOK).
 * It loads the file, paints it into a pixel buffer, receives our key and
 * mouse events, runs the editing commands and saves the file back.
 *
 * LibreOffice runs its own thread and reports its state (cursor, selection,
 * repaints, undo availability...) through a callback called from THAT
 * thread. GTK may only be used from the main thread, so the callback just
 * copies the message and queues it with g_idle_add(); _doc_dispatch() then
 * handles it in the main thread. The queued messages hold a reference to a
 * small EditDOCLink instead of the EditDOC, so a document closed meanwhile
 * is detected (link->doc == NULL) instead of being used after free().
 *
 * Positions: LibreOffice works in twips (1/1440 inch); the view in pixels.
 * px = twips * zoom - scroll, twips = (px + scroll) / zoom.
 */
#define LOK_USE_UNSTABLE_API   /* paintTile, postKeyEvent... are behind it */
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>           /* LibreOfficeKit.h uses bool without including it */
#include <math.h>
#include <glib/gstdio.h>
#include <LibreOfficeKit/LibreOfficeKitInit.h>   /* lok_init(): include in ONE .c only */
#include <LibreOfficeKit/LibreOfficeKitEnums.h>  /* LOK_CALLBACK_*, LOK_KEYEVENT_*... */
#include "../../include/edit_subclass/editDOCOps.h"

const char *const edit_doc_suffixes[] = {
    "docx", "doc", "odt", "rtf",    /* Writer */
    "xlsx", "xls", "ods",           /* Calc */
    "pptx", "ppt", "odp",           /* Impress */
    NULL
};

#define DOC_TWIPS_PER_PX  15.0     /* 100 %: 1440 twips per inch / 96 px per inch */
#define DOC_ZOOM_STEP     1.25
#define DOC_ZOOM_MIN      (0.25 / DOC_TWIPS_PER_PX)
#define DOC_ZOOM_MAX      (4.0 / DOC_TWIPS_PER_PX)
#define DOC_FOLLOW_MARGIN 24       /* px kept around the cursor when scrolling to it */

/* LibreOffice key codes (com.sun.star.awt.Key) */
enum {
    KEY_NUM0 = 256, KEY_A = 512, KEY_F1 = 768,
    KEY_DOWN = 1024, KEY_UP, KEY_LEFT, KEY_RIGHT, KEY_HOME, KEY_END, KEY_PAGEUP, KEY_PAGEDOWN,
    KEY_TAB = 1282, KEY_BACKSPACE = 1283, KEY_SPACE = 1284, KEY_INSERT = 1285,
    KEY_SHIFT = 0x1000, KEY_MOD1 = 0x2000, KEY_MOD2 = 0x4000,
};
#define DOC_MOUSE_LEFT 1

/* ---------- LibreOffice process ---------- */

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
    if (env != NULL && *env != '\0') {
        char *absolute = g_canonicalize_filename(env, NULL);
        s_office = lok_init(absolute);
        g_free(absolute);
    }

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

/* ---------- link shared with LibreOffice's thread ---------- */

struct EditDOCLink {
    EditDOC *doc;   /* only read and written in the main thread */
};

EditDOCLink *edit_doc_link_ref(EditDOC *doc) {
    return g_atomic_rc_box_acquire(doc->link);
}

EditDOC *edit_doc_link_get(EditDOCLink *link) {
    return link->doc;
}

void edit_doc_link_unref(EditDOCLink *link) {
    g_atomic_rc_box_release(link);
}

/* ---------- small helpers ---------- */

void edit_doc_command(EditDOC *doc, const char *command, const char *args) {
    if (doc->lok == NULL) return;
    doc->lok->pClass->postUnoCommand(doc->lok, command, args, false);
}

void edit_doc_key(EditDOC *doc, int char_code, int key_code) {
    if (doc->lok == NULL) return;
    doc->lok->pClass->postKeyEvent(doc->lok, LOK_KEYEVENT_KEYINPUT, char_code, key_code);
    doc->lok->pClass->postKeyEvent(doc->lok, LOK_KEYEVENT_KEYUP, char_code, key_code);
}

GdkClipboard *edit_doc_clipboard(EditDOC *doc) {
    if (doc->area != NULL) return gtk_widget_get_clipboard(doc->area);
    GdkDisplay *display = gdk_display_get_default();
    return display != NULL ? gdk_display_get_clipboard(display) : NULL;
}

/* "x, y, w, h" in twips; false for "EMPTY" or garbage */
static bool _doc_parse_rect(const char *text, GdkRectangle *rect) {
    if (text == NULL) return false;
    return sscanf(text, " %d , %d , %d , %d", &rect->x, &rect->y,
                  &rect->width, &rect->height) == 4;
}

/* value of "key":"value" in a flat LOK JSON message; NULL if absent */
static char *_doc_json_field(const char *json, const char *key) {
    char *pattern = g_strdup_printf("\"%s\":\"", key);
    const char *start = strstr(json, pattern);
    char *value = NULL;
    if (start != NULL) {
        start += strlen(pattern);
        const char *end = strchr(start, '"');
        if (end != NULL) value = g_strndup(start, end - start);
    }
    g_free(pattern);
    return value;
}

/* ---------- view geometry ---------- */

static double _doc_px_x(EditDOC *doc, double twips) {
    return twips * doc->zoom - gtk_adjustment_get_value(doc->hadj);
}

static double _doc_px_y(EditDOC *doc, double twips) {
    return twips * doc->zoom - gtk_adjustment_get_value(doc->vadj);
}

static int _doc_twips_x(EditDOC *doc, double px) {
    return (int)lround((px + gtk_adjustment_get_value(doc->hadj)) / doc->zoom);
}

static int _doc_twips_y(EditDOC *doc, double px) {
    return (int)lround((px + gtk_adjustment_get_value(doc->vadj)) / doc->zoom);
}

static void _doc_invalidate(EditDOC *doc) {
    doc->tile_dirty = true;
    if (doc->area != NULL) gtk_widget_queue_draw(doc->area);
}

/* scroll ranges follow the document size and the zoom */
static void _doc_configure(EditDOC *doc) {
    if (doc->area == NULL || doc->width_twips <= 0) return;
    int width = gtk_widget_get_width(doc->area);
    int height = gtk_widget_get_height(doc->area);
    if (width <= 0 || height <= 0) return;

    if (doc->fit_width) doc->zoom = (double)width / doc->width_twips;
    gtk_adjustment_configure(doc->hadj, gtk_adjustment_get_value(doc->hadj), 0,
                             MAX(doc->width_twips * doc->zoom, width),
                             width * 0.1, width * 0.9, width);
    gtk_adjustment_configure(doc->vadj, gtk_adjustment_get_value(doc->vadj), 0,
                             MAX(doc->height_twips * doc->zoom, height),
                             height * 0.1, height * 0.9, height);
    gtk_widget_set_visible(doc->hbar, doc->width_twips * doc->zoom > width + 1);
    gtk_widget_set_visible(doc->vbar, doc->height_twips * doc->zoom > height + 1);

    char *text = doc->fit_width ? g_strdup("Ajustar")
        : g_strdup_printf("%d %%", (int)lround(doc->zoom * DOC_TWIPS_PER_PX * 100));
    gtk_label_set_text(GTK_LABEL(doc->zoom_label), text);
    g_free(text);
    _doc_invalidate(doc);
}

static void _doc_scroll_to_cursor(EditDOC *doc) {
    const GdkRectangle *r = doc->has_cell_cursor ? &doc->cell_cursor : &doc->cursor;
    if (doc->area == NULL) return;

    double top = r->y * doc->zoom, bottom = (r->y + r->height) * doc->zoom;
    double value = gtk_adjustment_get_value(doc->vadj);
    double page = gtk_adjustment_get_page_size(doc->vadj);
    if (top < value) gtk_adjustment_set_value(doc->vadj, top - DOC_FOLLOW_MARGIN);
    else if (bottom > value + page)
        gtk_adjustment_set_value(doc->vadj, bottom - page + DOC_FOLLOW_MARGIN);

    double left = r->x * doc->zoom;
    double hvalue = gtk_adjustment_get_value(doc->hadj);
    double hpage = gtk_adjustment_get_page_size(doc->hadj);
    if (left < hvalue) gtk_adjustment_set_value(doc->hadj, left - DOC_FOLLOW_MARGIN);
    else if (left > hvalue + hpage)
        gtk_adjustment_set_value(doc->hadj, left - hpage + DOC_FOLLOW_MARGIN);
}

/* ---------- parts (sheets, slides) ---------- */

void edit_doc_refresh(EditDOC *doc) {
    if (doc->lok == NULL) return;
    doc->lok->pClass->getDocumentSize(doc->lok, &doc->width_twips, &doc->height_twips);

    if (doc->parts != NULL) {
        int n = doc->lok->pClass->getParts(doc->lok);
        GtkStringList *names = gtk_string_list_new(NULL);
        for (int i = 0; i < n; i++) {
            char *name = doc->lok->pClass->getPartName(doc->lok, i);
            gtk_string_list_append(names, name != NULL ? name : "?");
            free(name);   /* allocated by LibreOffice with malloc */
        }
        doc->updating_parts = true;
        gtk_drop_down_set_model(GTK_DROP_DOWN(doc->parts), G_LIST_MODEL(names));
        gtk_drop_down_set_selected(GTK_DROP_DOWN(doc->parts),
                                   (guint)doc->lok->pClass->getPart(doc->lok));
        doc->updating_parts = false;
        g_object_unref(names);
    }
    _doc_configure(doc);
}

static void _doc_on_part_selected(GObject *dropdown, GParamSpec *pspec, gpointer user_data) {
    EditDOC *doc = user_data;
    (void)pspec;
    if (doc->updating_parts || doc->lok == NULL) return;
    guint part = gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown));
    if (part == GTK_INVALID_LIST_POSITION) return;

    doc->lok->pClass->setPart(doc->lok, (int)part);
    doc->lok->pClass->getDocumentSize(doc->lok, &doc->width_twips, &doc->height_twips);
    gtk_adjustment_set_value(doc->vadj, 0);
    _doc_configure(doc);
    gtk_widget_grab_focus(doc->area);
}

/* ---------- LibreOffice callbacks ---------- */

typedef struct {
    EditDOCLink *link;
    int type;
    char *payload;
} DocEvent;

static void _doc_event_free(gpointer data) {
    DocEvent *ev = data;
    edit_doc_link_unref(ev->link);
    g_free(ev->payload);
    g_free(ev);
}

static void _doc_set_selection(EditDOC *doc, const char *payload) {
    bool had = doc->selection->len > 0;
    g_array_set_size(doc->selection, 0);
    char **parts = g_strsplit(payload != NULL ? payload : "", ";", -1);
    for (int i = 0; parts[i] != NULL; i++) {
        GdkRectangle r;
        if (_doc_parse_rect(parts[i], &r)) g_array_append_val(doc->selection, r);
    }
    g_strfreev(parts);
    if (had != (doc->selection->len > 0)) doc->base.notify(&doc->base, NULL);
}

/* ".uno:Undo=enabled", ".uno:StateTableCell=Table1:A1"... */
static void _doc_state_changed(EditDOC *doc, const char *payload) {
    const char *eq = strchr(payload, '=');
    if (payload[0] != '.' || eq == NULL) return;   /* JSON states: not used */
    const char *value = eq + 1;
    size_t len = eq - payload;
    bool changed = false;

    if (len == 9 && strncmp(payload, ".uno:Undo", len) == 0) {
        bool on = strcmp(value, "enabled") == 0;
        changed = on != doc->can_undo;
        doc->can_undo = on;
    } else if (len == 9 && strncmp(payload, ".uno:Redo", len) == 0) {
        bool on = strcmp(value, "enabled") == 0;
        changed = on != doc->can_redo;
        doc->can_redo = on;
    } else if (len == 19 && strncmp(payload, ".uno:StateTableCell", len) == 0) {
        bool on = *value != '\0';
        changed = on != doc->in_table;
        doc->in_table = on;
    }
    if (changed) doc->base.notify(&doc->base, NULL);
}

/*
 * LibreOffice may open a dialog (Calc asks how to merge filled cells).
 * A dialog we cannot show would block the document: the expected one is
 * answered with Enter (its default choice), any other one is closed.
 */
static void _doc_window(EditDOC *doc, const char *payload) {
    char *action = _doc_json_field(payload, "action");
    char *type = _doc_json_field(payload, "type");
    char *id_text = _doc_json_field(payload, "id");

    if (g_strcmp0(action, "created") == 0 && g_strcmp0(type, "dialog") == 0 && id_text != NULL) {
        unsigned id = (unsigned)g_ascii_strtoull(id_text, NULL, 10);
        char *name = _doc_json_field(payload, "unique_id");
        if (doc->expect_merge_dialog && g_strcmp0(name, "MergeCellsDialog") == 0) {
            doc->expect_merge_dialog = false;
            doc->lok->pClass->postWindowKeyEvent(doc->lok, id, LOK_KEYEVENT_KEYINPUT, 0, DOC_KEY_RETURN);
            doc->lok->pClass->postWindowKeyEvent(doc->lok, id, LOK_KEYEVENT_KEYUP, 0, DOC_KEY_RETURN);
        } else {
            doc->lok->pClass->postWindow(doc->lok, id, LOK_WINDOW_CLOSE, NULL);
            doc->base.notify(&doc->base,
                             "LibreOffice asked for a dialog StudyStudio cannot show: cancelled");
        }
        g_free(name);
    }
    g_free(action);
    g_free(type);
    g_free(id_text);
}

static gboolean _doc_dispatch(gpointer data) {
    DocEvent *ev = data;
    EditDOC *doc = edit_doc_link_get(ev->link);
    if (doc == NULL || doc->lok == NULL) return G_SOURCE_REMOVE;   /* closed meanwhile */
    const char *p = ev->payload != NULL ? ev->payload : "";

    switch (ev->type) {
    case LOK_CALLBACK_INVALIDATE_TILES:
        _doc_invalidate(doc);
        break;
    case LOK_CALLBACK_INVALIDATE_VISIBLE_CURSOR:
        if (_doc_parse_rect(p, &doc->cursor)) {
            if (doc->follow_cursor) _doc_scroll_to_cursor(doc);
            doc->follow_cursor = false;
            /* LibreOffice may not send INVALIDATE_TILES: the text around
             * the cursor probably changed too */
            _doc_invalidate(doc);
        }
        break;
    case LOK_CALLBACK_CURSOR_VISIBLE:
        doc->cursor_visible = strcmp(p, "true") == 0;
        if (doc->area != NULL) gtk_widget_queue_draw(doc->area);
        break;
    case LOK_CALLBACK_TEXT_SELECTION:
        _doc_set_selection(doc, p);
        _doc_invalidate(doc);          /* bold, italic... on a selection */
        break;
    case LOK_CALLBACK_CELL_CURSOR:
        doc->has_cell_cursor = _doc_parse_rect(p, &doc->cell_cursor);
        if (doc->has_cell_cursor && doc->follow_cursor) {
            _doc_scroll_to_cursor(doc);
            doc->follow_cursor = false;
        }
        _doc_invalidate(doc);          /* Calc: the edited cell */
        break;
    case LOK_CALLBACK_GRAPHIC_SELECTION: {
        bool had = doc->has_graphic;
        doc->has_graphic = _doc_parse_rect(p, &doc->graphic);
        if (had != doc->has_graphic) doc->base.notify(&doc->base, NULL);
        if (doc->area != NULL) gtk_widget_queue_draw(doc->area);
        break;
    }
    case LOK_CALLBACK_STATE_CHANGED:
        _doc_state_changed(doc, p);
        /* the only message after Delete/Backspace: the document changed */
        _doc_invalidate(doc);
        break;
    case LOK_CALLBACK_DOCUMENT_SIZE_CHANGED:
    case LOK_CALLBACK_SET_PART:
        edit_doc_refresh(doc);
        break;
    case LOK_CALLBACK_WINDOW:
        _doc_window(doc, p);
        break;
    default:
        break;
    }
    return G_SOURCE_REMOVE;
}

/* runs in LibreOffice's thread: no GTK here, only queue the message */
static void _doc_lok_callback(int type, const char *payload, void *data) {
    switch (type) {
    case LOK_CALLBACK_INVALIDATE_TILES:
    case LOK_CALLBACK_INVALIDATE_VISIBLE_CURSOR:
    case LOK_CALLBACK_CURSOR_VISIBLE:
    case LOK_CALLBACK_TEXT_SELECTION:
    case LOK_CALLBACK_CELL_CURSOR:
    case LOK_CALLBACK_GRAPHIC_SELECTION:
    case LOK_CALLBACK_STATE_CHANGED:
    case LOK_CALLBACK_DOCUMENT_SIZE_CHANGED:
    case LOK_CALLBACK_SET_PART:
    case LOK_CALLBACK_WINDOW:
        break;
    default:
        return;
    }
    DocEvent *ev = g_new(DocEvent, 1);
    ev->link = g_atomic_rc_box_acquire((EditDOCLink *)data);
    ev->type = type;
    ev->payload = g_strdup(payload);
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, _doc_dispatch, ev, _doc_event_free);
}

/* ---------- open / save ---------- */

static int _doc_open(Edit *self, const char *path) {
    EditDOC *doc = (EditDOC *)self;
    LibreOfficeKit *office = _doc_office();
    if (office == NULL) return EDIT_ERR_BACKEND;
    if (doc->lok != NULL) return EDIT_ERR_INVALID;   /* one document per EditDOC */

    LibreOfficeKitDocument *lok = office->pClass->documentLoad(office, path);
    if (lok == NULL) {
        char *err = office->pClass->getError(office);
        g_printerr("EditDOC: cannot open %s: %s\n", path, err ? err : "?");
        office->pClass->freeError(err);   /* allocated by LO: LO frees it */
        return EDIT_ERR_IO;
    }

    doc->lok = lok;
    doc->doc_type = lok->pClass->getDocumentType(lok);
    lok->pClass->initializeForRendering(lok, NULL);
    lok->pClass->registerCallback(lok, _doc_lok_callback, doc->link);
    lok->pClass->getDocumentSize(lok, &doc->width_twips, &doc->height_twips);

    /* text and slides fit the width; a sheet is better read at 100 % */
    doc->fit_width = doc->doc_type != LOK_DOCTYPE_SPREADSHEET;
    doc->zoom = 1.0 / DOC_TWIPS_PER_PX;
    edit_doc_bind_ops(doc);

    self->set_path(self, path);
    self->mark_modified(self, false);
    return EDIT_OK;
}

/*
 * saveAs() writes a copy and leaves the open document as it was. The copy
 * goes to a temporary name in the same folder (same extension, so
 * LibreOffice picks the same filter) and then replaces the file, so a
 * failed save never leaves a half-written document behind.
 */
static int _doc_save_to(Edit *self, const char *path) {
    EditDOC *doc = (EditDOC *)self;
    if (doc->lok == NULL) return EDIT_ERR_INVALID;

    char *dir = g_path_get_dirname(path);
    char *base = g_path_get_basename(path);
    char *tmp_name = g_strconcat(".~studystudio-", base, NULL);
    char *tmp = g_build_filename(dir, tmp_name, NULL);
    int rc = EDIT_OK;

    if (!doc->lok->pClass->saveAs(doc->lok, tmp, NULL, NULL)) {
        g_printerr("EditDOC: cannot save %s\n", path);
        rc = EDIT_ERR_IO;
    } else if (g_rename(tmp, path) != 0) {
        /* Windows refuses to replace a file LibreOffice keeps open:
         * write it in place instead */
        g_remove(tmp);
        if (!doc->lok->pClass->saveAs(doc->lok, path, NULL, NULL)) {
            g_printerr("EditDOC: cannot save %s\n", path);
            rc = EDIT_ERR_IO;
        }
    }
    if (rc != EDIT_OK) g_remove(tmp);

    g_free(tmp);
    g_free(tmp_name);
    g_free(base);
    g_free(dir);
    return rc;
}

/* ---------- drawing ---------- */

static void _doc_paint_tile(EditDOC *doc, int width, int height) {
    if (doc->tile == NULL || cairo_image_surface_get_width(doc->tile) != width ||
        cairo_image_surface_get_height(doc->tile) != height) {
        if (doc->tile != NULL) cairo_surface_destroy(doc->tile);
        doc->tile = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    }
    if (cairo_image_surface_get_stride(doc->tile) != width * 4) return;

    /* LOK paints premultiplied BGRA: the same bytes as CAIRO_FORMAT_ARGB32
     * on little-endian CPUs (x86, ARM). */
    cairo_surface_flush(doc->tile);
    doc->lok->pClass->paintTile(doc->lok, cairo_image_surface_get_data(doc->tile),
                                width, height,
                                _doc_twips_x(doc, 0), _doc_twips_y(doc, 0),
                                (int)lround(width / doc->zoom), (int)lround(height / doc->zoom));
    cairo_surface_mark_dirty(doc->tile);
    doc->tile_dirty = false;
}

static void _doc_rect_path(EditDOC *doc, cairo_t *cr, const GdkRectangle *r) {
    cairo_rectangle(cr, _doc_px_x(doc, r->x), _doc_px_y(doc, r->y),
                    MAX(r->width * doc->zoom, 1.0), MAX(r->height * doc->zoom, 1.0));
}

/* Paints only the visible part, then LibreOffice's cursor and selections. */
static void _doc_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    EditDOC *doc = user_data;
    (void)area;
    if (doc->lok == NULL || doc->width_twips <= 0 || width <= 0 || height <= 0) return;

    if (doc->tile_dirty || doc->tile == NULL ||
        cairo_image_surface_get_width(doc->tile) != width ||
        cairo_image_surface_get_height(doc->tile) != height)
        _doc_paint_tile(doc, width, height);
    cairo_set_source_surface(cr, doc->tile, 0, 0);
    cairo_paint(cr);

    if (doc->selection->len > 0) {
        cairo_set_source_rgba(cr, 0.21, 0.52, 0.89, 0.30);
        for (guint i = 0; i < doc->selection->len; i++)
            _doc_rect_path(doc, cr, &g_array_index(doc->selection, GdkRectangle, i));
        cairo_fill(cr);
    }
    if (doc->has_cell_cursor) {
        cairo_set_source_rgb(cr, 0.21, 0.52, 0.89);
        cairo_set_line_width(cr, 2.0);
        _doc_rect_path(doc, cr, &doc->cell_cursor);
        cairo_stroke(cr);
    }
    if (doc->has_graphic) {
        static const double dash[] = { 4.0, 3.0 };
        cairo_set_source_rgb(cr, 0.21, 0.52, 0.89);
        cairo_set_line_width(cr, 1.5);
        cairo_set_dash(cr, dash, 2, 0);
        _doc_rect_path(doc, cr, &doc->graphic);
        cairo_stroke(cr);
        cairo_set_dash(cr, NULL, 0, 0);
    }
    if (doc->cursor_visible && gtk_widget_has_focus(doc->area)) {
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_rectangle(cr, _doc_px_x(doc, doc->cursor.x), _doc_px_y(doc, doc->cursor.y),
                        2.0, MAX(doc->cursor.height * doc->zoom, 8.0));
        cairo_fill(cr);
    }
}

static void _doc_on_resize(GtkDrawingArea *area, int width, int height, gpointer user_data) {
    EditDOC *doc = user_data;
    (void)area; (void)width; (void)height;
    _doc_configure(doc);
}

static void _doc_on_scrolled(GtkAdjustment *adj, gpointer user_data) {
    (void)adj;
    _doc_invalidate((EditDOC *)user_data);
}

/* ---------- zoom ---------- */

static void _doc_set_zoom(EditDOC *doc, double zoom) {
    doc->fit_width = false;
    doc->zoom = CLAMP(zoom, DOC_ZOOM_MIN, DOC_ZOOM_MAX);
    _doc_configure(doc);
}

static void _doc_on_zoom_in(GtkButton *b, gpointer d) {
    (void)b;
    EditDOC *doc = d;
    _doc_set_zoom(doc, doc->zoom * DOC_ZOOM_STEP);
}

static void _doc_on_zoom_out(GtkButton *b, gpointer d) {
    (void)b;
    EditDOC *doc = d;
    _doc_set_zoom(doc, doc->zoom / DOC_ZOOM_STEP);
}

static void _doc_on_zoom_fit(GtkButton *b, gpointer d) {
    (void)b;
    EditDOC *doc = d;
    doc->fit_width = true;
    _doc_configure(doc);
}

static gboolean _doc_on_wheel(GtkEventControllerScroll *ctrl,
                              double dx, double dy, gpointer user_data) {
    EditDOC *doc = user_data;
    GdkModifierType state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(ctrl));
    if (state & GDK_CONTROL_MASK) {                    /* Ctrl + wheel: zoom */
        _doc_set_zoom(doc, dy < 0 ? doc->zoom * DOC_ZOOM_STEP : doc->zoom / DOC_ZOOM_STEP);
        return TRUE;
    }
    gtk_adjustment_set_value(doc->vadj, gtk_adjustment_get_value(doc->vadj)
                             + dy * gtk_adjustment_get_step_increment(doc->vadj));
    gtk_adjustment_set_value(doc->hadj, gtk_adjustment_get_value(doc->hadj)
                             + dx * gtk_adjustment_get_step_increment(doc->hadj));
    return TRUE;   /* handled */
}

/* ---------- keyboard ---------- */

/* LibreOffice code of a non-text key, 0 if it is not one we forward */
static int _doc_key_code(guint keyval) {
    switch (keyval) {
    case GDK_KEY_Down: case GDK_KEY_KP_Down:         return KEY_DOWN;
    case GDK_KEY_Up: case GDK_KEY_KP_Up:             return KEY_UP;
    case GDK_KEY_Left: case GDK_KEY_KP_Left:         return KEY_LEFT;
    case GDK_KEY_Right: case GDK_KEY_KP_Right:       return KEY_RIGHT;
    case GDK_KEY_Home: case GDK_KEY_KP_Home:         return KEY_HOME;
    case GDK_KEY_End: case GDK_KEY_KP_End:           return KEY_END;
    case GDK_KEY_Page_Up: case GDK_KEY_KP_Page_Up:   return KEY_PAGEUP;
    case GDK_KEY_Page_Down: case GDK_KEY_KP_Page_Down: return KEY_PAGEDOWN;
    case GDK_KEY_Return: case GDK_KEY_KP_Enter:      return DOC_KEY_RETURN;
    case GDK_KEY_Escape:                             return DOC_KEY_ESCAPE;
    case GDK_KEY_Tab: case GDK_KEY_ISO_Left_Tab:     return KEY_TAB;
    case GDK_KEY_BackSpace:                          return KEY_BACKSPACE;
    case GDK_KEY_Delete: case GDK_KEY_KP_Delete:     return DOC_KEY_DELETE;
    case GDK_KEY_Insert:                             return KEY_INSERT;
    case GDK_KEY_space:                              return KEY_SPACE;
    default: break;
    }
    if (keyval >= GDK_KEY_F1 && keyval <= GDK_KEY_F12) return KEY_F1 + (int)(keyval - GDK_KEY_F1);
    guint lower = gdk_keyval_to_lower(keyval);
    if (lower >= GDK_KEY_a && lower <= GDK_KEY_z) return KEY_A + (int)(lower - GDK_KEY_a);
    if (keyval >= GDK_KEY_0 && keyval <= GDK_KEY_9) return KEY_NUM0 + (int)(keyval - GDK_KEY_0);
    return 0;
}

static bool _doc_is_navigation(int code) {
    int key = code & 0x0fff;
    return (key >= KEY_DOWN && key <= KEY_PAGEDOWN) || key == DOC_KEY_ESCAPE ||
           (key >= KEY_F1 && key < KEY_F1 + 26);
}

/* our clipboard and history operations, for the usual shortcuts */
static bool _doc_shortcut(EditDOC *doc, guint keyval, GdkModifierType state) {
    Edit *self = &doc->base;
    bool shift = (state & GDK_SHIFT_MASK) != 0;
    int rc = EDIT_OK;

    switch (gdk_keyval_to_lower(keyval)) {
    case GDK_KEY_c: if (self->clipboard) rc = self->clipboard->copy(self); break;
    case GDK_KEY_x: if (self->clipboard) rc = self->clipboard->cut(self); break;
    case GDK_KEY_v: if (self->clipboard) rc = self->clipboard->paste(self, shift); break;
    case GDK_KEY_z: if (self->history) rc = shift ? self->history->redo(self)
                                                  : self->history->undo(self); break;
    case GDK_KEY_y: if (self->history) rc = self->history->redo(self); break;
    case GDK_KEY_a: edit_doc_command(doc, ".uno:SelectAll", NULL); break;
    default: return false;
    }
    if (rc != EDIT_OK) self->notify(self, edit_result_message(rc));
    return true;
}

static gboolean _doc_on_key_pressed(GtkEventControllerKey *ctrl, guint keyval, guint keycode,
                                    GdkModifierType state, gpointer user_data) {
    EditDOC *doc = user_data;
    (void)ctrl; (void)keycode;
    if (doc->lok == NULL) return FALSE;

    bool ctrl_down = (state & GDK_CONTROL_MASK) != 0;
    if (ctrl_down && _doc_shortcut(doc, keyval, state)) return TRUE;

    int code = _doc_key_code(keyval);
    if (code == 0) return FALSE;
    /* letters and digits without Ctrl/Alt are text: the IM context sends them */
    int base = code & 0x0fff;
    bool is_char = (base >= KEY_A && base < KEY_A + 26) || (base >= KEY_NUM0 && base < KEY_NUM0 + 10);
    if (is_char && !(state & (GDK_CONTROL_MASK | GDK_ALT_MASK))) return FALSE;

    if (state & GDK_SHIFT_MASK) code |= KEY_SHIFT;
    if (ctrl_down) code |= KEY_MOD1;
    if (state & GDK_ALT_MASK) code |= KEY_MOD2;

    int char_code = is_char || ctrl_down ? 0 : (int)gdk_keyval_to_unicode(keyval);
    doc->lok->pClass->postKeyEvent(doc->lok, LOK_KEYEVENT_KEYINPUT, char_code, code);
    doc->pressed_key = code;
    doc->follow_cursor = true;
    if (!_doc_is_navigation(code)) doc->base.mark_modified(&doc->base, true);
    return TRUE;
}

static void _doc_on_key_released(GtkEventControllerKey *ctrl, guint keyval, guint keycode,
                                 GdkModifierType state, gpointer user_data) {
    EditDOC *doc = user_data;
    (void)ctrl; (void)keyval; (void)keycode; (void)state;
    if (doc->lok == NULL || doc->pressed_key == 0) return;
    doc->lok->pClass->postKeyEvent(doc->lok, LOK_KEYEVENT_KEYUP, 0, doc->pressed_key);
    doc->pressed_key = 0;
}

/* text typed (also accents and compose sequences resolved by the IM) */
static void _doc_on_commit(GtkIMContext *im, const char *text, gpointer user_data) {
    EditDOC *doc = user_data;
    (void)im;
    if (doc->lok == NULL) return;
    for (const char *p = text; *p != '\0'; p = g_utf8_next_char(p))
        edit_doc_key(doc, (int)g_utf8_get_char(p), 0);
    doc->follow_cursor = true;
    doc->base.mark_modified(&doc->base, true);
}

static void _doc_on_focus_enter(GtkEventControllerFocus *ctrl, gpointer user_data) {
    EditDOC *doc = user_data;
    (void)ctrl;
    gtk_im_context_focus_in(doc->im);
    gtk_widget_queue_draw(doc->area);
}

static void _doc_on_focus_leave(GtkEventControllerFocus *ctrl, gpointer user_data) {
    EditDOC *doc = user_data;
    (void)ctrl;
    gtk_im_context_focus_out(doc->im);
    gtk_widget_queue_draw(doc->area);
}

/* ---------- mouse ---------- */

static int _doc_modifiers(GtkEventController *ctrl) {
    GdkModifierType state = gtk_event_controller_get_current_event_state(ctrl);
    int mods = 0;
    if (state & GDK_SHIFT_MASK) mods |= KEY_SHIFT;
    if (state & GDK_CONTROL_MASK) mods |= KEY_MOD1;
    if (state & GDK_ALT_MASK) mods |= KEY_MOD2;
    return mods;
}

static void _doc_on_pressed(GtkGestureClick *gesture, int n_press, double x, double y,
                            gpointer user_data) {
    EditDOC *doc = user_data;
    if (doc->lok == NULL) return;
    gtk_widget_grab_focus(doc->area);
    doc->mouse_down = true;
    doc->lok->pClass->postMouseEvent(doc->lok, LOK_MOUSEEVENT_MOUSEBUTTONDOWN,
                                     _doc_twips_x(doc, x), _doc_twips_y(doc, y), n_press,
                                     DOC_MOUSE_LEFT, _doc_modifiers(GTK_EVENT_CONTROLLER(gesture)));
}

static void _doc_on_released(GtkGestureClick *gesture, int n_press, double x, double y,
                             gpointer user_data) {
    EditDOC *doc = user_data;
    if (doc->lok == NULL || !doc->mouse_down) return;
    doc->mouse_down = false;
    doc->lok->pClass->postMouseEvent(doc->lok, LOK_MOUSEEVENT_MOUSEBUTTONUP,
                                     _doc_twips_x(doc, x), _doc_twips_y(doc, y), n_press,
                                     DOC_MOUSE_LEFT, _doc_modifiers(GTK_EVENT_CONTROLLER(gesture)));
}

/* dragging with the button held selects text / cells */
static void _doc_on_motion(GtkEventControllerMotion *ctrl, double x, double y, gpointer user_data) {
    EditDOC *doc = user_data;
    (void)ctrl;
    if (doc->lok == NULL || !doc->mouse_down) return;
    doc->lok->pClass->postMouseEvent(doc->lok, LOK_MOUSEEVENT_MOUSEMOVE,
                                     _doc_twips_x(doc, x), _doc_twips_y(doc, y), 1,
                                     DOC_MOUSE_LEFT, 0);
}

/* ---------- render ---------- */

static GtkWidget *_doc_toolbar(EditDOC *doc) {
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(bar, "context-bar");

    if (doc->doc_type == LOK_DOCTYPE_SPREADSHEET || doc->doc_type == LOK_DOCTYPE_PRESENTATION) {
        GtkWidget *label = gtk_label_new(doc->doc_type == LOK_DOCTYPE_SPREADSHEET ? "Hoja" : "Diapositiva");
        gtk_widget_add_css_class(label, "dim-label");
        doc->parts = gtk_drop_down_new(NULL, NULL);
        gtk_widget_set_tooltip_text(doc->parts, "Parte del documento que se muestra");
        g_signal_connect(doc->parts, "notify::selected", G_CALLBACK(_doc_on_part_selected), doc);
        gtk_box_append(GTK_BOX(bar), label);
        gtk_box_append(GTK_BOX(bar), doc->parts);
    }

    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(bar), spacer);

    GtkWidget *zoom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(zoom, "linked");
    GtkWidget *out = gtk_button_new_with_label("−");
    GtkWidget *fit = gtk_button_new_with_label("↔");
    GtkWidget *in = gtk_button_new_with_label("+");
    doc->zoom_label = gtk_label_new(NULL);
    gtk_widget_add_css_class(doc->zoom_label, "zoom-label");
    gtk_widget_set_tooltip_text(out, "Alejar (Ctrl + rueda)");
    gtk_widget_set_tooltip_text(fit, "Ajustar al ancho");
    gtk_widget_set_tooltip_text(in, "Acercar (Ctrl + rueda)");
    g_signal_connect(out, "clicked", G_CALLBACK(_doc_on_zoom_out), doc);
    g_signal_connect(fit, "clicked", G_CALLBACK(_doc_on_zoom_fit), doc);
    g_signal_connect(in, "clicked", G_CALLBACK(_doc_on_zoom_in), doc);
    gtk_box_append(GTK_BOX(zoom), out);
    gtk_box_append(GTK_BOX(zoom), doc->zoom_label);
    gtk_box_append(GTK_BOX(zoom), fit);
    gtk_box_append(GTK_BOX(zoom), in);
    gtk_box_append(GTK_BOX(bar), zoom);
    return bar;
}

static void _doc_render(Edit *self, GtkWidget *container) {
    EditDOC *doc = (EditDOC *)self;

    if (self->widget != NULL) return;          /* already rendered */
    if (!GTK_IS_BOX(container)) {
        g_printerr("EditDOC: container must be a GtkBox\n");
        return;
    }

    /* we keep our own references: used in callbacks and in finalize */
    doc->vadj = g_object_ref_sink(gtk_adjustment_new(0, 0, 0, 0, 0, 0));
    doc->hadj = g_object_ref_sink(gtk_adjustment_new(0, 0, 0, 0, 0, 0));
    g_signal_connect(doc->vadj, "value-changed", G_CALLBACK(_doc_on_scrolled), doc);
    g_signal_connect(doc->hadj, "value-changed", G_CALLBACK(_doc_on_scrolled), doc);

    doc->area = gtk_drawing_area_new();
    gtk_widget_set_hexpand(doc->area, TRUE);
    gtk_widget_set_vexpand(doc->area, TRUE);
    gtk_widget_set_focusable(doc->area, TRUE);
    gtk_widget_set_cursor_from_name(doc->area, "text");
    gtk_widget_add_css_class(doc->area, "doc-canvas");
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(doc->area), _doc_draw, doc, NULL);
    g_signal_connect(doc->area, "resize", G_CALLBACK(_doc_on_resize), doc);

    /* the area owns every controller added to it */
    GtkEventController *wheel = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
    g_signal_connect(wheel, "scroll", G_CALLBACK(_doc_on_wheel), doc);
    gtk_widget_add_controller(doc->area, wheel);

    doc->im = gtk_im_multicontext_new();
    gtk_im_context_set_client_widget(doc->im, doc->area);
    g_signal_connect(doc->im, "commit", G_CALLBACK(_doc_on_commit), doc);
    GtkEventController *keys = gtk_event_controller_key_new();
    gtk_event_controller_key_set_im_context(GTK_EVENT_CONTROLLER_KEY(keys), doc->im);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(_doc_on_key_pressed), doc);
    g_signal_connect(keys, "key-released", G_CALLBACK(_doc_on_key_released), doc);
    gtk_widget_add_controller(doc->area, keys);

    GtkEventController *focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "enter", G_CALLBACK(_doc_on_focus_enter), doc);
    g_signal_connect(focus, "leave", G_CALLBACK(_doc_on_focus_leave), doc);
    gtk_widget_add_controller(doc->area, focus);

    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
    g_signal_connect(click, "pressed", G_CALLBACK(_doc_on_pressed), doc);
    g_signal_connect(click, "released", G_CALLBACK(_doc_on_released), doc);
    gtk_widget_add_controller(doc->area, GTK_EVENT_CONTROLLER(click));

    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(_doc_on_motion), doc);
    gtk_widget_add_controller(doc->area, motion);

    doc->vbar = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, doc->vadj);
    doc->hbar = gtk_scrollbar_new(GTK_ORIENTATION_HORIZONTAL, doc->hadj);
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_attach(GTK_GRID(grid), doc->area, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), doc->vbar, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), doc->hbar, 0, 1, 1, 1);
    gtk_widget_set_vexpand(grid, TRUE);
    gtk_widget_add_css_class(grid, "editor-surface");

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(box), _doc_toolbar(doc));
    gtk_box_append(GTK_BOX(box), grid);
    gtk_widget_set_vexpand(box, TRUE);

    gtk_box_append(GTK_BOX(container), box);
    self->set_widget(self, box);
    /* the canvas may outlive us (e.g. it still had the focus): see finalize */
    g_object_add_weak_pointer(G_OBJECT(doc->area), (gpointer *)&doc->area);
    edit_doc_refresh(doc);
}

static void _doc_focus(Edit *self) {
    EditDOC *doc = (EditDOC *)self;
    if (doc->area != NULL) gtk_widget_grab_focus(doc->area);
}

/* virtual destructor: only frees what EditDOC added.
 * s_office is never destroyed: office->destroy() crashed at exit in the spike. */
static void _doc_finalize(Edit *self) {
    EditDOC *doc = (EditDOC *)self;
    if (doc->lok != NULL) {
        /* no callback can arrive once destroy() returns */
        doc->lok->pClass->registerCallback(doc->lok, NULL, NULL);
        doc->lok->pClass->destroy(doc->lok);
        doc->lok = NULL;
    }
    if (doc->area != NULL) {
        /* GTK can still send it events (focus leaving, a last draw): cut
         * every connection to this soon-freed EditDOC */
        GListModel *controllers = gtk_widget_observe_controllers(doc->area);
        for (guint i = 0; i < g_list_model_get_n_items(controllers); i++) {
            GObject *controller = g_list_model_get_item(controllers, i);
            g_signal_handlers_disconnect_by_data(controller, doc);
            g_object_unref(controller);
        }
        g_object_unref(controllers);
        g_signal_handlers_disconnect_by_data(doc->area, doc);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(doc->area), NULL, NULL, NULL);
        g_object_remove_weak_pointer(G_OBJECT(doc->area), (gpointer *)&doc->area);
        doc->area = NULL;
    }
    doc->link->doc = NULL;            /* queued callbacks see the doc is gone */
    edit_doc_link_unref(doc->link);
    doc->link = NULL;

    if (doc->vadj != NULL) {
        g_signal_handlers_disconnect_by_data(doc->vadj, doc);
        g_object_unref(doc->vadj);
        doc->vadj = NULL;
    }
    if (doc->hadj != NULL) {
        g_signal_handlers_disconnect_by_data(doc->hadj, doc);
        g_object_unref(doc->hadj);
        doc->hadj = NULL;
    }
    if (doc->im != NULL) {
        g_signal_handlers_disconnect_by_data(doc->im, doc);
        gtk_im_context_set_client_widget(doc->im, NULL);
        g_object_unref(doc->im);
        doc->im = NULL;
    }
    if (doc->tile != NULL) {
        cairo_surface_destroy(doc->tile);
        doc->tile = NULL;
    }
    g_array_unref(doc->selection);
    doc->parts = NULL;             /* destroyed with self->widget */
    doc->hbar = NULL;
    doc->vbar = NULL;
    doc->zoom_label = NULL;
}

/* constructor */
Edit *new_edit_doc(void) {
    EditDOC *doc = calloc(1, sizeof(EditDOC));  /* zeroed: every pointer NULL */
    if (doc == NULL) return NULL;

    edit_init(&doc->base);
    doc->base.open = _doc_open;
    doc->base.save_to = _doc_save_to;
    doc->base.render = _doc_render;
    doc->base.finalize = _doc_finalize;
    doc->base.focus = _doc_focus;

    doc->link = g_atomic_rc_box_new0(EditDOCLink);
    doc->link->doc = doc;
    doc->selection = g_array_new(FALSE, FALSE, sizeof(GdkRectangle));
    doc->zoom = 1.0 / DOC_TWIPS_PER_PX;
    doc->fit_width = true;
    /* visible until LibreOffice says otherwise: recent versions never send
     * CURSOR_VISIBLE "true" (LOKDocView also starts with it visible) */
    doc->cursor_visible = true;
    return &doc->base;   /* upcast */
}
