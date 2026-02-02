#ifndef REELGTK_APP_H
#define REELGTK_APP_H

#include <gtk/gtk.h>
#include <sqlite3.h>

/* Application name and paths */
#define APP_NAME "ReelGTK"
#define APP_ID "com.github.reelgtk"
#define CONFIG_DIR_NAME "reelgtk"
#define CACHE_DIR_NAME "reelgtk"
#define DB_FILENAME "library.db"
#define CONFIG_FILENAME "config.ini"

/* Poster dimensions (base at 96 DPI, scaled at runtime) */
#define POSTER_BASE_WIDTH 150
#define POSTER_BASE_HEIGHT 225 /* 2:3 aspect ratio for movie posters */
#define POSTER_THUMB_WIDTH 150
#define POSTER_THUMB_HEIGHT 225
#define POSTER_FULL_WIDTH 500

/* Theme preferences */
typedef enum {
  THEME_SYSTEM = 0, /* Follow GTK/system theme */
  THEME_LIGHT = 1,
  THEME_DARK = 2
} ThemePreference;

/* Forward declarations */
typedef struct _ReelApp ReelApp;
typedef struct _Film Film;
typedef struct _FilterState FilterState;

/* Match status enum */
typedef enum {
  MATCH_STATUS_UNMATCHED = 0,
  MATCH_STATUS_AUTO = 1,
  MATCH_STATUS_MANUAL = 2,
  MATCH_STATUS_IGNORED = 3
} MatchStatus;

/* Film structure */
struct _Film {
  gint64 id;
  gchar *file_path;
  gchar *title;
  gint year;
  gint runtime_minutes;
  gchar *plot;
  gchar *poster_path;
  gint tmdb_id;
  gchar *imdb_id;
  gdouble rating;
  gint64 added_date;
  MatchStatus match_status;

  /* Cached data */
  GdkPixbuf *poster_pixbuf;
};

/* Filter state */
struct _FilterState {
  gchar *genre;
  gint year_from;
  gint year_to;
  gchar *actor;
  gchar *director;
  gchar *search_text;
  gchar *sort_by;
  gboolean sort_ascending;
};

/* Main application state */
struct _ReelApp {
  GtkApplication *app;
  GtkWidget *window;
  GtkWidget *grid_view;
  GtkWidget *filter_bar;
  GtkWidget *status_bar;

  /* Database */
  sqlite3 *db;
  gchar *db_path;

  /* Configuration */
  gchar *config_path;
  gchar *cache_path;
  gchar *poster_cache_path;
  gchar *tmdb_api_key;
  gchar *player_command;
  gchar **library_paths;
  gint library_paths_count;

  /* State */
  FilterState filter;
  GList *films; /* List of Film* */
  gint total_films;
  gint unmatched_films;
  ThemePreference theme_preference;
  gdouble scale_factor; /* DPI scale factor */

  /* Async */
  GThreadPool *thread_pool;
  GAsyncQueue *ui_queue;
  guint ui_update_source;
};

/* Function prototypes - app lifecycle */
ReelApp *reel_app_new(void);
void reel_app_free(ReelApp *app);
gboolean reel_app_init_paths(ReelApp *app);

/* Film memory management */
Film *film_new(void);
void film_free(Film *film);
Film *film_copy(const Film *film);

/* Filter state */
void filter_state_init(FilterState *filter);
void filter_state_clear(FilterState *filter);

#endif /* REELGTK_APP_H */
