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

/* Include our headers after curl to avoid macro conflicts */
#include "config.h"
#include "db.h"
#include "scraper.h"

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
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "ReelGTK/1.0");

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
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "ReelGTK/1.0");

  CURLcode res = curl_easy_perform(curl);

  fclose(fp);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    unlink(dest_path);
    return FALSE;
  }

  return TRUE;
}

GList *scraper_search_tmdb(ReelApp *app, const gchar *query, gint year) {
  if (!app->tmdb_api_key || strlen(app->tmdb_api_key) == 0) {
    g_printerr("No TMDB API key configured\n");
    return NULL;
  }

  /* URL encode query */
  CURL *curl = curl_easy_init();
  char *encoded_query = curl_easy_escape(curl, query, 0);
  curl_easy_cleanup(curl);

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

gboolean scraper_fetch_and_update(ReelApp *app, gint64 film_id, gint tmdb_id) {
  if (!app->tmdb_api_key)
    return FALSE;

  /* Fetch movie details with credits */
  gchar *url =
      g_strdup_printf("%s/movie/%d?api_key=%s&append_to_response=credits",
                      TMDB_API_BASE, tmdb_id, app->tmdb_api_key);

  char *json_str = http_get(url);
  g_free(url);

  if (!json_str)
    return FALSE;

  struct json_object *root = json_tokener_parse(json_str);
  g_free(json_str);

  if (!root)
    return FALSE;

  /* Get current film */
  Film *film = db_film_get_by_id(app, film_id);
  if (!film) {
    json_object_put(root);
    return FALSE;
  }

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
} ScraperContext;

static gpointer scraper_thread_func(gpointer data) {
  ScraperContext *ctx = (ScraperContext *)data;
  ReelApp *app = ctx->app;

  GList *unmatched = db_films_get_unmatched(app);

  for (GList *l = unmatched; l != NULL && ctx->running; l = l->next) {
    Film *film = (Film *)l->data;

    if (!film->title)
      continue;

    g_print("Searching TMDB for: %s (%d)\n", film->title, film->year);

    GList *results = scraper_search_tmdb(app, film->title, film->year);

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

      if (good_match) {
        scraper_fetch_and_update(app, film->id, first->tmdb_id);
      }

      g_list_free_full(results, (GDestroyNotify)tmdb_search_result_free);
    }

    /* Rate limiting */
    g_usleep(250000); /* 250ms between requests */
  }

  g_list_free_full(unmatched, (GDestroyNotify)film_free);
  g_free(ctx);

  return NULL;
}

static ScraperContext *active_scraper = NULL;

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

void scraper_stop(ReelApp *app) {
  (void)app;
  if (active_scraper) {
    active_scraper->running = FALSE;
  }
}
