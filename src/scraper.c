/*
 * ReelGTK - TMDB Scraper
 * Fetches film metadata and posters from The Movie Database API
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Disable curl typecheck macros that conflict with GLib's __extension__ usage
 */
#define CURL_DISABLE_TYPECHECK 1
#include <curl/curl.h>
#include <json-c/json.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

/* Include our headers after curl to avoid macro conflicts */
#include "config.h"
#include "db.h"
#include "scraper.h"
#include "utils.h"

#define TMDB_API_BASE "https://api.themoviedb.org/3"
#define TMDB_IMAGE_BASE "https://image.tmdb.org/t/p"

/* Curl write callback */
typedef struct {
  char *data;
  size_t size;
} CurlBuffer;

static size_t my_write_callback(void *contents, size_t size, size_t nmemb,
                                void *userp) {
  size_t realsize = size * nmemb;
  CurlBuffer *buf = (CurlBuffer *)userp;

  buf->data = g_realloc(buf->data, buf->size + realsize + 1);
  memcpy(&(buf->data[buf->size]), contents, realsize);
  buf->size += realsize;
  buf->data[buf->size] = 0;

  return realsize;
}

static char *http_get(const char *url) {
  CURL *curl = curl_easy_init();
  if (!curl)
    return NULL;

  CurlBuffer buf = {NULL, 0};

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, my_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "ReelVault/1.0");

  CURLcode res = curl_easy_perform(curl);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    g_free(buf.data);
    return NULL;
  }

  return buf.data;
}

static gboolean download_file(const char *url, const char *dest_path) {
  CURL *curl = curl_easy_init();
  if (!curl)
    return FALSE;

  FILE *fp = fopen(dest_path, "wb");
  if (!fp) {
    curl_easy_cleanup(curl);
    return FALSE;
  }

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "ReelVault/1.0");

  CURLcode res = curl_easy_perform(curl);

  fclose(fp);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    unlink(dest_path);
    return FALSE;
  }

  return TRUE;
}

static gchar *clean_tmdb_query(const gchar *query) {
  if (!query)
    return NULL;

  const gchar *input = query;
  gchar *basename = NULL;

  /* If a user pastes a path, use just the filename. */
  if (strchr(query, '/') != NULL) {
    basename = g_path_get_basename(query);
    input = basename;
  }

  /* Strip common video extension. */
  gchar *no_ext = g_strdup(input);
  gchar *dot = strrchr(no_ext, '.');
  if (dot) {
    const char *ext = dot + 1;
    if (g_ascii_strcasecmp(ext, "mkv") == 0 || g_ascii_strcasecmp(ext, "mp4") == 0 ||
        g_ascii_strcasecmp(ext, "avi") == 0 || g_ascii_strcasecmp(ext, "mov") == 0 ||
        g_ascii_strcasecmp(ext, "m4v") == 0 || g_ascii_strcasecmp(ext, "wmv") == 0 ||
        g_ascii_strcasecmp(ext, "flv") == 0 || g_ascii_strcasecmp(ext, "webm") == 0) {
      *dot = '\0';
    }
  }

  gchar *normalized = utils_normalize_title(no_ext);
  g_free(no_ext);
  g_free(basename);

  if (!normalized)
    return NULL;

  /* Remove season/episode markers: S01E01, S01, "Season 1", etc. */
  GRegex *re_sxxeyy =
      g_regex_new("(?i)\\bS\\s*\\d{1,2}\\s*E\\s*\\d{1,2}\\b", 0, 0, NULL);
  gchar *tmp = g_regex_replace(re_sxxeyy, normalized, -1, 0, "", 0, NULL);
  g_regex_unref(re_sxxeyy);
  g_free(normalized);
  normalized = tmp;

  GRegex *re_sxx = g_regex_new("(?i)\\bS\\s*\\d{1,2}\\b", 0, 0, NULL);
  tmp = g_regex_replace(re_sxx, normalized, -1, 0, "", 0, NULL);
  g_regex_unref(re_sxx);
  g_free(normalized);
  normalized = tmp;

  GRegex *re_season =
      g_regex_new("(?i)\\b(Season|Series)\\s*\\d+\\b", 0, 0, NULL);
  tmp = g_regex_replace(re_season, normalized, -1, 0, "", 0, NULL);
  g_regex_unref(re_season);
  g_free(normalized);
  normalized = tmp;

  GRegex *re_ws = g_regex_new("\\s{2,}", 0, 0, NULL);
  tmp = g_regex_replace(re_ws, normalized, -1, 0, " ", 0, NULL);
  g_regex_unref(re_ws);
  g_free(normalized);
  normalized = tmp;

  g_strstrip(normalized);
  return normalized;
}

