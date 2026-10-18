/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.2
 * License: GPLv3
 * Last edited: 2026-10-18
 */

/* Drop-down menu listing every EditKind ("what do you want to edit?"). */
#ifndef MENU_EDIT_KIND_H
#define MENU_EDIT_KIND_H

#include "../menu.h"
#include "../edit.h"

/* called when the user picks a kind */
typedef void (*MenuEditKindChosen)(const EditKind *kind, gpointer user_data);

typedef struct {
    Menu base;                    /* must be the first member */
    MenuEditKindChosen on_chosen;
    gpointer user_data;           /* passed back to on_chosen */
} MenuEditKind;

/* enabled[i] false hides edit_kinds[i]; enabled NULL shows every kind */
Menu *new_menu_edit_kind(MenuEditKindChosen on_chosen, gpointer user_data,
                         const bool *enabled);

#endif
