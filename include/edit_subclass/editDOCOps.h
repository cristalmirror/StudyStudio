/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.4
 * License: GPLv3
 * Last edited: 2026-11-06
 */

/*
 * Internal to EditDOC: editDOC.c (document, view, input, callbacks) and
 * editDOCOps.c (editing tools as UNO commands) talk through these.
 */
#ifndef EDIT_DOC_OPS_H
#define EDIT_DOC_OPS_H

#include "editDOC.h"

/* LibreOffice key codes (com.sun.star.awt.Key) used by both files */
#define DOC_KEY_RETURN    1280
#define DOC_KEY_ESCAPE    1281
#define DOC_KEY_DELETE    1286

/* picks the capability tables for doc->doc_type (Writer, Calc, Impress) */
void edit_doc_bind_ops(EditDOC *doc);

/* posts a UNO command; args is a JSON object or NULL */
void edit_doc_command(EditDOC *doc, const char *command, const char *args);

/* a key press + release; char_code is the Unicode character or 0 */
void edit_doc_key(EditDOC *doc, int char_code, int key_code);

/* re-reads part list and size after slides / sheets changed */
void edit_doc_refresh(EditDOC *doc);

/* the system clipboard of the document's display, NULL without one */
GdkClipboard *edit_doc_clipboard(EditDOC *doc);

/* new reference to the link; the doc may be gone when it is used */
EditDOCLink *edit_doc_link_ref(EditDOC *doc);
EditDOC *edit_doc_link_get(EditDOCLink *link);    /* NULL once closed */
void edit_doc_link_unref(EditDOCLink *link);

#endif
