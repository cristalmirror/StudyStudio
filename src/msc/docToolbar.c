/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.4
 * License: GPLv3
 * Last edited: 2026-11-06
 */

/*
 * DocToolbar: one GAction per tool. Every action declares when the bound
 * editor offers it (available: its function pointer is not NULL) and when
 * it can run right now (ready: e.g. something is selected). Buttons use
 * "action-name", so GTK greys them out by itself when an action is disabled;
 * tools the editor does not offer are hidden instead of faked.
 */
#include <string.h>
#include "../../include/msc/docToolbar.h"

struct DocToolbar {
    GtkWidget *root;               /* GtkFlowBox: wraps on narrow windows */
    GSimpleActionGroup *actions;
    GtkWindow *window;
    GCancellable *cancellable;
    const DocToolbarOwner *owner;
    gpointer owner_data;
    Edit *edit;                    /* bound document, NULL if none */
    bool message_sent;             /* the editor explained the last failure */

    GPtrArray *items;              /* buttons tied to a ToolAction */
    GPtrArray *groups;             /* containers hidden when all items are */
    GtkWidget *color;              /* GtkColorDialogButton */
    GtkWidget *rows, *columns;     /* table size */
    GtkWidget *comment;            /* comment text */
};

typedef struct {
    const char *name;
    void (*run)(DocToolbar *bar);
    bool (*available)(Edit *edit);   /* NULL: always, while a document is open */
    bool (*ready)(Edit *edit);       /* NULL: whenever available */
} ToolAction;

/* ---------- reporting ---------- */

/* shows why an operation failed, unless the editor already did */
static void _report(DocToolbar *bar, int rc) {
    if (rc != EDIT_OK && !bar->message_sent)
        bar->owner->message(bar->owner_data, edit_result_message(rc));
}

static void _begin(DocToolbar *bar) {
    bar->message_sent = false;
}

/* ---------- availability ---------- */

static bool _history(Edit *e)   { return e->history != NULL; }
static bool _can_undo(Edit *e)  { return e->history->can_undo(e); }
static bool _can_redo(Edit *e)  { return e->history->can_redo(e); }
static bool _copy(Edit *e)      { return e->clipboard != NULL && e->clipboard->copy != NULL; }
static bool _cut(Edit *e)       { return e->clipboard != NULL && e->clipboard->cut != NULL; }
static bool _paste(Edit *e)     { return e->clipboard != NULL && e->clipboard->paste != NULL; }
static bool _selected(Edit *e)  { return e->clipboard->has_selection(e); }
static bool _highlight(Edit *e) { return e->markup != NULL && e->markup->highlight != NULL; }
static bool _shade(Edit *e)     { return e->markup != NULL && e->markup->shade != NULL; }
static bool _clear(Edit *e)     { return e->markup != NULL && e->markup->clear != NULL; }
static bool _image(Edit *e)     { return e->insert != NULL && e->insert->image != NULL; }
static bool _table(Edit *e)     { return e->insert != NULL && e->insert->table != NULL; }
static bool _row(Edit *e)       { return e->insert != NULL && e->insert->row != NULL; }
static bool _column(Edit *e)    { return e->insert != NULL && e->insert->column != NULL; }
static bool _page(Edit *e)      { return e->insert != NULL && e->insert->page != NULL; }
static bool _note(Edit *e)      { return e->insert != NULL && e->insert->annotation != NULL; }
static bool _merge(Edit *e)     { return e->combine != NULL && e->combine->merge_file != NULL; }
static bool _cells(Edit *e)     { return e->combine != NULL && e->combine->merge_cells != NULL; }
static bool _split(Edit *e)     { return e->combine != NULL && e->combine->split_cells != NULL; }
static bool _move(Edit *e)      { return e->combine != NULL && e->combine->move_page != NULL; }

/* ---------- file dialogs ---------- */

typedef void (*FileChosen)(DocToolbar *bar, const char *path);

static void _file_done(GObject *source, GAsyncResult *res, gpointer user_data) {
    DocToolbar *bar = user_data;
    FileChosen chosen = (FileChosen)g_object_get_data(source, "chosen");
    GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), res, NULL);
    if (file == NULL) return;                       /* cancelled */
    char *path = g_file_get_path(file);
    g_object_unref(file);
    /* the document may have been closed while the dialog was open */
    if (path != NULL && bar->edit != NULL) chosen(bar, path);
    g_free(path);
}

