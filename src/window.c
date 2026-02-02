/*
 * ReelGTK - Main Window
 * Application window with poster grid, filter bar, and status bar
 */

#include "window.h"
#include "config.h"
#include "db.h"
#include "filter.h"
#include "grid.h"
#include "scanner.h"
#include "scraper.h"
#include <string.h>

/* Forward declarations */
static void apply_theme_css(ReelApp *app);
static void on_theme_toggle_clicked(GtkButton *button, gpointer user_data);
static void on_scan_clicked(GtkButton *button, gpointer user_data);
static void on_settings_clicked(GtkButton *button, gpointer user_data);
static void on_unmatched_clicked(GtkButton *button, gpointer user_data);

void window_create(ReelApp *app) {
  /* Get DPI scale factor from GDK */
  GdkScreen *screen = gdk_screen_get_default();
  if (screen) {
    gdouble dpi = gdk_screen_get_resolution(screen);
    app->scale_factor = (dpi > 0) ? dpi / 96.0 : 1.0;
    /* Clamp to reasonable range */
    if (app->scale_factor < 1.0)
      app->scale_factor = 1.0;
    if (app->scale_factor > 3.0)
      app->scale_factor = 3.0;
  } else {
    app->scale_factor = 1.0;
  }
  g_print("DPI scale factor: %.2f\n", app->scale_factor);

  /* Create main window */
  app->window = gtk_application_window_new(app->app);
  gtk_window_set_title(GTK_WINDOW(app->window), APP_NAME);
  gtk_window_set_default_size(GTK_WINDOW(app->window), 1200, 800);
  gtk_window_set_position(GTK_WINDOW(app->window), GTK_WIN_POS_CENTER);

  /* Main vertical box */
  GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add(GTK_CONTAINER(app->window), main_box);

  /* Header bar */
  GtkWidget *header = gtk_header_bar_new();
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(header), APP_NAME);
  gtk_window_set_titlebar(GTK_WINDOW(app->window), header);

  /* Header bar buttons - left side */
  GtkWidget *scan_btn = gtk_button_new_from_icon_name("view-refresh-symbolic",
                                                      GTK_ICON_SIZE_BUTTON);
  gtk_widget_set_tooltip_text(scan_btn, "Scan Library");
  g_signal_connect(scan_btn, "clicked", G_CALLBACK(on_scan_clicked), app);
  gtk_header_bar_pack_start(GTK_HEADER_BAR(header), scan_btn);

  GtkWidget *unmatched_btn = gtk_button_new_from_icon_name(
      "dialog-question-symbolic", GTK_ICON_SIZE_BUTTON);
  gtk_widget_set_tooltip_text(unmatched_btn, "View Unmatched Films");
  g_signal_connect(unmatched_btn, "clicked", G_CALLBACK(on_unmatched_clicked),
                   app);
  gtk_header_bar_pack_start(GTK_HEADER_BAR(header), unmatched_btn);

  /* Header bar buttons - right side */
  GtkWidget *theme_btn = gtk_button_new_from_icon_name(
      "weather-clear-night-symbolic", GTK_ICON_SIZE_BUTTON);
  gtk_widget_set_tooltip_text(theme_btn, "Toggle Dark/Light Theme");
  g_signal_connect(theme_btn, "clicked", G_CALLBACK(on_theme_toggle_clicked),
                   app);
  gtk_header_bar_pack_end(GTK_HEADER_BAR(header), theme_btn);

  GtkWidget *settings_btn = gtk_button_new_from_icon_name(
      "emblem-system-symbolic", GTK_ICON_SIZE_BUTTON);
  gtk_widget_set_tooltip_text(settings_btn, "Settings");
  g_signal_connect(settings_btn, "clicked", G_CALLBACK(on_settings_clicked),
                   app);
  gtk_header_bar_pack_end(GTK_HEADER_BAR(header), settings_btn);

  /* Filter bar */
  app->filter_bar = filter_bar_create(app);
  gtk_box_pack_start(GTK_BOX(main_box), app->filter_bar, FALSE, FALSE, 0);

  /* Scrolled window for poster grid */
  GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start(GTK_BOX(main_box), scrolled, TRUE, TRUE, 0);

  /* Poster grid */
  app->grid_view = grid_create(app);
  gtk_container_add(GTK_CONTAINER(scrolled), app->grid_view);

  /* Status bar */
  app->status_bar = gtk_statusbar_new();
  gtk_box_pack_end(GTK_BOX(main_box), app->status_bar, FALSE, FALSE, 0);

  /* Initial load */
  window_refresh_films(app);
  window_update_status_bar(app);

  /* Apply theme CSS */
  apply_theme_css(app);
}

