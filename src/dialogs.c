#include "dialogs.h"
#include <string.h>

/* ── Shared search state ── */

static char    search_text[512] = "";
static char    replace_text[512] = "";
static gboolean match_case = FALSE;
static gboolean search_down = TRUE;

/* Persistent dialog windows */
static GtkWindow *find_dialog = NULL;
static GtkWindow *replace_dialog = NULL;

/* Find dialog widgets */
static GtkEditable *find_entry = NULL;
static GtkCheckButton *find_case_check = NULL;
static GtkCheckButton *find_dir_down = NULL;
static GtkCheckButton *find_dir_up = NULL;

/* Replace dialog widgets */
static GtkEditable *replace_find_entry = NULL;
static GtkEditable *replace_replace_entry = NULL;
static GtkCheckButton *replace_case_check = NULL;

const char *dialogs_get_search_text(void)  { return search_text; }
gboolean    dialogs_get_match_case(void)   { return match_case; }
gboolean    dialogs_get_search_down(void)  { return search_down; }

/* ── Search engine ── */

static gboolean
do_find(NotepadWindow *win, const char *needle, gboolean case_sensitive,
        gboolean forward, gboolean wrap)
{
    GtkTextBuffer *buf = notepad_window_get_buffer(win);
    GtkTextIter sel_start, sel_end, match_start, match_end;
    gboolean found = FALSE;

    gtk_text_buffer_get_selection_bounds(buf, &sel_start, &sel_end);

    GtkTextSearchFlags flags = GTK_TEXT_SEARCH_TEXT_ONLY;
    if (!case_sensitive)
        flags |= GTK_TEXT_SEARCH_CASE_INSENSITIVE;

    if (forward) {
        found = gtk_text_iter_forward_search(&sel_end, needle, flags,
                                              &match_start, &match_end, NULL);
        if (!found && wrap) {
            GtkTextIter start;
            gtk_text_buffer_get_start_iter(buf, &start);
            found = gtk_text_iter_forward_search(&start, needle, flags,
                                                  &match_start, &match_end, NULL);
        }
    } else {
        found = gtk_text_iter_backward_search(&sel_start, needle, flags,
                                               &match_start, &match_end, NULL);
        if (!found && wrap) {
            GtkTextIter end;
            gtk_text_buffer_get_end_iter(buf, &end);
            found = gtk_text_iter_backward_search(&end, needle, flags,
                                                   &match_start, &match_end, NULL);
        }
    }

    if (found) {
        gtk_text_buffer_select_range(buf, &match_start, &match_end);
        GtkTextView *tv = notepad_window_get_text_view(win);
        gtk_text_view_scroll_mark_onscreen(tv, gtk_text_buffer_get_insert(buf));
    }

    return found;
}

/* ── Find dialog ── */

static NotepadWindow *find_dialog_owner = NULL;

static void
sync_find_state(void)
{
    if (find_entry) {
        const char *t = gtk_editable_get_text(find_entry);
        g_strlcpy(search_text, t, sizeof(search_text));
    }
    if (find_case_check)
        match_case = gtk_check_button_get_active(find_case_check);
    if (find_dir_down)
        search_down = gtk_check_button_get_active(find_dir_down);
}

static void
on_find_next_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    NotepadWindow *win = NOTEPAD_WINDOW(user_data);
    sync_find_state();
    if (search_text[0] == '\0') return;
    do_find(win, search_text, match_case, search_down, TRUE);
}

static void
on_find_dialog_close(GtkWindow *window, gpointer user_data)
{
    (void)user_data;
    (void)window;
    find_dialog = NULL;
    find_entry = NULL;
    find_case_check = NULL;
    find_dir_down = NULL;
    find_dir_up = NULL;
    find_dialog_owner = NULL;
}

