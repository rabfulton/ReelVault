/*
 * ReelGTK - Film Collection Browser
 * Main entry point
 */

#include "app.h"
#include "config.h"
#include "db.h"
#include "window.h"
#include <gtk/gtk.h>
#include <locale.h>

static void on_activate(GtkApplication *gtk_app, gpointer user_data) {
  ReelApp *app = (ReelApp *)user_data;

  /* Initialize paths */
  if (!reel_app_init_paths(app)) {
    g_printerr("Failed to initialize application paths\n");
    return;
  }

  /* Load configuration */
  if (!config_load(app)) {
    g_print("No configuration found, will prompt for setup\n");
  }

  /* Initialize database */
  if (!db_init(app)) {
    GtkWidget *dialog = gtk_message_dialog_new(
        NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
        "Failed to initialize database at:\n%s", app->db_path);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return;
  }

  /* Create and show main window */
  window_create(app);
  gtk_widget_show_all(app->window);

  /* Check if first run (no API key) */
  if (app->tmdb_api_key == NULL || strlen(app->tmdb_api_key) == 0) {
    /* TODO: Show first-run setup dialog */
    g_print("First run detected - setup required\n");
  }
}

static void on_shutdown(GtkApplication *gtk_app, gpointer user_data) {
  ReelApp *app = (ReelApp *)user_data;

  /* Close database */
  db_close(app);

  /* Save configuration */
  config_save(app);
}

int main(int argc, char *argv[]) {
  setlocale(LC_ALL, "");
  g_set_prgname("reelvault");
  g_set_application_name(APP_NAME);

  /* Create application state */
  ReelApp *app = reel_app_new();
  if (app == NULL) {
    g_printerr("Failed to create application\n");
    return 1;
  }

  /* Create GTK application */
  app->app = gtk_application_new(APP_ID, G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app->app, "activate", G_CALLBACK(on_activate), app);
  g_signal_connect(app->app, "shutdown", G_CALLBACK(on_shutdown), app);

  /* Run application */
  int status = g_application_run(G_APPLICATION(app->app), argc, argv);

  /* Cleanup */
  g_object_unref(app->app);
  reel_app_free(app);

  return status;
}

/* Application state management */

ReelApp *reel_app_new(void) {
  ReelApp *app = g_new0(ReelApp, 1);
  filter_state_init(&app->filter);
  app->player_command = g_strdup("xdg-open");
  return app;
}

void reel_app_free(ReelApp *app) {
  if (app == NULL)
    return;

  filter_state_clear(&app->filter);

  g_free(app->db_path);
  g_free(app->config_path);
  g_free(app->cache_path);
  g_free(app->poster_cache_path);
  g_free(app->tmdb_api_key);
  g_free(app->player_command);
  g_free(app->gtk_theme_name);
  g_free(app->system_gtk_theme_name);

  if (app->library_paths) {
    g_strfreev(app->library_paths);
  }

  /* Free film list */
  g_list_free_full(app->films, (GDestroyNotify)film_free);

  if (app->thread_pool) {
    g_thread_pool_free(app->thread_pool, TRUE, FALSE);
  }
  if (app->ui_queue) {
    g_async_queue_unref(app->ui_queue);
  }

  g_free(app);
}