/* Apply comprehensive dark/light theme CSS */
static void apply_theme_css(ReelApp *app) {
  GtkCssProvider *css = gtk_css_provider_new();

  /* Calculate scaled font sizes */
  gint base_font = (gint)(11 * app->scale_factor);
  gint small_font = (gint)(10 * app->scale_factor);
  gint badge_font = (gint)(10 * app->scale_factor);

  gchar *css_str = g_strdup_printf(
      /* ===== LIGHT/DARK ADAPTIVE THEME ===== */

      /* Poster grid items */
      "flowbox > flowboxchild {"
      "    margin: 8px;"
      "    padding: 8px;"
      "    border-radius: 8px;"
      "    transition: all 200ms ease;"
      "}"
      "flowbox > flowboxchild:hover {"
      "    background-color: alpha(@theme_selected_bg_color, 0.15);"
      "    box-shadow: 0 2px 8px alpha(black, 0.15);"
      "}"
      "flowbox > flowboxchild:selected {"
      "    background-color: alpha(@theme_selected_bg_color, 0.3);"
      "}"

      /* Poster title - DPI-aware sizing */
      ".poster-title {"
      "    font-size: %dpt;"
      "    font-weight: 600;"
      "    margin-top: 6px;"
      "    color: @theme_fg_color;"
      "}"

      /* Poster year - DPI-aware sizing */
      ".poster-year {"
      "    font-size: %dpt;"
      "    color: alpha(@theme_fg_color, 0.6);"
      "}"

      /* Unmatched badge */
      ".unmatched-badge {"
      "    background-color: #e67e22;"
      "    color: white;"
      "    border-radius: 12px;"
      "    padding: 4px 8px;"
      "    font-size: %dpt;"
      "    font-weight: bold;"
      "    box-shadow: 0 1px 3px alpha(black, 0.3);"
      "}"

      /* Status bar styling */
      "statusbar {"
      "    padding: 4px 12px;"
      "    font-size: %dpt;"
      "}"

      /* Filter bar styling */
      ".filter-bar {"
      "    padding: 8px 12px;"
      "    background-color: alpha(@theme_bg_color, 0.97);"
      "    border-bottom: 1px solid alpha(@theme_fg_color, 0.1);"
      "}"
      ".filter-bar entry {"
      "    min-width: 200px;"
      "}"

      /* ===== DARK THEME SPECIFIC ===== */
      ".dark-theme {"
      "    background-color: #1e1e2e;"
      "    color: #cdd6f4;"
      "}"
      ".dark-theme headerbar {"
      "    background-color: #313244;"
      "    color: #cdd6f4;"
      "}"
      ".dark-theme .poster-title {"
      "    color: #cdd6f4;"
      "}"
      ".dark-theme .poster-year {"
      "    color: #a6adc8;"
      "}"
      ".dark-theme flowbox > flowboxchild:hover {"
      "    background-color: alpha(#89b4fa, 0.15);"
      "}"
      ".dark-theme flowbox > flowboxchild:selected {"
      "    background-color: alpha(#89b4fa, 0.25);"
      "}"
      ".dark-theme statusbar {"
      "    background-color: #313244;"
      "    color: #bac2de;"
      "}"

      /* ===== LIGHT THEME SPECIFIC ===== */
      ".light-theme {"
      "    background-color: #eff1f5;"
      "    color: #4c4f69;"
      "}"
      ".light-theme headerbar {"
      "    background-color: #dce0e8;"
      "    color: #4c4f69;"
      "}"
      ".light-theme .poster-title {"
      "    color: #4c4f69;"
      "}"
      ".light-theme .poster-year {"
      "    color: #6c6f85;"
      "}"
      ".light-theme flowbox > flowboxchild:hover {"
      "    background-color: alpha(#1e66f5, 0.12);"
      "}"
      ".light-theme flowbox > flowboxchild:selected {"
      "    background-color: alpha(#1e66f5, 0.2);"
      "}"
      ".light-theme statusbar {"
      "    background-color: #dce0e8;"
      "    color: #5c5f77;"
      "}",
      base_font, small_font, badge_font, small_font);

  gtk_css_provider_load_from_data(css, css_str, -1, NULL);
  g_free(css_str);

  gtk_style_context_add_provider_for_screen(
      gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(css);

  /* Apply initial theme class based on preference */
  GtkStyleContext *ctx = gtk_widget_get_style_context(app->window);
  if (app->theme_preference == THEME_DARK) {
    gtk_style_context_add_class(ctx, "dark-theme");
    gtk_style_context_remove_class(ctx, "light-theme");
  } else if (app->theme_preference == THEME_LIGHT) {
    gtk_style_context_add_class(ctx, "light-theme");
    gtk_style_context_remove_class(ctx, "dark-theme");
  }
  /* THEME_SYSTEM: let GTK handle it */
}

/* Theme toggle callback */
static void on_theme_toggle_clicked(GtkButton *button, gpointer user_data) {
  (void)button;
  ReelApp *app = (ReelApp *)user_data;

  GtkStyleContext *ctx = gtk_widget_get_style_context(app->window);

  /* Cycle through: System -> Dark -> Light -> System */
  if (app->theme_preference == THEME_SYSTEM) {
    app->theme_preference = THEME_DARK;
    gtk_style_context_add_class(ctx, "dark-theme");
    gtk_style_context_remove_class(ctx, "light-theme");

    /* Also tell GTK to prefer dark */
    g_object_set(gtk_settings_get_default(),
                 "gtk-application-prefer-dark-theme", TRUE, NULL);
  } else if (app->theme_preference == THEME_DARK) {
    app->theme_preference = THEME_LIGHT;
    gtk_style_context_add_class(ctx, "light-theme");
    gtk_style_context_remove_class(ctx, "dark-theme");

    g_object_set(gtk_settings_get_default(),
                 "gtk-application-prefer-dark-theme", FALSE, NULL);
  } else {
    app->theme_preference = THEME_SYSTEM;
    gtk_style_context_remove_class(ctx, "dark-theme");
    gtk_style_context_remove_class(ctx, "light-theme");
  }
}

void window_refresh_films(ReelApp *app) {
  /* Free existing list */
  if (app->films) {
    g_list_free_full(app->films, (GDestroyNotify)film_free);
    app->films = NULL;
  }

  /* Load from database with current filter */
  app->films = db_films_get_all(app, &app->filter);

  /* Update grid */
  grid_populate(app);

  /* Update counts */
  app->total_films = db_films_count(app);
  app->unmatched_films = db_films_count_unmatched(app);

  window_update_status_bar(app);
}

void window_update_status_bar(ReelApp *app) {
  gchar *status = g_strdup_printf("%d films | %d unmatched", app->total_films,
                                  app->unmatched_films);
  gtk_statusbar_pop(GTK_STATUSBAR(app->status_bar), 0);
  gtk_statusbar_push(GTK_STATUSBAR(app->status_bar), 0, status);
  g_free(status);
}

static void on_scan_clicked(GtkButton *button, gpointer user_data) {
  ReelApp *app = (ReelApp *)user_data;
  (void)button;

  if (app->library_paths == NULL || app->library_paths_count == 0) {
    GtkWidget *dialog =
        gtk_message_dialog_new(GTK_WINDOW(app->window), GTK_DIALOG_MODAL,
                               GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                               "No library paths configured.\nGo to Settings "
                               "to add your film directories.");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return;
  }

  window_scan_library(app);
}

static void on_settings_clicked(GtkButton *button, gpointer user_data) {
  ReelApp *app = (ReelApp *)user_data;
  (void)button;
  window_show_settings(app);
}

static void on_unmatched_clicked(GtkButton *button, gpointer user_data) {
  ReelApp *app = (ReelApp *)user_data;
  (void)button;
  window_show_unmatched(app);
}

/* Callback for add folder button in settings */
static void on_add_folder_clicked(GtkButton *btn, gpointer user_data) {
  GtkWidget *settings_dialog = GTK_WIDGET(user_data);
  ReelApp *app = g_object_get_data(G_OBJECT(settings_dialog), "app");
  GtkWidget *lib_list =
      g_object_get_data(G_OBJECT(settings_dialog), "lib_list");

  GtkWidget *chooser = gtk_file_chooser_dialog_new(
      "Select Film Directory", GTK_WINDOW(settings_dialog),
      GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Cancel", GTK_RESPONSE_CANCEL,
      "_Select", GTK_RESPONSE_ACCEPT, NULL);

  if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
    gchar *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
    config_add_library_path(app, folder);

    GtkWidget *row = gtk_label_new(folder);
    gtk_label_set_xalign(GTK_LABEL(row), 0);
    gtk_widget_show(row);
    gtk_list_box_insert(GTK_LIST_BOX(lib_list), row, -1);

    g_free(folder);
  }

  gtk_widget_destroy(chooser);
  (void)btn;
}

