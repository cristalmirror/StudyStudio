/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.0.12
 * License: GPLv3
 * Last edited: 2026-09-22
 */

#include <gtk/gtk.h>
#include "../include/subject.h"

/*this structure save the state of application*/
typedef struct {
    GtkWidget *destiny_content; //save the element
    GtkWidget *window; 
    int counter; //number of element(index)
} AppState;


/* Declarations: */
static void on_load_dialog_respose(GtkNativeDialog *dialog, int response, gpointer user_data);

/*
  function that execute when you press everywere the buttons
*/
static void on_subject_clicked(GtkButton *button, gpointer user_data) {
    (void)user_data;

    /*temporal text*/
    const char *text = gtk_button_get_label(button);
    g_print("Presionaste: %s\n", text);
}

/*
  this function is a callback that is executad
  when you press 'Add' button.
*/
static void on_add_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    // search our state in usaer_data
    AppState *state = (AppState *)user_data;
    state->counter++;

    // Make the new element (in this case, a new label)
    char *text_label = g_strdup_printf("Materia Num #%d", state->counter);
    GtkWidget *new_subject = gtk_button_new_with_label(text_label);
    g_signal_connect(new_subject,"clicked",G_CALLBACK(on_subject_clicked),NULL);
    g_free(text_label);

    // add the element to the destiny box
    gtk_box_append(GTK_BOX(state->destiny_content), new_subject);
}

/*load archive of subject*/

static void on_load_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppState *state = (AppState *)user_data;

    GtkFileChooserNative *dialog = gtk_file_chooser_native_new(
        "Cargar Materia",
        GTK_WINDOW(state->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Abrir", "_Cancelar"
    );

    g_signal_connect(dialog,"response",G_CALLBACK(on_load_dialog_respose), state);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}

/*load the file or folder, in a subject instance */
static void on_load_dialog_respose(GtkNativeDialog *dialog, int respose, gpointer user_data) {
    AppState *state = (AppState *)user_data;

    /* file manipulations */
    if (respose == GTK_RESPONSE_ACCEPT) {
        GFile *file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
        char *path = g_file_get_path(file);
    

        Subject *mat = new_subject(state->counter);
        if (mat != NULL) {
            uint8_t *buf = NULL;
            size_t size = 0;
            int rc = mat->load_subject(mat,path,&buf,&size);

            if (rc == 0) {
                g_print("Cargados %zu bytes desde %s",size,path);
                free(buf); 
            } else {
                g_print("Error al cargar (%d): %s\n",rc,path);
            }
            mat->close_subject(mat);
    
        }
        g_free(path);
        g_object_unref(file);
    }
    g_object_unref(dialog);
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

    // extract the windgets using id defined
    window = GTK_WIDGET(gtk_builder_get_object(builder,"main_window"));
    add_button = GTK_WIDGET(gtk_builder_get_object(builder,"add_button"));
    load_button =GTK_WIDGET(gtk_builder_get_object(builder,"load_button"));
    target_box = GTK_WIDGET(gtk_builder_get_object(builder,"target_box"));

    /*
      When you make a windows in GTK4 from XML, need asociate by hand
    */
    gtk_window_set_application(GTK_WINDOW(window), app);

    // to train the state and connect the signals
    AppState *state = g_malloc(sizeof(AppState));
    state->destiny_content = target_box;
    state->counter = 0;
    g_signal_connect(add_button, "clicked",G_CALLBACK(on_add_clicked), state);
    g_signal_connect(load_button,"clicked",G_CALLBACK(on_load_clicked), state);
    // print and clean
    gtk_window_present(GTK_WINDOW(window));
    g_object_unref(builder);
}

int main(int argc, char *argv[]) {
    GtkApplication *app = gtk_application_new("org.ejemplo.app", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
