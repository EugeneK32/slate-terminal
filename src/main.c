/* Slate Terminal — a small native GTK4 / libadwaita / VTE terminal.
 * Keyboard shortcuts use Linux evdev physical positions, not translated
 * Cyrillic / Latin keysyms. Licensed under MIT; see LICENSE.
 */
#include <adwaita.h>
#include <vte/vte.h>
#include <linux/input-event-codes.h>
#include <pwd.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>

#define APP_ID "dev.slate.Terminal"
#define APP_TITLE "Slate Terminal"

typedef struct _SlateWindow SlateWindow;
typedef struct _SlateTab SlateTab;

struct _SlateWindow {
    AdwApplicationWindow *window;
    AdwTabView *view;
    double font_scale;
    GKeyFile *settings;
};

struct _SlateTab {
    SlateWindow *owner;
    GtkWidget *container;
    VteTerminal *terminal;
    GtkWidget *search_bar;
    GtkWidget *search_entry;
    AdwTabPage *page;
};

static void new_tab(SlateWindow *sw, const char *cwd);
static SlateWindow *new_window(AdwApplication *app);
/* Optional initial folder, used by Nautilus and command-line launching. */
static char *startup_directory = NULL;
static void show_preferences(SlateWindow *sw);

static const char *const palette_hex[16] = {
    "#303030", "#e16b73", "#80c995", "#e2bb70",
    "#78a9e8", "#bca2e0", "#78bec9", "#d4d4d4",
    "#737373", "#ff8890", "#9cdead", "#f4d18f",
    "#9ac4ff", "#d4b8f4", "#9bd9df", "#ffffff"
};

static const char *const theme_css =
    "window.slate-window { background-color: #1e1e1e; }"
    ".slate-header { background-color: #303030; }"
    ".slate-header tabbar { background-color: transparent; }"
    ".slate-terminal-bg { background-color: #1e1e1e; }"
    ".slate-search { padding: 8px 12px; background-color: #303030; }";

static char *setting_string(GKeyFile *kf, const char *section,
                            const char *key, const char *fallback)
{
    GError *error = NULL;
    char *value = g_key_file_get_string(kf, section, key, &error);
    if (error) {
        g_clear_error(&error);
        return g_strdup(fallback);
    }
    return value;
}

static int setting_int(GKeyFile *kf, const char *section,
                       const char *key, int fallback)
{
    GError *error = NULL;
    int value = g_key_file_get_integer(kf, section, key, &error);
    if (error) {
        g_clear_error(&error);
        return fallback;
    }
    return value;
}

static double setting_double(GKeyFile *kf, const char *section,
                             const char *key, double fallback)
{
    GError *error = NULL;
    double value = g_key_file_get_double(kf, section, key, &error);
    if (error) {
        g_clear_error(&error);
        return fallback;
    }
    return value;
}

static void save_settings(SlateWindow *sw)
{
    char *directory = g_build_filename(g_get_user_config_dir(),
                                        "slate-terminal", NULL);
    if (g_mkdir_with_parents(directory, 0700) != 0) {
        g_warning("Could not create preferences directory: %s", directory);
        g_free(directory);
        return;
    }
    char *path = g_build_filename(directory, "config.ini", NULL);
    GError *error = NULL;
    if (!g_key_file_save_to_file(sw->settings, path, &error)) {
        g_warning("Could not save preferences: %s", error->message);
        g_clear_error(&error);
    }
    g_free(path);
    g_free(directory);
}

static SlateTab *selected_tab(SlateWindow *sw)
{
    AdwTabPage *page = adw_tab_view_get_selected_page(sw->view);
    if (!page) return NULL;
    return g_object_get_data(G_OBJECT(adw_tab_page_get_child(page)), "slate-tab");
}

static void focus_selected(SlateWindow *sw)
{
    SlateTab *tab = selected_tab(sw);
    if (tab)
        gtk_widget_grab_focus(GTK_WIDGET(tab->terminal));
}

static void on_selected_changed(GObject *object, GParamSpec *pspec, gpointer data)
{
    (void)object; (void)pspec;
    focus_selected(data);
}

