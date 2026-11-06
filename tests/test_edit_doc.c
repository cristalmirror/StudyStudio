/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.3
 * License: GPLv3
 * Last edited: 2026-11-06
 */

/*
 * EditDOC against a real LibreOffice (skipped when it is not installed):
 * every operation is run, the document saved as ODF and the XML inside
 * checked, so a passing test means the change really is in the file.
 */
#define LOK_USE_UNSTABLE_API
#include <stdlib.h>
#include <stdbool.h>
#include <LibreOfficeKit/LibreOfficeKit.h>
#include <LibreOfficeKit/LibreOfficeKitEnums.h>
#include "test_common.h"
#include "../include/edit_subclass/editDOCOps.h"

static Edit *new_doc(const char *factory) {
    Edit *edit = new_edit_doc();
    g_assert_cmpint(edit->open(edit, factory), ==, EDIT_OK);
    test_spin(500);                                  /* first callbacks */
    return edit;
}

/* LibreOffice runs commands in its own thread: give it time */
static void settle(void) {
    test_spin(700);
}

static gboolean can_undo(gpointer data) {
    Edit *edit = data;
    return edit->history->can_undo(edit);
}

static gboolean has_selection(gpointer data) {
    Edit *edit = data;
    return ((EditDOC *)edit)->selection->len > 0;
}

static char *selected_text(Edit *edit) {
    LibreOfficeKitDocument *lok = ((EditDOC *)edit)->lok;
    char *used = NULL;
    char *text = lok->pClass->getTextSelection(lok, "text/plain;charset=utf-8", &used);
    free(used);
    char *copy = g_strdup(text);
    free(text);
    return copy;
}

/* content.xml of a saved ODF file */
static char *odf_content(const char *path) {
    char *quoted = g_shell_quote(path);
    char *cmd = g_strdup_printf("unzip -p %s content.xml", quoted);
    char *out = NULL;
    gint status = 0;
    g_assert_true(g_spawn_command_line_sync(cmd, &out, NULL, &status, NULL));
    g_free(cmd);
    g_free(quoted);
    return out;
}

static void paste_plain(Edit *edit, const char *text) {
    test_clipboard_set(text);
    test_spin(100);
    g_assert_cmpint(edit->clipboard->paste(edit, true), ==, EDIT_OK);
    settle();
}

static void test_writer(void) {
    char *dir = test_dir();
    char *merge = test_write(dir, "extra.txt", "Texto combinado\n");
    char *saved = g_build_filename(dir, "writer.odt", NULL);
    Edit *edit = new_doc("private:factory/swriter");
    EditDOC *doc = (EditDOC *)edit;

    g_assert_cmpint(doc->doc_type, ==, LOK_DOCTYPE_TEXT);
    g_assert_nonnull(edit->markup->highlight);
    g_assert_nonnull(edit->insert->page);
    g_assert_nonnull(edit->combine->merge_file);
    g_assert_null(edit->combine->split_cells);         /* needs a dialog: not offered */

    /* text through the system clipboard (plain paste) */
    paste_plain(edit, "Hola StudyStudio");
    g_assert_true(edit->is_modified(edit));
    g_assert_true(test_wait(can_undo, edit, 3000));

    edit_doc_command(doc, ".uno:SelectAll", NULL);
    g_assert_true(test_wait(has_selection, edit, 3000));
    char *text = selected_text(edit);
    g_assert_cmpstr(text, ==, "Hola StudyStudio");
    g_free(text);

    /* copy reaches the system clipboard */
    g_assert_cmpint(edit->clipboard->copy(edit), ==, EDIT_OK);
    char *clip = test_clipboard_text();
    g_assert_cmpstr(clip, ==, "Hola StudyStudio");
    g_free(clip);

    GdkRGBA red = { 1, 0, 0, 1 }, gray = { 0.8, 0.8, 0.8, 1 };
    g_assert_cmpint(edit->markup->highlight(edit, &red), ==, EDIT_OK);
    g_assert_cmpint(edit->markup->shade(edit, &gray), ==, EDIT_OK);
    settle();

    edit_doc_command(doc, ".uno:GoToEndOfDoc", NULL);
    g_assert_cmpint(edit->combine->merge_file(edit, merge), ==, EDIT_OK);
    g_assert_cmpint(edit->insert->page(edit), ==, EDIT_OK);
    g_assert_cmpint(edit->insert->row(edit), ==, EDIT_ERR_INVALID);   /* not in a table */
    g_assert_cmpint(edit->insert->table(edit, 2, 3), ==, EDIT_OK);
    settle();
    g_assert_true(doc->in_table);
    g_assert_cmpint(edit->insert->row(edit), ==, EDIT_OK);
    g_assert_cmpint(edit->insert->column(edit), ==, EDIT_OK);
    g_assert_cmpint(edit->insert->annotation(edit, "Comentario \"citado\""), ==, EDIT_OK);
    settle();

    g_assert_cmpint(edit->save_as(edit, saved), ==, EDIT_OK);
    g_assert_false(edit->is_modified(edit));
    edit->close(edit);

    char *xml = odf_content(saved);
    g_assert_nonnull(strstr(xml, "Hola StudyStudio"));
    g_assert_nonnull(strstr(xml, "fo:background-color=\"#ff0000\""));   /* highlight */
    g_assert_nonnull(strstr(xml, "fo:background-color=\"#cccccc\""));   /* paragraph */
    g_assert_nonnull(strstr(xml, "Texto combinado"));
    g_assert_nonnull(strstr(xml, "fo:break-before=\"page\""));
    g_assert_nonnull(strstr(xml, "<table:table "));
    g_assert_nonnull(strstr(xml, "<office:annotation"));
    g_assert_nonnull(strstr(xml, "citado"));
    g_free(xml);

    /* reopen and undo the last change of a fresh session: none */
    edit = new_doc(saved);
    g_assert_false(edit->history->can_undo(edit));
    edit_doc_command((EditDOC *)edit, ".uno:SelectAll", NULL);
    g_assert_true(test_wait(has_selection, edit, 3000));
    text = selected_text(edit);
    g_assert_nonnull(strstr(text, "Hola StudyStudio"));
    g_free(text);
    edit->close(edit);

    g_free(saved);
    g_free(merge);
    g_free(dir);
}

