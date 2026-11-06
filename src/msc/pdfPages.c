/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.4
 * License: GPLv3
 * Last edited: 2026-11-06
 */

/*
 * pdfPages: qpdf's C API (qpdf-c.h), resolved with GModule at run time.
 * qpdf copies a page from another file together with everything it uses
 * (fonts, images, annotations), which is what merging needs.
 */
#include <gmodule.h>
#include <gio/gio.h>
#include "../../include/msc/pdfPages.h"

/* the few qpdf-c.h types we use, so the build does not need qpdf headers */
typedef struct _qpdf_data *qpdf_data;
typedef struct _qpdf_error *qpdf_error;
typedef unsigned int qpdf_oh;
typedef int QPDF_ERROR_CODE;
typedef int QPDF_BOOL;
#define QPDF_ERRORS (1 << 1)

static struct {
    qpdf_data (*init)(void);
    void (*cleanup)(qpdf_data *qpdf);
    void (*silence_errors)(qpdf_data qpdf);
    void (*set_suppress_warnings)(qpdf_data qpdf, QPDF_BOOL value);
    QPDF_BOOL (*has_error)(qpdf_data qpdf);
    qpdf_error (*get_error)(qpdf_data qpdf);
    const char *(*get_error_full_text)(qpdf_data qpdf, qpdf_error error);
    QPDF_ERROR_CODE (*read_memory)(qpdf_data qpdf, const char *description,
                                   const char *buffer, unsigned long long size,
                                   const char *password);
    int (*get_num_pages)(qpdf_data qpdf);
    qpdf_oh (*get_page_n)(qpdf_data qpdf, size_t index);
    QPDF_ERROR_CODE (*add_page)(qpdf_data qpdf, qpdf_data newpage_qpdf,
                                qpdf_oh newpage, QPDF_BOOL first);
    QPDF_ERROR_CODE (*add_page_at)(qpdf_data qpdf, qpdf_data newpage_qpdf,
                                   qpdf_oh newpage, QPDF_BOOL before, qpdf_oh refpage);
    QPDF_ERROR_CODE (*remove_page)(qpdf_data qpdf, qpdf_oh page);
    QPDF_ERROR_CODE (*init_write_memory)(qpdf_data qpdf);
    QPDF_ERROR_CODE (*write)(qpdf_data qpdf);
    size_t (*get_buffer_length)(qpdf_data qpdf);
    const unsigned char *(*get_buffer)(qpdf_data qpdf);
} q;

static GModule *s_module = NULL;
static bool s_tried = false;

static const char *const s_names[] = {
#ifdef _WIN32
    "qpdf30.dll", "libqpdf30.dll", "qpdf29.dll", "libqpdf29.dll", "qpdf28.dll", "libqpdf28.dll",
#else
    "libqpdf.so.30", "libqpdf.so.29", "libqpdf.so.28", "libqpdf.so",
#endif
    NULL
};

static bool _bind(GModule *module) {
#define BIND(field, name) \
    if (!g_module_symbol(module, name, (gpointer *)&q.field) || q.field == NULL) return false
    BIND(init, "qpdf_init");
    BIND(cleanup, "qpdf_cleanup");
    BIND(silence_errors, "qpdf_silence_errors");
    BIND(set_suppress_warnings, "qpdf_set_suppress_warnings");
    BIND(has_error, "qpdf_has_error");
    BIND(get_error, "qpdf_get_error");
    BIND(get_error_full_text, "qpdf_get_error_full_text");
    BIND(read_memory, "qpdf_read_memory");
    BIND(get_num_pages, "qpdf_get_num_pages");
    BIND(get_page_n, "qpdf_get_page_n");
    BIND(add_page, "qpdf_add_page");
    BIND(add_page_at, "qpdf_add_page_at");
    BIND(remove_page, "qpdf_remove_page");
    BIND(init_write_memory, "qpdf_init_write_memory");
    BIND(write, "qpdf_write");
    BIND(get_buffer_length, "qpdf_get_buffer_length");
    BIND(get_buffer, "qpdf_get_buffer");
#undef BIND
    return true;
}

