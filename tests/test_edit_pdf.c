/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.3
 * License: GPLv3
 * Last edited: 2026-11-06
 */

/* EditPDF: selection, copy, highlight/shade/clear, undo, save, page merge. */
#include <cairo-pdf.h>
#include <poppler.h>
#include "test_common.h"
#include "../include/edit_subclass/editPDF.h"
#include "../include/msc/pdfPages.h"

#define PAGE_W 595.0   /* A4 in points */
#define PAGE_H 842.0

/* a PDF whose page i says "<prefix> <i+1> alpha beta" at (72, 100) */
static char *make_pdf(const char *dir, const char *name, const char *prefix, int pages) {
    char *path = g_build_filename(dir, name, NULL);
    cairo_surface_t *surface = cairo_pdf_surface_create(path, PAGE_W, PAGE_H);
    cairo_t *cr = cairo_create(surface);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 24);
    for (int i = 0; i < pages; i++) {
        char *text = g_strdup_printf("%s %d alpha beta", prefix, i + 1);
        cairo_move_to(cr, 72, 100);
        cairo_show_text(cr, text);
        cairo_show_page(cr);
        g_free(text);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return path;
}

/* the line of text on every page, in area points (top-left origin) */
static const EditPDFArea s_line = { 60, 70, 400, 110 };

static int annot_count(Edit *edit, int page, PopplerAnnotType type) {
    EditPDF *pdf = (EditPDF *)edit;
    GList *mapping = poppler_page_get_annot_mapping(pdf->pages[page].page);
    int n = 0;
    for (GList *l = mapping; l != NULL; l = l->next) {
        PopplerAnnotMapping *m = l->data;
        if (poppler_annot_get_annot_type(m->annot) == type) n++;
    }
    poppler_page_free_annot_mapping(mapping);
    return n;
}

static char *page_text(Edit *edit, int page) {
    return poppler_page_get_text(((EditPDF *)edit)->pages[page].page);
}

static Edit *open_pdf(const char *path) {
    Edit *edit = new_edit_pdf();
    g_assert_cmpint(edit->open(edit, path), ==, EDIT_OK);
    return edit;
}

static void test_select_copy(void) {
    char *dir = test_dir();
    char *path = make_pdf(dir, "doc.pdf", "Page", 2);
    Edit *edit = open_pdf(path);

    g_assert_cmpint(((EditPDF *)edit)->n_pages, ==, 2);
    g_assert_null(edit->clipboard->cut);              /* page content is read-only */
    g_assert_null(edit->clipboard->paste);
    g_assert_false(edit->clipboard->has_selection(edit));
    g_assert_cmpint(edit->clipboard->copy(edit), ==, EDIT_ERR_NO_SELECTION);

    g_assert_true(edit_pdf_select(edit, 1, &s_line));
    g_assert_true(edit->clipboard->has_selection(edit));
    g_assert_cmpint(edit->clipboard->copy(edit), ==, EDIT_OK);
    char *clip = test_clipboard_text();
    g_assert_nonnull(clip);
    g_assert_nonnull(strstr(clip, "Page 2 alpha"));
    g_free(clip);

    edit->close(edit);
    g_free(path);
    g_free(dir);
}

static void test_markup_undo_persist(void) {
    char *dir = test_dir();
    char *path = make_pdf(dir, "doc.pdf", "Page", 1);
    Edit *edit = open_pdf(path);
    GdkRGBA red = { 1, 0, 0, 1 }, blue = { 0, 0, 1, 1 };

    g_assert_cmpint(edit->markup->highlight(edit, &red), ==, EDIT_ERR_NO_SELECTION);
    g_assert_true(edit_pdf_select(edit, 0, &s_line));
    g_assert_cmpint(edit->markup->highlight(edit, &red), ==, EDIT_OK);
    g_assert_cmpint(annot_count(edit, 0, POPPLER_ANNOT_HIGHLIGHT), ==, 1);
    g_assert_true(edit->is_modified(edit));

    /* undo / redo go through whole-file snapshots */
    g_assert_true(edit->history->can_undo(edit));
    g_assert_cmpint(edit->history->undo(edit), ==, EDIT_OK);
    g_assert_cmpint(annot_count(edit, 0, POPPLER_ANNOT_HIGHLIGHT), ==, 0);
    g_assert_cmpint(edit->history->redo(edit), ==, EDIT_OK);
    g_assert_cmpint(annot_count(edit, 0, POPPLER_ANNOT_HIGHLIGHT), ==, 1);

    /* shade the same line, then clear removes both from the selection */
    g_assert_true(edit_pdf_select(edit, 0, &s_line));
    g_assert_cmpint(edit->markup->shade(edit, &blue), ==, EDIT_OK);
    g_assert_cmpint(annot_count(edit, 0, POPPLER_ANNOT_SQUARE), ==, 1);
    g_assert_cmpint(edit->markup->clear(edit), ==, EDIT_OK);
    g_assert_cmpint(annot_count(edit, 0, POPPLER_ANNOT_HIGHLIGHT), ==, 0);
    g_assert_cmpint(annot_count(edit, 0, POPPLER_ANNOT_SQUARE), ==, 0);
    g_assert_cmpint(edit->history->undo(edit), ==, EDIT_OK);      /* both back */

    g_assert_true(edit_pdf_select(edit, 0, &s_line));
    g_assert_cmpint(edit->insert->annotation(edit, "repasar"), ==, EDIT_OK);
    g_assert_cmpint(edit->save(edit), ==, EDIT_OK);
    g_assert_false(edit->is_modified(edit));
    edit->close(edit);

    /* reopen: annotations and colors are in the file */
    edit = open_pdf(path);
    g_assert_cmpint(annot_count(edit, 0, POPPLER_ANNOT_HIGHLIGHT), ==, 1);
    g_assert_cmpint(annot_count(edit, 0, POPPLER_ANNOT_SQUARE), ==, 1);
    g_assert_cmpint(annot_count(edit, 0, POPPLER_ANNOT_TEXT), ==, 1);
    GList *mapping = poppler_page_get_annot_mapping(((EditPDF *)edit)->pages[0].page);
    for (GList *l = mapping; l != NULL; l = l->next) {
        PopplerAnnotMapping *m = l->data;
        if (poppler_annot_get_annot_type(m->annot) != POPPLER_ANNOT_HIGHLIGHT) continue;
        PopplerColor *c = poppler_annot_get_color(m->annot);
        g_assert_cmpuint(c->red, ==, 65535);
        g_assert_cmpuint(c->blue, ==, 0);
        g_free(c);
    }
    poppler_page_free_annot_mapping(mapping);
    char *text = page_text(edit, 0);
    g_assert_nonnull(strstr(text, "Page 1 alpha beta"));    /* content untouched */
    g_free(text);
    edit->close(edit);

    g_free(path);
    g_free(dir);
}

