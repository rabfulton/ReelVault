/*
 * ReelGTK - Database Layer
 * SQLite wrapper for film library storage
 */

#include "db.h"
#include <stdio.h>
#include <string.h>

/* SQL statements for schema creation */
static const char *SCHEMA_SQL =
    "CREATE TABLE IF NOT EXISTS films ("
    "    id INTEGER PRIMARY KEY,"
    "    file_path TEXT UNIQUE NOT NULL,"
    "    title TEXT,"
    "    year INTEGER,"
    "    runtime_minutes INTEGER,"
    "    plot TEXT,"
    "    poster_path TEXT,"
    "    tmdb_id INTEGER,"
    "    imdb_id TEXT,"
    "    rating REAL,"
    "    added_date INTEGER,"
    "    match_status INTEGER DEFAULT 0,"
    "    media_type INTEGER DEFAULT 0,"
    "    season_number INTEGER DEFAULT 0"
    ");"

    "CREATE TABLE IF NOT EXISTS film_files ("
    "    id INTEGER PRIMARY KEY,"
    "    film_id INTEGER REFERENCES films(id) ON DELETE CASCADE,"
    "    file_path TEXT UNIQUE NOT NULL,"
    "    label TEXT,"
    "    sort_order INTEGER DEFAULT 0"
    ");"

    "CREATE TABLE IF NOT EXISTS episodes ("
    "    id INTEGER PRIMARY KEY,"
    "    season_id INTEGER REFERENCES films(id) ON DELETE CASCADE,"
    "    episode_number INTEGER,"
    "    title TEXT,"
    "    file_path TEXT UNIQUE,"
    "    runtime_minutes INTEGER,"
    "    plot TEXT,"
    "    tmdb_id INTEGER,"
    "    air_date TEXT"
    ");"

    "CREATE TABLE IF NOT EXISTS genres ("
    "    id INTEGER PRIMARY KEY,"
    "    name TEXT UNIQUE"
    ");"

    "CREATE TABLE IF NOT EXISTS film_genres ("
    "    film_id INTEGER REFERENCES films(id) ON DELETE CASCADE,"
    "    genre_id INTEGER REFERENCES genres(id) ON DELETE CASCADE,"
    "    PRIMARY KEY (film_id, genre_id)"
    ");"

    "CREATE TABLE IF NOT EXISTS actors ("
    "    id INTEGER PRIMARY KEY,"
    "    name TEXT UNIQUE,"
    "    tmdb_id INTEGER"
    ");"

    "CREATE TABLE IF NOT EXISTS film_actors ("
    "    film_id INTEGER REFERENCES films(id) ON DELETE CASCADE,"
    "    actor_id INTEGER REFERENCES actors(id) ON DELETE CASCADE,"
    "    role TEXT,"
    "    cast_order INTEGER,"
    "    PRIMARY KEY (film_id, actor_id)"
    ");"

    "CREATE TABLE IF NOT EXISTS directors ("
    "    id INTEGER PRIMARY KEY,"
    "    name TEXT UNIQUE,"
    "    tmdb_id INTEGER"
    ");"

    "CREATE TABLE IF NOT EXISTS film_directors ("
    "    film_id INTEGER REFERENCES films(id) ON DELETE CASCADE,"
    "    director_id INTEGER REFERENCES directors(id) ON DELETE CASCADE,"
    "    PRIMARY KEY (film_id, director_id)"
    ");"

    "CREATE INDEX IF NOT EXISTS idx_films_year ON films(year);"
    "CREATE INDEX IF NOT EXISTS idx_films_title ON films(title);"
    "CREATE INDEX IF NOT EXISTS idx_films_match_status ON films(match_status);"
    "CREATE INDEX IF NOT EXISTS idx_films_tmdb_id ON films(tmdb_id);"
    "CREATE INDEX IF NOT EXISTS idx_films_rating ON films(rating);"
    "CREATE INDEX IF NOT EXISTS idx_films_added_date ON films(added_date);"
    "CREATE INDEX IF NOT EXISTS idx_film_files_film_id ON film_files(film_id);";