void window_show_settings(ReelApp *app) {
  GtkWidget *dialog = gtk_dialog_new_with_buttons(
      "Settings", GTK_WINDOW(app->window),
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel",
      GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);

  gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 400);

  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  gtk_container_set_border_width(GTK_CONTAINER(content), 12);
  gtk_box_set_spacing(GTK_BOX(content), 12);

  /* TMDB API Key */
  GtkWidget *api_frame = gtk_frame_new("TMDB API Key");
  gtk_box_pack_start(GTK_BOX(content), api_frame, FALSE, FALSE, 0);

  GtkWidget *api_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(api_entry),
                                 "Enter your TMDB API key");
  if (app->tmdb_api_key) {
    gtk_entry_set_text(GTK_ENTRY(api_entry), app->tmdb_api_key);
  }
  gtk_container_add(GTK_CONTAINER(api_frame), api_entry);
  gtk_container_set_border_width(GTK_CONTAINER(api_frame), 6);

  /* Player Command */
  GtkWidget *player_frame = gtk_frame_new("Video Player");
  gtk_box_pack_start(GTK_BOX(content), player_frame, FALSE, FALSE, 0);

  GtkWidget *player_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(
      GTK_ENTRY(player_entry),
      "Command to launch video player (e.g., mpv, vlc)");
  if (app->player_command) {
    gtk_entry_set_text(GTK_ENTRY(player_entry), app->player_command);
  }
  gtk_container_add(GTK_CONTAINER(player_frame), player_entry);
  gtk_container_set_border_width(GTK_CONTAINER(player_frame), 6);

  /* Library Paths */
  GtkWidget *lib_frame = gtk_frame_new("Library Paths");
  gtk_box_pack_start(GTK_BOX(content), lib_frame, TRUE, TRUE, 0);

  GtkWidget *lib_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_container_add(GTK_CONTAINER(lib_frame), lib_box);
  gtk_container_set_border_width(GTK_CONTAINER(lib_frame), 6);

  GtkWidget *lib_scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(lib_scroll),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start(GTK_BOX(lib_box), lib_scroll, TRUE, TRUE, 0);

  GtkWidget *lib_list = gtk_list_box_new();
  gtk_container_add(GTK_CONTAINER(lib_scroll), lib_list);

  /* Populate library paths */
  for (int i = 0; i < app->library_paths_count; i++) {
    GtkWidget *row = gtk_label_new(app->library_paths[i]);
    gtk_label_set_xalign(GTK_LABEL(row), 0);
    gtk_list_box_insert(GTK_LIST_BOX(lib_list), row, -1);
  }

  /* Add/Remove buttons */
  GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_pack_start(GTK_BOX(lib_box), btn_box, FALSE, FALSE, 0);

  GtkWidget *add_btn = gtk_button_new_with_label("Add Folder...");
  gtk_box_pack_start(GTK_BOX(btn_box), add_btn, FALSE, FALSE, 0);

  /* Store references for callback */
  g_object_set_data(G_OBJECT(dialog), "api_entry", api_entry);
  g_object_set_data(G_OBJECT(dialog), "player_entry", player_entry);
  g_object_set_data(G_OBJECT(dialog), "lib_list", lib_list);
  g_object_set_data(G_OBJECT(dialog), "app", app);

  /* Add folder callback */
  g_signal_connect(add_btn, "clicked", G_CALLBACK(on_add_folder_clicked),
                   dialog);

  gtk_widget_show_all(dialog);

  if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
    /* Save settings */
    const gchar *api_text = gtk_entry_get_text(GTK_ENTRY(api_entry));
    const gchar *player_text = gtk_entry_get_text(GTK_ENTRY(player_entry));

    if (api_text && strlen(api_text) > 0) {
      g_free(app->tmdb_api_key);
      app->tmdb_api_key = g_strdup(api_text);
    }

    if (player_text && strlen(player_text) > 0) {
      g_free(app->player_command);
      app->player_command = g_strdup(player_text);
    }

    config_save(app);
  }

  gtk_widget_destroy(dialog);
}