static void test_pages(void) {
    if (!pdf_pages_available()) {
        g_test_skip("libqpdf not found");
        return;
    }
    char *dir = test_dir();
    char *path = make_pdf(dir, "doc.pdf", "Page", 2);
    char *other = make_pdf(dir, "other.pdf", "Other", 2);
    Edit *edit = open_pdf(path);
    g_assert_nonnull(edit->combine);

    /* an annotation on page 1 must survive the restructuring */
    GdkRGBA yellow = { 1, 1, 0, 1 };
    g_assert_true(edit_pdf_select(edit, 0, &s_line));
    g_assert_cmpint(edit->markup->highlight(edit, &yellow), ==, EDIT_OK);

    /* insert after page 1 (the selected one) */
    g_assert_cmpint(edit->combine->merge_file(edit, other), ==, EDIT_OK);
    g_assert_cmpint(((EditPDF *)edit)->n_pages, ==, 4);
    const char *order[] = { "Page 1", "Other 1", "Other 2", "Page 2" };
    for (int i = 0; i < 4; i++) {
        char *t = page_text(edit, i);
        g_assert_nonnull(strstr(t, order[i]));
        g_free(t);
    }
    g_assert_cmpint(annot_count(edit, 0, POPPLER_ANNOT_HIGHLIGHT), ==, 1);

    /* move "Page 2" (last) one place earlier */
    g_assert_true(edit_pdf_select(edit, 3, &s_line));
    g_assert_cmpint(edit->combine->move_page(edit, +1), ==, EDIT_ERR_INVALID);   /* already last */
    g_assert_cmpint(edit->combine->move_page(edit, -1), ==, EDIT_OK);
    char *t = page_text(edit, 2);
    g_assert_nonnull(strstr(t, "Page 2"));
    g_free(t);

    /* undo restores the previous order */
    g_assert_cmpint(edit->history->undo(edit), ==, EDIT_OK);
    t = page_text(edit, 3);
    g_assert_nonnull(strstr(t, "Page 2"));
    g_free(t);
    g_assert_cmpint(edit->history->redo(edit), ==, EDIT_OK);

    g_assert_cmpint(edit->save(edit), ==, EDIT_OK);
    edit->close(edit);

    edit = open_pdf(path);
    g_assert_cmpint(((EditPDF *)edit)->n_pages, ==, 4);
    t = page_text(edit, 2);
    g_assert_nonnull(strstr(t, "Page 2"));
    g_free(t);
    g_assert_cmpint(annot_count(edit, 0, POPPLER_ANNOT_HIGHLIGHT), ==, 1);

    /* a file that is not a PDF is rejected and nothing changes */
    char *junk = test_write(dir, "junk.pdf", "this is not a pdf");
    g_assert_cmpint(edit->combine->merge_file(edit, junk), !=, EDIT_OK);
    g_assert_cmpint(((EditPDF *)edit)->n_pages, ==, 4);
    g_assert_false(edit->history->can_undo(edit));
    edit->close(edit);

    g_free(junk);
    g_free(other);
    g_free(path);
    g_free(dir);
}

static void test_invalid(void) {
    char *dir = test_dir();
    char *junk = test_write(dir, "junk.pdf", "%PDF-1.4 broken");
    char *missing = g_build_filename(dir, "none.pdf", NULL);
    Edit *edit = new_edit_pdf();
    g_assert_cmpint(edit->open(edit, missing), ==, EDIT_ERR_IO);
    g_assert_cmpint(edit->open(edit, junk), ==, EDIT_ERR_INVALID);
    g_assert_false(edit_pdf_select(edit, 0, &s_line));   /* no pages */
    edit->close(edit);
    g_free(missing);
    g_free(junk);
    g_free(dir);
}

int main(int argc, char *argv[]) {
    gtk_test_init(&argc, &argv, NULL);
    g_test_add_func("/edit-pdf/select-copy", test_select_copy);
    g_test_add_func("/edit-pdf/markup-undo-persist", test_markup_undo_persist);
    g_test_add_func("/edit-pdf/pages", test_pages);
    g_test_add_func("/edit-pdf/invalid", test_invalid);
    return g_test_run();
}
