/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.4
 * License: GPLv3
 * Last edited: 2026-11-06
 */

/*
 * EditDOC editing tools. LibreOffice already implements every operation as
 * a UNO command (the ones its own menus run); we post them with their
 * arguments so no dialog is needed. Writer, Calc and Impress understand
 * different commands, so each one gets its own capability tables and only
 * the operations checked against that application are offered.
 */
#define LOK_USE_UNSTABLE_API
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <LibreOfficeKit/LibreOfficeKit.h>
#include <LibreOfficeKit/LibreOfficeKitEnums.h>
#include "../../include/edit_subclass/editDOCOps.h"

#define DOC_NO_COLOR (-1L)   /* COL_TRANSPARENT: removes a background color */

static EditDOC *_doc(Edit *self) {
    return (EditDOC *)self;   /* valid: base is the first member */
}

/* a command that changes the document */
static void _doc_edit(EditDOC *doc, const char *command, const char *args) {
    edit_doc_command(doc, command, args);
    doc->base.mark_modified(&doc->base, true);
}

static void _doc_edit_long(EditDOC *doc, const char *command, const char *name, long value) {
    char *args = g_strdup_printf("{\"%s\":{\"type\":\"long\",\"value\":%ld}}", name, value);
    _doc_edit(doc, command, args);
    g_free(args);
}

/* {"name":{"type":"string","value":"..."}} with the value JSON-escaped */
static char *_doc_string_arg(const char *name, const char *value) {
    GString *json = g_string_new(NULL);
    g_string_append_printf(json, "{\"%s\":{\"type\":\"string\",\"value\":\"", name);
    for (const char *p = value; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') g_string_append_printf(json, "\\%c", c);
        else if (c < 0x20) g_string_append_printf(json, "\\u%04x", c);
        else g_string_append_c(json, (char)c);
    }
    g_string_append(json, "\"}}");
    return g_string_free(json, FALSE);
}

/* LibreOffice wants file URLs: file:///home/... or file:///C:/... */
static char *_doc_file_arg(const char *name, const char *path) {
    char *uri = g_filename_to_uri(path, NULL, NULL);
    if (uri == NULL) return NULL;
    char *args = _doc_string_arg(name, uri);
    g_free(uri);
    return args;
}

/* ---------- history: LibreOffice's own undo stack ---------- */

static bool _doc_can_undo(Edit *self) { return _doc(self)->can_undo; }
static bool _doc_can_redo(Edit *self) { return _doc(self)->can_redo; }

static int _doc_undo(Edit *self) {
    if (!_doc(self)->can_undo) return EDIT_ERR_INVALID;
    _doc_edit(_doc(self), ".uno:Undo", NULL);
    return EDIT_OK;
}

static int _doc_redo(Edit *self) {
    if (!_doc(self)->can_redo) return EDIT_ERR_INVALID;
    _doc_edit(_doc(self), ".uno:Redo", NULL);
    return EDIT_OK;
}

static const EditHistory s_history = { _doc_can_undo, _doc_can_redo, _doc_undo, _doc_redo };

/* ---------- clipboard ---------- */

static bool _doc_has_selection(Edit *self) {
    EditDOC *doc = _doc(self);
    /* in a sheet the active cell is always a selection */
    return doc->selection->len > 0 || doc->has_graphic || doc->has_cell_cursor;
}

/* selection as mime type, NULL if empty; LibreOffice allocates with malloc */
static GBytes *_doc_selection_bytes(EditDOC *doc, const char *mime) {
    char *used = NULL;
    char *data = doc->lok->pClass->getTextSelection(doc->lok, mime, &used);
    free(used);
    GBytes *bytes = (data != NULL && *data != '\0') ? g_bytes_new(data, strlen(data)) : NULL;
    free(data);
    return bytes;
}

