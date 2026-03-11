#include "mainwindow.h"
#include "dialogs.h"
#include <time.h>

struct _NotepadWindow {
    AdwApplicationWindow parent_instance;

    GtkTextView   *text_view;
    GtkTextBuffer *buffer;
    GtkLabel      *status_bar;
    GtkWidget     *status_box;
    GtkScrolledWindow *scrolled;

    GFile *file;
    char  *filename;       /* display name */
    gboolean word_wrap;

    PangoFontDescription *font_desc;
    GtkTextTag *font_tag;
};

G_DEFINE_FINAL_TYPE(NotepadWindow, notepad_window, ADW_TYPE_APPLICATION_WINDOW)

/* ── Accessors ── */

GtkTextView *notepad_window_get_text_view(NotepadWindow *self)
{ return self->text_view; }

GtkTextBuffer *notepad_window_get_buffer(NotepadWindow *self)
{ return self->buffer; }

const char *notepad_window_get_filename(NotepadWindow *self)
{ return self->filename; }

GFile *notepad_window_get_file(NotepadWindow *self)
{ return self->file; }

/* ── Helpers ── */

static void update_title(NotepadWindow *self)
{
    const char *name = self->filename ? self->filename : "Untitled";
    gboolean modified = gtk_text_buffer_get_modified(self->buffer);
    char *title;
    if (modified)
        title = g_strdup_printf("*%s - Notepad", name);
    else
        title = g_strdup_printf("%s - Notepad", name);
    gtk_window_set_title(GTK_WINDOW(self), title);
    g_free(title);
}

static void update_status_bar(NotepadWindow *self)
{
    GtkTextIter iter;
    GtkTextMark *mark = gtk_text_buffer_get_insert(self->buffer);
    gtk_text_buffer_get_iter_at_mark(self->buffer, &iter, mark);

    int line = gtk_text_iter_get_line(&iter) + 1;
    int col  = gtk_text_iter_get_line_offset(&iter) + 1;

    char *text = g_strdup_printf("Ln %d, Col %d", line, col);
    gtk_label_set_text(self->status_bar, text);
    g_free(text);
}

static void set_file(NotepadWindow *self, GFile *file)
{
    g_clear_object(&self->file);
    g_free(self->filename);

    if (file) {
        self->file = g_object_ref(file);
        self->filename = g_file_get_basename(file);
    } else {
        self->file = NULL;
        self->filename = NULL;
    }
}

static void load_file_contents(NotepadWindow *self, GFile *file)
{
    char *contents = NULL;
    gsize length = 0;
    GError *err = NULL;

    if (!g_file_load_contents(file, NULL, &contents, &length, NULL, &err)) {
        g_warning("Could not open file: %s", err->message);
        g_error_free(err);
        return;
    }

    /* Validate UTF-8 */
    if (!g_utf8_validate(contents, (gssize)length, NULL)) {
        g_warning("File is not valid UTF-8");
        g_free(contents);
        return;
    }

    gtk_text_buffer_set_text(self->buffer, contents, (int)length);
    gtk_text_buffer_set_modified(self->buffer, FALSE);
    set_file(self, file);
    update_title(self);

    /* Place cursor at start */
    GtkTextIter start;
    gtk_text_buffer_get_start_iter(self->buffer, &start);
    gtk_text_buffer_place_cursor(self->buffer, &start);

    /* Limit undo history to 1 */
    gtk_text_buffer_set_enable_undo(self->buffer, FALSE);
    gtk_text_buffer_set_enable_undo(self->buffer, TRUE);

    g_free(contents);
}

static void save_to_file(NotepadWindow *self, GFile *file)
{
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(self->buffer, &start, &end);
    char *text = gtk_text_buffer_get_text(self->buffer, &start, &end, FALSE);

    GError *err = NULL;
    if (!g_file_replace_contents(file, text, strlen(text), NULL, FALSE,
                                  G_FILE_CREATE_NONE, NULL, NULL, &err)) {
        g_warning("Could not save file: %s", err->message);
        g_error_free(err);
        g_free(text);
        return;
    }

    g_free(text);
    set_file(self, file);
    gtk_text_buffer_set_modified(self->buffer, FALSE);
    update_title(self);

    /* Reset undo after save to keep single-level behavior */
    gtk_text_buffer_set_enable_undo(self->buffer, FALSE);
    gtk_text_buffer_set_enable_undo(self->buffer, TRUE);
}