static bool _try(const char *name) {
    GModule *module = g_module_open(name, G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
    if (module == NULL) return false;
    if (!_bind(module)) {
        g_module_close(module);
        return false;
    }
    s_module = module;   /* kept open for the whole run */
    return true;
}

bool pdf_pages_available(void) {
    if (s_tried) return s_module != NULL;
    s_tried = true;
    if (!g_module_supported()) return false;

    const char *env = g_getenv("STUDYSTUDIO_QPDF_PATH");
    if (env != NULL && *env != '\0' && _try(env)) return true;
    for (int i = 0; s_names[i] != NULL; i++) {
        if (_try(s_names[i])) return true;
    }
    return false;
}

/* turns a qpdf error (if any) into a GError; true if there was one */
static bool _failed(qpdf_data pdf, QPDF_ERROR_CODE code, GError **error) {
    if (!(code & QPDF_ERRORS) && !q.has_error(pdf)) return false;
    const char *text = "unknown qpdf error";
    qpdf_error e = q.has_error(pdf) ? q.get_error(pdf) : NULL;
    if (e != NULL) text = q.get_error_full_text(pdf, e);
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "qpdf: %s", text);
    return true;
}

/* a qpdf object reading bytes; bytes must outlive it (qpdf does not copy) */
static qpdf_data _open(GBytes *bytes, const char *name, GError **error) {
    gsize size = 0;
    const char *data = g_bytes_get_data(bytes, &size);
    qpdf_data pdf = q.init();
    q.silence_errors(pdf);              /* errors are returned, not printed */
    q.set_suppress_warnings(pdf, 1);
    if (_failed(pdf, q.read_memory(pdf, name, data, size, NULL), error)) {
        q.cleanup(&pdf);
        return NULL;
    }
    return pdf;
}

static GBytes *_write(qpdf_data pdf, GError **error) {
    if (_failed(pdf, q.init_write_memory(pdf), error)) return NULL;
    if (_failed(pdf, q.write(pdf), error)) return NULL;
    return g_bytes_new(q.get_buffer(pdf), q.get_buffer_length(pdf));   /* copies */
}

GBytes *pdf_pages_insert(GBytes *doc, GBytes *other, int after, GError **error) {
    if (!pdf_pages_available()) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "qpdf not found");
        return NULL;
    }
    GBytes *result = NULL;
    qpdf_data dst = _open(doc, "document", error);
    qpdf_data src = dst != NULL ? _open(other, "inserted", error) : NULL;
    if (src == NULL) goto out;

    int n_dst = q.get_num_pages(dst), n_src = q.get_num_pages(src);
    if (n_dst < 0 || n_src < 0) {
        _failed(n_dst < 0 ? dst : src, QPDF_ERRORS, error);
        goto out;
    }
    if (after < -1 || after >= n_dst) after = n_dst - 1;

    /* every new page goes before the page that followed `after`, in order */
    bool append = after + 1 >= n_dst;
    qpdf_oh ref = append ? 0 : q.get_page_n(dst, (size_t)(after + 1));
    for (int i = 0; i < n_src; i++) {
        qpdf_oh page = q.get_page_n(src, (size_t)i);
        QPDF_ERROR_CODE rc = append ? q.add_page(dst, src, page, 0)
                                    : q.add_page_at(dst, src, page, 1, ref);
        if (_failed(dst, rc, error)) goto out;
    }
    result = _write(dst, error);
out:
    if (src != NULL) q.cleanup(&src);
    if (dst != NULL) q.cleanup(&dst);
    return result;
}

GBytes *pdf_pages_move(GBytes *doc, int from, int to, GError **error) {
    if (!pdf_pages_available()) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "qpdf not found");
        return NULL;
    }
    GBytes *result = NULL;
    qpdf_data pdf = _open(doc, "document", error);
    if (pdf == NULL) return NULL;

    int n = q.get_num_pages(pdf);
    if (n < 0) {
        _failed(pdf, QPDF_ERRORS, error);
        goto out;
    }
    if (from < 0 || from >= n || to < 0 || to >= n) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                    "page %d cannot move to %d (%d pages)", from + 1, to + 1, n);
        goto out;
    }

    qpdf_oh page = q.get_page_n(pdf, (size_t)from);
    if (_failed(pdf, q.remove_page(pdf, page), error)) goto out;
    /* the indices now refer to the n - 1 remaining pages */
    QPDF_ERROR_CODE rc = (to >= n - 1) ? q.add_page(pdf, pdf, page, 0)
                                       : q.add_page_at(pdf, pdf, page, 1, q.get_page_n(pdf, (size_t)to));
    if (_failed(pdf, rc, error)) goto out;
    result = _write(pdf, error);
out:
    q.cleanup(&pdf);
    return result;
}
