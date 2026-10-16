/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.2
 * License: GPLv3
 * Last edited: 2026-10-13
 */
/*
 * Subject menu: one entry per EditKind, reports the chosen one.
 */
#include <stdlib.h>
#include "../../include/msc/menuEditKind.h"

static void _menu_edit_kind_activate(Menu *self, gpointer item_data) {
    MenuEditKind *menu = (MenuEditKind *)self;   /* valid: base is the first member */
    menu->on_chosen((const EditKind *)item_data, menu->user_data);
}

Menu *new_menu_edit_kind(MenuEditKindChosen on_chosen, gpointer user_data) {
    MenuEditKind *menu = calloc(1, sizeof *menu);
    if (menu == NULL) return NULL;

    menu_init(&menu->base);
    menu->base.activate = _menu_edit_kind_activate;
    menu->on_chosen = on_chosen;
    menu->user_data = user_data;

    for (size_t i = 0; i < edit_kinds_count; i++) {
        const EditKind *kind = &edit_kinds[i];
        menu->base.add_item(&menu->base, kind->label,
                            kind->suffixes != NULL, (gpointer)kind);
    }
    return &menu->base;
}
