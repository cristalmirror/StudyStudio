/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.3
 * License: GPLv3
 * Last edited: 2026-11-06
 */

/*
 * Structural PDF operations (insert pages from another PDF, reorder pages).
 * Poppler can only render and annotate, so this uses qpdf (Apache-2.0),
 * loaded at run time like LibreOffice: if the library is missing the
 * operations are simply not offered. Every function works on complete
 * PDF files held in memory and returns a new file; the input is not touched.
 */
#ifndef PDF_PAGES_H
#define PDF_PAGES_H

#include <stdbool.h>
#include <glib.h>

/* true if libqpdf was found (STUDYSTUDIO_QPDF_PATH or the usual names) */
bool pdf_pages_available(void);

/* copy of doc with every page of other inserted after page index `after`
 * (-1: before the first page). NULL and error set on failure. */
GBytes *pdf_pages_insert(GBytes *doc, GBytes *other, int after, GError **error);

/* copy of doc with page `from` moved to index `to` (both 0-based) */
GBytes *pdf_pages_move(GBytes *doc, int from, int to, GError **error);

#endif