static void _ask_file(DocToolbar *bar, const char *title, const char *filter_name,
                      const char *const *suffixes, FileChosen chosen) {
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, title);
    if (suffixes != NULL) {
        GtkFileFilter *filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, filter_name);
        for (int i = 0; suffixes[i] != NULL; i++) gtk_file_filter_add_suffix(filter, suffixes[i]);
        gtk_file_dialog_set_default_filter(dialog, filter);
        g_object_unref(filter);
    }
    g_object_set_data(G_OBJECT(dialog), "chosen", (gpointer)chosen);
    gtk_file_dialog_open(dialog, bar->window, bar->cancellable, _file_done, bar);
    g_object_unref(dialog);   /* the pending operation keeps its own ref */
}

/* ---------- actions ---------- */

static void _run_save(DocToolbar *bar)    { bar->owner->save(bar->owner_data); }
static void _run_save_as(DocToolbar *bar) { bar->owner->save_as(bar->owner_data); }
static void _run_close(DocToolbar *bar)   { bar->owner->close(bar->owner_data); }

static void _run_undo(DocToolbar *bar)  { _report(bar, bar->edit->history->undo(bar->edit)); }
static void _run_redo(DocToolbar *bar)  { _report(bar, bar->edit->history->redo(bar->edit)); }
static void _run_copy(DocToolbar *bar)  { _report(bar, bar->edit->clipboard->copy(bar->edit)); }
static void _run_cut(DocToolbar *bar)   { _report(bar, bar->edit->clipboard->cut(bar->edit)); }
static void _run_paste(DocToolbar *bar) { _report(bar, bar->edit->clipboard->paste(bar->edit, false)); }
static void _run_paste_plain(DocToolbar *bar) {
    _report(bar, bar->edit->clipboard->paste(bar->edit, true));
}

static const GdkRGBA *_color(DocToolbar *bar) {
    return gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(bar->color));
}

static void _run_highlight(DocToolbar *bar) {
    _report(bar, bar->edit->markup->highlight(bar->edit, _color(bar)));
}

static void _run_shade(DocToolbar *bar) {
    _report(bar, bar->edit->markup->shade(bar->edit, _color(bar)));
}

static void _run_clear(DocToolbar *bar) { _report(bar, bar->edit->markup->clear(bar->edit)); }

static void _image_chosen(DocToolbar *bar, const char *path) {
    _begin(bar);
    _report(bar, bar->edit->insert->image(bar->edit, path));
}

static void _run_image(DocToolbar *bar) {
    static const char *const images[] = { "png", "jpg", "jpeg", "gif", "bmp", "svg", "webp", NULL };
    _ask_file(bar, "Insertar imagen", "Imágenes", images, _image_chosen);
}

static void _run_table(DocToolbar *bar) {
    int rows = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(bar->rows));
    int columns = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(bar->columns));
    _report(bar, bar->edit->insert->table(bar->edit, rows, columns));
}

static void _run_row(DocToolbar *bar)    { _report(bar, bar->edit->insert->row(bar->edit)); }
static void _run_column(DocToolbar *bar) { _report(bar, bar->edit->insert->column(bar->edit)); }
static void _run_page(DocToolbar *bar)   { _report(bar, bar->edit->insert->page(bar->edit)); }

static void _run_note(DocToolbar *bar) {
    const char *text = gtk_editable_get_text(GTK_EDITABLE(bar->comment));
    if (text == NULL || *text == '\0') {
        bar->owner->message(bar->owner_data, "Write the comment text first");
        return;
    }
    int rc = bar->edit->insert->annotation(bar->edit, text);
    _report(bar, rc);
    if (rc == EDIT_OK) gtk_editable_set_text(GTK_EDITABLE(bar->comment), "");
}

static void _merge_chosen(DocToolbar *bar, const char *path) {
    _begin(bar);
    if (!edit_path_has_suffix(path, bar->edit->combine->suffixes)) {
        _report(bar, EDIT_ERR_INVALID);
        return;
    }
    int rc = bar->edit->combine->merge_file(bar->edit, path);
    _report(bar, rc);
    if (rc == EDIT_OK && !bar->message_sent)
        bar->owner->message(bar->owner_data, "Content inserted");
}

static void _run_merge(DocToolbar *bar) {
    _ask_file(bar, "Insertar contenido de otro archivo", "Archivos compatibles",
              bar->edit->combine->suffixes, _merge_chosen);
}

