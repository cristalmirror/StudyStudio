/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.3
 * License: GPLv3
 * Last edited: 2026-11-06
 */

#include <gtk/gtk.h>
#include <signal.h>
#include <string.h>
#include "../include/subject.h"
#include "../include/edit.h"
#include "../include/edit_subclass/editDOC.h"
#include "../include/msc/menuEditKind.h"
#include "../include/msc/subjectDialog.h"
#include "../include/msc/docToolbar.h"

/*this structure save the state of application*/
typedef struct {
    GtkWidget *destiny_content; //save the element
    GtkWidget *window;
    GtkWidget *footer_label; /* status messages */
    GtkWidget *empty_hint;   /* shown until the first subject exists */
    GCancellable *cancellable; /* cancelled when the window closes */
    GPtrArray *panels;       /* every SubjectPanel (not owned) */
    bool closing;            /* the user already confirmed closing */
    int counter; //number of element(index)
} AppState;

/* one per "Materia Num" button: its menu and its editing area */
typedef struct {
    AppState *state;
    int number;                    /* the #x of "Materia Num #x" */
    Subject *subject;              /* NULL until the subject is created */
    GtkWidget *menu_button;        /* the subject button, opens the menu */
    GtkWidget *revealer;           /* shows / hides the editing area */
    GtkWidget *editor_box;         /* where the open Edit renders itself */
    GtkWidget *doc_name;           /* status line: file name */
    GtkWidget *doc_state;          /* status line: saved / unsaved */
    GtkWidget *spinner;            /* while a document opens */
    DocToolbar *toolbar;           /* tools of the open document */
    const EditKind *kind;          /* kind chosen in the menu */
    Edit *edit;                    /* open document, NULL if none */
    char *opening;                 /* path being opened, NULL if none */
    guint open_source;             /* idle that opens it */
} SubjectPanel;


/* Declarations: */
static void on_load_dialog_finish(GObject *source, GAsyncResult *res, gpointer user_data);

/* writes a message in the footer */
static void app_status(AppState *state, const char *msg) {
    gtk_label_set_text(GTK_LABEL(state->footer_label), msg);
}

/* footer message about the panel's document: "<name>: <text>" */
static void panel_status(SubjectPanel *panel, const char *text) {
    char *name = (panel->edit != NULL && panel->edit->path != NULL)
        ? g_path_get_basename(panel->edit->path) : NULL;
    char *msg = name != NULL ? g_strdup_printf("%s: %s", name, text) : g_strdup(text);
    app_status(panel->state, msg);
    g_free(msg);
    g_free(name);
}

/* ---------- document status line ---------- */

static void panel_update_status(SubjectPanel *panel) {
    if (panel->edit == NULL) {
        gtk_label_set_text(GTK_LABEL(panel->doc_name), "");
        gtk_label_set_text(GTK_LABEL(panel->doc_state), "");
        return;
    }
    char *name = g_path_get_basename(panel->edit->path);
    gtk_label_set_text(GTK_LABEL(panel->doc_name), name);
    gtk_widget_set_tooltip_text(panel->doc_name, panel->edit->path);
    g_free(name);

    bool modified = panel->edit->is_modified(panel->edit);
    gtk_label_set_text(GTK_LABEL(panel->doc_state), modified ? "● Sin guardar" : "✓ Guardado");
    gtk_widget_remove_css_class(panel->doc_state, modified ? "status-saved" : "status-modified");
    gtk_widget_add_css_class(panel->doc_state, modified ? "status-modified" : "status-saved");
}

/* ---------- open ---------- */

static gboolean panel_open_idle(gpointer user_data) {
    SubjectPanel *panel = (SubjectPanel *)user_data;
    char *path = panel->opening;
    panel->opening = NULL;
    panel->open_source = 0;

    Edit *edit = panel->kind->create();
    int rc = (edit != NULL) ? edit->open(edit, path) : EDIT_ERR_BACKEND;
    gtk_spinner_stop(GTK_SPINNER(panel->spinner));
    gtk_widget_set_visible(panel->spinner, FALSE);

    if (rc != EDIT_OK) {
        if (edit != NULL) edit->close(edit);
        char *name = g_path_get_basename(path);
        char *msg = g_strdup_printf("Cannot open %s: %s", name, edit_result_message(rc));
        app_status(panel->state, msg);
        g_free(msg);
        g_free(name);
    } else {
        panel->edit = edit;
        edit->render(edit, panel->editor_box);
        doc_toolbar_bind(panel->toolbar, edit);
        gtk_revealer_set_reveal_child(GTK_REVEALER(panel->revealer), TRUE);
        panel_update_status(panel);
        if (edit->focus != NULL) edit->focus(edit);
        panel_status(panel, "opened");
    }
    g_free(path);
    return G_SOURCE_REMOVE;
}

