/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.3
 * License: GPLv3
 * Last edited: 2026-11-06
 */

/* EditTXT: text and Markdown editing, clipboard, history and persistence. */
#include "test_common.h"
#include "../include/edit_subclass/editTXT.h"

static Edit *open_txt(const char *path) {
    Edit *edit = new_edit_txt();
    g_assert_cmpint(edit->open(edit, path), ==, EDIT_OK);
    return edit;
}

static char *text_of(Edit *edit) {
    GtkTextIter a, b;
    GtkTextBuffer *buffer = ((EditTXT *)edit)->buffer;
    gtk_text_buffer_get_bounds(buffer, &a, &b);
    return gtk_text_buffer_get_text(buffer, &a, &b, FALSE);
}

/* selects [from, to) in characters */
static void select_range(Edit *edit, int from, int to) {
    GtkTextBuffer *buffer = ((EditTXT *)edit)->buffer;
    GtkTextIter a, b;
    gtk_text_buffer_get_iter_at_offset(buffer, &a, from);
    gtk_text_buffer_get_iter_at_offset(buffer, &b, to);
    gtk_text_buffer_select_range(buffer, &a, &b);
}

/* what typing does: an undoable user action at the cursor */
static void type_text(Edit *edit, const char *text) {
    GtkTextBuffer *buffer = ((EditTXT *)edit)->buffer;
    gtk_text_buffer_begin_user_action(buffer);
    gtk_text_buffer_insert_interactive_at_cursor(buffer, text, -1, TRUE);
    gtk_text_buffer_end_user_action(buffer);
}

static void cursor_at(Edit *edit, int offset) {
    select_range(edit, offset, offset);
}

static void test_edit_undo_save(void) {
    char *dir = test_dir();
    char *path = test_write(dir, "notes.txt", "Hola mundo");
    Edit *edit = open_txt(path);

    g_assert_nonnull(edit->history);
    g_assert_nonnull(edit->clipboard);
    g_assert_nonnull(edit->combine);
    g_assert_null(edit->markup);                 /* plain text has no attributes */
    g_assert_false(edit->is_modified(edit));
    g_assert_false(edit->history->can_undo(edit));   /* loading is not undoable */

    cursor_at(edit, 4);
    type_text(edit, " querido");
    g_assert_true(edit->is_modified(edit));
    char *t = text_of(edit);
    g_assert_cmpstr(t, ==, "Hola querido mundo");
    g_free(t);

    g_assert_true(edit->history->can_undo(edit));
    g_assert_cmpint(edit->history->undo(edit), ==, EDIT_OK);
    t = text_of(edit);
    g_assert_cmpstr(t, ==, "Hola mundo");
    g_free(t);
    g_assert_true(edit->history->can_redo(edit));
    g_assert_cmpint(edit->history->redo(edit), ==, EDIT_OK);

    g_assert_cmpint(edit->save(edit), ==, EDIT_OK);
    g_assert_false(edit->is_modified(edit));
    edit->close(edit);

    char *on_disk = test_read(path);
    g_assert_cmpstr(on_disk, ==, "Hola querido mundo");
    g_free(on_disk);

    /* save as: the new path becomes the document's path */
    edit = open_txt(path);
    char *copy = g_build_filename(dir, "copy.txt", NULL);
    g_assert_cmpint(edit->save_as(edit, copy), ==, EDIT_OK);
    g_assert_cmpstr(edit->path, ==, copy);
    edit->close(edit);
    g_assert_true(g_file_test(copy, G_FILE_TEST_EXISTS));

    g_free(copy);
    g_free(path);
    g_free(dir);
}

static void test_clipboard(void) {
    char *dir = test_dir();
    char *path = test_write(dir, "clip.txt", "uno dos tres");
    Edit *edit = open_txt(path);

    g_assert_false(edit->clipboard->has_selection(edit));
    g_assert_cmpint(edit->clipboard->copy(edit), ==, EDIT_ERR_NO_SELECTION);

    select_range(edit, 4, 7);                          /* "dos" */
    g_assert_true(edit->clipboard->has_selection(edit));
    g_assert_cmpint(edit->clipboard->copy(edit), ==, EDIT_OK);
    char *clip = test_clipboard_text();
    g_assert_cmpstr(clip, ==, "dos");
    g_free(clip);

    g_assert_cmpint(edit->clipboard->cut(edit), ==, EDIT_OK);
    char *t = text_of(edit);
    g_assert_cmpstr(t, ==, "uno  tres");
    g_free(t);

    cursor_at(edit, 0);
    g_assert_cmpint(edit->clipboard->paste(edit, false), ==, EDIT_OK);
    test_spin(300);                                    /* paste is asynchronous */
    t = text_of(edit);
    g_assert_cmpstr(t, ==, "dosuno  tres");
    g_free(t);

    edit->close(edit);
    g_free(path);
    g_free(dir);
}

static void test_merge_file(void) {
    char *dir = test_dir();
    char *path = test_write(dir, "a.md", "inicio\nfin\n");
    char *other = test_write(dir, "b.txt", "INSERTADO\n");
    char *bad = g_build_filename(dir, "bad.txt", NULL);
    g_assert_true(g_file_set_contents(bad, "\xff\xfe\x00x", 4, NULL));
    Edit *edit = open_txt(path);

    cursor_at(edit, 7);                                /* start of "fin" */
    g_assert_cmpint(edit->combine->merge_file(edit, other), ==, EDIT_OK);
    char *t = text_of(edit);
    g_assert_cmpstr(t, ==, "inicio\nINSERTADO\nfin\n");
    g_free(t);

    /* a non UTF-8 file is refused and nothing changes */
    g_assert_cmpint(edit->combine->merge_file(edit, bad), ==, EDIT_ERR_INVALID);
    g_assert_true(edit->history->undo(edit) == EDIT_OK);   /* one step undoes the merge */
    t = text_of(edit);
    g_assert_cmpstr(t, ==, "inicio\nfin\n");
    g_free(t);

    edit->close(edit);
    g_free(bad);
    g_free(other);
    g_free(path);
    g_free(dir);
}