gboolean db_init(ReelApp *app) {
  int rc = sqlite3_open(app->db_path, &app->db);
  if (rc != SQLITE_OK) {
    g_printerr("Cannot open database: %s\n", sqlite3_errmsg(app->db));
    return FALSE;
  }

  /* Enable foreign keys */
  char *err_msg = NULL;
  rc = sqlite3_exec(app->db, "PRAGMA foreign_keys = ON;", NULL, NULL, &err_msg);
  if (rc != SQLITE_OK) {
    g_printerr("Failed to enable foreign keys: %s\n", err_msg);
    sqlite3_free(err_msg);
  }

  /* Create schema */
  rc = sqlite3_exec(app->db, SCHEMA_SQL, NULL, NULL, &err_msg);
  if (rc != SQLITE_OK) {
    g_printerr("Failed to create schema: %s\n", err_msg);
    sqlite3_free(err_msg);
    sqlite3_close(app->db);
    app->db = NULL;
    return FALSE;
  }

  /* Migration: Add new columns if they don't exist (ignore errors if they do)
   */
  sqlite3_exec(app->db,
               "ALTER TABLE films ADD COLUMN media_type INTEGER DEFAULT 0",
               NULL, NULL, NULL);
  sqlite3_exec(app->db,
               "ALTER TABLE films ADD COLUMN season_number INTEGER DEFAULT 0",
               NULL, NULL, NULL);

  /* Additional indexes (ignore errors if already exist) */
  sqlite3_exec(app->db, "CREATE INDEX IF NOT EXISTS idx_films_rating ON films(rating);",
               NULL, NULL, NULL);
  sqlite3_exec(app->db,
               "CREATE INDEX IF NOT EXISTS idx_films_added_date ON films(added_date);",
               NULL, NULL, NULL);

  g_print("Database initialized: %s\n", app->db_path);
  return TRUE;
}

void db_close(ReelApp *app) {
  if (app->db) {
    sqlite3_close(app->db);
    app->db = NULL;
  }
}

/* Film CRUD operations */

gboolean db_film_insert(ReelApp *app, Film *film) {
  const char *sql =
      "INSERT INTO films (file_path, title, year, runtime_minutes, plot, "
      "poster_path, tmdb_id, imdb_id, rating, added_date, match_status, "
      "media_type, season_number) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    g_printerr("Failed to prepare insert: %s\n", sqlite3_errmsg(app->db));
    return FALSE;
  }

  sqlite3_bind_text(stmt, 1, film->file_path, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, film->title, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 3, film->year);
  sqlite3_bind_int(stmt, 4, film->runtime_minutes);
  sqlite3_bind_text(stmt, 5, film->plot, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 6, film->poster_path, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 7, film->tmdb_id);
  sqlite3_bind_text(stmt, 8, film->imdb_id, -1, SQLITE_STATIC);
  sqlite3_bind_double(stmt, 9, film->rating);
  sqlite3_bind_int64(stmt, 10, film->added_date);
  sqlite3_bind_int(stmt, 11, film->match_status);
  sqlite3_bind_int(stmt, 12, film->media_type);
  sqlite3_bind_int(stmt, 13, film->season_number);

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    g_printerr("Failed to insert film: %s\n", sqlite3_errmsg(app->db));
    sqlite3_finalize(stmt);
    return FALSE;
  }

  film->id = sqlite3_last_insert_rowid(app->db);
  sqlite3_finalize(stmt);
  return TRUE;
}

gboolean db_film_update(ReelApp *app, const Film *film) {
  const char *sql =
      "UPDATE films SET title=?, year=?, runtime_minutes=?, plot=?, "
      "poster_path=?, tmdb_id=?, imdb_id=?, rating=?, match_status=?, "
      "media_type=?, season_number=? "
      "WHERE id=?";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    g_printerr("Failed to prepare update: %s\n", sqlite3_errmsg(app->db));
    return FALSE;
  }

  sqlite3_bind_text(stmt, 1, film->title, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, film->year);
  sqlite3_bind_int(stmt, 3, film->runtime_minutes);
  sqlite3_bind_text(stmt, 4, film->plot, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 5, film->poster_path, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 6, film->tmdb_id);
  sqlite3_bind_text(stmt, 7, film->imdb_id, -1, SQLITE_STATIC);
  sqlite3_bind_double(stmt, 8, film->rating);
  sqlite3_bind_int(stmt, 9, film->match_status);
  sqlite3_bind_int(stmt, 10, film->media_type);
  sqlite3_bind_int(stmt, 11, film->season_number);
  sqlite3_bind_int64(stmt, 12, film->id);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  return rc == SQLITE_DONE;
}

