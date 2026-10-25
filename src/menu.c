/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.3
 * License: GPLv3
 * Last edited: 2026-10-25
 */
/*
 * Common operations of the Menu hierarchy.
 */
#include <stdlib.h>
#include "../include/menu.h"

/* every entry ends here: close the popover, then let the subclass act */
static void _menu_on_item_clicked(GtkButton *button, gpointer user_data) {
    Menu *self = (Menu *)user_data;
    gpointer item_data = g_object_get_data(G_OBJECT(button), "item");

    self->popdown(self);
    if (self->activate != NULL) {
        self->activate(self, item_data);
    }
}

static GtkWidget *_menu_add_item(Menu *self, const char *label, bool sensitive, gpointer item_data) {
    GtkWidget *item = gtk_button_new_with_label(label);
    gtk_button_set_has_frame(GTK_BUTTON(item), FALSE);   /* looks like a menu entry */
    gtk_widget_set_sensitive(item, sensitive);           /* greyed: not ready yet */
    g_object_set_data(G_OBJECT(item), "item", item_data);
    g_signal_connect(item, "clicked", G_CALLBACK(_menu_on_item_clicked), self);
    gtk_box_append(GTK_BOX(self->list), item);
    return item;
}

static void _menu_popdown(Menu *self) {
    gtk_popover_popdown(GTK_POPOVER(self->popover));
}

/* destructor: GTK calls it when the popover is finalized */
static void _menu_free(gpointer data) {
    Menu *self = (Menu *)data;
    if (self->finalize != NULL) {
        self->finalize(self);   /* virtual destructor */
    }
    free(self);
}

void menu_init(Menu *self) {
    self->list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    self->popover = gtk_popover_new();
    gtk_popover_set_child(GTK_POPOVER(self->popover), self->list);

    /* the Menu lives as long as its popover: GTK frees it for us */
    g_object_set_data_full(G_OBJECT(self->popover), "menu", self, _menu_free);

    /* abstract: NULL until the subclass assigns them */
    self->activate = NULL;
    self->finalize = NULL;

    /* common: */
    self->add_item = _menu_add_item;
    self->popdown = _menu_popdown;
}