static void _run_cells(DocToolbar *bar) { _report(bar, bar->edit->combine->merge_cells(bar->edit)); }
static void _run_split(DocToolbar *bar) { _report(bar, bar->edit->combine->split_cells(bar->edit)); }
static void _run_up(DocToolbar *bar)    { _report(bar, bar->edit->combine->move_page(bar->edit, -1)); }
static void _run_down(DocToolbar *bar)  { _report(bar, bar->edit->combine->move_page(bar->edit, +1)); }

static const ToolAction s_actions[] = {
    { "save",          _run_save,        NULL,       NULL },
    { "save-as",       _run_save_as,     NULL,       NULL },
    { "close",         _run_close,       NULL,       NULL },
    { "undo",          _run_undo,        _history,   _can_undo },
    { "redo",          _run_redo,        _history,   _can_redo },
    { "copy",          _run_copy,        _copy,      _selected },
    { "cut",           _run_cut,         _cut,       _selected },
    { "paste",         _run_paste,       _paste,     NULL },
    { "paste-plain",   _run_paste_plain, _paste,     NULL },
    { "highlight",     _run_highlight,   _highlight, NULL },
    { "shade",         _run_shade,       _shade,     NULL },
    { "clear-markup",  _run_clear,       _clear,     NULL },
    { "insert-image",  _run_image,       _image,     NULL },
    { "insert-table",  _run_table,       _table,     NULL },
    { "insert-row",    _run_row,         _row,       NULL },
    { "insert-column", _run_column,      _column,    NULL },
    { "insert-page",   _run_page,        _page,      NULL },
    { "insert-note",   _run_note,        _note,      NULL },
    { "merge-file",    _run_merge,       _merge,     NULL },
    { "merge-cells",   _run_cells,       _cells,     NULL },
    { "split-cells",   _run_split,       _split,     NULL },
    { "page-up",       _run_up,          _move,      NULL },
    { "page-down",     _run_down,        _move,      NULL },
};

static const ToolAction *_find(const char *name) {
    for (size_t i = 0; i < G_N_ELEMENTS(s_actions); i++)
        if (strcmp(s_actions[i].name, name) == 0) return &s_actions[i];
    return NULL;
}

static bool _is_available(DocToolbar *bar, const ToolAction *a) {
    return bar->edit != NULL && (a->available == NULL || a->available(bar->edit));
}

static void _on_activate(GSimpleAction *action, GVariant *param, gpointer user_data) {
    DocToolbar *bar = user_data;
    (void)param;
    const ToolAction *a = _find(g_action_get_name(G_ACTION(action)));
    if (a == NULL || !_is_available(bar, a)) return;
    _begin(bar);
    a->run(bar);
}

/* enables each action from the editor's current state */
static void _refresh(DocToolbar *bar) {
    for (size_t i = 0; i < G_N_ELEMENTS(s_actions); i++) {
        const ToolAction *a = &s_actions[i];
        bool on = _is_available(bar, a) && (a->ready == NULL || a->ready(bar->edit));
        GAction *action = g_action_map_lookup_action(G_ACTION_MAP(bar->actions), a->name);
        g_simple_action_set_enabled(G_SIMPLE_ACTION(action), on);
    }
}

static void _on_edit_event(Edit *edit, const char *message, gpointer user_data) {
    DocToolbar *bar = user_data;
    (void)edit;
    if (message != NULL) {
        bar->message_sent = true;
        bar->owner->message(bar->owner_data, message);
    }
    _refresh(bar);
    bar->owner->changed(bar->owner_data);
}

/* ---------- widgets ---------- */

static GtkWidget *_group(DocToolbar *bar, bool linked) {
    GtkWidget *group = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, linked ? 0 : 4);
    gtk_widget_add_css_class(group, "tool-group");
    if (linked) gtk_widget_add_css_class(group, "linked");
    gtk_flow_box_append(GTK_FLOW_BOX(bar->root), group);
    g_ptr_array_add(bar->groups, group);
    return group;
}

/* icon and/or label; the tooltip doubles as accessible name */
static GtkWidget *_button(DocToolbar *bar, GtkWidget *parent, const char *icon,
                          const char *label, const char *tooltip, const char *action) {
    GtkWidget *button = gtk_button_new();
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    if (icon != NULL) gtk_box_append(GTK_BOX(content), gtk_image_new_from_icon_name(icon));
    if (label != NULL) gtk_box_append(GTK_BOX(content), gtk_label_new(label));
    gtk_button_set_child(GTK_BUTTON(button), content);
    gtk_widget_set_tooltip_text(button, tooltip);
    gtk_accessible_update_property(GTK_ACCESSIBLE(button), GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   tooltip, -1);

    char *detailed = g_strconcat("doc.", action, NULL);
    gtk_actionable_set_action_name(GTK_ACTIONABLE(button), detailed);
    g_free(detailed);
    g_object_set_data(G_OBJECT(button), "tool", (gpointer)_find(action));
    g_ptr_array_add(bar->items, button);
    gtk_box_append(GTK_BOX(parent), button);
    return button;
}