/* puts the selection on the system clipboard as HTML (formatted) and text */
static int _doc_copy(Edit *self) {
    EditDOC *doc = _doc(self);
    GdkClipboard *clipboard = edit_doc_clipboard(doc);
    if (clipboard == NULL || doc->lok == NULL) return EDIT_ERR_BACKEND;

    GBytes *text = _doc_selection_bytes(doc, "text/plain;charset=utf-8");
    GBytes *html = _doc_selection_bytes(doc, "text/html");
    if (text == NULL && html == NULL) return EDIT_ERR_NO_SELECTION;

    GdkContentProvider *providers[2];
    int n = 0;
    if (html != NULL) providers[n++] = gdk_content_provider_new_for_bytes("text/html", html);
    if (text != NULL) {
        /* as a string GTK also offers it as text/plain, UTF8_STRING... */
        char *str = g_strndup(g_bytes_get_data(text, NULL), g_bytes_get_size(text));
        providers[n++] = gdk_content_provider_new_typed(G_TYPE_STRING, str);
        g_free(str);
    }
    GdkContentProvider *content = gdk_content_provider_new_union(providers, n);
    gdk_clipboard_set_content(clipboard, content);
    g_object_unref(content);
    if (html != NULL) g_bytes_unref(html);
    if (text != NULL) g_bytes_unref(text);
    return EDIT_OK;
}

static int _doc_cut(Edit *self) {
    int rc = _doc_copy(self);
    if (rc != EDIT_OK) return rc;
    EditDOC *doc = _doc(self);
    edit_doc_key(doc, 0, DOC_KEY_DELETE);
    doc->base.mark_modified(&doc->base, true);
    return EDIT_OK;
}

/* the clipboard is read asynchronously; the document may be closed by then */
typedef struct {
    EditDOCLink *link;
    char *mime;
} DocPaste;

static void _doc_paste_free(DocPaste *job) {
    edit_doc_link_unref(job->link);
    g_free(job->mime);
    g_free(job);
}

static void _doc_paste_data(DocPaste *job, const char *data, gsize size) {
    EditDOC *doc = edit_doc_link_get(job->link);
    if (doc == NULL || doc->lok == NULL) return;
    if (data == NULL || size == 0) {
        doc->base.notify(&doc->base, "The clipboard is empty");
    } else if (!doc->lok->pClass->paste(doc->lok, job->mime, data, size)) {
        doc->base.notify(&doc->base, "LibreOffice rejected the clipboard content");
    } else {
        doc->base.mark_modified(&doc->base, true);
    }
}

static void _doc_paste_text_done(GObject *source, GAsyncResult *res, gpointer user_data) {
    DocPaste *job = user_data;
    char *text = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(source), res, NULL);
    _doc_paste_data(job, text, text != NULL ? strlen(text) : 0);
    g_free(text);
    _doc_paste_free(job);
}

static void _doc_paste_splice_done(GObject *source, GAsyncResult *res, gpointer user_data) {
    DocPaste *job = user_data;
    GOutputStream *out = G_OUTPUT_STREAM(source);
    if (g_output_stream_splice_finish(out, res, NULL) >= 0) {
        GBytes *bytes = g_memory_output_stream_steal_as_bytes(G_MEMORY_OUTPUT_STREAM(out));
        _doc_paste_data(job, g_bytes_get_data(bytes, NULL), g_bytes_get_size(bytes));
        g_bytes_unref(bytes);
    }
    g_object_unref(out);
    _doc_paste_free(job);
}

static void _doc_paste_read_done(GObject *source, GAsyncResult *res, gpointer user_data) {
    DocPaste *job = user_data;
    const char *mime = NULL;
    GInputStream *in = gdk_clipboard_read_finish(GDK_CLIPBOARD(source), res, &mime, NULL);
    if (in == NULL) {
        _doc_paste_free(job);
        return;
    }
    g_free(job->mime);
    job->mime = g_strdup(mime);
    GOutputStream *out = g_memory_output_stream_new_resizable();
    g_output_stream_splice_async(out, in, G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE,
                                 G_PRIORITY_DEFAULT, NULL, _doc_paste_splice_done, job);
    g_object_unref(in);
}

/*
 * Formatted paste prefers HTML (keeps bold, tables, colors between
 * documents), then a PNG image; plain paste always takes the text only.
 */