void
dialogs_show_find(NotepadWindow *win)
{
    if (find_dialog) {
        gtk_window_present(find_dialog);
        return;
    }

    find_dialog_owner = win;

    GtkWindow *dlg = GTK_WINDOW(gtk_window_new());
    find_dialog = dlg;
    gtk_window_set_title(dlg, "Find");
    gtk_window_set_transient_for(dlg, GTK_WINDOW(win));
    gtk_window_set_resizable(dlg, FALSE);
    gtk_window_set_destroy_with_parent(dlg, TRUE);

    g_signal_connect(dlg, "destroy", G_CALLBACK(on_find_dialog_close), NULL);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_widget_set_margin_start(grid, 12);
    gtk_widget_set_margin_end(grid, 12);
    gtk_widget_set_margin_top(grid, 12);
    gtk_widget_set_margin_bottom(grid, 12);

    /* Row 0: label + entry */
    GtkWidget *lbl = gtk_label_new("Find what:");
    gtk_grid_attach(GTK_GRID(grid), lbl, 0, 0, 1, 1);

    GtkWidget *entry = gtk_search_entry_new();
    gtk_widget_set_hexpand(entry, TRUE);
    gtk_editable_set_text(GTK_EDITABLE(entry), search_text);
    gtk_grid_attach(GTK_GRID(grid), entry, 1, 0, 2, 1);
    find_entry = GTK_EDITABLE(entry);

    /* Row 1: match case */
    GtkWidget *case_chk = gtk_check_button_new_with_label("Match case");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(case_chk), match_case);
    gtk_grid_attach(GTK_GRID(grid), case_chk, 0, 1, 1, 1);
    find_case_check = GTK_CHECK_BUTTON(case_chk);

    /* Row 1: direction */
    GtkWidget *dir_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *dir_lbl = gtk_label_new("Direction:");
    gtk_box_append(GTK_BOX(dir_box), dir_lbl);

    GtkWidget *up_radio = gtk_check_button_new_with_label("Up");
    GtkWidget *down_radio = gtk_check_button_new_with_label("Down");
    gtk_check_button_set_group(GTK_CHECK_BUTTON(up_radio), GTK_CHECK_BUTTON(down_radio));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(down_radio), search_down);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(up_radio), !search_down);
    gtk_box_append(GTK_BOX(dir_box), up_radio);
    gtk_box_append(GTK_BOX(dir_box), down_radio);
    gtk_grid_attach(GTK_GRID(grid), dir_box, 1, 1, 2, 1);
    find_dir_down = GTK_CHECK_BUTTON(down_radio);
    find_dir_up = GTK_CHECK_BUTTON(up_radio);

    /* Row 2: buttons */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_END);

    GtkWidget *find_btn = gtk_button_new_with_label("Find Next");
    g_signal_connect(find_btn, "clicked", G_CALLBACK(on_find_next_clicked), win);
    gtk_box_append(GTK_BOX(btn_box), find_btn);

    GtkWidget *cancel_btn = gtk_button_new_with_label("Cancel");
    g_signal_connect_swapped(cancel_btn, "clicked",
                             G_CALLBACK(gtk_window_destroy), dlg);
    gtk_box_append(GTK_BOX(btn_box), cancel_btn);

    gtk_grid_attach(GTK_GRID(grid), btn_box, 0, 2, 3, 1);

    gtk_window_set_child(dlg, grid);
    gtk_window_present(dlg);
}

/* ── Replace dialog ── */

static NotepadWindow *replace_dialog_owner = NULL;

static void
sync_replace_state(void)
{
    if (replace_find_entry) {
        const char *t = gtk_editable_get_text(replace_find_entry);
        g_strlcpy(search_text, t, sizeof(search_text));
    }
    if (replace_replace_entry) {
        const char *t = gtk_editable_get_text(replace_replace_entry);
        g_strlcpy(replace_text, t, sizeof(replace_text));
    }
    if (replace_case_check)
        match_case = gtk_check_button_get_active(replace_case_check);
    search_down = TRUE;
}

static void
on_replace_find_next(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    NotepadWindow *win = NOTEPAD_WINDOW(user_data);
    sync_replace_state();
    if (search_text[0] == '\0') return;
    do_find(win, search_text, match_case, TRUE, TRUE);
}

