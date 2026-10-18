/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.3
 * License: GPLv3
 * Last edited: 2026-10-18
 */

#include <gtk/gtk.h>
#include "../include/subject.h"
#include "../include/edit.h"
#include "../include/msc/menuEditKind.h"
#include "../include/msc/subjectDialog.h"

/*this structure save the state of application*/
typedef struct {
    GtkWidget *destiny_content; //save the element
    GtkWidget *window;
    GtkWidget *footer_label; /* status messages */
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
    const EditKind *kind;          /* kind chosen in the menu */
    Edit *edit;                    /* open document, NULL if none */
} SubjectPanel;


/* Declarations: */
static void on_load_dialog_finish(GObject *source, GAsyncResult *res, gpointer user_data);

/* writes a message in the footer */
static void app_status(AppState *state, const char *msg) {
    gtk_label_set_text(GTK_LABEL(state->footer_label), msg);
}

/* the file dialog finished: create the chosen editor and open the file */
static void on_open_doc_finish(GObject *source, GAsyncResult *res, gpointer user_data) {
    SubjectPanel *panel = (SubjectPanel *)user_data;
    GError *error = NULL;

    GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), res, &error);
    if (file == NULL) {
        g_clear_error(&error);   /* cancelled */
        return;
    }
    char *path = g_file_get_path(file);
    g_object_unref(file);

    Edit *edit = panel->kind->create();
    if (edit == NULL || edit->open(edit, path) != 0) {
        if (edit != NULL) edit->close(edit);
        app_status(panel->state, "Error: cannot open the document");
    } else {
        edit->render(edit, panel->editor_box);
        panel->edit = edit;
        gtk_revealer_set_reveal_child(GTK_REVEALER(panel->revealer), TRUE);
        app_status(panel->state, path);
    }
    g_free(path);
}

/* an entry of the subject menu: ask for a file of that kind */
static void on_kind_chosen(const EditKind *kind, gpointer user_data) {
    SubjectPanel *panel = (SubjectPanel *)user_data;

    if (panel->edit != NULL) {
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
    gtk_file_dialog_open(dialog, GTK_WINDOW(panel->state->window), NULL,
                         on_open_doc_finish, panel);
    g_object_unref(filter);
    g_object_unref(dialog);
}

static void on_save_doc_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    SubjectPanel *panel = (SubjectPanel *)user_data;

    if (panel->edit == NULL) return;
    if (panel->edit->save(panel->edit) == 0) {
        app_status(panel->state, "Document saved");
    } else {
        app_status(panel->state, "Error saving the document");
    }
}

static void on_close_doc_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    SubjectPanel *panel = (SubjectPanel *)user_data;

    if (panel->edit == NULL) return;
    if (panel->edit->is_modified(panel->edit)) {
        app_status(panel->state, "Unsaved changes: save before closing");
        return;
    }
    panel->edit->close(panel->edit);   /* removes its widget and frees it */
    panel->edit = NULL;
    gtk_revealer_set_reveal_child(GTK_REVEALER(panel->revealer), FALSE);
    app_status(panel->state, "Document closed");
}

/* runs when the panel's box is finalized (e.g. the window closes) */
static void subject_panel_free(gpointer data) {
    SubjectPanel *panel = (SubjectPanel *)data;
    if (panel->edit != NULL) panel->edit->close(panel->edit);
    if (panel->subject != NULL) panel->subject->close_subject(panel->subject);
    g_free(panel);
}

/* helper: a button connected to a SubjectPanel callback */
static GtkWidget *panel_button(const char *label, GCallback cb, SubjectPanel *panel) {
    GtkWidget *button = gtk_button_new_with_label(label);
    g_signal_connect(button, "clicked", cb, panel);
    return button;
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
    int rc = (subject != NULL)
           ? subject->make_subject(subject, base_dir, name, folders)
           : -1;
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

    SubjectPanel *panel = g_new0(SubjectPanel, 1);
    panel->state = state;
    panel->number = state->counter;

    /*
     * Make the new element: a menu button WITHOUT popover. Until the
     * subject exists, clicking it opens the "Nueva materia" dialog;
     * subject_panel_set_subject() gives it the edit menu afterwards.
     */
    char *text_label = g_strdup_printf("Materia Num #%d", state->counter);
    panel->menu_button = gtk_menu_button_new();
    gtk_menu_button_set_label(GTK_MENU_BUTTON(panel->menu_button), text_label);
    gtk_menu_button_set_create_popup_func(GTK_MENU_BUTTON(panel->menu_button),
                                          on_subject_button_activated, panel, NULL);
    g_free(text_label);

    /* toolbar of the open document */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(toolbar), panel_button("💾 Guardar", G_CALLBACK(on_save_doc_clicked), panel));
    gtk_box_append(GTK_BOX(toolbar), panel_button("✖ Cerrar", G_CALLBACK(on_close_doc_clicked), panel));

    /* the Edit appends its own widget here; fixed height because we are
     * already inside a GtkScrolledWindow */
    panel->editor_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(panel->editor_box, -1, 500);

    GtkWidget *tools = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_append(GTK_BOX(tools), toolbar);
    gtk_box_append(GTK_BOX(tools), panel->editor_box);

    panel->revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(panel->revealer),
                                     GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_child(GTK_REVEALER(panel->revealer), tools);

    GtkWidget *panel_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_append(GTK_BOX(panel_box), panel->menu_button);
    gtk_box_append(GTK_BOX(panel_box), panel->revealer);
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
    gtk_file_dialog_open(dialog, GTK_WINDOW(state->window), NULL,
                         on_load_dialog_finish, state);
    g_object_unref(dialog);
}

/*load the file or folder, in a subject instance */
static void on_load_dialog_finish(GObject *source, GAsyncResult *res, gpointer user_data) {
    AppState *state = (AppState *)user_data;
    GError *error = NULL;

    GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), res, &error);
    if (file == NULL) {
        /* the user closing the dialog is not an error worth reporting */
        if (!g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)) {
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

/*
  function of activate
*/
static void activate(GtkApplication *app, gpointer user_data) {
    GtkWidget *window, *add_button, *load_button, *target_box;
    GtkBuilder *builder;
    (void)user_data;
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
    state->counter = 0;
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
    GtkApplication *app = gtk_application_new("org.ejemplo.app", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
