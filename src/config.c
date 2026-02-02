/*
 * ReelGTK - Configuration
 * INI-style config file handling
 */

#include "config.h"
#include <stdio.h>
#include <string.h>

gboolean config_load(ReelApp *app) {
  GKeyFile *keyfile = g_key_file_new();
  GError *error = NULL;

  if (!g_key_file_load_from_file(keyfile, app->config_path, G_KEY_FILE_NONE,
                                 &error)) {
    g_print("Config file not found or invalid: %s\n", error->message);
    g_error_free(error);
    g_key_file_free(keyfile);
    return FALSE;
  }

  /* TMDB API key */
  gchar *api_key = g_key_file_get_string(keyfile, "tmdb", "api_key", NULL);
  if (api_key) {
    g_free(app->tmdb_api_key);
    app->tmdb_api_key = api_key;
  }

  /* Player command */
  gchar *player = g_key_file_get_string(keyfile, "player", "command", NULL);
  if (player) {
    g_free(app->player_command);
    app->player_command = player;
  }

  /* Library paths */
  gchar *paths_str = g_key_file_get_string(keyfile, "library", "paths", NULL);
  if (paths_str && strlen(paths_str) > 0) {
    if (app->library_paths) {
      g_strfreev(app->library_paths);
    }
    app->library_paths = g_strsplit(paths_str, ";", -1);
    app->library_paths_count = g_strv_length(app->library_paths);
    g_free(paths_str);
  }

  g_key_file_free(keyfile);
  g_print("Configuration loaded from: %s\n", app->config_path);
  return TRUE;
}

gboolean config_save(ReelApp *app) {
  GKeyFile *keyfile = g_key_file_new();

  /* TMDB API key */
  if (app->tmdb_api_key) {
    g_key_file_set_string(keyfile, "tmdb", "api_key", app->tmdb_api_key);
  }

  /* Player command */
  if (app->player_command) {
    g_key_file_set_string(keyfile, "player", "command", app->player_command);
  }

  /* Library paths */
  if (app->library_paths && app->library_paths_count > 0) {
    gchar *paths_str = g_strjoinv(";", app->library_paths);
    g_key_file_set_string(keyfile, "library", "paths", paths_str);
    g_free(paths_str);
  }

  /* Write to file */
  GError *error = NULL;
  gchar *data = g_key_file_to_data(keyfile, NULL, NULL);

  if (!g_file_set_contents(app->config_path, data, -1, &error)) {
    g_printerr("Failed to save config: %s\n", error->message);
    g_error_free(error);
    g_free(data);
    g_key_file_free(keyfile);
    return FALSE;
  }

  g_free(data);
  g_key_file_free(keyfile);
  g_print("Configuration saved to: %s\n", app->config_path);
  return TRUE;
}

void config_set_api_key(ReelApp *app, const gchar *api_key) {
  g_free(app->tmdb_api_key);
  app->tmdb_api_key = g_strdup(api_key);
}

void config_set_player_command(ReelApp *app, const gchar *command) {
  g_free(app->player_command);
  app->player_command = g_strdup(command);
}

void config_add_library_path(ReelApp *app, const gchar *path) {
  /* Check if already exists */
  for (int i = 0; i < app->library_paths_count; i++) {
    if (g_strcmp0(app->library_paths[i], path) == 0) {
      return;
    }
  }

  /* Add to array */
  gchar **new_paths = g_new0(gchar *, app->library_paths_count + 2);
  for (int i = 0; i < app->library_paths_count; i++) {
    new_paths[i] = g_strdup(app->library_paths[i]);
  }
  new_paths[app->library_paths_count] = g_strdup(path);

  if (app->library_paths) {
    g_strfreev(app->library_paths);
  }
  app->library_paths = new_paths;
  app->library_paths_count++;
}

void config_remove_library_path(ReelApp *app, const gchar *path) {
  if (app->library_paths == NULL || app->library_paths_count == 0)
    return;

  gint remove_idx = -1;
  for (int i = 0; i < app->library_paths_count; i++) {
    if (g_strcmp0(app->library_paths[i], path) == 0) {
      remove_idx = i;
      break;
    }
  }

  if (remove_idx < 0)
    return;

  /* Create new array without the removed path */
  gchar **new_paths = g_new0(gchar *, app->library_paths_count);
  int j = 0;
  for (int i = 0; i < app->library_paths_count; i++) {
    if (i != remove_idx) {
      new_paths[j++] = g_strdup(app->library_paths[i]);
    }
  }

  g_strfreev(app->library_paths);
  app->library_paths = new_paths;
  app->library_paths_count--;
}
