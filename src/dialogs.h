#pragma once

#include "mainwindow.h"

G_BEGIN_DECLS

void dialogs_show_find(NotepadWindow *win);
void dialogs_show_replace(NotepadWindow *win);
void dialogs_show_goto(NotepadWindow *win);

/* Called by Find Next action / F3 */
void dialogs_find_next(NotepadWindow *win);

/* Shared search state */
const char *dialogs_get_search_text(void);
gboolean    dialogs_get_match_case(void);
gboolean    dialogs_get_search_down(void);

G_END_DECLS