static void on_page_detached(AdwTabView *view, AdwTabPage *page,
                             gint position, gpointer data)
{
    (void)page; (void)position;
    SlateWindow *sw = data;
    if (adw_tab_view_get_n_pages(view) == 0)
        gtk_window_close(GTK_WINDOW(sw->window));
}

static void close_tab(SlateWindow *sw)
{
    AdwTabPage *page = adw_tab_view_get_selected_page(sw->view);
    if (page) adw_tab_view_close_page(sw->view, page);
}

static void on_add_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    SlateWindow *sw = data;
    SlateTab *tab = selected_tab(sw);
    char *cwd = NULL;
    if (tab) {
        const char *uri = vte_terminal_get_current_directory_uri(tab->terminal);
        if (uri && g_str_has_prefix(uri, "file://"))
            cwd = g_filename_from_uri(uri, NULL, NULL);
    }
    new_tab(sw, cwd);
    g_free(cwd);
}

static void on_title_changed(VteTerminal *terminal, gpointer data)
{
    SlateTab *tab = data;
    const char *title = vte_terminal_get_window_title(terminal);
    if (title && *title) {
        char *short_title = g_utf8_substring(title, 0,
            MIN((glong)g_utf8_strlen(title, -1), (glong)32));
        adw_tab_page_set_title(tab->page, short_title);
        g_free(short_title);
    }
}

static void on_child_exited(VteTerminal *terminal, gint exit_status, gpointer data)
{
    (void)terminal; (void)exit_status;
    SlateTab *tab = data;
    adw_tab_view_close_page(tab->owner->view, tab->page);
}

static void on_spawn(VteTerminal *terminal, GPid pid,
                     GError *error, gpointer data)
{
    (void)pid; (void)data;
    if (error && terminal) {
        char *message = g_strdup_printf("\r\nSlate: could not start shell: %s\r\n", error->message);
        vte_terminal_feed(terminal, message, -1);
        g_free(message);
    }
    if (!error && terminal)
        g_object_set_data(G_OBJECT(terminal), "slate-shell-pid", GINT_TO_POINTER((int)pid));
    if (error) g_warning("Could not start shell: %s", error->message);
}

static void on_search_changed(GtkSearchEntry *entry, gpointer data)
{
    SlateTab *tab = data;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (!text || !*text) {
        vte_terminal_search_set_regex(tab->terminal, NULL, 0);
        return;
    }
    GError *error = NULL;
    VteRegex *regex = vte_regex_new_for_search(text, -1, 0, &error);
    if (regex) {
        vte_terminal_search_set_regex(tab->terminal, regex, 0);
        vte_terminal_search_set_wrap_around(tab->terminal, TRUE);
        vte_terminal_search_find_next(tab->terminal);
        vte_regex_unref(regex);
    }
    g_clear_error(&error);
}

static void on_search_activate(GtkSearchEntry *entry, gpointer data)
{
    (void)entry;
    SlateTab *tab = data;
    vte_terminal_search_find_next(tab->terminal);
}

static void toggle_search(SlateTab *tab)
{
    if (!tab) return;
    gboolean shown = gtk_revealer_get_reveal_child(GTK_REVEALER(tab->search_bar));
    gtk_revealer_set_reveal_child(GTK_REVEALER(tab->search_bar), !shown);
    if (!shown)
        gtk_widget_grab_focus(tab->search_entry);
    else
        gtk_widget_grab_focus(GTK_WIDGET(tab->terminal));
}

