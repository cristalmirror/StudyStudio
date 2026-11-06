/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.4
 * License: GPLv3
 * Last edited: 2026-11-06
 */

/*
 * EditTXT: implementation of the abstract methods of Edit
 * for plain-text documents (.txt, .md).
 *
 * GtkTextView already gives typing, cursor, mouse/keyboard selection and
 * undo/redo (GtkTextBuffer keeps the history). Plain text has no
 * attributes, so .txt only gets history, clipboard and "insert file".
 * Markdown stores its extras as text the format defines:
 *   highlight -> <mark style="background-color: #rrggbb">...</mark>
 *   table     -> GitHub-flavoured pipe table
 *   image     -> ![name](path)
 *   comment   -> <!-- text -->
 */
#include <stdlib.h>
#include <string.h>
#include "../../include/edit_subclass/editTXT.h"

const char *const edit_txt_suffixes[] = { "txt", "md", NULL };

#define MD_EMPTY_CELL "     |"
#define MD_RULE_CELL  " --- |"

/* ---------- state ---------- */

/* GTK calls this every time the buffer's "modified" flag flips */
static void _txt_on_modified_changed(GtkTextBuffer *buffer, gpointer user_data) {
    Edit *self = (Edit *)user_data;
    self->mark_modified(self, gtk_text_buffer_get_modified(buffer));
}

/* selection, can-undo or can-redo changed: the toolbar must refresh */
static void _txt_on_state(GObject *buffer, GParamSpec *pspec, gpointer user_data) {
    Edit *self = (Edit *)user_data;
    (void)buffer; (void)pspec;
    self->notify(self, NULL);
}

/* ---------- history (GtkTextBuffer's own undo stack) ---------- */

static bool _txt_can_undo(Edit *self) {
    return gtk_text_buffer_get_can_undo(((EditTXT *)self)->buffer);
}

static bool _txt_can_redo(Edit *self) {
    return gtk_text_buffer_get_can_redo(((EditTXT *)self)->buffer);
}

static int _txt_undo(Edit *self) {
    if (!_txt_can_undo(self)) return EDIT_ERR_INVALID;
    gtk_text_buffer_undo(((EditTXT *)self)->buffer);
    return EDIT_OK;
}

static int _txt_redo(Edit *self) {
    if (!_txt_can_redo(self)) return EDIT_ERR_INVALID;
    gtk_text_buffer_redo(((EditTXT *)self)->buffer);
    return EDIT_OK;
}

static const EditHistory s_history = { _txt_can_undo, _txt_can_redo, _txt_undo, _txt_redo };

/* ---------- clipboard ---------- */

static GdkClipboard *_txt_clipboard(EditTXT *doc) {
    if (doc->view != NULL) return gtk_widget_get_clipboard(doc->view);
    GdkDisplay *display = gdk_display_get_default();
    return display != NULL ? gdk_display_get_clipboard(display) : NULL;
}

static bool _txt_has_selection(Edit *self) {
    return gtk_text_buffer_get_has_selection(((EditTXT *)self)->buffer);
}

static int _txt_copy(Edit *self) {
    EditTXT *doc = (EditTXT *)self;
    GdkClipboard *clipboard = _txt_clipboard(doc);
    if (clipboard == NULL) return EDIT_ERR_BACKEND;
    if (!_txt_has_selection(self)) return EDIT_ERR_NO_SELECTION;
    gtk_text_buffer_copy_clipboard(doc->buffer, clipboard);
    return EDIT_OK;
}

static int _txt_cut(Edit *self) {
    EditTXT *doc = (EditTXT *)self;
    GdkClipboard *clipboard = _txt_clipboard(doc);
    if (clipboard == NULL) return EDIT_ERR_BACKEND;
    if (!_txt_has_selection(self)) return EDIT_ERR_NO_SELECTION;
    gtk_text_buffer_cut_clipboard(doc->buffer, clipboard, TRUE);
    return EDIT_OK;
}

/* the buffer only holds text, so formatted and plain paste are the same */
static int _txt_paste(Edit *self, bool plain_text) {
    EditTXT *doc = (EditTXT *)self;
    GdkClipboard *clipboard = _txt_clipboard(doc);
    (void)plain_text;
    if (clipboard == NULL) return EDIT_ERR_BACKEND;
    /* asynchronous: GTK inserts the text at the cursor once it arrives */
    gtk_text_buffer_paste_clipboard(doc->buffer, clipboard, NULL, TRUE);
    return EDIT_OK;
}

static const EditClipboard s_clipboard = { _txt_has_selection, _txt_copy, _txt_cut, _txt_paste };