GList *scraper_search_tmdb(ReelApp *app, const gchar *query, gint year) {
  if (!app->tmdb_api_key || strlen(app->tmdb_api_key) == 0) {
    g_printerr("No TMDB API key configured\n");
    return NULL;
  }

  /* URL encode query */
  gchar *clean_query = clean_tmdb_query(query);
  if (!clean_query || strlen(clean_query) == 0) {
    g_free(clean_query);
    return NULL;
  }
  CURL *curl = curl_easy_init();
  char *encoded_query = curl_easy_escape(curl, clean_query, 0);
  curl_easy_cleanup(curl);
  g_free(clean_query);

  /* Build URL */
  gchar *url;
  if (year > 0) {
    url =
        g_strdup_printf("%s/search/movie?api_key=%s&query=%s&year=%d",
                        TMDB_API_BASE, app->tmdb_api_key, encoded_query, year);
  } else {
    url = g_strdup_printf("%s/search/movie?api_key=%s&query=%s", TMDB_API_BASE,
                          app->tmdb_api_key, encoded_query);
  }
  curl_free(encoded_query);

  /* Fetch results */
  char *json_str = http_get(url);
  g_free(url);

  if (!json_str) {
    g_printerr("Failed to search TMDB\n");
    return NULL;
  }

  /* Parse JSON */
  struct json_object *root = json_tokener_parse(json_str);
  g_free(json_str);

  if (!root) {
    g_printerr("Failed to parse TMDB response\n");
    return NULL;
  }

  GList *results = NULL;

  struct json_object *results_array;
  if (json_object_object_get_ex(root, "results", &results_array)) {
    int len = json_object_array_length(results_array);
    for (int i = 0; i < len && i < 10; i++) {
      struct json_object *item = json_object_array_get_idx(results_array, i);

      TmdbSearchResult *result = g_new0(TmdbSearchResult, 1);

      struct json_object *val;
      if (json_object_object_get_ex(item, "id", &val)) {
        result->tmdb_id = json_object_get_int(val);
      }
      if (json_object_object_get_ex(item, "title", &val)) {
        result->title = g_strdup(json_object_get_string(val));
      }
      if (json_object_object_get_ex(item, "release_date", &val)) {
        const char *date = json_object_get_string(val);
        if (date && strlen(date) >= 4) {
          result->year = atoi(date);
        }
      }
      if (json_object_object_get_ex(item, "poster_path", &val)) {
        const char *path = json_object_get_string(val);
        if (path) {
          result->poster_path = g_strdup(path);
        }
      }
      if (json_object_object_get_ex(item, "overview", &val)) {
        result->overview = g_strdup(json_object_get_string(val));
      }
      if (json_object_object_get_ex(item, "vote_average", &val)) {
        result->vote_average = json_object_get_double(val);
      }

      results = g_list_append(results, result);
    }
  }

  json_object_put(root);
  return results;
}

