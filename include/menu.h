/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.3
 * License: GPLv3
 * Last edited: 2026-10-13
 */

/*
 * Abstract base "class" for every drop-down menu of StudyStudio.
 * Each subclass embeds Menu as its FIRST member, calls menu_init()
 * and assigns its own activate (and finalize if it owns memory).
 */
#ifndef MENU_H
#define MENU_H
#include <stdbool.h>
#include <gtk/gtk.h>

typedef struct Menu Menu;

struct Menu {
    GtkWidget *popover;   /* root widget, give it to a GtkMenuButton */
    GtkWidget *list;      /* vertical box holding the entries */

    /* Abstract: assigned by each subclass */
    void (*activate)(Menu *self, gpointer item_data);  /* an entry was clicked */
    void (*finalize)(Menu *self);                      /* frees subclass fields, may be NULL */

    /* Common: implemented once in menu.c */
    GtkWidget *(*add_item)(Menu *self, const char *label, bool sensitive, gpointer item_data);
    void (*popdown)(Menu *self);
};

/* protected: only subclass constructors call it */
void menu_init(Menu *self);

#endif