/* ---------- helpers ---------- */

/* inserts text at the cursor, replacing the selection, as one undo step */
static void _txt_insert_at_cursor(EditTXT *doc, const char *text) {
    gtk_text_buffer_begin_user_action(doc->buffer);
    gtk_text_buffer_delete_selection(doc->buffer, TRUE, TRUE);
    gtk_text_buffer_insert_at_cursor(doc->buffer, text, -1);
    gtk_text_buffer_end_user_action(doc->buffer);
}

/* text of one line, without its line break */
static char *_txt_line(GtkTextBuffer *buffer, int line) {
    GtkTextIter start, end;
    gtk_text_buffer_get_iter_at_line(buffer, &start, line);
    end = start;
    if (!gtk_text_iter_ends_line(&end)) gtk_text_iter_forward_to_line_end(&end);
    return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

static int _txt_cursor_line(GtkTextBuffer *buffer) {
    GtkTextIter cursor;
    gtk_text_buffer_get_iter_at_mark(buffer, &cursor, gtk_text_buffer_get_insert(buffer));
    return gtk_text_iter_get_line(&cursor);
}

/* ---------- Markdown: highlight ---------- */

static int _md_highlight(Edit *self, const GdkRGBA *color) {
    EditTXT *doc = (EditTXT *)self;
    GtkTextIter start, end;
    if (!gtk_text_buffer_get_selection_bounds(doc->buffer, &start, &end))
        return EDIT_ERR_NO_SELECTION;

    char open_tag[64];
    g_snprintf(open_tag, sizeof open_tag, "<mark style=\"background-color: #%06x\">",
               (unsigned)edit_color_rgb(color));

    /* marks keep their place while text is inserted around them */
    GtkTextMark *m_end = gtk_text_buffer_create_mark(doc->buffer, NULL, &end, FALSE);
    gtk_text_buffer_begin_user_action(doc->buffer);
    gtk_text_buffer_insert(doc->buffer, &start, open_tag, -1);
    gtk_text_buffer_get_iter_at_mark(doc->buffer, &end, m_end);
    gtk_text_buffer_insert(doc->buffer, &end, "</mark>", -1);
    gtk_text_buffer_end_user_action(doc->buffer);
    gtk_text_buffer_delete_mark(doc->buffer, m_end);
    return EDIT_OK;
}

/*
 * Removes <mark ...> and </mark> inside the selection. If the selection
 * (or the bare cursor) sits inside a highlight, the range grows to its tags.
 * Nothing to remove is not an error: the text is already unhighlighted.
 */
static int _md_clear(Edit *self) {
    EditTXT *doc = (EditTXT *)self;
    GtkTextIter start, end, found, found_end;

    if (!gtk_text_buffer_get_selection_bounds(doc->buffer, &start, &end)) {
        gtk_text_buffer_get_iter_at_mark(doc->buffer, &start, gtk_text_buffer_get_insert(doc->buffer));
        end = start;
    }

    /* an opening tag before the start that is not closed before it */
    if (gtk_text_iter_backward_search(&start, "<mark", 0, &found, NULL, NULL)) {
        GtkTextIter close;
        if (!gtk_text_iter_forward_search(&found, "</mark>", 0, &close, NULL, &start))
            start = found;
    }
    /* a closing tag after the end with no new opening tag before it */
    if (gtk_text_iter_forward_search(&end, "</mark>", 0, &found, &found_end, NULL)) {
        GtkTextIter open;
        if (!gtk_text_iter_forward_search(&end, "<mark", 0, &open, NULL, &found))
            end = found_end;
    }

    char *text = gtk_text_buffer_get_text(doc->buffer, &start, &end, FALSE);
    GRegex *tags = g_regex_new("</?mark\\b[^>]*>", G_REGEX_CASELESS, 0, NULL);
    char *clean = g_regex_replace_literal(tags, text, -1, 0, "", 0, NULL);
    g_regex_unref(tags);

    if (clean != NULL && strcmp(clean, text) != 0) {
        gtk_text_buffer_begin_user_action(doc->buffer);
        gtk_text_buffer_delete(doc->buffer, &start, &end);
        gtk_text_buffer_insert(doc->buffer, &start, clean, -1);
        gtk_text_buffer_end_user_action(doc->buffer);
    }
    g_free(clean);
    g_free(text);
    return EDIT_OK;
}

static const EditMarkup s_md_markup = { _md_highlight, NULL, _md_clear };

/* ---------- Markdown: insert ---------- */

static bool _md_is_table_line(const char *line) {
    while (*line == ' ' || *line == '\t') line++;
    return *line == '|';
}

static bool _md_is_rule_line(const char *line) {
    return g_regex_match_simple("^\\s*\\|?(\\s*:?-+:?\\s*\\|)+\\s*:?-*:?\\s*$", line, 0, 0);
}

/* number of cells of a table line: pipes that are not escaped with '\' */
static int _md_cell_count(const char *line) {
    int pipes = 0;
    for (const char *p = line; *p != '\0'; p++) {
        if (*p == '\\' && p[1] != '\0') { p++; continue; }
        if (*p == '|') pipes++;
    }
    char *trimmed = g_strstrip(g_strdup(line));
    size_t len = strlen(trimmed);
    int cells = pipes - 1;                                   /* "| a | b |" */
    if (len > 0 && trimmed[0] != '|') cells++;
    if (len > 0 && trimmed[len - 1] != '|') cells++;
    g_free(trimmed);
    return MAX(cells, 1);
}

static int _md_table(Edit *self, int rows, int columns) {
    EditTXT *doc = (EditTXT *)self;
    if (rows < 1 || columns < 1 || rows > 200 || columns > 50) return EDIT_ERR_INVALID;

    GString *table = g_string_new(NULL);
    GtkTextIter cursor;
    gtk_text_buffer_get_iter_at_mark(doc->buffer, &cursor, gtk_text_buffer_get_insert(doc->buffer));

    /* a table must start on its own line, after a blank one */
    if (!gtk_text_iter_starts_line(&cursor)) {
        g_string_append(table, "\n\n");
    } else if (gtk_text_iter_get_line(&cursor) > 0) {
        char *prev = _txt_line(doc->buffer, gtk_text_iter_get_line(&cursor) - 1);
        if (*g_strstrip(prev) != '\0') g_string_append_c(table, '\n');
        g_free(prev);
    }

    g_string_append_c(table, '|');
    for (int c = 0; c < columns; c++) g_string_append_printf(table, " Col %d |", c + 1);
    g_string_append(table, "\n|");
    for (int c = 0; c < columns; c++) g_string_append(table, MD_RULE_CELL);
    for (int r = 0; r < rows; r++) {
        g_string_append(table, "\n|");
        for (int c = 0; c < columns; c++) g_string_append(table, MD_EMPTY_CELL);
    }
    g_string_append_c(table, '\n');

    _txt_insert_at_cursor(doc, table->str);
    g_string_free(table, TRUE);
    return EDIT_OK;
}

/* new empty row below the cursor's row (below the rule if on the header) */
static int _md_row(Edit *self) {
    EditTXT *doc = (EditTXT *)self;
    int line = _txt_cursor_line(doc->buffer);
    char *text = _txt_line(doc->buffer, line);
    if (!_md_is_table_line(text)) {
        g_free(text);
        self->notify(self, "Put the cursor inside a Markdown table");
        return EDIT_ERR_INVALID;
    }
    int cells = _md_cell_count(text);
    g_free(text);

    if (line + 1 < gtk_text_buffer_get_line_count(doc->buffer)) {
        char *next = _txt_line(doc->buffer, line + 1);
        if (_md_is_rule_line(next)) line++;                  /* skip the rule */
        g_free(next);
    }

    GString *row = g_string_new("\n|");
    for (int c = 0; c < cells; c++) g_string_append(row, MD_EMPTY_CELL);

    GtkTextIter at;
    gtk_text_buffer_get_iter_at_line(doc->buffer, &at, line);
    if (!gtk_text_iter_ends_line(&at)) gtk_text_iter_forward_to_line_end(&at);
    gtk_text_buffer_begin_user_action(doc->buffer);
    gtk_text_buffer_insert(doc->buffer, &at, row->str, -1);
    gtk_text_buffer_end_user_action(doc->buffer);
    g_string_free(row, TRUE);
    return EDIT_OK;
}

/* new last column on every line of the table around the cursor */
static int _md_column(Edit *self) {
    EditTXT *doc = (EditTXT *)self;
    int line = _txt_cursor_line(doc->buffer);
    int count = gtk_text_buffer_get_line_count(doc->buffer);
    int first = line, last = line;

    char *text = _txt_line(doc->buffer, line);
    bool in_table = _md_is_table_line(text);
    g_free(text);
    if (!in_table) {
        self->notify(self, "Put the cursor inside a Markdown table");
        return EDIT_ERR_INVALID;
    }

    for (;;) {                                               /* table bounds */
        if (first == 0) break;
        char *t = _txt_line(doc->buffer, first - 1);
        bool ok = _md_is_table_line(t);
        g_free(t);
        if (!ok) break;
        first--;
    }
    for (;;) {
        if (last + 1 >= count) break;
        char *t = _txt_line(doc->buffer, last + 1);
        bool ok = _md_is_table_line(t);
        g_free(t);
        if (!ok) break;
        last++;
    }

    /* text without line breaks: inserting it never shifts line numbers */
    gtk_text_buffer_begin_user_action(doc->buffer);
    for (int l = first; l <= last; l++) {
        char *t = _txt_line(doc->buffer, l);
        char *trimmed = g_strchomp(g_strdup(t));
        size_t len = strlen(trimmed);
        bool closed = len > 0 && trimmed[len - 1] == '|';
        char *cell = g_strconcat(closed ? "" : " |",
                                 _md_is_rule_line(t) ? MD_RULE_CELL : MD_EMPTY_CELL, NULL);
        GtkTextIter at;
        gtk_text_buffer_get_iter_at_line(doc->buffer, &at, l);
        if (!gtk_text_iter_ends_line(&at)) gtk_text_iter_forward_to_line_end(&at);
        gtk_text_buffer_insert(doc->buffer, &at, cell, -1);
        g_free(cell);
        g_free(trimmed);
        g_free(t);
    }
    gtk_text_buffer_end_user_action(doc->buffer);
    return EDIT_OK;
}

/* ![name](path): relative to the .md file when the image is below its folder */
static int _md_image(Edit *self, const char *path) {
    EditTXT *doc = (EditTXT *)self;
    if (path == NULL) return EDIT_ERR_INVALID;

    GFile *image = g_file_new_for_path(path);
    char *target = NULL;
    if (self->path != NULL) {
        GFile *doc_file = g_file_new_for_path(self->path);
        GFile *dir = g_file_get_parent(doc_file);
        if (dir != NULL) {
            target = g_file_get_relative_path(dir, image);
            g_object_unref(dir);
        }
        g_object_unref(doc_file);
    }
    if (target == NULL) target = g_file_get_path(image);
    g_object_unref(image);
    g_strdelimit(target, "\\", '/');                         /* Windows paths */

    char *name = g_path_get_basename(path);
    /* <...> lets the destination contain spaces (CommonMark) */
    char *markdown = g_strdup_printf("![%s](<%s>)", name, target);
    _txt_insert_at_cursor(doc, markdown);
    g_free(markdown);
    g_free(name);
    g_free(target);
    return EDIT_OK;
}

/* <!-- text -->: an HTML comment cannot contain "--" */
static int _md_annotation(Edit *self, const char *text) {
    if (text == NULL || *text == '\0') return EDIT_ERR_INVALID;
    GRegex *dashes = g_regex_new("-{2,}", 0, 0, NULL);
    char *safe = g_regex_replace_literal(dashes, text, -1, 0, "-", 0, NULL);
    g_regex_unref(dashes);
    char *comment = g_strdup_printf("<!-- %s -->", safe);
    _txt_insert_at_cursor((EditTXT *)self, comment);
    g_free(comment);
    g_free(safe);
    return EDIT_OK;
}

static const EditInsert s_md_insert = {
    .image = _md_image, .table = _md_table, .row = _md_row, .column = _md_column,
    .page = NULL, .page_label = NULL, .annotation = _md_annotation,
};

/* ---------- combine: insert another text file at the cursor ---------- */

/* reads a UTF-8 text file; NULL (and a message) on failure */
static char *_txt_read(Edit *self, const char *path, int *rc) {
    char *contents = NULL;
    gsize len = 0;
    GError *error = NULL;

    if (!g_file_get_contents(path, &contents, &len, &error)) {
        g_printerr("EditTXT: cannot open %s: %s\n", path, error->message);
        g_clear_error(&error);
        *rc = EDIT_ERR_IO;
        return NULL;
    }
    /* GtkTextBuffer only accepts valid UTF-8 and an int length */
    if (len > G_MAXINT || !g_utf8_validate(contents, (gssize)len, NULL)) {
        g_printerr("EditTXT: %s is not valid UTF-8 text\n", path);
        if (self != NULL) self->notify(self, "The file is not UTF-8 text");
        g_free(contents);
        *rc = EDIT_ERR_INVALID;
        return NULL;
    }
    *rc = EDIT_OK;
    return contents;
}

static int _txt_merge_file(Edit *self, const char *path) {
    int rc;
    char *contents = _txt_read(self, path, &rc);
    if (contents == NULL) return rc;
    _txt_insert_at_cursor((EditTXT *)self, contents);
    g_free(contents);
    return EDIT_OK;
}

static const EditCombine s_combine = { edit_txt_suffixes, _txt_merge_file, NULL, NULL, NULL };

/* ---------- open / save ---------- */

static int _txt_open(Edit *self, const char *path) {
    EditTXT *doc = (EditTXT *)self;
    int rc;
    char *contents = _txt_read(NULL, path, &rc);
    if (contents == NULL) return rc;

    /* loading is not an edit: it must not be undoable */
    gtk_text_buffer_begin_irreversible_action(doc->buffer);
    gtk_text_buffer_set_text(doc->buffer, contents, -1);
    gtk_text_buffer_end_irreversible_action(doc->buffer);
    g_free(contents);
    gtk_text_buffer_set_modified(doc->buffer, FALSE);  /* freshly loaded */
    self->set_path(self, path);

    static const char *const md[] = { "md", NULL };
    doc->markdown = edit_path_has_suffix(path, md);
    self->markup = doc->markdown ? &s_md_markup : NULL;
    self->insert = doc->markdown ? &s_md_insert : NULL;
    return EDIT_OK;
}

static int _txt_save_to(Edit *self, const char *path) {
    EditTXT *doc = (EditTXT *)self;
    GtkTextIter start, end;
    GError *error = NULL;

    gtk_text_buffer_get_bounds(doc->buffer, &start, &end);
    char *text = gtk_text_buffer_get_text(doc->buffer, &start, &end, FALSE);

    /* atomic write: temp file + rename, never leaves a half-written file */
    gboolean ok = g_file_set_contents(path, text, -1, &error);
    g_free(text);

    if (!ok) {
        g_printerr("EditTXT: cannot save %s: %s\n", path, error->message);
        g_clear_error(&error);
        return EDIT_ERR_IO;
    }
    gtk_text_buffer_set_modified(doc->buffer, FALSE);
    return EDIT_OK;
}

/* ---------- view ---------- */

static void _txt_render(Edit *self, GtkWidget *container) {
    EditTXT *doc = (EditTXT *)self;

    if (self->widget != NULL) return;          /* already rendered */
    if (!GTK_IS_BOX(container)) {
        g_printerr("EditTXT: container must be a GtkBox\n");
        return;
    }

    doc->view = gtk_text_view_new_with_buffer(doc->buffer);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(doc->view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(doc->view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(doc->view), 16);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(doc->view), 16);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(doc->view), 12);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(doc->view), 12);
    gtk_widget_add_css_class(doc->view, "editor-text");

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), doc->view);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_add_css_class(scroll, "editor-surface");

    gtk_box_append(GTK_BOX(container), scroll);
    self->set_widget(self, scroll);
    /* the view dies with the scrolled window: forget it then */
    g_object_add_weak_pointer(G_OBJECT(doc->view), (gpointer *)&doc->view);
}