static void
on_replace_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    NotepadWindow *win = NOTEPAD_WINDOW(user_data);
    sync_replace_state();
    if (search_text[0] == '\0') return;

    GtkTextBuffer *buf = notepad_window_get_buffer(win);
    GtkTextIter sel_start, sel_end;

    if (gtk_text_buffer_get_selection_bounds(buf, &sel_start, &sel_end)) {
        char *selected = gtk_text_buffer_get_text(buf, &sel_start, &sel_end, FALSE);
        gboolean matches;
        if (match_case)
            matches = (strcmp(selected, search_text) == 0);
        else
            matches = (g_ascii_strcasecmp(selected, search_text) == 0);
        g_free(selected);

        if (matches) {
            gtk_text_buffer_delete(buf, &sel_start, &sel_end);
            gtk_text_buffer_insert(buf, &sel_start, replace_text, -1);
        }
    }

    do_find(win, search_text, match_case, TRUE, TRUE);
}

static void
on_replace_all_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    NotepadWindow *win = NOTEPAD_WINDOW(user_data);
    sync_replace_state();
    if (search_text[0] == '\0') return;

    GtkTextBuffer *buf = notepad_window_get_buffer(win);
    GtkTextIter start;
    gtk_text_buffer_get_start_iter(buf, &start);

    GtkTextSearchFlags flags = GTK_TEXT_SEARCH_TEXT_ONLY;
    if (!match_case)
        flags |= GTK_TEXT_SEARCH_CASE_INSENSITIVE;

    GtkTextIter match_start, match_end;
    int count = 0;

    gtk_text_buffer_begin_user_action(buf);
    while (gtk_text_iter_forward_search(&start, search_text, flags,
                                         &match_start, &match_end, NULL)) {
        gtk_text_buffer_delete(buf, &match_start, &match_end);
        gtk_text_buffer_insert(buf, &match_start, replace_text, -1);
        start = match_start;
        count++;
    }
    gtk_text_buffer_end_user_action(buf);
}

static void
on_replace_dialog_close(GtkWindow *window, gpointer user_data)
{
    (void)user_data; (void)window;
    replace_dialog = NULL;
    replace_find_entry = NULL;
    replace_replace_entry = NULL;
    replace_case_check = NULL;
    replace_dialog_owner = NULL;
}