/* the file dialog finished: open the file once the status is painted */
static void on_open_doc_finish(GObject *source, GAsyncResult *res, gpointer user_data) {
    SubjectPanel *panel = (SubjectPanel *)user_data;
    GError *error = NULL;

    GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), res, &error);
    if (file == NULL) {
        g_clear_error(&error);   /* cancelled */
        return;
    }
    if (panel->edit != NULL || panel->opening != NULL) {
        g_object_unref(file);
        return;
    }
    panel->opening = g_file_get_path(file);
    g_object_unref(file);

    /* opening blocks (the first office file starts LibreOffice): show it
     * first; a low priority idle runs after GTK has redrawn */
    gtk_widget_set_visible(panel->spinner, TRUE);
    gtk_spinner_start(GTK_SPINNER(panel->spinner));
    app_status(panel->state, "Opening the document…");
    panel->open_source = g_idle_add_full(G_PRIORITY_LOW, panel_open_idle, panel, NULL);
}

/* an entry of the subject menu: ask for a file of that kind */
static void on_kind_chosen(const EditKind *kind, gpointer user_data) {
    SubjectPanel *panel = (SubjectPanel *)user_data;

    if (panel->edit != NULL || panel->opening != NULL) {
        app_status(panel->state, "Close the current document first");
        return;
    }
    if (kind->available != NULL && !kind->available()) {
        app_status(panel->state, "Error: LibreOffice not found (set STUDYSTUDIO_LO_PATH)");
        return;
    }
    panel->kind = kind;

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, kind->filter_name);
    for (int i = 0; kind->suffixes[i] != NULL; i++) {
        gtk_file_filter_add_suffix(filter, kind->suffixes[i]);
    }

    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Abrir documento");
    gtk_file_dialog_set_default_filter(dialog, filter);

    /* start in the subject's folder of this kind (made by make_subject) */
    char *kind_dir = g_build_filename(panel->subject->dir, kind->filter_name, NULL);
    if (g_file_test(kind_dir, G_FILE_TEST_IS_DIR)) {
        GFile *initial = g_file_new_for_path(kind_dir);
        gtk_file_dialog_set_initial_folder(dialog, initial);
        g_object_unref(initial);
    }
    g_free(kind_dir);
    gtk_file_dialog_open(dialog, GTK_WINDOW(panel->state->window),
                         panel->state->cancellable, on_open_doc_finish, panel);
    g_object_unref(filter);
    g_object_unref(dialog);
}

/* ---------- save, save as, close ---------- */

static bool panel_save(SubjectPanel *panel) {
    if (panel->edit == NULL) return true;
    int rc = panel->edit->save(panel->edit);
    panel_status(panel, rc == EDIT_OK ? "saved" : edit_result_message(rc));
    panel_update_status(panel);
    return rc == EDIT_OK;
}

static void on_toolbar_save(gpointer owner) {
    panel_save((SubjectPanel *)owner);
}

static void on_save_as_finish(GObject *source, GAsyncResult *res, gpointer user_data) {
    SubjectPanel *panel = (SubjectPanel *)user_data;
    GFile *file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), res, NULL);
    if (file == NULL) return;   /* cancelled */
    char *path = g_file_get_path(file);
    g_object_unref(file);
    if (path == NULL || panel->edit == NULL) {
        g_free(path);
        return;
    }

    /* the editor writes by extension: keep one of its kind */
    if (!edit_path_has_suffix(path, panel->kind->suffixes)) {
        const char *ext = strrchr(panel->edit->path, '.');
        char *fixed = g_strconcat(path, ext != NULL ? ext : "", NULL);
        g_free(path);
        path = fixed;
    }
    int rc = panel->edit->save_as(panel->edit, path);
    panel_status(panel, rc == EDIT_OK ? "saved" : edit_result_message(rc));
    panel_update_status(panel);
    g_free(path);
}