/* popover entries close the popover once used */
static void _popdown(GtkButton *button, gpointer user_data) {
    (void)user_data;
    GtkWidget *popover = gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_POPOVER);
    if (popover != NULL) gtk_popover_popdown(GTK_POPOVER(popover));
}

static GtkWidget *_menu_item(DocToolbar *bar, GtkWidget *menu, const char *label,
                             const char *action) {
    GtkWidget *item = _button(bar, menu, NULL, label, label, action);
    gtk_widget_add_css_class(item, "flat");
    gtk_widget_add_css_class(item, "menu-item");
    g_signal_connect_after(item, "clicked", G_CALLBACK(_popdown), NULL);
    return item;
}

/* a "Insert ▾" style button; its popover box is returned */
static GtkWidget *_menu(DocToolbar *bar, GtkWidget *group, const char *icon,
                        const char *label, const char *tooltip) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(box, "tool-menu");
    GtkWidget *popover = gtk_popover_new();
    gtk_popover_set_child(GTK_POPOVER(popover), box);

    /* icon-name and label replace each other: use a child with both */
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(content), gtk_image_new_from_icon_name(icon));
    gtk_box_append(GTK_BOX(content), gtk_label_new(label));
    GtkWidget *button = gtk_menu_button_new();
    gtk_menu_button_set_child(GTK_MENU_BUTTON(button), content);
    gtk_menu_button_set_always_show_arrow(GTK_MENU_BUTTON(button), TRUE);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(button), popover);
    gtk_widget_set_tooltip_text(button, tooltip);
    gtk_box_append(GTK_BOX(group), button);
    g_ptr_array_add(bar->groups, box);   /* its items decide if the menu shows */
    g_object_set_data(G_OBJECT(box), "shown", button);
    return box;
}