static int _doc_paste(Edit *self, bool plain_text) {
    EditDOC *doc = _doc(self);
    GdkClipboard *clipboard = edit_doc_clipboard(doc);
    if (clipboard == NULL || doc->lok == NULL) return EDIT_ERR_BACKEND;

    DocPaste *job = g_new0(DocPaste, 1);
    job->link = edit_doc_link_ref(doc);
    GdkContentFormats *formats = gdk_clipboard_get_formats(clipboard);

    static const char *const rich[] = { "text/html", "image/png", NULL };
    for (int i = 0; !plain_text && rich[i] != NULL; i++) {
        if (gdk_content_formats_contain_mime_type(formats, rich[i])) {
            const char *mimes[] = { rich[i], NULL };
            gdk_clipboard_read_async(clipboard, mimes, G_PRIORITY_DEFAULT, NULL,
                                     _doc_paste_read_done, job);
            return EDIT_OK;
        }
    }
    job->mime = g_strdup("text/plain;charset=utf-8");
    gdk_clipboard_read_text_async(clipboard, NULL, _doc_paste_text_done, job);
    return EDIT_OK;
}

static const EditClipboard s_clipboard = { _doc_has_selection, _doc_copy, _doc_cut, _doc_paste };

/* ---------- markup ---------- */

static int _doc_char_highlight(Edit *self, const GdkRGBA *color) {
    if (!_doc_has_selection(self)) return EDIT_ERR_NO_SELECTION;
    _doc_edit_long(_doc(self), ".uno:CharBackColor", "CharBackColor", (long)edit_color_rgb(color));
    return EDIT_OK;
}

/* Writer: paragraph background; Calc: cell background */
static int _doc_background(Edit *self, const GdkRGBA *color) {
    _doc_edit_long(_doc(self), ".uno:BackgroundColor", "BackgroundColor", (long)edit_color_rgb(color));
    return EDIT_OK;
}

/* Impress: fill of the selected shape */
static int _doc_fill(Edit *self, const GdkRGBA *color) {
    if (!_doc(self)->has_graphic) {
        self->notify(self, "Select a shape first");
        return EDIT_ERR_NO_SELECTION;
    }
    _doc_edit_long(_doc(self), ".uno:FillColor", "FillColor", (long)edit_color_rgb(color));
    return EDIT_OK;
}

static int _doc_writer_clear(Edit *self) {
    _doc_edit_long(_doc(self), ".uno:CharBackColor", "CharBackColor", DOC_NO_COLOR);
    _doc_edit_long(_doc(self), ".uno:BackgroundColor", "BackgroundColor", DOC_NO_COLOR);
    return EDIT_OK;
}

static int _doc_calc_clear(Edit *self) {
    _doc_edit_long(_doc(self), ".uno:BackgroundColor", "BackgroundColor", DOC_NO_COLOR);
    return EDIT_OK;
}

static int _doc_impress_clear(Edit *self) {
    _doc_edit_long(_doc(self), ".uno:CharBackColor", "CharBackColor", DOC_NO_COLOR);
    return EDIT_OK;
}

static const EditMarkup s_writer_markup = { _doc_char_highlight, _doc_background, _doc_writer_clear };
static const EditMarkup s_calc_markup = { NULL, _doc_background, _doc_calc_clear };
static const EditMarkup s_impress_markup = { _doc_char_highlight, _doc_fill, _doc_impress_clear };

/* ---------- insert ---------- */

static int _doc_image(Edit *self, const char *path) {
    char *args = _doc_file_arg("FileName", path);
    if (args == NULL) return EDIT_ERR_INVALID;
    _doc_edit(_doc(self), ".uno:InsertGraphic", args);
    g_free(args);
    return EDIT_OK;
}

static int _doc_table(Edit *self, int rows, int columns) {
    if (rows < 1 || columns < 1 || rows > 500 || columns > 64) return EDIT_ERR_INVALID;
    char *args = g_strdup_printf("{\"Columns\":{\"type\":\"long\",\"value\":%d},"
                                 "\"Rows\":{\"type\":\"long\",\"value\":%d}}", columns, rows);
    _doc_edit(_doc(self), ".uno:InsertTable", args);
    g_free(args);
    return EDIT_OK;
}

/* Writer only has rows and columns inside a table */
static bool _doc_needs_table(EditDOC *doc) {
    if (doc->doc_type != LOK_DOCTYPE_TEXT || doc->in_table) return false;
    doc->base.notify(&doc->base, "Put the cursor inside a table first");
    return true;
}

static int _doc_row(Edit *self) {
    if (_doc_needs_table(_doc(self))) return EDIT_ERR_INVALID;
    _doc_edit(_doc(self), ".uno:InsertRowsAfter", NULL);
    return EDIT_OK;
}