/* ── Unsaved-changes prompt ── */

typedef void (*AfterSaveCheck)(NotepadWindow *self);

typedef struct {
    NotepadWindow *win;
    AfterSaveCheck callback;
} SaveCheckData;

static void do_save_then(NotepadWindow *self, AfterSaveCheck callback);

static void
save_check_response_cb(GObject *src, GAsyncResult *r, gpointer ud)
{
    SaveCheckData *d = ud;
    const char *resp = adw_alert_dialog_choose_finish(ADW_ALERT_DIALOG(src), r);
    if (g_strcmp0(resp, "save") == 0) {
        do_save_then(d->win, d->callback);
    } else if (g_strcmp0(resp, "discard") == 0) {
        if (d->callback) d->callback(d->win);
    }
    /* "cancel" → do nothing */
    g_free(d);
}

static void
prompt_save_changes(NotepadWindow *self, AfterSaveCheck callback)
{
    if (!gtk_text_buffer_get_modified(self->buffer)) {
        if (callback) callback(self);
        return;
    }

    const char *name = self->filename ? self->filename : "Untitled";
    char *msg = g_strdup_printf("Save changes to %s before closing?", name);

    AdwAlertDialog *dlg = ADW_ALERT_DIALOG(adw_alert_dialog_new(msg, NULL));
    g_free(msg);

    adw_alert_dialog_add_responses(dlg,
        "cancel",  "Cancel",
        "discard", "Discard",
        "save",    "Save",
        NULL);
    adw_alert_dialog_set_response_appearance(dlg, "discard", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_response_appearance(dlg, "save", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(dlg, "save");
    adw_alert_dialog_set_close_response(dlg, "cancel");

    SaveCheckData *data = g_new(SaveCheckData, 1);
    data->win = self;
    data->callback = callback;

    adw_alert_dialog_choose(dlg, GTK_WIDGET(self), NULL,
                            save_check_response_cb, data);
}

/* ── Save / Save As ── */

static void
save_as_finish(GObject *source, GAsyncResult *res, gpointer user_data)
{
    SaveCheckData *data = user_data;
    GtkFileDialog *dlg = GTK_FILE_DIALOG(source);
    GError *err = NULL;
    GFile *file = gtk_file_dialog_save_finish(dlg, res, &err);
    if (file) {
        save_to_file(data->win, file);
        g_object_unref(file);
        if (data->callback) data->callback(data->win);
    }
    if (err) g_error_free(err);
    g_free(data);
}

static void
do_save_then(NotepadWindow *self, AfterSaveCheck callback)
{
    if (self->file) {
        save_to_file(self, self->file);
        if (callback) callback(self);
    } else {
        GtkFileDialog *dlg = gtk_file_dialog_new();
        gtk_file_dialog_set_title(dlg, "Save As");

        SaveCheckData *data = g_new(SaveCheckData, 1);
        data->win = self;
        data->callback = callback;

        gtk_file_dialog_save(dlg, GTK_WINDOW(self), NULL, save_as_finish, data);
        g_object_unref(dlg);
    }
}

/* ── Actions ── */

static void after_new(NotepadWindow *self)
{
    gtk_text_buffer_set_text(self->buffer, "", 0);
    gtk_text_buffer_set_modified(self->buffer, FALSE);
    set_file(self, NULL);
    update_title(self);
    gtk_text_buffer_set_enable_undo(self->buffer, FALSE);
    gtk_text_buffer_set_enable_undo(self->buffer, TRUE);
}

static void
action_new(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    NotepadWindow *self = NOTEPAD_WINDOW(user_data);
    prompt_save_changes(self, after_new);
}

static void
open_file_finish(GObject *source, GAsyncResult *res, gpointer user_data)
{
    NotepadWindow *self = NOTEPAD_WINDOW(user_data);
    GtkFileDialog *dlg = GTK_FILE_DIALOG(source);
    GError *err = NULL;
    GFile *file = gtk_file_dialog_open_finish(dlg, res, &err);
    if (file) {
        load_file_contents(self, file);
        g_object_unref(file);
    }
    if (err) g_error_free(err);
}

static void after_open(NotepadWindow *self)
{
    GtkFileDialog *dlg = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dlg, "Open");
    gtk_file_dialog_open(dlg, GTK_WINDOW(self), NULL, open_file_finish, self);
    g_object_unref(dlg);
}

static void
action_open(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    NotepadWindow *self = NOTEPAD_WINDOW(user_data);
    prompt_save_changes(self, after_open);
}

static void
action_save(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    NotepadWindow *self = NOTEPAD_WINDOW(user_data);
    do_save_then(self, NULL);
}

static void
action_save_as_finish(GObject *source, GAsyncResult *res, gpointer user_data)
{
    NotepadWindow *self = NOTEPAD_WINDOW(user_data);
    GtkFileDialog *dlg = GTK_FILE_DIALOG(source);
    GError *err = NULL;
    GFile *file = gtk_file_dialog_save_finish(dlg, res, &err);
    if (file) {
        save_to_file(self, file);
        g_object_unref(file);
    }
    if (err) g_error_free(err);
}

static void
action_save_as(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    NotepadWindow *self = NOTEPAD_WINDOW(user_data);
    GtkFileDialog *dlg = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dlg, "Save As");
    gtk_file_dialog_save(dlg, GTK_WINDOW(self), NULL, action_save_as_finish, self);
    g_object_unref(dlg);
}