gboolean db_film_delete(ReelApp *app, gint64 film_id) {
  const char *sql = "DELETE FROM films WHERE id=?";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    return FALSE;

  sqlite3_bind_int64(stmt, 1, film_id);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  return rc == SQLITE_DONE;
}

static Film *film_from_row(sqlite3_stmt *stmt) {
  Film *film = film_new();

  film->id = sqlite3_column_int64(stmt, 0);
  film->file_path = g_strdup((const gchar *)sqlite3_column_text(stmt, 1));
  film->title = g_strdup((const gchar *)sqlite3_column_text(stmt, 2));
  film->year = sqlite3_column_int(stmt, 3);
  film->runtime_minutes = sqlite3_column_int(stmt, 4);
  film->plot = g_strdup((const gchar *)sqlite3_column_text(stmt, 5));
  film->poster_path = g_strdup((const gchar *)sqlite3_column_text(stmt, 6));
  film->tmdb_id = sqlite3_column_int(stmt, 7);
  film->imdb_id = g_strdup((const gchar *)sqlite3_column_text(stmt, 8));
  film->rating = sqlite3_column_double(stmt, 9);
  film->added_date = sqlite3_column_int64(stmt, 10);
  film->match_status = sqlite3_column_int(stmt, 11);

  /* Check for new columns (indices 12 and 13) */
  if (sqlite3_column_count(stmt) > 12) {
    film->media_type = sqlite3_column_int(stmt, 12);
    film->season_number = sqlite3_column_int(stmt, 13);
  }

  return film;
}

sqlite3 *db_open_readonly(const gchar *db_path) {
  sqlite3 *db = NULL;
  if (sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
    if (db)
      sqlite3_close(db);
    return NULL;
  }
  return db;
}

void db_close_handle(sqlite3 *db) {
  if (db)
    sqlite3_close(db);
}

static GString *build_films_query(const FilterState *filter, gboolean paged,
                                  gint limit, gint offset) {
  GString *sql = g_string_new("SELECT f.* FROM films f");
  gboolean has_where = FALSE;

  if (filter && filter->genre && strlen(filter->genre) > 0) {
    g_string_append(sql, " JOIN film_genres fg ON f.id = fg.film_id"
                         " JOIN genres g ON fg.genre_id = g.id");
    g_string_append_printf(sql, " WHERE g.name = '%s'", filter->genre);
    has_where = TRUE;
  }

  if (filter && filter->year_from > 0) {
    g_string_append_printf(sql, " %s f.year >= %d", has_where ? "AND" : "WHERE",
                           filter->year_from);
    has_where = TRUE;
  }

  if (filter && filter->year_to > 0) {
    g_string_append_printf(sql, " %s f.year <= %d", has_where ? "AND" : "WHERE",
                           filter->year_to);
    has_where = TRUE;
  }

  if (filter && filter->search_text && strlen(filter->search_text) > 0) {
    g_string_append_printf(sql, " %s f.title LIKE '%%%s%%'",
                           has_where ? "AND" : "WHERE", filter->search_text);
    has_where = TRUE;
  }

  if (filter && filter->plot_text && strlen(filter->plot_text) > 0) {
    g_string_append_printf(sql, " %s f.plot LIKE '%%%s%%'",
                           has_where ? "AND" : "WHERE", filter->plot_text);
    has_where = TRUE;
  }

  if (filter && filter->actor && strlen(filter->actor) > 0) {
    if (!has_where) {
      g_string_append(sql, " WHERE");
    } else {
      g_string_append(sql, " AND");
    }
    g_string_append(sql, " f.id IN (SELECT fa.film_id FROM film_actors fa"
                         " JOIN actors a ON fa.actor_id = a.id"
                         " WHERE a.name LIKE '%");
    g_string_append(sql, filter->actor);
    g_string_append(sql, "%')");
    has_where = TRUE;
  }

  if (filter && filter->sort_by) {
    if (g_strcmp0(filter->sort_by, "year") == 0) {
      g_string_append(sql, " ORDER BY f.year");
    } else if (g_strcmp0(filter->sort_by, "rating") == 0) {
      g_string_append(sql, " ORDER BY f.rating");
    } else if (g_strcmp0(filter->sort_by, "added") == 0) {
      g_string_append(sql, " ORDER BY f.added_date");
    } else {
      g_string_append(sql, " ORDER BY f.title COLLATE NOCASE");
    }
    g_string_append(sql, (filter->sort_ascending ? " ASC" : " DESC"));
  } else {
    g_string_append(sql, " ORDER BY f.title COLLATE NOCASE ASC");
  }

  if (paged && limit > 0) {
    g_string_append_printf(sql, " LIMIT %d OFFSET %d", limit, offset);
  }

  return sql;
}