static void on_toolbar_save_as(gpointer owner) {
    SubjectPanel *panel = (SubjectPanel *)owner;
    if (panel->edit == NULL) return;

    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Guardar como");
    char *name = g_path_get_basename(panel->edit->path);
    gtk_file_dialog_set_initial_name(dialog, name);
    g_free(name);
    char *dir = g_path_get_dirname(panel->edit->path);
    GFile *folder = g_file_new_for_path(dir);
    gtk_file_dialog_set_initial_folder(dialog, folder);
    g_object_unref(folder);
    g_free(dir);
    gtk_file_dialog_save(dialog, GTK_WINDOW(panel->state->window),
                         panel->state->cancellable, on_save_as_finish, panel);
    g_object_unref(dialog);
}

static void panel_close_document(SubjectPanel *panel) {
    if (panel->edit == NULL) return;
    doc_toolbar_bind(panel->toolbar, NULL);
    panel->edit->close(panel->edit);   /* removes its widget and frees it */
    panel->edit = NULL;
    gtk_revealer_set_reveal_child(GTK_REVEALER(panel->revealer), FALSE);
    panel_update_status(panel);
    app_status(panel->state, "Document closed");
}

/* buttons of the unsaved-changes question */
enum { CHOICE_CANCEL, CHOICE_DISCARD, CHOICE_SAVE };

static void on_close_choice(GObject *source, GAsyncResult *res, gpointer user_data) {
    SubjectPanel *panel = (SubjectPanel *)user_data;
    int choice = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), res, NULL);
    if (panel->edit == NULL) return;
    if (choice == CHOICE_SAVE && !panel_save(panel)) return;   /* keep it open */
    if (choice == CHOICE_SAVE || choice == CHOICE_DISCARD) panel_close_document(panel);
}

static GtkAlertDialog *unsaved_dialog(const char *message, const char *discard, const char *save) {
    GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", message);
    const char *buttons[] = { "Cancelar", discard, save, NULL };
    gtk_alert_dialog_set_detail(dialog, "Los cambios que no guardes se perderán.");
    gtk_alert_dialog_set_buttons(dialog, buttons);
    gtk_alert_dialog_set_cancel_button(dialog, CHOICE_CANCEL);
    gtk_alert_dialog_set_default_button(dialog, CHOICE_SAVE);
    return dialog;
}

static void on_toolbar_close(gpointer owner) {
    SubjectPanel *panel = (SubjectPanel *)owner;
    if (panel->edit == NULL) return;
    if (!panel->edit->is_modified(panel->edit)) {
        panel_close_document(panel);
        return;
    }
    char *name = g_path_get_basename(panel->edit->path);
    char *question = g_strdup_printf("¿Guardar los cambios de «%s»?", name);
    GtkAlertDialog *dialog = unsaved_dialog(question, "Descartar", "Guardar");
    gtk_alert_dialog_choose(dialog, GTK_WINDOW(panel->state->window),
                            panel->state->cancellable, on_close_choice, panel);
    g_object_unref(dialog);
    g_free(question);
    g_free(name);
}

static void on_toolbar_changed(gpointer owner) {
    panel_update_status((SubjectPanel *)owner);
}

static void on_toolbar_message(gpointer owner, const char *text) {
    panel_status((SubjectPanel *)owner, text);
}

static const DocToolbarOwner s_toolbar_owner = {
    on_toolbar_save, on_toolbar_save_as, on_toolbar_close,
    on_toolbar_changed, on_toolbar_message,
};

/* runs when the panel's box is finalized (e.g. the window closes) */
static void subject_panel_free(gpointer data) {
    SubjectPanel *panel = (SubjectPanel *)data;
    if (panel->open_source != 0) g_source_remove(panel->open_source);
    g_free(panel->opening);
    if (panel->edit != NULL) panel->edit->close(panel->edit);
    if (panel->subject != NULL) panel->subject->close_subject(panel->subject);
    g_ptr_array_remove(panel->state->panels, panel);
    g_free(panel);
}

/*
 * The subject exists (created or, later, loaded): the button takes its
 * name and from now on unfolds the edit menu with the kinds it uses
 * (kinds NULL: every kind).
 */