static void
after_quit(NotepadWindow *w)
{
    gtk_window_destroy(GTK_WINDOW(w));
}

static void
action_quit(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    NotepadWindow *self = NOTEPAD_WINDOW(user_data);
    prompt_save_changes(self, after_quit);
}

static void
action_undo(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    NotepadWindow *self = NOTEPAD_WINDOW(user_data);
    if (gtk_text_buffer_get_can_undo(self->buffer)) {
        gtk_text_buffer_undo(self->buffer);
        /* Disable further undo to enforce single-level */
        gtk_text_buffer_set_enable_undo(self->buffer, FALSE);
        gtk_text_buffer_set_enable_undo(self->buffer, TRUE);
    }
}

static void
action_cut(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    NotepadWindow *self = NOTEPAD_WINDOW(user_data);
    GdkClipboard *clip = gdk_display_get_clipboard(gdk_display_get_default());
    gtk_text_buffer_cut_clipboard(self->buffer, clip, TRUE);
}

static void
action_copy(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    NotepadWindow *self = NOTEPAD_WINDOW(user_data);
    GdkClipboard *clip = gdk_display_get_clipboard(gdk_display_get_default());
    gtk_text_buffer_copy_clipboard(self->buffer, clip);
}

static void
action_paste(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    NotepadWindow *self = NOTEPAD_WINDOW(user_data);
    GdkClipboard *clip = gdk_display_get_clipboard(gdk_display_get_default());
    gtk_text_buffer_paste_clipboard(self->buffer, clip, NULL, TRUE);
}

static void
action_delete(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    NotepadWindow *self = NOTEPAD_WINDOW(user_data);
    gtk_text_buffer_delete_selection(self->buffer, TRUE, TRUE);
}

static void
action_find(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    dialogs_show_find(NOTEPAD_WINDOW(user_data));
}

static void
action_find_next(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    dialogs_find_next(NOTEPAD_WINDOW(user_data));
}

static void
action_replace(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    dialogs_show_replace(NOTEPAD_WINDOW(user_data));
}

static void
action_goto(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    dialogs_show_goto(NOTEPAD_WINDOW(user_data));
}

static void
action_select_all(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    NotepadWindow *self = NOTEPAD_WINDOW(user_data);
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(self->buffer, &start, &end);
    gtk_text_buffer_select_range(self->buffer, &start, &end);
}

static void
action_time_date(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    NotepadWindow *self = NOTEPAD_WINDOW(user_data);

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);

    int hour12 = tm->tm_hour % 12;
    if (hour12 == 0) hour12 = 12;
    const char *ampm = tm->tm_hour >= 12 ? "PM" : "AM";

    char buf[64];
    snprintf(buf, sizeof(buf), "%d:%02d %s %d/%d/%d",
             hour12, tm->tm_min, ampm,
             tm->tm_mon + 1, tm->tm_mday, tm->tm_year + 1900);

    gtk_text_buffer_insert_at_cursor(self->buffer, buf, -1);
}