GList *db_films_get_page_db(sqlite3 *db, const FilterState *filter, gint limit,
                            gint offset) {
  GString *sql = build_films_query(filter, TRUE, limit, offset);

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db, sql->str, -1, &stmt, NULL);
  g_string_free(sql, TRUE);
  if (rc != SQLITE_OK)
    return NULL;

  GList *films = NULL;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    films = g_list_append(films, film_from_row(stmt));
  }

  sqlite3_finalize(stmt);
  return films;
}

gint db_films_count_db(sqlite3 *db) {
  const char *sql = "SELECT COUNT(*) FROM films";
  sqlite3_stmt *stmt;
  int count = 0;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
  }
  return count;
}

gint db_films_count_unmatched_db(sqlite3 *db) {
  const char *sql = "SELECT COUNT(*) FROM films WHERE match_status = 0";
  sqlite3_stmt *stmt;
  int count = 0;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
  }
  return count;
}

static FilmFile *film_file_from_row(sqlite3_stmt *stmt) {
  FilmFile *file = g_new0(FilmFile, 1);
  file->id = sqlite3_column_int64(stmt, 0);
  file->film_id = sqlite3_column_int64(stmt, 1);
  file->file_path = g_strdup((const gchar *)sqlite3_column_text(stmt, 2));
  file->label = g_strdup((const gchar *)sqlite3_column_text(stmt, 3));
  file->sort_order = sqlite3_column_int(stmt, 4);
  return file;
}

void film_file_free(FilmFile *file) {
  if (!file)
    return;
  g_free(file->file_path);
  g_free(file->label);
  g_free(file);
}

gboolean db_film_file_attach(ReelApp *app, gint64 film_id,
                             const gchar *file_path, const gchar *label,
                             gint sort_order) {
  const char *sql = "INSERT OR IGNORE INTO film_files (film_id, file_path, "
                    "label, sort_order) VALUES (?, ?, ?, ?)";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    return FALSE;

  sqlite3_bind_int64(stmt, 1, film_id);
  sqlite3_bind_text(stmt, 2, file_path, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, label, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 4, sort_order);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return rc == SQLITE_DONE;
}

gboolean db_film_file_delete(ReelApp *app, gint64 film_file_id) {
  const char *sql = "DELETE FROM film_files WHERE id=?";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    return FALSE;

  sqlite3_bind_int64(stmt, 1, film_file_id);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  return rc == SQLITE_DONE;
}

GList *db_film_files_get(ReelApp *app, gint64 film_id) {
  const char *sql =
      "SELECT id, film_id, file_path, label, sort_order FROM film_files "
      "WHERE film_id = ? ORDER BY sort_order ASC, id ASC";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    return NULL;

  sqlite3_bind_int64(stmt, 1, film_id);

  GList *files = NULL;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    files = g_list_append(files, film_file_from_row(stmt));
  }

  sqlite3_finalize(stmt);
  return files;
}

gboolean db_is_file_tracked(ReelApp *app, const gchar *file_path) {
  const char *sql =
      "SELECT 1 FROM films WHERE file_path = ? "
      "UNION ALL "
      "SELECT 1 FROM film_files WHERE file_path = ? "
      "UNION ALL "
      "SELECT 1 FROM episodes WHERE file_path = ? "
      "LIMIT 1";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    return FALSE;

  sqlite3_bind_text(stmt, 1, file_path, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, file_path, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, file_path, -1, SQLITE_STATIC);

  gboolean exists = (sqlite3_step(stmt) == SQLITE_ROW);
  sqlite3_finalize(stmt);
  return exists;
}