static void _build(DocToolbar *bar) {
    GtkWidget *file = _group(bar, true);
    _button(bar, file, "document-save-symbolic", "Guardar", "Guardar (Ctrl+S)", "save");
    _button(bar, file, "document-save-as-symbolic", NULL, "Guardar como… (Ctrl+Mayús+S)", "save-as");
    _button(bar, file, "window-close-symbolic", NULL, "Cerrar documento", "close");

    GtkWidget *history = _group(bar, true);
    _button(bar, history, "edit-undo-symbolic", NULL, "Deshacer (Ctrl+Z)", "undo");
    _button(bar, history, "edit-redo-symbolic", NULL, "Rehacer (Ctrl+Y)", "redo");

    GtkWidget *clip = _group(bar, true);
    _button(bar, clip, "edit-cut-symbolic", NULL, "Cortar (Ctrl+X)", "cut");
    _button(bar, clip, "edit-copy-symbolic", NULL, "Copiar (Ctrl+C)", "copy");
    _button(bar, clip, "edit-paste-symbolic", NULL, "Pegar (Ctrl+V)", "paste");
    _button(bar, clip, NULL, "Sin formato", "Pegar sin formato (Ctrl+Mayús+V)", "paste-plain");

    GtkWidget *markup = _group(bar, false);
    GtkColorDialog *dialog = gtk_color_dialog_new();
    gtk_color_dialog_set_title(dialog, "Color de resaltado");
    bar->color = gtk_color_dialog_button_new(dialog);   /* takes our dialog ref */
    GdkRGBA yellow = { 1.0, 0.92, 0.0, 1.0 };
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(bar->color), &yellow);
    gtk_widget_set_tooltip_text(bar->color, "Color para resaltar, sombrear y las herramientas PDF");
    gtk_box_append(GTK_BOX(markup), bar->color);
    GtkWidget *marks = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(marks, "linked");
    gtk_box_append(GTK_BOX(markup), marks);
    g_ptr_array_add(bar->groups, marks);
    _button(bar, marks, "format-text-highlight-symbolic", "Resaltar",
            "Resaltar la selección con el color elegido", "highlight");
    _button(bar, marks, NULL, "Sombrear",
            "Sombrear el bloque (párrafo, celda, forma o área PDF)", "shade");
    _button(bar, marks, "edit-clear-symbolic", NULL, "Quitar resaltado y sombreado", "clear-markup");

    GtkWidget *tools = _group(bar, false);
    GtkWidget *insert = _menu(bar, tools, "list-add-symbolic", "Insertar", "Insertar elementos");
    _menu_item(bar, insert, "Imagen…", "insert-image");
    GtkWidget *table = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    bar->rows = gtk_spin_button_new_with_range(1, 200, 1);
    bar->columns = gtk_spin_button_new_with_range(1, 50, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(bar->rows), 3);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(bar->columns), 3);
    gtk_widget_set_tooltip_text(bar->rows, "Filas");
    gtk_widget_set_tooltip_text(bar->columns, "Columnas");
    gtk_box_append(GTK_BOX(table), bar->rows);
    gtk_box_append(GTK_BOX(table), gtk_label_new("×"));
    gtk_box_append(GTK_BOX(table), bar->columns);
    GtkWidget *table_button = _button(bar, table, NULL, "Tabla", "Insertar tabla (filas × columnas)",
                                      "insert-table");
    g_signal_connect_after(table_button, "clicked", G_CALLBACK(_popdown), NULL);
    gtk_box_append(GTK_BOX(insert), table);
    g_object_set_data(G_OBJECT(table_button), "row", table);
    _menu_item(bar, insert, "Fila debajo", "insert-row");
    _menu_item(bar, insert, "Columna después", "insert-column");
    GtkWidget *page = _menu_item(bar, insert, "Salto de página", "insert-page");
    g_object_set_data(G_OBJECT(bar->root), "page-item", page);
    GtkWidget *note = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    bar->comment = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(bar->comment), "Texto del comentario…");
    gtk_box_append(GTK_BOX(note), bar->comment);
    GtkWidget *note_button = _button(bar, note, NULL, "Comentar",
                                     "Añadir comentario en la posición actual", "insert-note");
    g_signal_connect_after(note_button, "clicked", G_CALLBACK(_popdown), NULL);
    g_object_set_data(G_OBJECT(note_button), "row", note);
    gtk_box_append(GTK_BOX(insert), note);

    GtkWidget *combine = _menu(bar, tools, "view-dual-symbolic", "Combinar",
                               "Combinar o reorganizar contenido");
    _menu_item(bar, combine, "Insertar desde otro archivo…", "merge-file");
    _menu_item(bar, combine, "Combinar celdas", "merge-cells");
    _menu_item(bar, combine, "Separar celdas", "split-cells");
    _menu_item(bar, combine, "Mover página antes", "page-up");
    _menu_item(bar, combine, "Mover página después", "page-down");
}

/* shows only what the editor offers; a group without visible items hides */
static void _apply_visibility(DocToolbar *bar) {
    for (guint i = 0; i < bar->items->len; i++) {
        GtkWidget *item = g_ptr_array_index(bar->items, i);
        const ToolAction *a = g_object_get_data(G_OBJECT(item), "tool");
        bool visible = a != NULL && _is_available(bar, a);
        GtkWidget *row = g_object_get_data(G_OBJECT(item), "row");   /* table, comment */
        gtk_widget_set_visible(row != NULL ? row : item, visible);
    }
    if (bar->edit != NULL && bar->edit->insert != NULL && bar->edit->insert->page_label != NULL) {
        GtkWidget *page = g_object_get_data(G_OBJECT(bar->root), "page-item");
        GtkWidget *content = gtk_button_get_child(GTK_BUTTON(page));
        gtk_label_set_text(GTK_LABEL(gtk_widget_get_last_child(content)),
                           bar->edit->insert->page_label);
    }

    /* inner containers first: they were registered after their parents */
    for (guint i = bar->groups->len; i-- > 0;) {
        GtkWidget *group = g_ptr_array_index(bar->groups, i);
        bool any = false;
        for (GtkWidget *c = gtk_widget_get_first_child(group); c != NULL && !any;
             c = gtk_widget_get_next_sibling(c))
            any = gtk_widget_get_visible(c) && c != bar->color;   /* the color alone is no tool */
        GtkWidget *shown = g_object_get_data(G_OBJECT(group), "shown");
        gtk_widget_set_visible(shown != NULL ? shown : group, any);
    }
    /* the flow box wraps each group in a child: hide empty ones too */
    for (GtkWidget *c = gtk_widget_get_first_child(bar->root); c != NULL;
         c = gtk_widget_get_next_sibling(c)) {
        GtkWidget *group = gtk_flow_box_child_get_child(GTK_FLOW_BOX_CHILD(c));
        gtk_widget_set_visible(c, group != NULL && gtk_widget_get_visible(group));
    }
}