static void subject_panel_set_subject(SubjectPanel *panel, Subject *subject, const bool *kinds) {
    GtkMenuButton *button = GTK_MENU_BUTTON(panel->menu_button);

    panel->subject = subject;
    gtk_menu_button_set_label(button, subject->name);
    gtk_widget_set_tooltip_text(panel->menu_button, "Abrir un documento de esta materia");
    gtk_menu_button_set_create_popup_func(button, NULL, NULL, NULL);   /* no more dialog */
    Menu *menu = new_menu_edit_kind(on_kind_chosen, panel, kinds);
    gtk_menu_button_set_popover(button, menu->popover);
}

/* "Crear" was pressed in the dialog: make the folders on disk */
static bool on_subject_dialog_done(const char *name, const char *base_dir,
                                   const bool *chosen, gpointer user_data) {
    SubjectPanel *panel = (SubjectPanel *)user_data;

    /* one subfolder per chosen kind, NULL-terminated like the suffix lists */
    const char **folders = g_new0(const char *, edit_kinds_count + 1);
    size_t n = 0;
    for (size_t i = 0; i < edit_kinds_count; i++) {
        if (chosen[i]) folders[n++] = edit_kinds[i].filter_name;
    }

    Subject *subject = new_subject(panel->number);
    int rc = (subject != NULL) ? subject->make_subject(subject, base_dir, name, folders) : -1;
    g_free(folders);

    if (rc != 0) {
        /* make_subject already wrote the reason in the footer */
        if (subject != NULL) subject->close_subject(subject);
        return false;   /* keep the dialog open */
    }
    subject_panel_set_subject(panel, subject, chosen);
    return true;
}

/*
 * The subject button was pressed and has no popover yet (subject not
 * created): GTK asks us for one, we open the dialog instead.
 */
static void on_subject_button_activated(GtkMenuButton *button, gpointer user_data) {
    SubjectPanel *panel = (SubjectPanel *)user_data;

    /* without a popover the button would stay pressed: release it */
    gtk_menu_button_set_active(button, FALSE);
    if (panel->subject != NULL) return;
    new_subject_dialog(GTK_WINDOW(panel->state->window), on_subject_dialog_done, panel);
}

/*
  this function is a callback that is executed
  when you press 'Add' button.
*/
static void on_add_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    // search our state in usaer_data
    AppState *state = (AppState *)user_data;
    state->counter++;
    gtk_widget_set_visible(state->empty_hint, FALSE);

    SubjectPanel *panel = g_new0(SubjectPanel, 1);
    panel->state = state;
    panel->number = state->counter;
    g_ptr_array_add(state->panels, panel);

    /*
     * Make the new element: a menu button WITHOUT popover. Until the
     * subject exists, clicking it opens the "Nueva materia" dialog;
     * subject_panel_set_subject() gives it the edit menu afterwards.
     */
    char *text_label = g_strdup_printf("Materia Num #%d", state->counter);
    panel->menu_button = gtk_menu_button_new();
    gtk_menu_button_set_label(GTK_MENU_BUTTON(panel->menu_button), text_label);
    gtk_widget_set_tooltip_text(panel->menu_button, "Configurar la materia");
    gtk_widget_set_halign(panel->menu_button, GTK_ALIGN_START);
    gtk_menu_button_set_create_popup_func(GTK_MENU_BUTTON(panel->menu_button),
                                          on_subject_button_activated, panel, NULL);
    g_free(text_label);

    /* tools of the open document: shown according to what it supports */
    panel->toolbar = doc_toolbar_new(GTK_WINDOW(state->window), state->cancellable,
                                     &s_toolbar_owner, panel);

    /* the Edit appends its own widget here; fixed height because we are
     * already inside a GtkScrolledWindow */
    panel->editor_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(panel->editor_box, -1, 560);

    /* status line: document name, saved state, loading spinner */
    GtkWidget *status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(status, "doc-status");
    panel->doc_name = gtk_label_new(NULL);
    gtk_widget_add_css_class(panel->doc_name, "doc-name");
    gtk_label_set_ellipsize(GTK_LABEL(panel->doc_name), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_xalign(GTK_LABEL(panel->doc_name), 0);
    gtk_widget_set_hexpand(panel->doc_name, TRUE);
    panel->doc_state = gtk_label_new(NULL);
    gtk_box_append(GTK_BOX(status), panel->doc_name);
    gtk_box_append(GTK_BOX(status), panel->doc_state);

    GtkWidget *tools = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append(GTK_BOX(tools), doc_toolbar_widget(panel->toolbar));
    gtk_box_append(GTK_BOX(tools), panel->editor_box);
    gtk_box_append(GTK_BOX(tools), status);

    panel->revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(panel->revealer),
                                     GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_child(GTK_REVEALER(panel->revealer), tools);

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    panel->spinner = gtk_spinner_new();
    gtk_widget_set_visible(panel->spinner, FALSE);
    gtk_box_append(GTK_BOX(header), panel->menu_button);
    gtk_box_append(GTK_BOX(header), panel->spinner);

    GtkWidget *panel_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(panel_box, "subject-card");
    gtk_box_append(GTK_BOX(panel_box), header);
    gtk_box_append(GTK_BOX(panel_box), panel->revealer);
    /* Ctrl+S, Ctrl+C... act on this panel's document */
    doc_toolbar_attach(panel->toolbar, panel_box);
    /* the panel lives as long as its box: GTK frees it for us */
    g_object_set_data_full(G_OBJECT(panel_box), "panel", panel, subject_panel_free);

    // add the element to the destiny box
    gtk_box_append(GTK_BOX(state->destiny_content), panel_box);
}

