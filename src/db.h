#ifndef REELGTK_DB_H
#define REELGTK_DB_H

#include "app.h"
#include <glib.h>
#include <sqlite3.h>

typedef struct {
  gint64 id;
  gint64 film_id;
  gchar *file_path;
  gchar *label;
  gint sort_order;
} FilmFile;

/* Database initialization */
gboolean db_init(ReelApp *app);
void db_close(ReelApp *app);

/* Film CRUD operations */
gboolean db_film_insert(ReelApp *app, Film *film);
gboolean db_film_update(ReelApp *app, const Film *film);
gboolean db_film_delete(ReelApp *app, gint64 film_id);
Film *db_film_get_by_id(ReelApp *app, gint64 film_id);
Film *db_film_get_by_path(ReelApp *app, const gchar *file_path);

/* Film queries */
GList *db_films_get_all(ReelApp *app, const FilterState *filter);
GList *db_films_get_unmatched(ReelApp *app);
gint db_films_count(ReelApp *app);
gint db_films_count_unmatched(ReelApp *app);

/* Read-only helpers for background loading */
sqlite3 *db_open_readonly(const gchar *db_path);
void db_close_handle(sqlite3 *db);
GList *db_films_get_page_db(sqlite3 *db, const FilterState *filter, gint limit,
                            gint offset);
gint db_films_count_db(sqlite3 *db);
gint db_films_count_unmatched_db(sqlite3 *db);

/* Additional file attachments (multi-part, alternate cuts) */
gboolean db_film_file_attach(ReelApp *app, gint64 film_id,
                             const gchar *file_path, const gchar *label,
                             gint sort_order);
gboolean db_film_file_delete(ReelApp *app, gint64 film_file_id);
GList *db_film_files_get(ReelApp *app, gint64 film_id);
void film_file_free(FilmFile *file);

/* Fast check for any tracked path */
gboolean db_is_file_tracked(ReelApp *app, const gchar *file_path);

/* Episode CRUD operations (for TV seasons) */
gboolean db_episode_insert(ReelApp *app, Episode *episode);
gboolean db_episode_update(ReelApp *app, const Episode *episode);
GList *db_episodes_get_for_season(ReelApp *app, gint64 season_id);
Episode *db_episode_get_by_path(ReelApp *app, const gchar *file_path);
gint db_episodes_count_for_season(ReelApp *app, gint64 season_id);

/* Genre operations */
gboolean db_genre_add_to_film(ReelApp *app, gint64 film_id, const gchar *genre);
GList *db_genres_get_for_film(ReelApp *app, gint64 film_id);
GList *db_genres_get_all(ReelApp *app);

/* Actor operations */
gboolean db_actor_add_to_film(ReelApp *app, gint64 film_id, const gchar *name,
                              const gchar *role, gint cast_order, gint tmdb_id);
GList *db_actors_get_for_film(ReelApp *app, gint64 film_id);
GList *db_actors_get_all(ReelApp *app);

/* Director operations */
gboolean db_director_add_to_film(ReelApp *app, gint64 film_id,
                                 const gchar *name, gint tmdb_id);
GList *db_directors_get_for_film(ReelApp *app, gint64 film_id);
GList *db_directors_get_all(ReelApp *app);

/* Helper types for list results */
typedef struct {
  gint id;
  gchar *name;
  gint tmdb_id;
} DbPerson;

typedef struct {
  gint id;
  gchar *name;
  gchar *role;
  gint cast_order;
  gint tmdb_id;
} DbCastMember;

void db_person_free(DbPerson *person);
void db_cast_member_free(DbCastMember *member);

#endif /* REELGTK_DB_H */