Film *db_film_get_by_id(ReelApp *app, gint64 film_id) {
  const char *sql = "SELECT * FROM films WHERE id=?";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    return NULL;

  sqlite3_bind_int64(stmt, 1, film_id);

  Film *film = NULL;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    film = film_from_row(stmt);
  }

  sqlite3_finalize(stmt);
  return film;
}

Film *db_film_get_by_path(ReelApp *app, const gchar *file_path) {
  const char *sql = "SELECT * FROM films WHERE file_path=?";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    return NULL;

  sqlite3_bind_text(stmt, 1, file_path, -1, SQLITE_STATIC);

  Film *film = NULL;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    film = film_from_row(stmt);
  }

  sqlite3_finalize(stmt);
  return film;
}

GList *db_films_get_all(ReelApp *app, const FilterState *filter) {
  GString *sql = g_string_new("SELECT f.* FROM films f");
  GList *films = NULL;

  /* Build WHERE clause based on filter */
  gboolean has_where = FALSE;

  if (filter && filter->genre && strlen(filter->genre) > 0) {
    g_string_append(sql, " JOIN film_genres fg ON f.id = fg.film_id"
                         " JOIN genres g ON fg.genre_id = g.id");
    g_string_append_printf(sql, " WHERE g.name = '%s'", filter->genre);
    has_where = TRUE;
  }

  if (filter && filter->year_from > 0) {
    g_string_append_printf(sql, " %s f.year >= %d", has_where ? "AND" : "WHERE",
                           filter->year_from);
    has_where = TRUE;
  }

  if (filter && filter->year_to > 0) {
    g_string_append_printf(sql, " %s f.year <= %d", has_where ? "AND" : "WHERE",
                           filter->year_to);
    has_where = TRUE;
  }

  if (filter && filter->search_text && strlen(filter->search_text) > 0) {
    g_string_append_printf(sql, " %s f.title LIKE '%%%s%%'",
                           has_where ? "AND" : "WHERE", filter->search_text);
    has_where = TRUE;
  }

  if (filter && filter->actor && strlen(filter->actor) > 0) {
    if (!has_where) {
      g_string_append(sql, " WHERE");
    } else {
      g_string_append(sql, " AND");
    }
    g_string_append(sql, " f.id IN (SELECT fa.film_id FROM film_actors fa"
                         " JOIN actors a ON fa.actor_id = a.id"
                         " WHERE a.name LIKE '%");
    g_string_append(sql, filter->actor);
    g_string_append(sql, "%')");
    has_where = TRUE;
  }

  /* Add ORDER BY */
  if (filter && filter->sort_by) {
    if (g_strcmp0(filter->sort_by, "year") == 0) {
      g_string_append(sql, " ORDER BY f.year");
    } else if (g_strcmp0(filter->sort_by, "rating") == 0) {
      g_string_append(sql, " ORDER BY f.rating");
    } else if (g_strcmp0(filter->sort_by, "added") == 0) {
      g_string_append(sql, " ORDER BY f.added_date");
    } else {
      g_string_append(sql, " ORDER BY f.title COLLATE NOCASE");
    }
    g_string_append(sql, filter->sort_ascending ? " ASC" : " DESC");
  } else {
    g_string_append(sql, " ORDER BY f.title COLLATE NOCASE ASC");
  }

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(app->db, sql->str, -1, &stmt, NULL);
  g_string_free(sql, TRUE);

  if (rc != SQLITE_OK) {
    g_printerr("Failed to query films: %s\n", sqlite3_errmsg(app->db));
    return NULL;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    Film *film = film_from_row(stmt);
    films = g_list_append(films, film);
  }

  sqlite3_finalize(stmt);
  return films;
}

GList *db_films_get_unmatched(ReelApp *app) {
  const char *sql =
      "SELECT * FROM films WHERE match_status = 0 ORDER BY file_path";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    return NULL;

  GList *films = NULL;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    Film *film = film_from_row(stmt);
    films = g_list_append(films, film);
  }

  sqlite3_finalize(stmt);
  return films;
}