static void test_calc(void) {
    char *dir = test_dir();
    char *saved = g_build_filename(dir, "calc.ods", NULL);
    Edit *edit = new_doc("private:factory/scalc");
    EditDOC *doc = (EditDOC *)edit;

    g_assert_cmpint(doc->doc_type, ==, LOK_DOCTYPE_SPREADSHEET);
    g_assert_null(edit->markup->highlight);             /* cells: shading only */
    g_assert_nonnull(edit->combine->merge_cells);
    g_assert_nonnull(edit->combine->split_cells);
    g_assert_null(edit->combine->merge_file);

    /* two filled cells: merging them makes Calc ask (answered with Enter) */
    edit_doc_command(doc, ".uno:GoToCell", "{\"ToPoint\":{\"type\":\"string\",\"value\":\"A1\"}}");
    paste_plain(edit, "12");
    edit_doc_command(doc, ".uno:GoToCell", "{\"ToPoint\":{\"type\":\"string\",\"value\":\"B1\"}}");
    paste_plain(edit, "34");
    edit_doc_command(doc, ".uno:GoToCell", "{\"ToPoint\":{\"type\":\"string\",\"value\":\"A1:B1\"}}");
    settle();
    g_assert_cmpint(edit->combine->merge_cells(edit), ==, EDIT_OK);
    settle();
    g_assert_false(doc->expect_merge_dialog);           /* the dialog came and was answered */

    edit_doc_command(doc, ".uno:GoToCell", "{\"ToPoint\":{\"type\":\"string\",\"value\":\"A3:B4\"}}");
    GdkRGBA yellow = { 1, 1, 0, 1 };
    g_assert_cmpint(edit->markup->shade(edit, &yellow), ==, EDIT_OK);
    g_assert_cmpint(edit->insert->annotation(edit, "nota de celda"), ==, EDIT_OK);
    g_assert_cmpint(edit->insert->row(edit), ==, EDIT_OK);
    settle();

    g_assert_cmpint(edit->save_as(edit, saved), ==, EDIT_OK);
    edit->close(edit);

    char *xml = odf_content(saved);
    g_assert_nonnull(strstr(xml, "table:number-columns-spanned=\"2\""));
    g_assert_nonnull(strstr(xml, "fo:background-color=\"#ffff00\""));
    g_assert_nonnull(strstr(xml, "nota de celda"));
    g_assert_nonnull(strstr(xml, "<text:p>34</text:p>"));   /* hidden content kept */
    g_free(xml);
    g_free(saved);
    g_free(dir);
}

static void test_impress(void) {
    char *dir = test_dir();
    char *saved = g_build_filename(dir, "slides.odp", NULL);
    Edit *edit = new_doc("private:factory/simpress");
    EditDOC *doc = (EditDOC *)edit;

    g_assert_cmpint(doc->doc_type, ==, LOK_DOCTYPE_PRESENTATION);
    g_assert_null(edit->insert->annotation);            /* needs a dialog: not offered */
    g_assert_cmpstr(edit->insert->page_label, ==, "Nueva diapositiva");

    g_assert_cmpint(edit->insert->page(edit), ==, EDIT_OK);
    g_assert_cmpint(edit->insert->page(edit), ==, EDIT_OK);
    settle();
    g_assert_cmpint(doc->lok->pClass->getParts(doc->lok), ==, 3);
    g_assert_cmpint(edit->insert->table(edit, 2, 2), ==, EDIT_OK);   /* on slide 3 */
    settle();
    g_assert_cmpint(edit->combine->move_page(edit, -1), ==, EDIT_OK);
    settle();
    g_assert_cmpint(doc->lok->pClass->getPart(doc->lok), ==, 1);     /* followed the slide */

    g_assert_cmpint(edit->save_as(edit, saved), ==, EDIT_OK);
    edit->close(edit);

    char *xml = odf_content(saved);
    /* the slide with the table is now the second one */
    char *second = strstr(xml, "draw:name=\"page2\"");
    char *third = strstr(xml, "draw:name=\"page3\"");
    char *table = strstr(xml, "<table:table");
    g_assert_nonnull(second);
    g_assert_nonnull(third);
    g_assert_nonnull(table);
    g_assert_true(table > second && table < third);
    g_free(xml);
    g_free(saved);
    g_free(dir);
}