static void _on_color_changed(GObject *button, GParamSpec *pspec, gpointer user_data) {
    DocToolbar *bar = user_data;
    (void)pspec;
    if (bar->edit != NULL && bar->edit->set_color != NULL)
        bar->edit->set_color(bar->edit,
                             gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(button)));
}

void doc_toolbar_bind(DocToolbar *bar, Edit *edit) {
    if (bar->edit != NULL) bar->edit->set_listener(bar->edit, NULL, NULL);
    bar->edit = edit;
    if (edit != NULL) {
        edit->set_listener(edit, _on_edit_event, bar);
        if (edit->set_color != NULL) edit->set_color(edit, _color(bar));
    }
    _apply_visibility(bar);
    _refresh(bar);
}

static void _free(gpointer data) {
    DocToolbar *bar = data;
    if (bar->edit != NULL) bar->edit->set_listener(bar->edit, NULL, NULL);
    g_object_unref(bar->actions);
    g_ptr_array_unref(bar->items);
    g_ptr_array_unref(bar->groups);
    g_free(bar);
}

DocToolbar *doc_toolbar_new(GtkWindow *window, GCancellable *cancellable,
                            const DocToolbarOwner *owner, gpointer owner_data) {
    DocToolbar *bar = g_new0(DocToolbar, 1);
    bar->window = window;
    bar->cancellable = cancellable;
    bar->owner = owner;
    bar->owner_data = owner_data;
    bar->items = g_ptr_array_new();
    bar->groups = g_ptr_array_new();

    bar->actions = g_simple_action_group_new();
    for (size_t i = 0; i < G_N_ELEMENTS(s_actions); i++) {
        GSimpleAction *action = g_simple_action_new(s_actions[i].name, NULL);
        g_signal_connect(action, "activate", G_CALLBACK(_on_activate), bar);
        g_action_map_add_action(G_ACTION_MAP(bar->actions), G_ACTION(action));
        g_object_unref(action);
    }

    bar->root = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(bar->root), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(bar->root), FALSE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(bar->root), 12);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(bar->root), 8);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(bar->root), 4);
    gtk_widget_add_css_class(bar->root, "doc-toolbar");
    gtk_widget_insert_action_group(bar->root, "doc", G_ACTION_GROUP(bar->actions));
    g_object_set_data_full(G_OBJECT(bar->root), "doc-toolbar", bar, _free);

    _build(bar);
    g_signal_connect(bar->color, "notify::rgba", G_CALLBACK(_on_color_changed), bar);
    doc_toolbar_bind(bar, NULL);
    return bar;
}

GtkWidget *doc_toolbar_widget(DocToolbar *bar) {
    return bar->root;
}

static void _shortcut(GtkEventController *ctrl, const char *accel, const char *action) {
    GtkShortcut *shortcut = gtk_shortcut_new(gtk_shortcut_trigger_parse_string(accel),
                                             gtk_named_action_new(action));
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(ctrl), shortcut);
}

void doc_toolbar_attach(DocToolbar *bar, GtkWidget *scope) {
    gtk_widget_insert_action_group(scope, "doc", G_ACTION_GROUP(bar->actions));

    /* saving wins over whatever has the focus */
    GtkEventController *save = gtk_shortcut_controller_new();
    gtk_event_controller_set_propagation_phase(save, GTK_PHASE_CAPTURE);
    _shortcut(save, "<Control>s", "doc.save");
    _shortcut(save, "<Control><Shift>s", "doc.save-as");
    gtk_widget_add_controller(scope, save);

    /* editing keys: the text view and the office canvas handle their own
     * first; these only run when nothing inside did (e.g. a PDF page) */
    GtkEventController *edit = gtk_shortcut_controller_new();
    gtk_event_controller_set_propagation_phase(edit, GTK_PHASE_BUBBLE);
    _shortcut(edit, "<Control>c", "doc.copy");
    _shortcut(edit, "<Control>x", "doc.cut");
    _shortcut(edit, "<Control>v", "doc.paste");
    _shortcut(edit, "<Control><Shift>v", "doc.paste-plain");
    _shortcut(edit, "<Control>z", "doc.undo");
    _shortcut(edit, "<Control><Shift>z", "doc.redo");
    _shortcut(edit, "<Control>y", "doc.redo");
    gtk_widget_add_controller(scope, edit);
}