gint db_films_count(ReelApp *app) {
  const char *sql = "SELECT COUNT(*) FROM films";
  sqlite3_stmt *stmt;
  int count = 0;

  if (sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
  }

  return count;
}

gint db_films_count_unmatched(ReelApp *app) {
  const char *sql = "SELECT COUNT(*) FROM films WHERE match_status = 0";
  sqlite3_stmt *stmt;
  int count = 0;

  if (sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
  }

  return count;
}

/* Genre operations */

static gint db_get_or_create_genre(ReelApp *app, const gchar *name) {
  /* Try to find existing */
  const char *sql_find = "SELECT id FROM genres WHERE name = ?";
  sqlite3_stmt *stmt;
  gint id = -1;

  if (sqlite3_prepare_v2(app->db, sql_find, -1, &stmt, NULL) == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
  }

  if (id >= 0)
    return id;

  /* Create new */
  const char *sql_insert = "INSERT INTO genres (name) VALUES (?)";
  if (sqlite3_prepare_v2(app->db, sql_insert, -1, &stmt, NULL) == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_DONE) {
      id = sqlite3_last_insert_rowid(app->db);
    }
    sqlite3_finalize(stmt);
  }

  return id;
}

gboolean db_genre_add_to_film(ReelApp *app, gint64 film_id,
                              const gchar *genre) {
  gint genre_id = db_get_or_create_genre(app, genre);
  if (genre_id < 0)
    return FALSE;

  const char *sql =
      "INSERT OR IGNORE INTO film_genres (film_id, genre_id) VALUES (?, ?)";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return FALSE;

  sqlite3_bind_int64(stmt, 1, film_id);
  sqlite3_bind_int(stmt, 2, genre_id);

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  return rc == SQLITE_DONE;
}