void window_scan_library(ReelApp *app) {
  /* Show progress dialog */
  GtkWidget *dialog = gtk_message_dialog_new(
      GTK_WINDOW(app->window), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
      GTK_BUTTONS_NONE, "Scanning library...");
  gtk_widget_show(dialog);

  /* Process events to show dialog */
  while (gtk_events_pending()) {
    gtk_main_iteration();
  }

  /* Scan directories */
  int new_films = 0;
  for (int i = 0; i < app->library_paths_count; i++) {
    new_films += scanner_scan_directory(app, app->library_paths[i]);
  }

  gtk_widget_destroy(dialog);

  /* Show result */
  GtkWidget *result = gtk_message_dialog_new(
      GTK_WINDOW(app->window), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
      GTK_BUTTONS_OK,
      "Scan complete.\nFound %d new films.\n\nUse the unmatched button to "
      "review films that need matching.",
      new_films);
  gtk_dialog_run(GTK_DIALOG(result));
  gtk_widget_destroy(result);

  /* Refresh display */
  window_refresh_films(app);

  /* Start scraping in background if we have an API key */
  if (app->tmdb_api_key && strlen(app->tmdb_api_key) > 0 && new_films > 0) {
    scraper_start_background(app);
  }
}

/* Callback for match button in unmatched list */
static void on_match_film_clicked(GtkButton *btn, gpointer user_data) {
  (void)user_data;
  ReelApp *app = g_object_get_data(G_OBJECT(btn), "app");
  gint64 film_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "film_id"));
  /* TODO: Open match dialog */
  g_print("Match film ID: %ld\n", film_id);
  (void)app;
}