/*load archive of subject*/

static void on_load_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppState *state = (AppState *)user_data;

    /*
     * GtkFileDialog is asynchronous: gtk_file_dialog_open() returns at once
     * and GTK calls on_load_dialog_finish() when the user picks or cancels.
     * The pending operation keeps its own reference to the dialog, so we
     * can release ours right away.
     */
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Cargar Materia");
    gtk_file_dialog_open(dialog, GTK_WINDOW(state->window), state->cancellable,
                         on_load_dialog_finish, state);
    g_object_unref(dialog);
}

/*load the file or folder, in a subject instance */
static void on_load_dialog_finish(GObject *source, GAsyncResult *res, gpointer user_data) {
    AppState *state = (AppState *)user_data;
    GError *error = NULL;

    GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), res, &error);
    if (file == NULL) {
        /* the user closing the dialog, or the window closing (cancelled),
         * is not an error worth reporting */
        if (!g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED) &&
            !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            g_print("Error al abrir el archivo: %s\n", error->message);
        }
        g_clear_error(&error);
        return;
    }

    /* file manipulations */
    char *path = g_file_get_path(file);

    Subject *mat = new_subject(state->counter);
    if (mat != NULL) {
        uint8_t *buf = NULL;
        size_t size = 0;
        int rc = mat->load_subject(mat,path,&buf,&size);

        if (rc == 0) {
            g_print("Cargados %zu bytes desde %s\n",size,path);
            free(buf);
        } else {
            g_print("Error al cargar (%d): %s\n",rc,path);
        }
        mat->close_subject(mat);
    }
    g_free(path);
    g_object_unref(file);
}
/* callback that subject.c calls with each message */
static void on_subject_log(const char *msg, void *user_data) {
    GtkLabel *label = GTK_LABEL(user_data);
    char *clean = g_strchomp(g_strdup(msg));   /* strip trailing \n */
    gtk_label_set_text(label, clean);
    g_free(clean);
}

/* ---------- closing the window with unsaved documents ---------- */

static void on_quit_choice(GObject *source, GAsyncResult *res, gpointer user_data) {
    AppState *state = (AppState *)user_data;
    int choice = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), res, NULL);
    if (choice != CHOICE_SAVE && choice != CHOICE_DISCARD) return;

    if (choice == CHOICE_SAVE) {
        for (guint i = 0; i < state->panels->len; i++) {
            SubjectPanel *panel = g_ptr_array_index(state->panels, i);
            if (panel->edit != NULL && panel->edit->is_modified(panel->edit) &&
                !panel_save(panel))
                return;   /* panel_save explained the error: stay open */
        }
    }
    state->closing = true;
    gtk_window_close(GTK_WINDOW(state->window));
}