/* TV Search */
GList *scraper_search_tv(ReelApp *app, const gchar *query, gint year) {
  if (!app->tmdb_api_key || strlen(app->tmdb_api_key) == 0)
    return NULL;

  gchar *clean_query = clean_tmdb_query(query);
  if (!clean_query || strlen(clean_query) == 0) {
    g_free(clean_query);
    return NULL;
  }

  CURL *curl = curl_easy_init();
  char *encoded_query = curl_easy_escape(curl, clean_query, 0);
  curl_easy_cleanup(curl);
  g_free(clean_query);

  gchar *url;
  if (year > 0) {
    url = g_strdup_printf(
        "%s/search/tv?api_key=%s&query=%s&first_air_date_year=%d",
        TMDB_API_BASE, app->tmdb_api_key, encoded_query, year);
  } else {
    url = g_strdup_printf("%s/search/tv?api_key=%s&query=%s", TMDB_API_BASE,
                          app->tmdb_api_key, encoded_query);
  }
  curl_free(encoded_query);

  char *json_str = http_get(url);
  g_free(url);

  if (!json_str)
    return NULL;

  struct json_object *root = json_tokener_parse(json_str);
  g_free(json_str);

  if (!root)
    return NULL;

  GList *results = NULL;
  struct json_object *results_array;
  if (json_object_object_get_ex(root, "results", &results_array)) {
    int len = json_object_array_length(results_array);
    for (int i = 0; i < len && i < 10; i++) {
      struct json_object *item = json_object_array_get_idx(results_array, i);
      TmdbSearchResult *result = g_new0(TmdbSearchResult, 1);
      struct json_object *val;

      if (json_object_object_get_ex(item, "id", &val))
        result->tmdb_id = json_object_get_int(val);
      if (json_object_object_get_ex(item, "name", &val))
        result->title = g_strdup(json_object_get_string(val));
      if (json_object_object_get_ex(item, "first_air_date", &val)) {
        const char *date = json_object_get_string(val);
        if (date && strlen(date) >= 4)
          result->year = atoi(date);
      }
      if (json_object_object_get_ex(item, "poster_path", &val)) {
        const char *path = json_object_get_string(val);
        if (path)
          result->poster_path = g_strdup(path);
      }
      if (json_object_object_get_ex(item, "overview", &val))
        result->overview = g_strdup(json_object_get_string(val));
      if (json_object_object_get_ex(item, "vote_average", &val))
        result->vote_average = json_object_get_double(val);

      results = g_list_append(results, result);
    }
  }
  json_object_put(root);
  return results;
}

static gboolean fetch_tv_season_details(ReelApp *app, Film *film,
                                        gint show_id) {
  gchar *url = g_strdup_printf("%s/tv/%d/season/%d?api_key=%s", TMDB_API_BASE,
                               show_id, film->season_number, app->tmdb_api_key);

  char *json_str = http_get(url);
  g_free(url);
  if (!json_str)
    return FALSE;

  struct json_object *root = json_tokener_parse(json_str);
  g_free(json_str);
  if (!root)
    return FALSE;

  struct json_object *val;

  if (json_object_object_get_ex(root, "overview", &val)) {
    const char *overview = json_object_get_string(val);
    if (overview && strlen(overview) > 0) {
      g_free(film->plot);
      film->plot = g_strdup(overview);
    }
  }

  if (json_object_object_get_ex(root, "air_date", &val)) {
    const char *date = json_object_get_string(val);
    if (date && strlen(date) >= 4)
      film->year = atoi(date);
  }

  /* Poster */
  if (json_object_object_get_ex(root, "poster_path", &val)) {
    const char *poster_path = json_object_get_string(val);
    /* Use film->id for local filename to avoid collisions with show vs season
     * vs movie ID space */
    if (poster_path) {
      gchar *url = g_strdup_printf("%s/w500%s", TMDB_IMAGE_BASE, poster_path);
      gchar *dest = g_build_filename(
          app->poster_cache_path, g_strdup_printf("%ld.jpg", film->id), NULL);
      gboolean ok = download_file(url, dest);
      g_free(url);

      if (ok) {
        g_free(film->poster_path);
        film->poster_path = dest; /* Takes ownership */
        gchar *thumb = g_build_filename(
            app->poster_cache_path, g_strdup_printf("%ld_thumb.jpg", film->id),
            NULL);
        /* Always refresh thumb to avoid stale/wrong grid posters. */
        GError *thumb_err = NULL;
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(
            dest, POSTER_THUMB_WIDTH, POSTER_THUMB_HEIGHT, TRUE, &thumb_err);
        if (pixbuf) {
          gdk_pixbuf_save(pixbuf, thumb, "jpeg", &thumb_err, "quality", "85",
                          NULL);
          g_object_unref(pixbuf);
        }
        if (thumb_err)
          g_error_free(thumb_err);
        g_free(thumb);
      } else {
        g_free(dest);
      }
    }
  }

  film->tmdb_id = show_id;
  film->match_status = MATCH_STATUS_AUTO;

  db_film_update(app, film);

  /* Episodes */
  struct json_object *episodes;
  if (json_object_object_get_ex(root, "episodes", &episodes)) {
    int len = json_object_array_length(episodes);
    GList *local_episodes = db_episodes_get_for_season(app, film->id);

    for (int i = 0; i < len; i++) {
      struct json_object *ep_json = json_object_array_get_idx(episodes, i);
      int ep_num = 0;
      if (json_object_object_get_ex(ep_json, "episode_number", &val))
        ep_num = json_object_get_int(val);

      for (GList *l = local_episodes; l != NULL; l = l->next) {
        Episode *local_ep = (Episode *)l->data;
        if (local_ep->episode_number == ep_num) {
          if (json_object_object_get_ex(ep_json, "name", &val)) {
            g_free(local_ep->title);
            local_ep->title = g_strdup(json_object_get_string(val));
          }
          if (json_object_object_get_ex(ep_json, "overview", &val)) {
            g_free(local_ep->plot);
            local_ep->plot = g_strdup(json_object_get_string(val));
          }
          if (json_object_object_get_ex(ep_json, "runtime", &val))
            local_ep->runtime_minutes = json_object_get_int(val);
          if (json_object_object_get_ex(ep_json, "id", &val))
            local_ep->tmdb_id = json_object_get_int(val);
          if (json_object_object_get_ex(ep_json, "air_date", &val)) {
            g_free(local_ep->air_date);
            local_ep->air_date = g_strdup(json_object_get_string(val));
          }
          db_episode_update(app, local_ep);
          break;
        }
      }
    }
    g_list_free_full(local_episodes, (GDestroyNotify)episode_free);
  }

  json_object_put(root);
  return TRUE;
}