static int _doc_column(Edit *self) {
    if (_doc_needs_table(_doc(self))) return EDIT_ERR_INVALID;
    _doc_edit(_doc(self), ".uno:InsertColumnsAfter", NULL);
    return EDIT_OK;
}

static int _doc_page_break(Edit *self) {
    _doc_edit(_doc(self), ".uno:InsertPagebreak", NULL);
    return EDIT_OK;
}

static int _doc_new_slide(Edit *self) {
    _doc_edit(_doc(self), ".uno:InsertPage", NULL);
    return EDIT_OK;
}

static int _doc_annotation(Edit *self, const char *text) {
    if (text == NULL || *text == '\0') return EDIT_ERR_INVALID;
    char *args = _doc_string_arg("Text", text);
    _doc_edit(_doc(self), ".uno:InsertAnnotation", args);
    g_free(args);
    return EDIT_OK;
}

static const EditInsert s_writer_insert = {
    .image = _doc_image, .table = _doc_table, .row = _doc_row, .column = _doc_column,
    .page = _doc_page_break, .page_label = "Salto de página", .annotation = _doc_annotation,
};
static const EditInsert s_calc_insert = {
    .image = _doc_image, .row = _doc_row, .column = _doc_column, .annotation = _doc_annotation,
};
/* Impress comments need a dialog in LibreOfficeKit: not offered */
static const EditInsert s_impress_insert = {
    .image = _doc_image, .table = _doc_table,
    .page = _doc_new_slide, .page_label = "Nueva diapositiva",
};

/* ---------- combine ---------- */

static const char *const s_writer_merge_suffixes[] = { "odt", "docx", "doc", "rtf", "txt", NULL };

/* Writer: the other document's content at the cursor */
static int _doc_insert_document(Edit *self, const char *path) {
    char *args = _doc_file_arg("Name", path);
    if (args == NULL) return EDIT_ERR_INVALID;
    _doc_edit(_doc(self), ".uno:InsertDoc", args);
    g_free(args);
    return EDIT_OK;
}

static int _doc_merge_cells(Edit *self) {
    EditDOC *doc = _doc(self);
    if (_doc_needs_table(doc)) return EDIT_ERR_INVALID;
    /* Calc asks what to do with the hidden cells' content: keep it (Enter) */
    doc->expect_merge_dialog = doc->doc_type == LOK_DOCTYPE_SPREADSHEET;
    _doc_edit(doc, ".uno:MergeCells", NULL);
    return EDIT_OK;
}

static int _doc_split_cells(Edit *self) {
    _doc_edit(_doc(self), ".uno:SplitCell", NULL);
    return EDIT_OK;
}

/* Impress: the current slide one place earlier or later */
static int _doc_move_slide(Edit *self, int delta) {
    if (delta != -1 && delta != 1) return EDIT_ERR_INVALID;
    _doc_edit(_doc(self), delta < 0 ? ".uno:MovePageUp" : ".uno:MovePageDown", NULL);
    return EDIT_OK;
}

/* Writer "split cells" opens a dialog in LibreOfficeKit: not offered */
static const EditCombine s_writer_combine = {
    s_writer_merge_suffixes, _doc_insert_document, _doc_merge_cells, NULL, NULL,
};
static const EditCombine s_calc_combine = {
    NULL, NULL, _doc_merge_cells, _doc_split_cells, NULL,
};
static const EditCombine s_impress_combine = {
    NULL, NULL, NULL, NULL, _doc_move_slide,
};

void edit_doc_bind_ops(EditDOC *doc) {
    Edit *self = &doc->base;
    self->history = &s_history;
    self->clipboard = &s_clipboard;
    switch (doc->doc_type) {
    case LOK_DOCTYPE_TEXT:
        self->markup = &s_writer_markup;
        self->insert = &s_writer_insert;
        self->combine = &s_writer_combine;
        break;
    case LOK_DOCTYPE_SPREADSHEET:
        self->markup = &s_calc_markup;
        self->insert = &s_calc_insert;
        self->combine = &s_calc_combine;
        break;
    case LOK_DOCTYPE_PRESENTATION:
        self->markup = &s_impress_markup;
        self->insert = &s_impress_insert;
        self->combine = &s_impress_combine;
        break;
    default:   /* drawings: view, text input, clipboard and history only */
        self->markup = NULL;
        self->insert = NULL;
        self->combine = NULL;
        break;
    }
}
