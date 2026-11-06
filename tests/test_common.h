/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.4
 * License: GPLv3
 * Last edited: 2026-11-06
 */

/*
 * Helpers shared by the GLib tests. They need a GTK display: run them on a
 * desktop or headless with Broadway:
 *   gtk4-broadwayd :5 &  GDK_BACKEND=broadway BROADWAY_DISPLAY=:5 make check
 */
#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <string.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

/* runs the main loop until cond(data) is true or timeout_ms passes */
static inline gboolean test_wait(gboolean (*cond)(gpointer), gpointer data, int timeout_ms) {
    gint64 end = g_get_monotonic_time() + (gint64)timeout_ms * 1000;
    while (!cond(data)) {
        if (g_get_monotonic_time() > end) return FALSE;
        if (!g_main_context_iteration(NULL, FALSE)) g_usleep(5000);
    }
    return TRUE;
}

/* lets queued idles (callbacks, async clipboard...) run for a while */
static inline void test_spin(int ms) {
    gint64 end = g_get_monotonic_time() + (gint64)ms * 1000;
    while (g_get_monotonic_time() < end) {
        if (!g_main_context_iteration(NULL, FALSE)) g_usleep(5000);
    }
}

/* ---------- system clipboard ---------- */

typedef struct {
    gboolean done;
    char *text;
} TestClip;

static inline void _test_clip_done(GObject *source, GAsyncResult *res, gpointer data) {
    TestClip *clip = data;
    clip->text = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(source), res, NULL);
    clip->done = TRUE;
}

static inline gboolean _test_clip_ready(gpointer data) {
    return ((TestClip *)data)->done;
}

/* text on the clipboard, newly allocated (NULL if none) */
static inline char *test_clipboard_text(void) {
    GdkClipboard *clipboard = gdk_display_get_clipboard(gdk_display_get_default());
    TestClip clip = { FALSE, NULL };
    gdk_clipboard_read_text_async(clipboard, NULL, _test_clip_done, &clip);
    test_wait(_test_clip_ready, &clip, 3000);
    return clip.text;
}

static inline void test_clipboard_set(const char *text) {
    gdk_clipboard_set_text(gdk_display_get_clipboard(gdk_display_get_default()), text);
}

/* ---------- files ---------- */

/* new temporary folder; free with g_free (contents are left for inspection) */
static inline char *test_dir(void) {
    char *dir = g_dir_make_tmp("studystudio-test-XXXXXX", NULL);
    g_assert_nonnull(dir);
    return dir;
}

static inline char *test_write(const char *dir, const char *name, const char *contents) {
    char *path = g_build_filename(dir, name, NULL);
    g_assert_true(g_file_set_contents(path, contents, -1, NULL));
    return path;
}

static inline char *test_read(const char *path) {
    char *contents = NULL;
    g_assert_true(g_file_get_contents(path, &contents, NULL, NULL));
    return contents;
}

#endif