static void
action_word_wrap(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)param;
    NotepadWindow *self = NOTEPAD_WINDOW(user_data);
    GVariant *state = g_action_get_state(G_ACTION(action));
    gboolean active = !g_variant_get_boolean(state);
    g_variant_unref(state);

    g_simple_action_set_state(action, g_variant_new_boolean(active));
    self->word_wrap = active;

    gtk_text_view_set_wrap_mode(self->text_view,
        active ? GTK_WRAP_WORD : GTK_WRAP_NONE);
}

static void
font_dialog_finish(GObject *source, GAsyncResult *res, gpointer user_data)
{
    NotepadWindow *self = NOTEPAD_WINDOW(user_data);
    GtkFontDialog *dlg = GTK_FONT_DIALOG(source);
    GError *err = NULL;
    PangoFontDescription *desc = gtk_font_dialog_choose_font_finish(dlg, res, &err);
    if (!desc) {
        if (err) g_error_free(err);
        return;
    }

    if (self->font_desc)
        pango_font_description_free(self->font_desc);
    self->font_desc = desc;

    g_object_set(self->font_tag, "font-desc", desc, NULL);

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(self->buffer, &start, &end);
    gtk_text_buffer_apply_tag(self->buffer, self->font_tag, &start, &end);
}

static void
action_font(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    NotepadWindow *self = NOTEPAD_WINDOW(user_data);

    GtkFontDialog *dlg = gtk_font_dialog_new();
    gtk_font_dialog_set_title(dlg, "Font");

    gtk_font_dialog_choose_font(dlg, GTK_WINDOW(self),
                                 self->font_desc, NULL,
                                 font_dialog_finish, self);
    g_object_unref(dlg);
}

static void
on_begin_print(GtkPrintOperation *op, G_GNUC_UNUSED GtkPrintContext *ctx,
               G_GNUC_UNUSED gpointer ud)
{
    gtk_print_operation_set_n_pages(op, 1);
}

static void
on_draw_page(G_GNUC_UNUSED GtkPrintOperation *op, GtkPrintContext *ctx,
             G_GNUC_UNUSED int page, gpointer ud)
{
    NotepadWindow *self = NOTEPAD_WINDOW(ud);
    cairo_t *cr = gtk_print_context_get_cairo_context(ctx);
    PangoLayout *layout = gtk_print_context_create_pango_layout(ctx);

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(self->buffer, &start, &end);
    char *text = gtk_text_buffer_get_text(self->buffer, &start, &end, FALSE);

    pango_layout_set_text(layout, text, -1);
    pango_layout_set_width(layout,
        (int)(gtk_print_context_get_width(ctx) * PANGO_SCALE));

    if (self->font_desc)
        pango_layout_set_font_description(layout, self->font_desc);

    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
    g_free(text);
}

static void
action_print(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    NotepadWindow *self = NOTEPAD_WINDOW(user_data);

    GtkPrintOperation *op = gtk_print_operation_new();
    g_signal_connect(op, "begin-print", G_CALLBACK(on_begin_print), NULL);
    g_signal_connect(op, "draw-page", G_CALLBACK(on_draw_page), self);

    gtk_print_operation_run(op, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
                            GTK_WINDOW(self), NULL);
    g_object_unref(op);
}

static void
action_about(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    NotepadWindow *self = NOTEPAD_WINDOW(user_data);

    AdwAboutDialog *dlg = ADW_ABOUT_DIALOG(adw_about_dialog_new());
    adw_about_dialog_set_application_name(dlg, "Notepad");
    adw_about_dialog_set_version(dlg, "1.0");
    adw_about_dialog_set_comments(dlg, "A Windows 98 Notepad clone built with GTK4");
    adw_about_dialog_set_application_icon(dlg, "text-editor");

    adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(self));
}

/* ── Signals ── */

static void
on_insert_text(GtkTextBuffer *buffer, GtkTextIter *location,
               const char *text, int len, gpointer user_data)
{
    (void)location; (void)text; (void)len;
    NotepadWindow *self = NOTEPAD_WINDOW(user_data);
    if (self->font_desc) {
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gtk_text_buffer_apply_tag(buffer, self->font_tag, &start, &end);
    }
}

static void
on_modified_changed(GtkTextBuffer *buffer, gpointer user_data)
{
    (void)buffer;
    update_title(NOTEPAD_WINDOW(user_data));
}