static void apply_terminal_style(SlateTab *tab)
{
    SlateWindow *sw = tab->owner;
    char *font_name = setting_string(sw->settings, "Appearance", "font", "Adwaita Mono 12");
    PangoFontDescription *font = pango_font_description_from_string(font_name);
    vte_terminal_set_font(tab->terminal, font);
    vte_terminal_set_font_scale(tab->terminal, sw->font_scale);
    /* Extra leading avoids clipped ascenders with some font/renderers. */
    vte_terminal_set_cell_height_scale(tab->terminal,
        CLAMP(setting_double(sw->settings, "Appearance", "line_spacing", 1.16), 1.0, 1.6));
    pango_font_description_free(font);
    g_free(font_name);

    char *background = setting_string(sw->settings, "Appearance", "background", "#1e1e1e");
    char *foreground = setting_string(sw->settings, "Appearance", "foreground", "#e7e7e7");
    GdkRGBA bg, fg, palette[16], cursor, selection;
    if (!gdk_rgba_parse(&bg, background)) gdk_rgba_parse(&bg, "#1e1e1e");
    if (!gdk_rgba_parse(&fg, foreground)) gdk_rgba_parse(&fg, "#e7e7e7");
    for (int i = 0; i < 16; ++i) gdk_rgba_parse(&palette[i], palette_hex[i]);
    vte_terminal_set_colors(tab->terminal, &fg, &bg, palette, 16);
    gdk_rgba_parse(&cursor, "#d8d8d8");
    gdk_rgba_parse(&selection, "#355c87");
    vte_terminal_set_color_cursor(tab->terminal, &cursor);
    vte_terminal_set_color_highlight(tab->terminal, &selection);
    vte_terminal_set_cursor_shape(tab->terminal, VTE_CURSOR_SHAPE_IBEAM);
    vte_terminal_set_scrollback_lines(tab->terminal, 15000);
    vte_terminal_set_audible_bell(tab->terminal, FALSE);
    vte_terminal_set_mouse_autohide(tab->terminal, TRUE);
    vte_terminal_set_rewrap_on_resize(tab->terminal, TRUE);
    vte_terminal_set_delete_binding(tab->terminal, VTE_ERASE_DELETE_SEQUENCE);
    g_free(background);
    g_free(foreground);
}

static void new_tab(SlateWindow *sw, const char *cwd)
{
    SlateTab *tab = g_new0(SlateTab, 1);
    tab->owner = sw;

    tab->container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    g_object_set_data_full(G_OBJECT(tab->container), "slate-tab", tab, g_free);
    gtk_widget_add_css_class(tab->container, "slate-terminal-bg");

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(outer, TRUE);
    gtk_widget_set_vexpand(outer, TRUE);
    int padding = CLAMP(setting_int(sw->settings, "Appearance", "padding", 12), 0, 40);
    gtk_widget_set_margin_start(outer, padding);
    gtk_widget_set_margin_end(outer, padding);
    gtk_widget_set_margin_top(outer, padding);
    gtk_widget_set_margin_bottom(outer, padding);

    tab->terminal = VTE_TERMINAL(vte_terminal_new());
    gtk_widget_set_hexpand(GTK_WIDGET(tab->terminal), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(tab->terminal), TRUE);
    gtk_box_append(GTK_BOX(outer), GTK_WIDGET(tab->terminal));
    gtk_box_append(GTK_BOX(tab->container), outer);

    tab->search_bar = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(tab->search_bar),
                                      GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
    GtkWidget *search_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(search_box, "slate-search");
    tab->search_entry = gtk_search_entry_new();
    gtk_widget_set_hexpand(tab->search_entry, TRUE);
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(tab->search_entry), "Find in terminal…");
    gtk_box_append(GTK_BOX(search_box), tab->search_entry);
    gtk_revealer_set_child(GTK_REVEALER(tab->search_bar), search_box);
    gtk_box_append(GTK_BOX(tab->container), tab->search_bar);

    tab->page = adw_tab_view_append(sw->view, tab->container);
    adw_tab_page_set_title(tab->page, "Terminal");
    adw_tab_view_set_selected_page(sw->view, tab->page);
    apply_terminal_style(tab);

    g_signal_connect(tab->terminal, "window-title-changed", G_CALLBACK(on_title_changed), tab);
    g_signal_connect(tab->terminal, "child-exited", G_CALLBACK(on_child_exited), tab);
    g_signal_connect(tab->search_entry, "search-changed", G_CALLBACK(on_search_changed), tab);
    g_signal_connect(tab->search_entry, "activate", G_CALLBACK(on_search_activate), tab);

    struct passwd *pw = getpwuid(getuid());
    const char *shell = (pw && pw->pw_shell && *pw->pw_shell) ? pw->pw_shell : "/bin/sh";
    char *configured_shell = setting_string(sw->settings, "Terminal", "shell", "");
    if (*configured_shell && g_path_is_absolute(configured_shell) &&
        g_file_test(configured_shell, G_FILE_TEST_IS_EXECUTABLE))
        shell = configured_shell;
    /* ZLE clipboard bridge applies only to a local Zsh shell. */
    char *shell_basename = g_path_get_basename(shell);
    g_object_set_data(G_OBJECT(tab->terminal), "slate-zsh",
        GINT_TO_POINTER(g_strcmp0(shell_basename, "zsh") == 0));
    g_free(shell_basename);
    char *argv[] = {(char *)shell, NULL};
    vte_terminal_spawn_async(tab->terminal, VTE_PTY_DEFAULT,
                            cwd ? cwd : g_get_home_dir(), argv, NULL,
                            G_SPAWN_DEFAULT, NULL, NULL, NULL,
                            -1, NULL, on_spawn, NULL);
    g_free(configured_shell);
    gtk_widget_grab_focus(GTK_WIDGET(tab->terminal));
}