void
dialogs_show_replace(NotepadWindow *win)
{
    if (replace_dialog) {
        gtk_window_present(replace_dialog);
        return;
    }

    replace_dialog_owner = win;

    GtkWindow *dlg = GTK_WINDOW(gtk_window_new());
    replace_dialog = dlg;
    gtk_window_set_title(dlg, "Replace");
    gtk_window_set_transient_for(dlg, GTK_WINDOW(win));
    gtk_window_set_resizable(dlg, FALSE);
    gtk_window_set_destroy_with_parent(dlg, TRUE);

    g_signal_connect(dlg, "destroy", G_CALLBACK(on_replace_dialog_close), NULL);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_widget_set_margin_start(grid, 12);
    gtk_widget_set_margin_end(grid, 12);
    gtk_widget_set_margin_top(grid, 12);
    gtk_widget_set_margin_bottom(grid, 12);

    /* Row 0: Find what */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Find what:"), 0, 0, 1, 1);
    GtkWidget *find_e = gtk_entry_new();
    gtk_widget_set_hexpand(find_e, TRUE);
    gtk_editable_set_text(GTK_EDITABLE(find_e), search_text);
    gtk_grid_attach(GTK_GRID(grid), find_e, 1, 0, 1, 1);
    replace_find_entry = GTK_EDITABLE(find_e);

    /* Row 1: Replace with */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Replace with:"), 0, 1, 1, 1);
    GtkWidget *rep_e = gtk_entry_new();
    gtk_widget_set_hexpand(rep_e, TRUE);
    gtk_editable_set_text(GTK_EDITABLE(rep_e), replace_text);
    gtk_grid_attach(GTK_GRID(grid), rep_e, 1, 1, 1, 1);
    replace_replace_entry = GTK_EDITABLE(rep_e);

    /* Row 2: Match case */
    GtkWidget *case_chk = gtk_check_button_new_with_label("Match case");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(case_chk), match_case);
    gtk_grid_attach(GTK_GRID(grid), case_chk, 0, 2, 2, 1);
    replace_case_check = GTK_CHECK_BUTTON(case_chk);

    /* Row 3: Buttons */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_END);

    GtkWidget *fn_btn = gtk_button_new_with_label("Find Next");
    g_signal_connect(fn_btn, "clicked", G_CALLBACK(on_replace_find_next), win);
    gtk_box_append(GTK_BOX(btn_box), fn_btn);

    GtkWidget *rep_btn = gtk_button_new_with_label("Replace");
    g_signal_connect(rep_btn, "clicked", G_CALLBACK(on_replace_clicked), win);
    gtk_box_append(GTK_BOX(btn_box), rep_btn);

    GtkWidget *all_btn = gtk_button_new_with_label("Replace All");
    g_signal_connect(all_btn, "clicked", G_CALLBACK(on_replace_all_clicked), win);
    gtk_box_append(GTK_BOX(btn_box), all_btn);

    GtkWidget *cancel_btn = gtk_button_new_with_label("Cancel");
    g_signal_connect_swapped(cancel_btn, "clicked",
                             G_CALLBACK(gtk_window_destroy), dlg);
    gtk_box_append(GTK_BOX(btn_box), cancel_btn);

    gtk_grid_attach(GTK_GRID(grid), btn_box, 0, 3, 2, 1);

    gtk_window_set_child(dlg, grid);
    gtk_window_present(dlg);
}

/* ── Go To dialog ── */

static void
goto_response_cb(GObject *src, GAsyncResult *r, gpointer ud)
{
    NotepadWindow *w = NOTEPAD_WINDOW(ud);
    AdwAlertDialog *d = ADW_ALERT_DIALOG(src);
    const char *resp = adw_alert_dialog_choose_finish(d, r);
    if (g_strcmp0(resp, "go") != 0) return;

    GtkWidget *extra = adw_alert_dialog_get_extra_child(d);
    const char *text = gtk_editable_get_text(GTK_EDITABLE(extra));
    int line = atoi(text);
    if (line < 1) return;

    GtkTextBuffer *buf = notepad_window_get_buffer(w);
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_line(buf, &iter, line - 1);
    gtk_text_buffer_place_cursor(buf, &iter);
    gtk_text_view_scroll_mark_onscreen(
        notepad_window_get_text_view(w),
        gtk_text_buffer_get_insert(buf));
}

void
dialogs_show_goto(NotepadWindow *win)
{
    AdwAlertDialog *dlg = ADW_ALERT_DIALOG(adw_alert_dialog_new("Go To Line", NULL));
    adw_alert_dialog_add_responses(dlg,
        "cancel", "Cancel",
        "go",     "Go To",
        NULL);
    adw_alert_dialog_set_default_response(dlg, "go");
    adw_alert_dialog_set_close_response(dlg, "cancel");
    adw_alert_dialog_set_response_appearance(dlg, "go", ADW_RESPONSE_SUGGESTED);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Line number");
    gtk_entry_set_input_purpose(GTK_ENTRY(entry), GTK_INPUT_PURPOSE_DIGITS);
    adw_alert_dialog_set_extra_child(dlg, entry);

    adw_alert_dialog_choose(dlg, GTK_WIDGET(win), NULL,
                            goto_response_cb, win);
}

/* ── Find Next (F3) ── */

void
dialogs_find_next(NotepadWindow *win)
{
    if (search_text[0] == '\0') {
        dialogs_show_find(win);
        return;
    }
    do_find(win, search_text, match_case, search_down, TRUE);
}