static void
on_mark_set(GtkTextBuffer *buffer, GtkTextIter *location,
            GtkTextMark *mark, gpointer user_data)
{
    (void)buffer; (void)location;
    if (mark == gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(buffer)))
        update_status_bar(NOTEPAD_WINDOW(user_data));
}

static gboolean
on_close_request(GtkWindow *window, gpointer user_data)
{
    (void)user_data;
    NotepadWindow *self = NOTEPAD_WINDOW(window);

    if (!gtk_text_buffer_get_modified(self->buffer))
        return FALSE; /* allow close */

    prompt_save_changes(self, after_quit);
    return TRUE; /* prevent close, dialog will handle it */
}

/* ── Action entries ── */

static const GActionEntry win_actions[] = {
    { "new-file",   action_new,        NULL, NULL, NULL, {0} },
    { "open",       action_open,       NULL, NULL, NULL, {0} },
    { "save",       action_save,       NULL, NULL, NULL, {0} },
    { "save-as",    action_save_as,    NULL, NULL, NULL, {0} },
    { "print",      action_print,      NULL, NULL, NULL, {0} },
    { "quit",       action_quit,       NULL, NULL, NULL, {0} },
    { "undo",       action_undo,       NULL, NULL, NULL, {0} },
    { "cut",        action_cut,        NULL, NULL, NULL, {0} },
    { "copy",       action_copy,       NULL, NULL, NULL, {0} },
    { "paste",      action_paste,      NULL, NULL, NULL, {0} },
    { "delete",     action_delete,     NULL, NULL, NULL, {0} },
    { "find",       action_find,       NULL, NULL, NULL, {0} },
    { "find-next",  action_find_next,  NULL, NULL, NULL, {0} },
    { "replace",    action_replace,    NULL, NULL, NULL, {0} },
    { "goto",       action_goto,       NULL, NULL, NULL, {0} },
    { "select-all", action_select_all, NULL, NULL, NULL, {0} },
    { "time-date",  action_time_date,  NULL, NULL, NULL, {0} },
    { "word-wrap",  NULL,              NULL, "false", action_word_wrap, {0} },
    { "font",       action_font,       NULL, NULL, NULL, {0} },
    { "about",      action_about,      NULL, NULL, NULL, {0} },
};

/* ── Class init ── */

static void
notepad_window_dispose(GObject *obj)
{
    NotepadWindow *self = NOTEPAD_WINDOW(obj);
    g_clear_object(&self->file);
    g_free(self->filename);
    self->filename = NULL;
    if (self->font_desc) {
        pango_font_description_free(self->font_desc);
        self->font_desc = NULL;
    }
    G_OBJECT_CLASS(notepad_window_parent_class)->dispose(obj);
}

static void
notepad_window_class_init(NotepadWindowClass *klass)
{
    GObjectClass *obj_class = G_OBJECT_CLASS(klass);
    obj_class->dispose = notepad_window_dispose;
}

static void
setup_accels(GtkWidget *widget, G_GNUC_UNUSED gpointer data)
{
    GtkApplication *app = gtk_window_get_application(GTK_WINDOW(widget));
    if (!app) return;

    gtk_application_set_accels_for_action(app, "win.new-file",
        (const char *[]){"<Control>n", NULL});
    gtk_application_set_accels_for_action(app, "win.open",
        (const char *[]){"<Control>o", NULL});
    gtk_application_set_accels_for_action(app, "win.save",
        (const char *[]){"<Control>s", NULL});
    gtk_application_set_accels_for_action(app, "win.save-as",
        (const char *[]){"<Control><Shift>s", NULL});
    gtk_application_set_accels_for_action(app, "win.undo",
        (const char *[]){"<Control>z", NULL});
    gtk_application_set_accels_for_action(app, "win.cut",
        (const char *[]){"<Control>x", NULL});
    gtk_application_set_accels_for_action(app, "win.copy",
        (const char *[]){"<Control>c", NULL});
    gtk_application_set_accels_for_action(app, "win.paste",
        (const char *[]){"<Control>v", NULL});
    gtk_application_set_accels_for_action(app, "win.delete",
        (const char *[]){"Delete", NULL});
    gtk_application_set_accels_for_action(app, "win.find",
        (const char *[]){"<Control>f", NULL});
    gtk_application_set_accels_for_action(app, "win.find-next",
        (const char *[]){"F3", NULL});
    gtk_application_set_accels_for_action(app, "win.replace",
        (const char *[]){"<Control>h", NULL});
    gtk_application_set_accels_for_action(app, "win.goto",
        (const char *[]){"<Control>g", NULL});
    gtk_application_set_accels_for_action(app, "win.select-all",
        (const char *[]){"<Control>a", NULL});
    gtk_application_set_accels_for_action(app, "win.time-date",
        (const char *[]){"F5", NULL});
    gtk_application_set_accels_for_action(app, "win.print",
        (const char *[]){"<Control>p", NULL});
}