gboolean scraper_fetch_and_update(ReelApp *app, gint64 film_id, gint tmdb_id) {
  if (!app->tmdb_api_key)
    return FALSE;

  Film *film = db_film_get_by_id(app, film_id);
  if (!film)
    return FALSE;

  if (film->media_type == MEDIA_TV_SEASON) {
    gboolean ret = fetch_tv_season_details(app, film, tmdb_id);
    film_free(film);
    return ret;
  }

  /* Fetch movie details with credits */
  gchar *url =
      g_strdup_printf("%s/movie/%d?api_key=%s&append_to_response=credits",
                      TMDB_API_BASE, tmdb_id, app->tmdb_api_key);

  char *json_str = http_get(url);
  g_free(url);

  if (!json_str) {
    film_free(film);
    return FALSE;
  }

  struct json_object *root = json_tokener_parse(json_str);
  g_free(json_str);

  if (!root) {
    film_free(film);
    return FALSE;
  }

  /* Update film fields */

  /* Update film fields */
  struct json_object *val;

  if (json_object_object_get_ex(root, "title", &val)) {
    g_free(film->title);
    film->title = g_strdup(json_object_get_string(val));
  }

  if (json_object_object_get_ex(root, "release_date", &val)) {
    const char *date = json_object_get_string(val);
    if (date && strlen(date) >= 4) {
      film->year = atoi(date);
    }
  }

  if (json_object_object_get_ex(root, "runtime", &val)) {
    film->runtime_minutes = json_object_get_int(val);
  }

  if (json_object_object_get_ex(root, "overview", &val)) {
    g_free(film->plot);
    film->plot = g_strdup(json_object_get_string(val));
  }

  if (json_object_object_get_ex(root, "vote_average", &val)) {
    film->rating = json_object_get_double(val);
  }

  if (json_object_object_get_ex(root, "imdb_id", &val)) {
    g_free(film->imdb_id);
    film->imdb_id = g_strdup(json_object_get_string(val));
  }

  film->tmdb_id = tmdb_id;
  film->match_status = MATCH_STATUS_AUTO;

  /* Download poster */
  if (json_object_object_get_ex(root, "poster_path", &val)) {
    const char *poster_path = json_object_get_string(val);
    if (poster_path && scraper_download_poster(app, poster_path, tmdb_id)) {
      g_free(film->poster_path);
      film->poster_path = g_build_filename(
          app->poster_cache_path, g_strdup_printf("%d.jpg", tmdb_id), NULL);
    }
  }

  /* Update film in database */
  db_film_update(app, film);

  /* Process genres */
  struct json_object *genres;
  if (json_object_object_get_ex(root, "genres", &genres)) {
    int len = json_object_array_length(genres);
    for (int i = 0; i < len; i++) {
      struct json_object *genre = json_object_array_get_idx(genres, i);
      if (json_object_object_get_ex(genre, "name", &val)) {
        db_genre_add_to_film(app, film_id, json_object_get_string(val));
      }
    }
    app->genres_dirty = TRUE;
  }

  /* Process credits */
  struct json_object *credits;
  if (json_object_object_get_ex(root, "credits", &credits)) {
    /* Cast */
    struct json_object *cast;
    if (json_object_object_get_ex(credits, "cast", &cast)) {
      int len = json_object_array_length(cast);
      for (int i = 0; i < len && i < 10; i++) {
        struct json_object *person = json_object_array_get_idx(cast, i);

        const char *name = NULL;
        const char *character = NULL;
        int person_id = 0;

        if (json_object_object_get_ex(person, "name", &val)) {
          name = json_object_get_string(val);
        }
        if (json_object_object_get_ex(person, "character", &val)) {
          character = json_object_get_string(val);
        }
        if (json_object_object_get_ex(person, "id", &val)) {
          person_id = json_object_get_int(val);
        }

        if (name) {
          db_actor_add_to_film(app, film_id, name, character, i, person_id);
        }
      }
    }

    /* Crew - find directors */
    struct json_object *crew;
    if (json_object_object_get_ex(credits, "crew", &crew)) {
      int len = json_object_array_length(crew);
      for (int i = 0; i < len; i++) {
        struct json_object *person = json_object_array_get_idx(crew, i);

        if (json_object_object_get_ex(person, "job", &val)) {
          if (g_strcmp0(json_object_get_string(val), "Director") == 0) {
            const char *name = NULL;
            int person_id = 0;

            if (json_object_object_get_ex(person, "name", &val)) {
              name = json_object_get_string(val);
            }
            if (json_object_object_get_ex(person, "id", &val)) {
              person_id = json_object_get_int(val);
            }

            if (name) {
              db_director_add_to_film(app, film_id, name, person_id);
            }
          }
        }
      }
    }
  }

  film_free(film);
  json_object_put(root);

  g_print("Updated film from TMDB: %d\n", tmdb_id);
  return TRUE;
}

