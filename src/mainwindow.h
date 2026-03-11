#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define NOTEPAD_TYPE_WINDOW (notepad_window_get_type())
G_DECLARE_FINAL_TYPE(NotepadWindow, notepad_window, NOTEPAD, WINDOW, AdwApplicationWindow)

NotepadWindow *notepad_window_new(GtkApplication *app);

/* Accessors for dialogs.c */
GtkTextView   *notepad_window_get_text_view(NotepadWindow *self);
GtkTextBuffer *notepad_window_get_buffer(NotepadWindow *self);
const char    *notepad_window_get_filename(NotepadWindow *self);
GFile         *notepad_window_get_file(NotepadWindow *self);

/* Actions triggered from dialogs */
void notepad_window_find_next(NotepadWindow *self);

G_END_DECLS