/* Physical mapping: XKB keycode == Linux evdev keycode + 8 on normal
 * GNOME Wayland/Xorg evdev keyboards. If a different keyboard backend uses
 * other keycodes, Latin keys still work through the keysym fallback. */
static char physical_letter(guint keycode, guint keyval)
{
    struct { guint code; char letter; } const keys[] = {
        {KEY_A,'a'}, {KEY_B,'b'}, {KEY_C,'c'}, {KEY_D,'d'},
        {KEY_E,'e'}, {KEY_F,'f'}, {KEY_G,'g'}, {KEY_H,'h'},
        {KEY_I,'i'}, {KEY_J,'j'}, {KEY_K,'k'}, {KEY_L,'l'},
        {KEY_M,'m'}, {KEY_N,'n'}, {KEY_O,'o'}, {KEY_P,'p'},
        {KEY_Q,'q'}, {KEY_R,'r'}, {KEY_S,'s'}, {KEY_T,'t'},
        {KEY_U,'u'}, {KEY_V,'v'}, {KEY_W,'w'}, {KEY_X,'x'},
        {KEY_Y,'y'}, {KEY_Z,'z'}
    };
    for (guint i = 0; i < G_N_ELEMENTS(keys); ++i)
        if (keycode == keys[i].code + 8) return keys[i].letter;
    if (keyval >= GDK_KEY_a && keyval <= GDK_KEY_z) return (char)keyval;
    if (keyval >= GDK_KEY_A && keyval <= GDK_KEY_Z) return (char)(keyval + 32);
    return 0;
}

static void send_text(VteTerminal *term, const char *bytes)
{
    vte_terminal_feed_child(term, bytes, -1);
}

/* Only send the ZLE clipboard escape when the local login shell owns
 * the foreground PTY, not while SSH/vim/sudo/another command is running. */
static gboolean local_shell_is_foreground(VteTerminal *terminal)
{
    if (!GPOINTER_TO_INT(g_object_get_data(G_OBJECT(terminal), "slate-zsh")))
        return FALSE;
    VtePty *pty = vte_terminal_get_pty(terminal);
    if (!pty) return FALSE;
    int fd = vte_pty_get_fd(pty);
    pid_t shell = GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(terminal), "slate-shell-pid"));
    return fd >= 0 && shell > 0 && tcgetpgrp(fd) == shell;
}

/* Preferences update all terminal tabs immediately, then persist config.ini. */
static void refresh_all_tabs(SlateWindow *sw)
{
    for (gint i = 0; i < adw_tab_view_get_n_pages(sw->view); ++i) {
        AdwTabPage *page = adw_tab_view_get_nth_page(sw->view, i);
        SlateTab *tab = g_object_get_data(
            G_OBJECT(adw_tab_page_get_child(page)), "slate-tab");
        if (tab) {
            GtkWidget *outer = gtk_widget_get_first_child(tab->container);
            int pad = CLAMP(setting_int(sw->settings, "Appearance", "padding", 12), 0, 40);
            gtk_widget_set_margin_start(outer, pad);
            gtk_widget_set_margin_end(outer, pad);
            gtk_widget_set_margin_top(outer, pad);
            gtk_widget_set_margin_bottom(outer, pad);
            apply_terminal_style(tab);
        }
    }
}