static gboolean on_close_request(GtkWindow *window, gpointer user_data) {
    AppState *state = (AppState *)user_data;
    if (state->closing) return FALSE;

    int unsaved = 0;
    for (guint i = 0; i < state->panels->len; i++) {
        SubjectPanel *panel = g_ptr_array_index(state->panels, i);
        if (panel->edit != NULL && panel->edit->is_modified(panel->edit)) unsaved++;
    }
    if (unsaved == 0) return FALSE;   /* let it close */

    char *question = g_strdup_printf(unsaved == 1 ? "Hay %d documento sin guardar"
                                                  : "Hay %d documentos sin guardar", unsaved);
    GtkAlertDialog *dialog = unsaved_dialog(question, "Descartar y salir", "Guardar y salir");
    gtk_alert_dialog_choose(dialog, window, state->cancellable, on_quit_choice, state);
    g_object_unref(dialog);
    g_free(question);
    return TRUE;   /* wait for the answer */
}

/*
 * The window starts being destroyed (its children still exist):
 * nothing may reach it from now on.
 */
static void on_window_destroy(GtkWidget *window, gpointer user_data) {
    (void)window;
    AppState *state = (AppState *)user_data;
    subject_set_logger(NULL, NULL);            /* footer_label is about to die */
    g_cancellable_cancel(state->cancellable);  /* pending file dialogs end with an error */
}

/* runs when the window is finalized: after every panel was freed */
static void app_state_free(gpointer data) {
    AppState *state = (AppState *)data;
    g_object_unref(state->cancellable);
    g_ptr_array_unref(state->panels);
    g_free(state);
}

/* styles of every StudyStudio widget, embedded in the GResource */
static void load_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(provider, "/org/studystudio/style.css");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                               GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);   /* the display keeps its own reference */
}

/*
  function of activate
*/
static void activate(GtkApplication *app, gpointer user_data) {
    GtkWidget *window, *add_button, *load_button, *target_box;
    GtkBuilder *builder;
    (void)user_data;
    load_css();
    // Load the XML archive
    builder = gtk_builder_new_from_resource("/org/studystudio/interface.ui");

    // extract the widgets using id defined
    window = GTK_WIDGET(gtk_builder_get_object(builder,"main_window"));
    add_button = GTK_WIDGET(gtk_builder_get_object(builder,"add_button"));
    load_button =GTK_WIDGET(gtk_builder_get_object(builder,"load_button"));
    target_box = GTK_WIDGET(gtk_builder_get_object(builder,"target_box"));

    /*
      When you make a windows in GTK4 from XML, need associate by hand
    */
    gtk_window_set_application(GTK_WINDOW(window), app);

    // to train the state and connect the signals
    AppState *state = g_new0(AppState, 1);   /* zeroed: no garbage fields */
    state->window = window;
    state->destiny_content = target_box;
    state->empty_hint = GTK_WIDGET(gtk_builder_get_object(builder, "empty_hint"));
    state->counter = 0;
    state->cancellable = g_cancellable_new();
    state->panels = g_ptr_array_new();
    /* the state lives as long as the window: GTK frees it for us */
    g_object_set_data_full(G_OBJECT(window), "app-state", state, app_state_free);
    g_signal_connect(window, "close-request", G_CALLBACK(on_close_request), state);
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), state);
    g_signal_connect(add_button, "clicked",G_CALLBACK(on_add_clicked), state);
    g_signal_connect(load_button,"clicked",G_CALLBACK(on_load_clicked), state);
    // print and clean
    gtk_window_present(GTK_WINDOW(window));


    /* footer implementations */
    GtkWidget *footer_label = GTK_WIDGET(gtk_builder_get_object(builder, "footer_label"));
    state->footer_label = footer_label;
    subject_set_logger(on_subject_log, footer_label);
    g_object_unref(builder);
}

int main(int argc, char *argv[]) {
#ifndef _WIN32
    /*
     * Writing to a pipe whose reader died (tar not installed, tar rejected
     * the data) sends SIGPIPE, which kills the whole app by default.
     * Ignored, write() fails with EPIPE and subject.c reports the error.
     */
    signal(SIGPIPE, SIG_IGN);
#endif
    GtkApplication *app = gtk_application_new("org.ejemplo.app", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    /*
     * LibreOffice's static destructors crash inside exit() (SIGSEGV in
     * desktop_LOKClipboard). Everything of ours is already saved and freed
     * here, so if LibreOffice was started, leave without running them.
     */
    if (edit_doc_started()) {
        fflush(NULL);
        _Exit(status);   /* C99, <stdlib.h>: skips atexit and static destructors */
    }
    return status;
}