gboolean reel_app_init_paths(ReelApp *app) {
  const gchar *config_home = g_get_user_config_dir();
  const gchar *cache_home = g_get_user_cache_dir();

  /* Config directory (with backward-compat fallback for previous app name) */
  gchar *config_dir = g_build_filename(config_home, CONFIG_DIR_NAME, NULL);
  gchar *old_config_dir = g_build_filename(config_home, "reelgtk", NULL);

  gchar *candidate_config = g_build_filename(config_dir, CONFIG_FILENAME, NULL);
  gchar *candidate_db = g_build_filename(config_dir, DB_FILENAME, NULL);
  gchar *old_config = g_build_filename(old_config_dir, CONFIG_FILENAME, NULL);
  gchar *old_db = g_build_filename(old_config_dir, DB_FILENAME, NULL);

  gboolean have_new = g_file_test(candidate_db, G_FILE_TEST_EXISTS) ||
                      g_file_test(candidate_config, G_FILE_TEST_EXISTS);
  gboolean have_old =
      g_file_test(old_db, G_FILE_TEST_EXISTS) ||
      g_file_test(old_config, G_FILE_TEST_EXISTS);

  const gchar *use_dir = config_dir;
  if (!have_new && have_old) {
    use_dir = old_config_dir;
  } else {
    if (g_mkdir_with_parents(config_dir, 0755) != 0) {
      g_printerr("Failed to create config directory: %s\n", config_dir);
      g_free(config_dir);
      g_free(old_config_dir);
      g_free(candidate_config);
      g_free(candidate_db);
      g_free(old_config);
      g_free(old_db);
      return FALSE;
    }
  }

  app->config_path = g_build_filename(use_dir, CONFIG_FILENAME, NULL);
  app->db_path = g_build_filename(use_dir, DB_FILENAME, NULL);

  g_free(candidate_config);
  g_free(candidate_db);
  g_free(old_config);
  g_free(old_db);
  g_free(config_dir);
  g_free(old_config_dir);

  /* Cache directory */
  gchar *cache_dir = g_build_filename(cache_home, CACHE_DIR_NAME, NULL);
  gchar *old_cache_dir = g_build_filename(cache_home, "reelgtk", NULL);

  const gchar *use_cache_dir = cache_dir;
  if (!g_file_test(cache_dir, G_FILE_TEST_IS_DIR) &&
      g_file_test(old_cache_dir, G_FILE_TEST_IS_DIR)) {
    use_cache_dir = old_cache_dir;
  } else {
    if (g_mkdir_with_parents(cache_dir, 0755) != 0) {
      g_printerr("Failed to create cache directory: %s\n", cache_dir);
      g_free(cache_dir);
      g_free(old_cache_dir);
      return FALSE;
    }
  }

  app->cache_path = g_strdup(use_cache_dir);
  if (g_mkdir_with_parents(app->cache_path, 0755) != 0) {
    g_printerr("Failed to create cache directory: %s\n", app->cache_path);
    g_free(cache_dir);
    g_free(old_cache_dir);
    return FALSE;
  }
  g_free(cache_dir);
  g_free(old_cache_dir);

  /* Poster cache directory */
  app->poster_cache_path = g_build_filename(app->cache_path, "posters", NULL);
  if (g_mkdir_with_parents(app->poster_cache_path, 0755) != 0) {
    g_printerr("Failed to create poster cache: %s\n", app->poster_cache_path);
    return FALSE;
  }

  return TRUE;
}

/* Film memory management */

Film *film_new(void) { return g_new0(Film, 1); }

void film_free(Film *film) {
  if (film == NULL)
    return;

  g_free(film->file_path);
  g_free(film->title);
  g_free(film->plot);
  g_free(film->poster_path);
  g_free(film->imdb_id);

  if (film->poster_pixbuf) {
    g_object_unref(film->poster_pixbuf);
  }

  g_free(film);
}

Film *film_copy(const Film *film) {
  if (film == NULL)
    return NULL;

  Film *copy = film_new();
  copy->id = film->id;
  copy->file_path = g_strdup(film->file_path);
  copy->title = g_strdup(film->title);
  copy->year = film->year;
  copy->runtime_minutes = film->runtime_minutes;
  copy->plot = g_strdup(film->plot);
  copy->poster_path = g_strdup(film->poster_path);
  copy->tmdb_id = film->tmdb_id;
  copy->imdb_id = g_strdup(film->imdb_id);
  copy->rating = film->rating;
  copy->added_date = film->added_date;
  copy->match_status = film->match_status;
  copy->media_type = film->media_type;
  copy->season_number = film->season_number;

  if (film->poster_pixbuf) {
    copy->poster_pixbuf = g_object_ref(film->poster_pixbuf);
  }

  return copy;
}

Episode *episode_new(void) { return g_new0(Episode, 1); }

void episode_free(Episode *episode) {
  if (episode == NULL)
    return;

  g_free(episode->title);
  g_free(episode->file_path);
  g_free(episode->plot);
  g_free(episode->air_date);

  g_free(episode);
}

/* Filter state */

void filter_state_init(FilterState *filter) {
  memset(filter, 0, sizeof(FilterState));
  filter->sort_by = g_strdup("title");
  filter->sort_ascending = TRUE;
}

void filter_state_clear(FilterState *filter) {
  g_free(filter->genre);
  g_free(filter->actor);
  g_free(filter->director);
  g_free(filter->search_text);
  g_free(filter->plot_text);
  g_free(filter->sort_by);
}