static void on_font_changed(GObject *button, GParamSpec *pspec, gpointer data)
{
    (void)pspec;
    SlateWindow *sw = data;
    const PangoFontDescription *desc =
        gtk_font_dialog_button_get_font_desc(GTK_FONT_DIALOG_BUTTON(button));
    if (!desc) return;
    char *font = pango_font_description_to_string(desc);
    g_key_file_set_string(sw->settings, "Appearance", "font", font);
    g_free(font);
    sw->font_scale = 1.0; /* The font chooser controls the actual size. */
    refresh_all_tabs(sw);
    save_settings(sw);
}

static void on_spacing_changed(GtkSpinButton *button, gpointer data)
{
    SlateWindow *sw = data;
    g_key_file_set_double(sw->settings, "Appearance", "line_spacing",
                          gtk_spin_button_get_value(button));
    refresh_all_tabs(sw);
    save_settings(sw);
}

static void on_padding_changed(GtkSpinButton *button, gpointer data)
{
    SlateWindow *sw = data;
    g_key_file_set_integer(sw->settings, "Appearance", "padding",
                           gtk_spin_button_get_value_as_int(button));
    refresh_all_tabs(sw);
    save_settings(sw);
}

static void on_preferences_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    show_preferences(data);
}

static void show_preferences(SlateWindow *sw)
{
    AdwDialog *dialog = adw_preferences_dialog_new();
    GtkWidget *page = adw_preferences_page_new();
    adw_preferences_page_set_title(ADW_PREFERENCES_PAGE(page), "Appearance");
    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(dialog), ADW_PREFERENCES_PAGE(page));

    GtkWidget *group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(group), "Text");
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(group));

    GtkWidget *font_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(font_row), "Font and size");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(font_row), "Choose a monospace font");
    GtkFontDialog *font_dialog = gtk_font_dialog_new();
    GtkWidget *font_button = gtk_font_dialog_button_new(font_dialog);
    /* gtk_font_dialog_button_new() takes ownership of font_dialog. */
    gtk_font_dialog_button_set_use_font(GTK_FONT_DIALOG_BUTTON(font_button), TRUE);
    gtk_font_dialog_button_set_use_size(GTK_FONT_DIALOG_BUTTON(font_button), TRUE);
    char *font_name = setting_string(sw->settings, "Appearance", "font", "Adwaita Mono 12");
    PangoFontDescription *font = pango_font_description_from_string(font_name);
    gtk_font_dialog_button_set_font_desc(GTK_FONT_DIALOG_BUTTON(font_button), font);
    pango_font_description_free(font);
    g_free(font_name);
    gtk_widget_set_valign(font_button, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(font_row), font_button);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), font_row);
    g_signal_connect(font_button, "notify::font-desc", G_CALLBACK(on_font_changed), sw);

    GtkWidget *spacing_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(spacing_row), "Line spacing");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(spacing_row), "Extra height to prevent clipped glyphs");
    GtkWidget *spacing_spin = gtk_spin_button_new_with_range(1.0, 1.6, 0.02);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spacing_spin), 2);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spacing_spin),
        CLAMP(setting_double(sw->settings, "Appearance", "line_spacing", 1.16), 1.0, 1.6));
    gtk_widget_set_valign(spacing_spin, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(spacing_row), spacing_spin);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), spacing_row);
    g_signal_connect(spacing_spin, "value-changed", G_CALLBACK(on_spacing_changed), sw);

    GtkWidget *padding_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(padding_row), "Window padding");
    GtkWidget *padding_spin = gtk_spin_button_new_with_range(0, 40, 2);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(padding_spin),
        CLAMP(setting_int(sw->settings, "Appearance", "padding", 12), 0, 40));
    gtk_widget_set_valign(padding_spin, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(padding_row), padding_spin);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), padding_row);
    g_signal_connect(padding_spin, "value-changed", G_CALLBACK(on_padding_changed), sw);

    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(sw->window));
}

static void zoom(SlateWindow *sw, double amount)
{
    sw->font_scale = CLAMP(sw->font_scale + amount, 0.65, 2.5);
    for (gint i = 0; i < adw_tab_view_get_n_pages(sw->view); ++i) {
        AdwTabPage *page = adw_tab_view_get_nth_page(sw->view, i);
        SlateTab *tab = g_object_get_data(
            G_OBJECT(adw_tab_page_get_child(page)), "slate-tab");
        if (tab) vte_terminal_set_font_scale(tab->terminal, sw->font_scale);
    }
}

