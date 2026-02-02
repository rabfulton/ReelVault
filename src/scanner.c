/*
 * ReelGTK - Directory Scanner
 * Scans directories for video files and parses filenames
 */

#include "scanner.h"
#include "db.h"
#include "utils.h"
#include <ctype.h>
#include <string.h>

/* Supported video extensions */
static const char *VIDEO_EXTENSIONS[] = {
    ".mkv", ".mp4", ".avi", ".mov", ".m4v", ".wmv", ".flv", ".webm", NULL};

static gboolean is_video_file(const gchar *filename) {
  const gchar *ext = strrchr(filename, '.');
  if (ext == NULL)
    return FALSE;

  for (int i = 0; VIDEO_EXTENSIONS[i] != NULL; i++) {
    if (g_ascii_strcasecmp(ext, VIDEO_EXTENSIONS[i]) == 0) {
      return TRUE;
    }
  }
  return FALSE;
}

gboolean scanner_parse_filename(const gchar *filename, gchar **title,
                                gint *year) {
  *title = NULL;
  *year = 0;

  /* Get basename without extension */
  gchar *basename = g_path_get_basename(filename);
  gchar *dot = strrchr(basename, '.');
  if (dot)
    *dot = '\0';

  /* Try to find year in various formats */
  GRegex *year_regex =
      g_regex_new("(?:^|[._ \\[\\(])([12][0-9]{3})(?:[._ \\]\\)]|$)",
                  G_REGEX_CASELESS, 0, NULL);

  GMatchInfo *match_info;
  if (g_regex_match(year_regex, basename, 0, &match_info)) {
    gchar *year_str = g_match_info_fetch(match_info, 1);
    *year = atoi(year_str);
    g_free(year_str);

    /* Get position of year match to truncate title */
    gint start_pos, end_pos;
    g_match_info_fetch_pos(match_info, 0, &start_pos, &end_pos);

    if (start_pos > 0) {
      gchar *title_part = g_strndup(basename, start_pos);
      *title = utils_normalize_title(title_part);
      g_free(title_part);
    }
  }
  g_match_info_free(match_info);
  g_regex_unref(year_regex);

  /* If no title extracted, use cleaned basename */
  if (*title == NULL) {
    *title = utils_normalize_title(basename);
  }

  g_free(basename);
  return (*title != NULL);
}

static gint scan_directory_recursive(ReelApp *app, const gchar *path,
                                     gint depth) {
  if (depth > 10)
    return 0; /* Prevent infinite recursion */

  GDir *dir = g_dir_open(path, 0, NULL);
  if (dir == NULL)
    return 0;

  gint added = 0;
  const gchar *name;

  while ((name = g_dir_read_name(dir)) != NULL) {
    /* Skip hidden files */
    if (name[0] == '.')
      continue;

    gchar *full_path = g_build_filename(path, name, NULL);

    if (g_file_test(full_path, G_FILE_TEST_IS_DIR)) {
      /* Recurse into subdirectory */
      added += scan_directory_recursive(app, full_path, depth + 1);
    } else if (is_video_file(name)) {
      /* Check if already in database */
      Film *existing = db_film_get_by_path(app, full_path);
      if (existing) {
        film_free(existing);
        g_free(full_path);
        continue;
      }

      /* Parse filename */
      gchar *title = NULL;
      gint year = 0;
      scanner_parse_filename(name, &title, &year);

      /* Create film entry */
      Film *film = film_new();
      film->file_path = g_strdup(full_path);
      film->title = title; /* Takes ownership */
      film->year = year;
      film->added_date = g_get_real_time() / 1000000;
      film->match_status = MATCH_STATUS_UNMATCHED;

      if (db_film_insert(app, film)) {
        added++;
        g_print("Added: %s\n", full_path);
      }

      film_free(film);
    }

    g_free(full_path);
  }

  g_dir_close(dir);
  return added;
}

gint scanner_scan_directory(ReelApp *app, const gchar *path) {
  g_print("Scanning: %s\n", path);
  return scan_directory_recursive(app, path, 0);
}