gboolean scraper_download_poster(ReelApp *app, const gchar *poster_path,
                                 gint tmdb_id) {
  gchar *url = g_strdup_printf("%s/w500%s", TMDB_IMAGE_BASE, poster_path);
  gchar *dest = g_build_filename(app->poster_cache_path,
                                 g_strdup_printf("%d.jpg", tmdb_id), NULL);

  gboolean success = download_file(url, dest);

  if (success) {
    gchar *thumb =
        g_build_filename(app->poster_cache_path,
                         g_strdup_printf("%d_thumb.jpg", tmdb_id), NULL);

    /* Always refresh thumb so grid never shows a stale poster. */
    GError *error = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(
        dest, POSTER_THUMB_WIDTH, POSTER_THUMB_HEIGHT, TRUE, &error);
    if (pixbuf) {
      gdk_pixbuf_save(pixbuf, thumb, "jpeg", &error, "quality", "85", NULL);
      g_object_unref(pixbuf);
    }
    if (error)
      g_error_free(error);

    g_free(thumb);
  }

  g_free(url);
  g_free(dest);

  return success;
}

void tmdb_search_result_free(TmdbSearchResult *result) {
  if (result) {
    g_free(result->title);
    g_free(result->poster_path);
    g_free(result->overview);
    g_free(result);
  }
}

/* Background scraping */

typedef struct {
  ReelApp *app;
  gboolean running;
  ScraperProgressFunc progress_cb;
  ScraperDoneFunc done_cb;
  gpointer user_data;
  gint total;
  gint done;
  gint pending_callbacks;
} ScraperContext;

static ScraperContext *active_scraper = NULL;

typedef struct {
  ScraperContext *ctx;
  gchar *title;
} ScraperProgressPayload;

static gboolean scraper_progress_idle(gpointer data) {
  ScraperProgressPayload *p = (ScraperProgressPayload *)data;
  if (p->ctx->progress_cb) {
    p->ctx->progress_cb(p->ctx->app, p->ctx->done, p->ctx->total, p->title,
                        p->ctx->user_data);
  }
  p->ctx->pending_callbacks--;
  g_free(p->title);
  g_free(p);
  return G_SOURCE_REMOVE;
}