static gboolean on_key(GtkEventControllerKey *controller, guint keyval,
                       guint keycode, GdkModifierType modifiers, gpointer data)
{
    (void)controller;
    SlateWindow *sw = data;
    SlateTab *tab = selected_tab(sw);
    if (!tab) return GDK_EVENT_PROPAGATE;

    const gboolean ctrl  = !!(modifiers & GDK_CONTROL_MASK);
    const gboolean shift = !!(modifiers & GDK_SHIFT_MASK);
    const gboolean alt   = !!(modifiers & (GDK_ALT_MASK | GDK_META_MASK | GDK_SUPER_MASK));
    char letter = physical_letter(keycode, keyval);

    if (keyval == GDK_KEY_Escape && gtk_revealer_get_reveal_child(GTK_REVEALER(tab->search_bar))) {
        gtk_revealer_set_reveal_child(GTK_REVEALER(tab->search_bar), FALSE);
        gtk_widget_grab_focus(GTK_WIDGET(tab->terminal));
        return GDK_EVENT_STOP;
    }
    if (ctrl && shift && !alt && letter == 'f') {
        toggle_search(tab);
        return GDK_EVENT_STOP;
    }
    if (ctrl && !shift && !alt && keycode == KEY_COMMA + 8) {
        show_preferences(sw);
        return GDK_EVENT_STOP;
    }

    /* When searching, don't steal text-editing shortcuts from GtkSearchEntry. */
    GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(sw->window));
    if (focus && (focus == tab->search_entry ||
                  gtk_widget_is_ancestor(focus, tab->search_entry)))
        return GDK_EVENT_PROPAGATE;

    if (ctrl && shift && !alt) {
        if (letter == 'c') {
            if (vte_terminal_get_has_selection(tab->terminal))
                vte_terminal_copy_clipboard_format(tab->terminal, VTE_FORMAT_TEXT);
            else if (local_shell_is_foreground(tab->terminal))
                /* For Zsh ZLE shift-selected input: see slate-keybindings.zsh.
                 * Avoid sending escape codes to SSH, editors and other apps. */
                send_text(tab->terminal, "\033[27;6;99~");
            return GDK_EVENT_STOP;
        }
        if (letter == 'v') { vte_terminal_paste_clipboard(tab->terminal); return GDK_EVENT_STOP; }
        if (letter == 't') { on_add_clicked(NULL, sw); return GDK_EVENT_STOP; }
        if (letter == 'w') { close_tab(sw); return GDK_EVENT_STOP; }
        if (letter == 'n') { new_window(ADW_APPLICATION(gtk_window_get_application(GTK_WINDOW(sw->window)))); return GDK_EVENT_STOP; }
    }
    if (ctrl && !alt) {
        if (keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab) {
            if (shift) adw_tab_view_select_previous_page(sw->view);
            else adw_tab_view_select_next_page(sw->view);
            return GDK_EVENT_STOP;
        }
        if (keyval == GDK_KEY_Page_Up || keyval == GDK_KEY_KP_Page_Up) {
            adw_tab_view_select_previous_page(sw->view);
            return GDK_EVENT_STOP;
        }
        if (keyval == GDK_KEY_Page_Down || keyval == GDK_KEY_KP_Page_Down) {
            adw_tab_view_select_next_page(sw->view);
            return GDK_EVENT_STOP;
        }
        if (keycode == KEY_EQUAL + 8) { zoom(sw, 0.1); return GDK_EVENT_STOP; }
        if (keycode == KEY_MINUS + 8) { zoom(sw, -0.1); return GDK_EVENT_STOP; }
        if (keycode == KEY_0 + 8 && !shift) { zoom(sw, 1.0 - sw->font_scale); return GDK_EVENT_STOP; }
        if (!shift) {
            /* Standard ASCII Ctrl+A..Z regardless of RU/EN layout. */
            if (letter) {
                char code[2] = {(char)(letter - 'a' + 1), 0};
                vte_terminal_feed_child(tab->terminal, code, 1);
                return GDK_EVENT_STOP;
            }
        }
    }
    if (alt && !ctrl && !shift) {
        guint number = 0;
        if (keycode >= KEY_1 + 8 && keycode <= KEY_9 + 8)
            number = keycode - (KEY_1 + 8) + 1;
        if (number && number <= (guint)adw_tab_view_get_n_pages(sw->view)) {
            adw_tab_view_set_selected_page(sw->view,
                adw_tab_view_get_nth_page(sw->view, number - 1));
            return GDK_EVENT_STOP;
        }
    }

    /* Stable xterm-style sequences; ZLE bindings provided in shell/ . */
    if (!alt) {
        const char *suffix = NULL;
        switch (keyval) {
            case GDK_KEY_Left:  case GDK_KEY_KP_Left:  suffix = "D"; break;
            case GDK_KEY_Right: case GDK_KEY_KP_Right: suffix = "C"; break;
            case GDK_KEY_Up:    case GDK_KEY_KP_Up:    suffix = "A"; break;
            case GDK_KEY_Down:  case GDK_KEY_KP_Down:  suffix = "B"; break;
            case GDK_KEY_Home:  case GDK_KEY_KP_Home:  suffix = "H"; break;
            case GDK_KEY_End:   case GDK_KEY_KP_End:   suffix = "F"; break;
            default: break;
        }
        if (suffix && (ctrl || shift || keyval == GDK_KEY_Home || keyval == GDK_KEY_End)) {
            int modifier = 1 + (shift ? 1 : 0) + (ctrl ? 4 : 0);
            char *sequence = (ctrl || shift) ? g_strdup_printf("\033[1;%d%s", modifier, suffix)
                                              : g_strdup_printf("\033[%s", suffix);
            send_text(tab->terminal, sequence);
            g_free(sequence);
            return GDK_EVENT_STOP;
        }
        if (keyval == GDK_KEY_Delete || keyval == GDK_KEY_KP_Delete) {
            send_text(tab->terminal, "\033[3~");
            return GDK_EVENT_STOP;
        }
    }
    return GDK_EVENT_PROPAGATE;
}

