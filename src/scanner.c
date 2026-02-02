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

/* TV Series detection */
static gboolean is_season_directory(const gchar *name, gint *season_number) {
  if (g_ascii_strncasecmp(name, "Season", 6) == 0 ||
      g_ascii_strncasecmp(name, "Series", 6) == 0) {
    /* Extract number */
    const char *p = name + 6;
    while (*p && !isdigit(*p))
      p++;
    if (*p) {
      *season_number = atoi(p);
      return TRUE;
    }
  } else if (g_ascii_toupper(name[0]) == 'S') {
    /* Common shorthand: S01, S1, S 01, S-01, S_01 */
    const char *p = name + 1;
    while (*p && (*p == ' ' || *p == '-' || *p == '_' || *p == '.'))
      p++;
    if (isdigit(*p)) {
      *season_number = atoi(p);
      return TRUE;
    }
  } else if (g_ascii_strcasecmp(name, "Specials") == 0) {
    *season_number = 0;
    return TRUE;
  }
  return FALSE;
}

static gboolean detect_season_from_episode_filenames(const gchar *path,
                                                     gint *season_number) {
  *season_number = 0;

  GDir *dir = g_dir_open(path, 0, NULL);
  if (!dir)
    return FALSE;

  GRegex *re = g_regex_new("[Ss](\\d{1,2})[Ee](\\d{1,2})", 0, 0, NULL);
  gboolean found = FALSE;
  gint season = 0;

  const gchar *name;
  while ((name = g_dir_read_name(dir)) != NULL) {
    if (name[0] == '.')
      continue;
    if (!is_video_file(name))
      continue;

    GMatchInfo *match_info = NULL;
    if (g_regex_match(re, name, 0, &match_info)) {
      gchar *season_str = g_match_info_fetch(match_info, 1);
      gint s = atoi(season_str);
      g_free(season_str);

      if (!found) {
        found = TRUE;
        season = s;
      } else if (s != season) {
        /* Avoid mis-grouping multi-season folders for now. */
        found = FALSE;
        break;
      }
    }
    if (match_info)
      g_match_info_free(match_info);
  }

  g_dir_close(dir);
  g_regex_unref(re);

  if (found && season >= 0) {
    *season_number = season;
    return TRUE;
  }
  return FALSE;
}

static gboolean parse_sxxeyy_from_filename(const gchar *name, gint *season_number,
                                          gint *episode_number) {
  if (!name) {
    *season_number = 0;
    *episode_number = 0;
    return FALSE;
  }

  GRegex *re = g_regex_new("[Ss](\\d{1,2})[Ee](\\d{1,2})", 0, 0, NULL);
  GMatchInfo *match_info = NULL;
  gboolean matched = g_regex_match(re, name, 0, &match_info);

  if (!matched) {
    if (match_info)
      g_match_info_free(match_info);
    g_regex_unref(re);
    *season_number = 0;
    *episode_number = 0;
    return FALSE;
  }

  gchar *season_str = g_match_info_fetch(match_info, 1);
  gchar *episode_str = g_match_info_fetch(match_info, 2);
  *season_number = atoi(season_str);
  *episode_number = atoi(episode_str);
  g_free(season_str);
  g_free(episode_str);

  g_match_info_free(match_info);
  g_regex_unref(re);
  return TRUE;
}

static gchar *derive_show_name_from_dirname(const gchar *dir_name) {
  if (!dir_name)
    return NULL;

  gchar *show = utils_normalize_title(dir_name);
  if (!show)
    return NULL;

  /* Prefer the part before a release-group separator (after normalization). */
  char *sep = strstr(show, " - ");
  if (sep)
    *sep = '\0';

  /* Remove common season markers embedded in folder names (e.g. "S01"). */
  GRegex *re_sxx = g_regex_new("(?i)\\bS\\s*\\d{1,2}\\b", 0, 0, NULL);
  gchar *tmp = g_regex_replace(re_sxx, show, -1, 0, "", 0, NULL);
  g_regex_unref(re_sxx);
  g_free(show);
  show = tmp;

  GRegex *re_season = g_regex_new("(?i)\\b(Season|Series)\\s*\\d+\\b", 0, 0,
                                  NULL);
  tmp = g_regex_replace(re_season, show, -1, 0, "", 0, NULL);
  g_regex_unref(re_season);
  g_free(show);
  show = tmp;

  /* Collapse repeated whitespace. */
  GRegex *re_ws = g_regex_new("\\s{2,}", 0, 0, NULL);
  tmp = g_regex_replace(re_ws, show, -1, 0, " ", 0, NULL);
  g_regex_unref(re_ws);
  g_free(show);
  show = tmp;

  g_strstrip(show);
  return show;
}

static gchar *derive_show_name_from_episode_filename(const gchar *name) {
  if (!name)
    return NULL;

  gchar *normalized = utils_normalize_title(name);
  if (!normalized)
    return NULL;

  /* Chop at SxxEyy if present */
  GRegex *re = g_regex_new("(?i)\\bS\\s*\\d{1,2}\\s*E\\s*\\d{1,2}\\b", 0, 0,
                           NULL);
  GMatchInfo *match_info = NULL;
  if (g_regex_match(re, normalized, 0, &match_info)) {
    gint start_pos = 0, end_pos = 0;
    g_match_info_fetch_pos(match_info, 0, &start_pos, &end_pos);
    if (start_pos > 0) {
      gchar *tmp = g_strndup(normalized, start_pos);
      g_free(normalized);
      normalized = tmp;
    }
  }
  if (match_info)
    g_match_info_free(match_info);
  g_regex_unref(re);

  g_strstrip(normalized);
  return normalized;
}

