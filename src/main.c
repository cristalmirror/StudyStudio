#include <gtk/gtk.h>

/*this structure save the state of application*/
typedef struct {
    GtkWidget *destiny_content; //save the element 
    int counter; //number of element(index)
} AppState;

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

/*
  function of activate
*/
static void activate(GtkApplication *app, gpointer user_data) {
    GtkWidget *window, *add_button, *target_box;
    GtkBuilder *builder;

    // Load the XML archive
    builder = gtk_builder_new_from_resource("/org/studystudio/interface.ui");

    // extract the windgets using id defined
    window = GTK_WIDGET(gtk_builder_get_object(builder,"main_window"));
    add_button= GTK_WIDGET(gtk_builder_get_object(builder,"add_button"));
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