static gboolean scraper_done_idle(gpointer data) {
  ScraperContext *ctx = (ScraperContext *)data;
  if (ctx->pending_callbacks > 0) {
    return G_SOURCE_CONTINUE;
  }
  gboolean canceled = !ctx->running;
  if (ctx->done_cb) {
    ctx->done_cb(ctx->app, canceled, ctx->user_data);
  }
  /* Mark scraper as inactive once UI is notified. */
  if (active_scraper == ctx)
    active_scraper = NULL;
  g_free(ctx);
  return G_SOURCE_REMOVE;
}

static gpointer scraper_thread_func(gpointer data) {
  ScraperContext *ctx = (ScraperContext *)data;
  ReelApp *app = ctx->app;

  sqlite3 *db = NULL;
  if (sqlite3_open(app->db_path, &db) != SQLITE_OK) {
    if (db)
      sqlite3_close(db);
    g_idle_add(scraper_done_idle, ctx);
    return NULL;
  }
  sqlite3_exec(db, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL);

  ReelApp thread_app = *app;
  thread_app.db = db;

  GList *unmatched = db_films_get_unmatched(&thread_app);
  ctx->total = g_list_length(unmatched);
  ctx->done = 0;

  for (GList *l = unmatched; l != NULL && ctx->running; l = l->next) {
    Film *film = (Film *)l->data;

    if (!film->title)
      continue;

    g_print("Searching TMDB for: %s (%d)\n", film->title, film->year);

    GList *results;
    if (film->media_type == MEDIA_TV_SEASON) {
      results = scraper_search_tv(&thread_app, film->title, film->year);
    } else {
      results = scraper_search_tmdb(&thread_app, film->title, film->year);
    }

    if (results) {
      TmdbSearchResult *first = (TmdbSearchResult *)results->data;

      /* Auto-match if good confidence */
      /* Simple heuristic: if top result title is similar and year matches */
      gboolean good_match = FALSE;

      if (first->year == film->year) {
        good_match = TRUE;
      } else if (film->year == 0 && g_list_length(results) == 1) {
        good_match = TRUE;
      }

      if (film->media_type == MEDIA_TV_SEASON && g_list_length(results) > 0) {
        /* Check if title matches reasonably well ignoring "Season X" part
         * handled in search */
        good_match = TRUE; /* Let's assume the first match is decent for now */
      }

      if (good_match) {
        scraper_fetch_and_update(&thread_app, film->id, first->tmdb_id);
      }

      g_list_free_full(results, (GDestroyNotify)tmdb_search_result_free);
    }

    ctx->done++;
    if (ctx->progress_cb) {
      ctx->pending_callbacks++;
      ScraperProgressPayload *p = g_new0(ScraperProgressPayload, 1);
      p->ctx = ctx;
      p->title = g_strdup(film->title ? film->title : "");
      g_idle_add(scraper_progress_idle, p);
    }

    /* Rate limiting */
    g_usleep(250000); /* 250ms between requests */
  }

  g_list_free_full(unmatched, (GDestroyNotify)film_free);
  sqlite3_close(db);
  g_idle_add(scraper_done_idle, ctx);

  return NULL;
}

void scraper_start_background(ReelApp *app) {
  if (active_scraper && active_scraper->running) {
    g_print("Scraper already running\n");
    return;
  }

  ScraperContext *ctx = g_new0(ScraperContext, 1);
  ctx->app = app;
  ctx->running = TRUE;
  active_scraper = ctx;

  g_thread_new("scraper", scraper_thread_func, ctx);
}

void scraper_start_background_with_progress(ReelApp *app,
                                            ScraperProgressFunc progress_cb,
                                            ScraperDoneFunc done_cb,
                                            gpointer user_data) {
  if (active_scraper && active_scraper->running) {
    g_print("Scraper already running\n");
    return;
  }

  ScraperContext *ctx = g_new0(ScraperContext, 1);
  ctx->app = app;
  ctx->running = TRUE;
  ctx->progress_cb = progress_cb;
  ctx->done_cb = done_cb;
  ctx->user_data = user_data;
  active_scraper = ctx;

  g_thread_new("scraper", scraper_thread_func, ctx);
}

void scraper_stop(ReelApp *app) {
  (void)app;
  if (active_scraper) {
    active_scraper->running = FALSE;
  }
}