static void test_markdown(void) {
    char *dir = test_dir();
    char *path = test_write(dir, "tema.md", "Palabra clave aqui\n");
    char *image = test_write(dir, "fig 1.png", "not really a png");
    Edit *edit = open_txt(path);

    g_assert_nonnull(edit->markup);
    g_assert_nonnull(edit->markup->highlight);
    g_assert_null(edit->markup->shade);
    g_assert_nonnull(edit->insert);
    g_assert_null(edit->insert->page);

    GdkRGBA red = { 1, 0, 0, 1 };
    g_assert_cmpint(edit->markup->highlight(edit, &red), ==, EDIT_ERR_NO_SELECTION);
    select_range(edit, 8, 13);                         /* "clave" */
    g_assert_cmpint(edit->markup->highlight(edit, &red), ==, EDIT_OK);
    char *t = text_of(edit);
    g_assert_cmpstr(t, ==, "Palabra <mark style=\"background-color: #ff0000\">clave</mark> aqui\n");
    g_free(t);

    /* clear with the bare cursor inside the highlight */
    cursor_at(edit, 52);
    g_assert_cmpint(edit->markup->clear(edit), ==, EDIT_OK);
    t = text_of(edit);
    g_assert_cmpstr(t, ==, "Palabra clave aqui\n");
    g_free(t);
    g_assert_cmpint(edit->history->undo(edit), ==, EDIT_OK);   /* highlight is back */

    /* table at the end, then a row and a column */
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(((EditTXT *)edit)->buffer, &end);
    gtk_text_buffer_place_cursor(((EditTXT *)edit)->buffer, &end);
    g_assert_cmpint(edit->insert->table(edit, 1, 2), ==, EDIT_OK);
    t = text_of(edit);
    g_assert_nonnull(strstr(t, "\n| Col 1 | Col 2 |\n| --- | --- |\n|     |     |\n"));
    g_free(t);

    /* the cursor is after the table: rows need it inside */
    g_assert_cmpint(edit->insert->row(edit), ==, EDIT_ERR_INVALID);
    GtkTextBuffer *buffer = ((EditTXT *)edit)->buffer;
    GtkTextIter header;
    gtk_text_buffer_get_iter_at_line(buffer, &header, 2);   /* "| Col 1 | Col 2 |" */
    gtk_text_buffer_place_cursor(buffer, &header);
    g_assert_cmpint(edit->insert->row(edit), ==, EDIT_OK);   /* goes below the rule */
    g_assert_cmpint(edit->insert->column(edit), ==, EDIT_OK);
    t = text_of(edit);
    g_assert_nonnull(strstr(t, "| Col 1 | Col 2 |     |\n| --- | --- | --- |\n|     |     |     |\n|     |     |     |\n"));
    g_free(t);

    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_place_cursor(buffer, &end);
    g_assert_cmpint(edit->insert->image(edit, image), ==, EDIT_OK);
    g_assert_cmpint(edit->insert->annotation(edit, "revisar -- luego"), ==, EDIT_OK);
    g_assert_cmpint(edit->save(edit), ==, EDIT_OK);
    edit->close(edit);

    /* reopen: everything is in the file */
    char *saved = test_read(path);
    g_assert_nonnull(strstr(saved, "<mark style=\"background-color: #ff0000\">clave</mark>"));
    g_assert_nonnull(strstr(saved, "![fig 1.png](<fig 1.png>)"));
    g_assert_nonnull(strstr(saved, "<!-- revisar - luego -->"));
    g_free(saved);
    edit = open_txt(path);
    g_assert_false(edit->is_modified(edit));
    edit->close(edit);

    g_free(image);
    g_free(path);
    g_free(dir);
}

static void test_invalid_inputs(void) {
    char *dir = test_dir();
    char *empty = test_write(dir, "empty.txt", "");
    char *missing = g_build_filename(dir, "missing.txt", NULL);
    char *bad = g_build_filename(dir, "latin1.txt", NULL);
    g_assert_true(g_file_set_contents(bad, "caf\xe9", 4, NULL));

    Edit *edit = new_edit_txt();
    g_assert_cmpint(edit->open(edit, missing), ==, EDIT_ERR_IO);
    g_assert_cmpint(edit->open(edit, bad), ==, EDIT_ERR_INVALID);
    g_assert_cmpint(edit->open(edit, empty), ==, EDIT_OK);
    g_assert_false(edit->clipboard->has_selection(edit));

    /* saving into a folder that does not exist fails and says so */
    char *nowhere = g_build_filename(dir, "no", "such", "dir.txt", NULL);
    g_assert_cmpint(edit->save_as(edit, nowhere), ==, EDIT_ERR_IO);
    g_assert_cmpstr(edit->path, ==, empty);           /* path kept on failure */
    edit->close(edit);

    g_free(nowhere);
    g_free(bad);
    g_free(missing);
    g_free(empty);
    g_free(dir);
}

int main(int argc, char *argv[]) {
    gtk_test_init(&argc, &argv, NULL);
    g_test_add_func("/edit-txt/edit-undo-save", test_edit_undo_save);
    g_test_add_func("/edit-txt/clipboard", test_clipboard);
    g_test_add_func("/edit-txt/merge-file", test_merge_file);
    g_test_add_func("/edit-txt/markdown", test_markdown);
    g_test_add_func("/edit-txt/invalid-inputs", test_invalid_inputs);
    return g_test_run();
}
