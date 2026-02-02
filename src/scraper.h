#ifndef REELGTK_SCRAPER_H
#define REELGTK_SCRAPER_H

#include "app.h"

/* Search result from TMDB */
typedef struct {
  gint tmdb_id;
  gchar *title;
  gint year;
  gchar *poster_path;
  gchar *overview;
  gdouble vote_average;
} TmdbSearchResult;

/* Start background scraping of unmatched films */
void scraper_start_background(ReelApp *app);

/* Stop background scraping */
void scraper_stop(ReelApp *app);

/* Search TMDB for a film (synchronous) */
GList *scraper_search_tmdb(ReelApp *app, const gchar *query, gint year);

/* Fetch full details and update film in database */
gboolean scraper_fetch_and_update(ReelApp *app, gint64 film_id, gint tmdb_id);

/* Download poster image */
gboolean scraper_download_poster(ReelApp *app, const gchar *poster_path,
                                 gint tmdb_id);

/* Free search result */
void tmdb_search_result_free(TmdbSearchResult *result);

#endif /* REELGTK_SCRAPER_H */