static void
notepad_window_init(NotepadWindow *self)
{
    self->file = NULL;
    self->filename = NULL;
    self->word_wrap = FALSE;
    self->font_desc = NULL;

    /* Window setup */
    gtk_window_set_default_size(GTK_WINDOW(self), 640, 480);
    gtk_window_set_title(GTK_WINDOW(self), "Untitled - Notepad");

    /* Actions */
    g_action_map_add_action_entries(G_ACTION_MAP(self),
                                    win_actions, G_N_ELEMENTS(win_actions), self);

    /* Menu bar */
    char *ui_path = g_build_filename(g_get_current_dir(), "src", "menus.ui", NULL);
    GtkBuilder *builder = gtk_builder_new_from_file(ui_path);
    g_free(ui_path);
    GMenuModel *menubar = G_MENU_MODEL(gtk_builder_get_object(builder, "menubar"));

    GtkWidget *menu_bar = gtk_popover_menu_bar_new_from_model(menubar);
    g_object_unref(builder);

    /* Text view */
    self->buffer = gtk_text_buffer_new(NULL);
    gtk_text_buffer_set_enable_undo(self->buffer, TRUE);

    self->text_view = GTK_TEXT_VIEW(gtk_text_view_new_with_buffer(self->buffer));
    gtk_text_view_set_wrap_mode(self->text_view, GTK_WRAP_NONE);
    gtk_text_view_set_monospace(self->text_view, TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(self->text_view), TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(self->text_view), TRUE);

    /* Scrolled window */
    self->scrolled = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new());
    gtk_scrolled_window_set_child(self->scrolled, GTK_WIDGET(self->text_view));
    gtk_widget_set_vexpand(GTK_WIDGET(self->scrolled), TRUE);

    /* Status bar */
    self->status_bar = GTK_LABEL(gtk_label_new("Ln 1, Col 1"));
    gtk_widget_set_halign(GTK_WIDGET(self->status_bar), GTK_ALIGN_START);
    gtk_widget_set_margin_start(GTK_WIDGET(self->status_bar), 6);
    gtk_widget_set_margin_end(GTK_WIDGET(self->status_bar), 6);
    gtk_widget_set_margin_top(GTK_WIDGET(self->status_bar), 2);
    gtk_widget_set_margin_bottom(GTK_WIDGET(self->status_bar), 2);

    self->status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append(GTK_BOX(self->status_box), GTK_WIDGET(self->status_bar));

    /* Add separator above status bar */
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);

    /* Main layout */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(vbox), menu_bar);
    gtk_box_append(GTK_BOX(vbox), GTK_WIDGET(self->scrolled));
    gtk_box_append(GTK_BOX(vbox), sep);
    gtk_box_append(GTK_BOX(vbox), self->status_box);

    adw_application_window_set_content(ADW_APPLICATION_WINDOW(self), vbox);

    /* Font tag */
    self->font_tag = gtk_text_buffer_create_tag(self->buffer, "font", NULL);

    /* Connect signals */
    g_signal_connect(self->buffer, "modified-changed",
                     G_CALLBACK(on_modified_changed), self);
    g_signal_connect(self->buffer, "mark-set",
                     G_CALLBACK(on_mark_set), self);
    g_signal_connect_after(self->buffer, "insert-text",
                           G_CALLBACK(on_insert_text), self);
    g_signal_connect(self, "close-request",
                     G_CALLBACK(on_close_request), NULL);

    /* Keyboard shortcuts */
    g_signal_connect(self, "realize", G_CALLBACK(setup_accels), NULL);
}

NotepadWindow *
notepad_window_new(GtkApplication *app)
{
    return g_object_new(NOTEPAD_TYPE_WINDOW,
                        "application", app,
                        NULL);
}

void
notepad_window_find_next(NotepadWindow *self)
{
    dialogs_find_next(self);
}