static void free_window(gpointer data)
{
    SlateWindow *sw = data;
    g_key_file_unref(sw->settings);
    g_free(sw);
}

static SlateWindow *new_window(AdwApplication *app)
{
    SlateWindow *sw = g_new0(SlateWindow, 1);
    sw->font_scale = 1.0;
    sw->settings = g_key_file_new();
    char *config_path = g_build_filename(g_get_user_config_dir(),
                                         "slate-terminal", "config.ini", NULL);
    g_key_file_load_from_file(sw->settings, config_path, G_KEY_FILE_KEEP_COMMENTS, NULL);
    g_free(config_path);

    sw->window = ADW_APPLICATION_WINDOW(adw_application_window_new(GTK_APPLICATION(app)));
    gtk_window_set_title(GTK_WINDOW(sw->window), APP_TITLE);
    gtk_window_set_default_size(GTK_WINDOW(sw->window), 1050, 720);
    gtk_widget_add_css_class(GTK_WIDGET(sw->window), "slate-window");
    sw->view = ADW_TAB_VIEW(adw_tab_view_new());
    /* No GTK keysyms-based tab shortcuts: our key handler owns these. */
    adw_tab_view_set_shortcuts(sw->view, ADW_TAB_VIEW_SHORTCUT_NONE);

    GtkWidget *toolbar = adw_toolbar_view_new();
    GtkWidget *header = adw_header_bar_new();
    gtk_widget_add_css_class(header, "slate-header");
    adw_header_bar_set_show_start_title_buttons(ADW_HEADER_BAR(header), FALSE);
    adw_header_bar_set_show_end_title_buttons(ADW_HEADER_BAR(header), TRUE);
    adw_header_bar_set_centering_policy(ADW_HEADER_BAR(header), ADW_CENTERING_POLICY_LOOSE);

    GtkWidget *settings_button = gtk_button_new_from_icon_name("preferences-system-symbolic");
    gtk_widget_set_tooltip_text(settings_button, "Preferences (Ctrl+,)");
    gtk_widget_add_css_class(settings_button, "flat");
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), settings_button);
    g_signal_connect(settings_button, "clicked", G_CALLBACK(on_preferences_clicked), sw);

    GtkWidget *new_button = gtk_button_new_from_icon_name("tab-new-symbolic");
    gtk_widget_set_tooltip_text(new_button, "New tab (Ctrl+Shift+T)");
    gtk_widget_add_css_class(new_button, "flat");
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), new_button);
    g_signal_connect(new_button, "clicked", G_CALLBACK(on_add_clicked), sw);

    GtkWidget *bar = GTK_WIDGET(adw_tab_bar_new());
    adw_tab_bar_set_view(ADW_TAB_BAR(bar), sw->view);
    adw_tab_bar_set_autohide(ADW_TAB_BAR(bar), FALSE);
    adw_tab_bar_set_expand_tabs(ADW_TAB_BAR(bar), FALSE);
    gtk_widget_set_hexpand(bar, TRUE);
    gtk_widget_set_size_request(bar, 300, -1);
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), bar);

    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), GTK_WIDGET(sw->view));
    adw_application_window_set_content(sw->window, toolbar);

    GtkEventController *keyboard = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keyboard, GTK_PHASE_CAPTURE);
    gtk_widget_add_controller(GTK_WIDGET(sw->window), keyboard);
    g_signal_connect(keyboard, "key-pressed", G_CALLBACK(on_key), sw);
    g_signal_connect(sw->view, "notify::selected-page", G_CALLBACK(on_selected_changed), sw);
    g_signal_connect(sw->view, "page-detached", G_CALLBACK(on_page_detached), sw);
    g_object_set_data_full(G_OBJECT(sw->window), "slate-window", sw, free_window);

    new_tab(sw, startup_directory &&
               g_file_test(startup_directory, G_FILE_TEST_IS_DIR)
               ? startup_directory : NULL);
    gtk_window_present(GTK_WINDOW(sw->window));
    return sw;
}