/* the controller of a given type the editor attached to its canvas */
static GObject *controller_of(GtkWidget *widget, GType type) {
    GListModel *list = gtk_widget_observe_controllers(widget);
    GObject *found = NULL;
    for (guint i = 0; i < g_list_model_get_n_items(list) && found == NULL; i++) {
        GObject *c = g_list_model_get_item(list, i);
        if (G_TYPE_CHECK_INSTANCE_TYPE(c, type)) found = c;
        g_object_unref(c);   /* the widget keeps it alive */
    }
    g_object_unref(list);
    return found;
}

static gboolean key(GObject *keys, guint keyval, GdkModifierType state) {
    gboolean handled = FALSE;
    g_signal_emit_by_name(keys, "key-pressed", keyval, 0, state, &handled);
    g_signal_emit_by_name(keys, "key-released", keyval, 0, state);
    return handled;
}

/* the path the window uses: rendered canvas, IM text, keys and clicks */
static void test_input(void) {
    char *dir = test_dir();
    char *saved = g_build_filename(dir, "typed.odt", NULL);
    Edit *edit = new_doc("private:factory/swriter");
    EditDOC *doc = (EditDOC *)edit;

    GtkWidget *window = gtk_window_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(window), box);
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    edit->render(edit, box);
    gtk_window_present(GTK_WINDOW(window));
    test_spin(800);                                   /* realized, sized, painted */
    g_assert_nonnull(doc->tile);                      /* LibreOffice painted the view */
    g_assert_cmpfloat(doc->zoom, >, 0.0);

    edit->focus(edit);
    g_signal_emit_by_name(doc->im, "commit", "Árbol ñ");  /* accents come from the IM */
    settle();
    GObject *keys = controller_of(doc->area, GTK_TYPE_EVENT_CONTROLLER_KEY);
    g_assert_nonnull(keys);
    g_assert_true(key(keys, GDK_KEY_BackSpace, 0));
    settle();
    g_assert_true(key(keys, GDK_KEY_Home, GDK_SHIFT_MASK));   /* select to line start */
    g_assert_true(test_wait(has_selection, edit, 3000));
    char *text = selected_text(edit);
    g_assert_cmpstr(text, ==, "Árbol ");
    g_free(text);

    /* a click collapses the selection and moves the cursor */
    GObject *click = controller_of(doc->area, GTK_TYPE_GESTURE_CLICK);
    g_assert_nonnull(click);
    g_signal_emit_by_name(click, "pressed", 1, 400.0, 300.0);
    g_signal_emit_by_name(click, "released", 1, 400.0, 300.0);
    settle();
    g_assert_cmpuint(doc->selection->len, ==, 0);

    /* Ctrl+Z is handled by the canvas and reaches LibreOffice's history */
    g_assert_true(test_wait(can_undo, edit, 3000));
    g_assert_true(key(keys, GDK_KEY_z, GDK_CONTROL_MASK));
    settle();

    g_assert_cmpint(edit->save_as(edit, saved), ==, EDIT_OK);
    g_signal_emit_by_name(doc->im, "commit", "Más");
    settle();
    g_assert_true(edit->is_modified(edit));
    g_assert_cmpint(edit->save(edit), ==, EDIT_OK);   /* temp file + rename over it */

    edit->close(edit);
    char *xml = odf_content(saved);
    g_assert_nonnull(strstr(xml, "Más"));
    g_free(xml);

    /* an existing file, reopened and rendered, takes typing too */
    edit = new_doc(saved);
    doc = (EditDOC *)edit;
    edit->render(edit, box);
    test_spin(800);
    edit->focus(edit);
    g_signal_emit_by_name(doc->im, "commit", "Reabierto ");
    settle();
    edit_doc_command(doc, ".uno:SelectAll", NULL);
    g_assert_true(test_wait(has_selection, edit, 3000));
    text = selected_text(edit);
    g_assert_nonnull(strstr(text, "Reabierto "));
    g_free(text);
    edit->close(edit);
    gtk_window_destroy(GTK_WINDOW(window));
    g_free(saved);
    g_free(dir);
}

int main(int argc, char *argv[]) {
    gtk_test_init(&argc, &argv, NULL);
    if (!edit_doc_available()) {
        g_print("LibreOffice not found: EditDOC tests skipped\n");
        return 77;   /* automake's "skipped" exit code */
    }
    g_test_add_func("/edit-doc/writer", test_writer);
    g_test_add_func("/edit-doc/calc", test_calc);
    g_test_add_func("/edit-doc/impress", test_impress);
    g_test_add_func("/edit-doc/input", test_input);
    int rc = g_test_run();
    /* LibreOffice crashes in its static destructors: leave like main() does */
    fflush(NULL);
    _Exit(rc);
}
