/**
 * Developer: cristalmirror
 * Repository: https://github.com/cristalmirror/StudyStudio
 * Version: 0.1.4
 * License: GPLv3
 * Last edited: 2026-11-06
 */

/*
 * Abstract base "class" for every editor of StudyStudio.
 * Each subclass embeds Edit as its FIRST member, calls edit_init()
 * and assigns its own open/save_to/render/finalize.
 *
 * Editing tools are split into small capability contracts (EditHistory,
 * EditClipboard, ...). An editor points to the ones its backend really
 * supports and leaves the others NULL; inside a contract, a NULL function
 * means that single operation does not apply to the open format. The UI
 * only shows what is not NULL, so no editor is forced to fake an operation.
 */
#ifndef EDIT_H
#define EDIT_H
#include <stdbool.h>
#include <gtk/gtk.h>


typedef struct Edit Edit;

/* result of every editing operation: 0 or a negative code */
typedef enum {
    EDIT_OK               =  0,
    EDIT_ERR_UNSUPPORTED  = -1,   /* the backend cannot do it */
    EDIT_ERR_NO_SELECTION = -2,   /* the operation needs a selection */
    EDIT_ERR_IO           = -3,   /* reading or writing a file failed */
    EDIT_ERR_BACKEND      = -4,   /* GTK, LibreOffice, Poppler or qpdf failed */
    EDIT_ERR_INVALID      = -5,   /* bad argument or incompatible file */
} EditResult;

/* a short user-facing text for an EditResult */
const char *edit_result_message(int result);

/* 0xRRGGBB form of an opaque color, as LibreOffice and HTML use it */
guint32 edit_color_rgb(const GdkRGBA *color);

/*
 * Observer: the editor calls it when its state changes (selection,
 * undo/redo availability, modified flag...). message is NULL for a plain
 * state change, or a text worth showing to the user.
 */
typedef void (*EditListener)(Edit *edit, const char *message, gpointer user_data);

/* undo / redo, using the backend's own history */
typedef struct {
    bool (*can_undo)(Edit *self);
    bool (*can_redo)(Edit *self);
    int (*undo)(Edit *self);
    int (*redo)(Edit *self);
} EditHistory;

/* system clipboard (GdkClipboard); cut and paste are NULL for read-only content */
typedef struct {
    bool (*has_selection)(Edit *self);
    int (*copy)(Edit *self);
    int (*cut)(Edit *self);
    int (*paste)(Edit *self, bool plain_text);   /* plain_text: drop formatting */
} EditClipboard;

/*
 * Persistent colouring of the selection, stored in the document.
 * highlight: the text itself (character background / PDF highlight).
 * shade:     the block around it (paragraph, cell, shape, PDF area).
 * clear:     removes both from the selection.
 */
typedef struct {
    int (*highlight)(Edit *self, const GdkRGBA *color);
    int (*shade)(Edit *self, const GdkRGBA *color);
    int (*clear)(Edit *self);
} EditMarkup;

/* new elements at the cursor */
typedef struct {
    int (*image)(Edit *self, const char *path);
    int (*table)(Edit *self, int rows, int columns);
    int (*row)(Edit *self);                          /* below the current one */
    int (*column)(Edit *self);                       /* after the current one */
    int (*page)(Edit *self);                         /* page break / new slide */
    const char *page_label;                          /* how page() is called */
    int (*annotation)(Edit *self, const char *text);
} EditInsert;

/* operations that join or reorganise content; each one has its own meaning */
typedef struct {
    const char *const *suffixes;                     /* files merge_file accepts */
    int (*merge_file)(Edit *self, const char *path); /* bring another document in */
    int (*merge_cells)(Edit *self);
    int (*split_cells)(Edit *self);
    int (*move_page)(Edit *self, int delta);         /* -1 earlier, +1 later */
} EditCombine;

struct Edit {

    char *path; /* owned copy of the file path */
    bool modified;/* true while there are unsaved changes */
    GtkWidget *widget; /* root widget created by render (NULL until then) */

    /* capabilities: NULL when the editor or the open format lacks them */
    const EditHistory *history;
    const EditClipboard *clipboard;
    const EditMarkup *markup;
    const EditInsert *insert;
    const EditCombine *combine;

    EditListener listener;          /* set with set_listener, may be NULL */
    gpointer listener_data;

    /* Abstract: assigned by each subclass */
    int (*open)(Edit *self, const char *path);
    int (*save_to)(Edit *self, const char *path);    /* write the document to path */
    void (*render)(Edit *self, GtkWidget *container);
    void (*finalize)(Edit *self);
    void (*focus)(Edit *self);                       /* may be NULL */
    void (*set_color)(Edit *self, const GdkRGBA *color); /* tools' color, may be NULL */

    /* Common: implemented once in edit.c */
    int (*save)(Edit *self);                         /* save_to(path) */
    int (*save_as)(Edit *self, const char *path);    /* save_to(path) + adopt it */
    void (*close)(Edit *self);
    bool (*is_modified)(Edit *self);
    void (*mark_modified)(Edit *self, bool modified);
    void (*set_path)(Edit *self,const char *path);
    void (*set_widget)(Edit *self, GtkWidget *widget);
    void (*set_listener)(Edit *self, EditListener listener, gpointer user_data);
    void (*notify)(Edit *self, const char *message); /* protected: subclasses call it */

};

/* describes one Edit subclass: used by menus and by the factory */
typedef struct {
    const char *label;              /* menu entry */
    const char *filter_name;        /* name shown in the file dialog */
    const char *const *suffixes;    /* NULL: editor not implemented yet */
    Edit *(*create)(void);          /* subclass constructor */
    bool (*available)(void);        /* NULL: always available */
} EditKind;

/* every editor kind, defined in edit.c */
extern const EditKind edit_kinds[];
extern const size_t edit_kinds_count;

/* protected: only subclass constructors call it */
void edit_init(Edit *self);

/* true if path ends in ".<suffix>" for one of the NULL-terminated suffixes */
bool edit_path_has_suffix(const char *path, const char *const *suffixes);

/* factory: picks the subclass from the file extension and opens the file */
Edit *new_edit_for_path(const char *path);

#endif