GList *db_genres_get_for_film(ReelApp *app, gint64 film_id) {
  const char *sql = "SELECT g.name FROM genres g"
                    " JOIN film_genres fg ON g.id = fg.genre_id"
                    " WHERE fg.film_id = ?"
                    " ORDER BY g.name";

  sqlite3_stmt *stmt;
  GList *genres = NULL;

  if (sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
    sqlite3_bind_int64(stmt, 1, film_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      genres = g_list_append(
          genres, g_strdup((const gchar *)sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);
  }

  return genres;
}

GList *db_genres_get_all(ReelApp *app) {
  const char *sql = "SELECT DISTINCT name FROM genres ORDER BY name";

  sqlite3_stmt *stmt;
  GList *genres = NULL;

  if (sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      genres = g_list_append(
          genres, g_strdup((const gchar *)sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);
  }

  return genres;
}

/* Actor operations */

static gint db_get_or_create_actor(ReelApp *app, const gchar *name,
                                   gint tmdb_id) {
  const char *sql_find = "SELECT id FROM actors WHERE name = ?";
  sqlite3_stmt *stmt;
  gint id = -1;

  if (sqlite3_prepare_v2(app->db, sql_find, -1, &stmt, NULL) == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
  }

  if (id >= 0)
    return id;

  const char *sql_insert = "INSERT INTO actors (name, tmdb_id) VALUES (?, ?)";
  if (sqlite3_prepare_v2(app->db, sql_insert, -1, &stmt, NULL) == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, tmdb_id);
    if (sqlite3_step(stmt) == SQLITE_DONE) {
      id = sqlite3_last_insert_rowid(app->db);
    }
    sqlite3_finalize(stmt);
  }

  return id;
}

gboolean db_actor_add_to_film(ReelApp *app, gint64 film_id, const gchar *name,
                              const gchar *role, gint cast_order,
                              gint tmdb_id) {
  gint actor_id = db_get_or_create_actor(app, name, tmdb_id);
  if (actor_id < 0)
    return FALSE;

  const char *sql = "INSERT OR REPLACE INTO film_actors (film_id, actor_id, "
                    "role, cast_order) VALUES (?, ?, ?, ?)";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return FALSE;

  sqlite3_bind_int64(stmt, 1, film_id);
  sqlite3_bind_int(stmt, 2, actor_id);
  sqlite3_bind_text(stmt, 3, role, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 4, cast_order);

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  return rc == SQLITE_DONE;
}

GList *db_actors_get_for_film(ReelApp *app, gint64 film_id) {
  const char *sql =
      "SELECT a.id, a.name, fa.role, fa.cast_order, a.tmdb_id FROM actors a"
      " JOIN film_actors fa ON a.id = fa.actor_id"
      " WHERE fa.film_id = ?"
      " ORDER BY fa.cast_order";

  sqlite3_stmt *stmt;
  GList *actors = NULL;

  if (sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
    sqlite3_bind_int64(stmt, 1, film_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      DbCastMember *member = g_new0(DbCastMember, 1);
      member->id = sqlite3_column_int(stmt, 0);
      member->name = g_strdup((const gchar *)sqlite3_column_text(stmt, 1));
      member->role = g_strdup((const gchar *)sqlite3_column_text(stmt, 2));
      member->cast_order = sqlite3_column_int(stmt, 3);
      member->tmdb_id = sqlite3_column_int(stmt, 4);
      actors = g_list_append(actors, member);
    }
    sqlite3_finalize(stmt);
  }

  return actors;
}

GList *db_actors_get_all(ReelApp *app) {
  const char *sql = "SELECT DISTINCT name FROM actors ORDER BY name";

  sqlite3_stmt *stmt;
  GList *actors = NULL;

  if (sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      actors = g_list_append(
          actors, g_strdup((const gchar *)sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);
  }

  return actors;
}

/* Director operations */

static gint db_get_or_create_director(ReelApp *app, const gchar *name,
                                      gint tmdb_id) {
  const char *sql_find = "SELECT id FROM directors WHERE name = ?";
  sqlite3_stmt *stmt;
  gint id = -1;

  if (sqlite3_prepare_v2(app->db, sql_find, -1, &stmt, NULL) == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
  }

  if (id >= 0)
    return id;

  const char *sql_insert =
      "INSERT INTO directors (name, tmdb_id) VALUES (?, ?)";
  if (sqlite3_prepare_v2(app->db, sql_insert, -1, &stmt, NULL) == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, tmdb_id);
    if (sqlite3_step(stmt) == SQLITE_DONE) {
      id = sqlite3_last_insert_rowid(app->db);
    }
    sqlite3_finalize(stmt);
  }

  return id;
}

gboolean db_director_add_to_film(ReelApp *app, gint64 film_id,
                                 const gchar *name, gint tmdb_id) {
  gint director_id = db_get_or_create_director(app, name, tmdb_id);
  if (director_id < 0)
    return FALSE;

  const char *sql = "INSERT OR IGNORE INTO film_directors (film_id, "
                    "director_id) VALUES (?, ?)";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return FALSE;

  sqlite3_bind_int64(stmt, 1, film_id);
  sqlite3_bind_int(stmt, 2, director_id);

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  return rc == SQLITE_DONE;
}

GList *db_directors_get_for_film(ReelApp *app, gint64 film_id) {
  const char *sql = "SELECT d.id, d.name, d.tmdb_id FROM directors d"
                    " JOIN film_directors fd ON d.id = fd.director_id"
                    " WHERE fd.film_id = ?";

  sqlite3_stmt *stmt;
  GList *directors = NULL;

  if (sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
    sqlite3_bind_int64(stmt, 1, film_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      DbPerson *person = g_new0(DbPerson, 1);
      person->id = sqlite3_column_int(stmt, 0);
      person->name = g_strdup((const gchar *)sqlite3_column_text(stmt, 1));
      person->tmdb_id = sqlite3_column_int(stmt, 2);
      directors = g_list_append(directors, person);
    }
    sqlite3_finalize(stmt);
  }

  return directors;
}

GList *db_directors_get_all(ReelApp *app) {
  const char *sql = "SELECT DISTINCT name FROM directors ORDER BY name";

  sqlite3_stmt *stmt;
  GList *directors = NULL;

  if (sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      directors = g_list_append(
          directors, g_strdup((const gchar *)sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);
  }

  return directors;
}

/* Helper cleanup functions */

void db_person_free(DbPerson *person) {
  if (person) {
    g_free(person->name);
    g_free(person);
  }
}

/* Episode operations */

static Episode *episode_from_row(sqlite3_stmt *stmt) {
  Episode *episode = episode_new();

  episode->id = sqlite3_column_int64(stmt, 0);
  episode->season_id = sqlite3_column_int64(stmt, 1);
  episode->episode_number = sqlite3_column_int(stmt, 2);
  episode->title = g_strdup((const gchar *)sqlite3_column_text(stmt, 3));
  episode->file_path = g_strdup((const gchar *)sqlite3_column_text(stmt, 4));
  episode->runtime_minutes = sqlite3_column_int(stmt, 5);
  episode->plot = g_strdup((const gchar *)sqlite3_column_text(stmt, 6));
  episode->tmdb_id = sqlite3_column_int(stmt, 7);
  episode->air_date = g_strdup((const gchar *)sqlite3_column_text(stmt, 8));

  return episode;
}

gboolean db_episode_insert(ReelApp *app, Episode *episode) {
  const char *sql =
      "INSERT INTO episodes (season_id, episode_number, title, file_path, "
      "runtime_minutes, plot, tmdb_id, air_date) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?)";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    g_printerr("Failed to prepare episode insert: %s\n",
               sqlite3_errmsg(app->db));
    return FALSE;
  }

  sqlite3_bind_int64(stmt, 1, episode->season_id);
  sqlite3_bind_int(stmt, 2, episode->episode_number);
  sqlite3_bind_text(stmt, 3, episode->title, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 4, episode->file_path, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 5, episode->runtime_minutes);
  sqlite3_bind_text(stmt, 6, episode->plot, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 7, episode->tmdb_id);
  sqlite3_bind_text(stmt, 8, episode->air_date, -1, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    g_printerr("Failed to insert episode: %s\n", sqlite3_errmsg(app->db));
    sqlite3_finalize(stmt);
    return FALSE;
  }

  episode->id = sqlite3_last_insert_rowid(app->db);
  sqlite3_finalize(stmt);
  return TRUE;
}

gboolean db_episode_update(ReelApp *app, const Episode *episode) {
  const char *sql =
      "UPDATE episodes SET season_id=?, episode_number=?, title=?, "
      "runtime_minutes=?, plot=?, tmdb_id=?, air_date=? "
      "WHERE id=?";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    g_printerr("Failed to prepare episode update: %s\n",
               sqlite3_errmsg(app->db));
    return FALSE;
  }

  sqlite3_bind_int64(stmt, 1, episode->season_id);
  sqlite3_bind_int(stmt, 2, episode->episode_number);
  sqlite3_bind_text(stmt, 3, episode->title, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 4, episode->runtime_minutes);
  sqlite3_bind_text(stmt, 5, episode->plot, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 6, episode->tmdb_id);
  sqlite3_bind_text(stmt, 7, episode->air_date, -1, SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 8, episode->id);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  return rc == SQLITE_DONE;
}

GList *db_episodes_get_for_season(ReelApp *app, gint64 season_id) {
  const char *sql =
      "SELECT * FROM episodes WHERE season_id = ? ORDER BY episode_number";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    return NULL;

  sqlite3_bind_int64(stmt, 1, season_id);

  GList *list = NULL;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    list = g_list_append(list, episode_from_row(stmt));
  }

  sqlite3_finalize(stmt);
  return list;
}

Episode *db_episode_get_by_path(ReelApp *app, const gchar *file_path) {
  const char *sql = "SELECT * FROM episodes WHERE file_path = ?";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    return NULL;

  sqlite3_bind_text(stmt, 1, file_path, -1, SQLITE_STATIC);

  Episode *episode = NULL;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    episode = episode_from_row(stmt);
  }

  sqlite3_finalize(stmt);
  return episode;
}

gint db_episodes_count_for_season(ReelApp *app, gint64 season_id) {
  const char *sql = "SELECT COUNT(*) FROM episodes WHERE season_id = ?";
  sqlite3_stmt *stmt;
  int count = 0;

  if (sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
    sqlite3_bind_int64(stmt, 1, season_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
  }

  return count;
}

void db_cast_member_free(DbCastMember *member) {
  if (member) {
    g_free(member->name);
    g_free(member->role);
    g_free(member);
  }
}