void window_show_unmatched(ReelApp *app) {
  GList *unmatched = db_films_get_unmatched(app);
  gint count = g_list_length(unmatched);

  if (count == 0) {
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(app->window), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK, "All films are matched!");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_list_free_full(unmatched, (GDestroyNotify)film_free);
    return;
  }

  /* Create unmatched films dialog */
  GtkWidget *dialog = gtk_dialog_new_with_buttons(
      "Unmatched Films", GTK_WINDOW(app->window),
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Close",
      GTK_RESPONSE_CLOSE, NULL);

  gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 400);

  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

  GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 0);

  GtkWidget *list = gtk_list_box_new();
  gtk_container_add(GTK_CONTAINER(scroll), list);

  for (GList *l = unmatched; l != NULL; l = l->next) {
    Film *film = (Film *)l->data;
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(row), 6);

    GtkWidget *label = gtk_label_new(film->file_path);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_START);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_pack_start(GTK_BOX(row), label, TRUE, TRUE, 0);

    GtkWidget *match_btn = gtk_button_new_with_label("Match...");
    g_object_set_data(G_OBJECT(match_btn), "film_id",
                      GINT_TO_POINTER(film->id));
    g_object_set_data(G_OBJECT(match_btn), "app", app);
    g_object_set_data(G_OBJECT(match_btn), "parent_dialog", dialog);

    g_signal_connect(match_btn, "clicked", G_CALLBACK(on_match_film_clicked),
                     NULL);

    gtk_box_pack_end(GTK_BOX(row), match_btn, FALSE, FALSE, 0);

    gtk_list_box_insert(GTK_LIST_BOX(list), row, -1);
  }

  gtk_widget_show_all(dialog);
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);

  g_list_free_full(unmatched, (GDestroyNotify)film_free);

  /* Refresh in case anything changed */
  window_refresh_films(app);
}