static void _txt_focus(Edit *self) {
    EditTXT *doc = (EditTXT *)self;
    if (doc->view != NULL) gtk_widget_grab_focus(doc->view);
}

/* virtual destructor: only frees what EditTXT added */
static void _txt_finalize(Edit *self) {
    EditTXT *doc = (EditTXT *)self;
    if (doc->view != NULL) {
        g_object_remove_weak_pointer(G_OBJECT(doc->view), (gpointer *)&doc->view);
        doc->view = NULL;
    }
    if (doc->buffer != NULL) {
        g_signal_handlers_disconnect_by_data(doc->buffer, doc);
        g_object_unref(doc->buffer);
        doc->buffer = NULL;
    }
}

/* constructor */
Edit *new_edit_txt(void) {
    EditTXT *doc = calloc(1, sizeof(EditTXT));  /* zeroed: every pointer NULL */
    if (doc == NULL) return NULL;

    edit_init(&doc->base);
    doc->base.open = _txt_open;
    doc->base.save_to = _txt_save_to;
    doc->base.render = _txt_render;
    doc->base.finalize = _txt_finalize;
    doc->base.focus = _txt_focus;
    doc->base.history = &s_history;
    doc->base.clipboard = &s_clipboard;
    doc->base.combine = &s_combine;

    doc->buffer = gtk_text_buffer_new(NULL);
    gtk_text_buffer_set_enable_undo(doc->buffer, TRUE);
    g_signal_connect(doc->buffer, "modified-changed",
                     G_CALLBACK(_txt_on_modified_changed), doc);
    g_signal_connect(doc->buffer, "notify::has-selection", G_CALLBACK(_txt_on_state), doc);
    g_signal_connect(doc->buffer, "notify::can-undo", G_CALLBACK(_txt_on_state), doc);
    g_signal_connect(doc->buffer, "notify::can-redo", G_CALLBACK(_txt_on_state), doc);
    return &doc->base;   /* upcast */
}