static gint scan_tv_season(ReelApp *app, const gchar *path, gint season_num,
                           const gchar *show_name) {
  gint added = 0;

  /* Check if season already exists */
  Film *season = db_film_get_by_path(app, path);
  if (!season) {
    season = film_new();
    season->file_path = g_strdup(path);
    season->title = g_strdup_printf("%s - Season %d", show_name,
                                    season_num); /* Initial title */
    season->year = 0; /* Will be updated by scraper */
    season->media_type = MEDIA_TV_SEASON;
    season->season_number = season_num;
    season->added_date = g_get_real_time() / 1000000;
    season->match_status = MATCH_STATUS_UNMATCHED;

    if (db_film_insert(app, season)) {
      g_print("Added Season: %s\n", path);
    } else {
      film_free(season);
      return 0;
    }
  } else if (season->media_type != MEDIA_TV_SEASON ||
             season->season_number != season_num) {
    /* Upgrade/repair existing entry (e.g. previously scanned incorrectly). */
    season->media_type = MEDIA_TV_SEASON;
    season->season_number = season_num;
    if (!season->title || strlen(season->title) == 0) {
      g_free(season->title);
      season->title =
          g_strdup_printf("%s - Season %d", show_name, season_num);
    }
    db_film_update(app, season);
  }

  /* Scan episodes in season directory */
  GDir *dir = g_dir_open(path, 0, NULL);
  if (dir) {
    const gchar *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
      if (name[0] == '.')
        continue;

      gchar *full_path = g_build_filename(path, name, NULL);
      if (!g_file_test(full_path, G_FILE_TEST_IS_DIR) && is_video_file(name)) {
        /* If this episode file was previously inserted as a film, remove it so
           episodes live only in the episodes table and don't clutter the grid. */
        Film *wrong_film = db_film_get_by_path(app, full_path);
        if (wrong_film) {
          db_film_delete(app, wrong_film->id);
          film_free(wrong_film);
        }

        /* Check if episode exists */
        Episode *ep = db_episode_get_by_path(app, full_path);
        if (!ep) {
          ep = episode_new();
          ep->season_id = season->id;
          ep->file_path = g_strdup(full_path);
          ep->title = g_strdup(name);

          /* Try to extract episode number SxxExx or Exx */
          GRegex *ep_regex = g_regex_new("[Ee](\\d+)", 0, 0, NULL);
          GMatchInfo *match_info;
          if (g_regex_match(ep_regex, name, 0, &match_info)) {
            gchar *ep_str = g_match_info_fetch(match_info, 1);
            ep->episode_number = atoi(ep_str);
            g_free(ep_str);
          }
          g_match_info_free(match_info);
          g_regex_unref(ep_regex);

          if (db_episode_insert(app, ep)) {
            added++;
          }
          episode_free(ep);
        } else {
          episode_free(ep);
        }
      }
      g_free(full_path);
    }
    g_dir_close(dir);
  }

  film_free(season);
  return added;
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
      /* Check for TV Season folder */
      gint season_num = 0;
      if (is_season_directory(name, &season_num)) {
        /* Parent folder name is show name */
        gchar *parent = g_path_get_dirname(full_path);
        gchar *show_name = g_path_get_basename(parent);
        g_free(parent);

        added += scan_tv_season(app, full_path, season_num, show_name);
        g_free(show_name);
      } else if (detect_season_from_episode_filenames(full_path, &season_num)) {
        /* Some libraries put episodes directly in a season folder named like
           "Show.Name.S01.1080p..." (no "Season 1" directory). */
        gchar *show_name = derive_show_name_from_dirname(name);
        if (!show_name || strlen(show_name) == 0) {
          g_free(show_name);
          show_name = utils_normalize_title(name);
        }

        added += scan_tv_season(app, full_path, season_num, show_name);
        g_free(show_name);
      } else {
        /* Recurse into normal subdirectory */
        added += scan_directory_recursive(app, full_path, depth + 1);
      }
    } else if (is_video_file(name)) {
      /* Only process as individual film if NOT inside a season folder
         (This is covered because we don't recurse into season folders except
         via scan_tv_season) */

      /* If this looks like an episodic filename and the parent folder is a
         single-season directory, treat it as a TV season instead of a film. */
      gint season_num = 0;
      gint episode_num = 0;
      if (parse_sxxeyy_from_filename(name, &season_num, &episode_num)) {
        (void)episode_num;
        gchar *dir_basename = g_path_get_basename(path);
        gint inferred_season = 0;
        gboolean single_season_dir =
            is_season_directory(dir_basename, &inferred_season) ||
            detect_season_from_episode_filenames(path, &inferred_season);

        if (single_season_dir) {
          if (inferred_season > 0)
            season_num = inferred_season;

          gchar *show_name = NULL;
          if (is_season_directory(dir_basename, &inferred_season)) {
            gchar *parent = g_path_get_dirname(full_path);
            gchar *parent_base = g_path_get_basename(parent);
            show_name = derive_show_name_from_dirname(parent_base);
            g_free(parent_base);
            g_free(parent);
          } else {
            show_name = derive_show_name_from_dirname(dir_basename);
            if (!show_name || strlen(show_name) == 0) {
              g_free(show_name);
              show_name = derive_show_name_from_episode_filename(name);
            }
          }

          if (!show_name || strlen(show_name) == 0) {
            g_free(show_name);
            show_name = utils_normalize_title(dir_basename);
          }

          added += scan_tv_season(app, path, season_num, show_name);
          g_free(show_name);
          g_free(dir_basename);
          g_free(full_path);
          continue;
        }

        g_free(dir_basename);
      }

      /* Check if already in database */
      if (db_is_file_tracked(app, full_path)) {
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
      film->media_type = MEDIA_FILM;

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
