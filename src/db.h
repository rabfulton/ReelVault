#ifndef REELGTK_DB_H
#define REELGTK_DB_H

#include "app.h"
#include <glib.h>
#include <sqlite3.h>

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