static void on_startup(GApplication *app, gpointer data)
{
    (void)app;
    GtkCssProvider *css = data;
    GdkDisplay *display = gdk_display_get_default();
    if (display)
        gtk_style_context_add_provider_for_display(display,
            GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    AdwStyleManager *style = adw_style_manager_get_default();
    adw_style_manager_set_color_scheme(style, ADW_COLOR_SCHEME_FORCE_DARK);
}

static void on_activate(GApplication *app, gpointer data)
{
    (void)data;
    new_window(ADW_APPLICATION(app));
}

int main(int argc, char **argv)
{
    /* GTK renderer must be chosen before GDK initializes. Cairo works around
     * corrupted glyph fragments seen with some GNOME/Vulkan GPU drivers. */
    if (!g_getenv("GSK_RENDERER")) {
        GKeyFile *render_settings = g_key_file_new();
        char *path = g_build_filename(g_get_user_config_dir(),
                                       "slate-terminal", "config.ini", NULL);
        g_key_file_load_from_file(render_settings, path, G_KEY_FILE_NONE, NULL);
        char *renderer = setting_string(render_settings, "Graphics", "renderer", "cairo");
        if (g_str_equal(renderer, "cairo") || g_str_equal(renderer, "gl") ||
            g_str_equal(renderer, "ngl") || g_str_equal(renderer, "vulkan"))
            g_setenv("GSK_RENDERER", renderer, FALSE);
        g_free(renderer);
        g_free(path);
        g_key_file_unref(render_settings);
    }
    g_set_application_name(APP_TITLE);
    /* Non-unique: opening from Files creates a new process with its own cwd.
     * This avoids a running instance silently discarding command-line args. */
    AdwApplication *app = adw_application_new(APP_ID, G_APPLICATION_NON_UNIQUE);
    GOptionEntry options[] = {
        {"working-directory", 'd', 0, G_OPTION_ARG_FILENAME,
         &startup_directory, "Initial working directory", "DIRECTORY"},
        {NULL}
    };
    g_application_add_main_option_entries(G_APPLICATION(app), options);
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css, theme_css);
    g_signal_connect(app, "startup", G_CALLBACK(on_startup), css);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    g_object_set_data_full(G_OBJECT(app), "slate-css", css, g_object_unref);
    int result = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    g_free(startup_directory);
    return result;
}
