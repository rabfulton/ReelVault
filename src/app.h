#ifndef REELGTK_APP_H
#define REELGTK_APP_H

#include <gtk/gtk.h>
#include <sqlite3.h>

/* Application name and paths */
#define APP_NAME "ReelVault"
#define APP_ID "com.github.reelvault"
#define CONFIG_DIR_NAME "reelvault"
#define CACHE_DIR_NAME "reelvault"
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

/* Media type enum */
typedef enum { MEDIA_FILM = 0, MEDIA_TV_SEASON = 1 } MediaType;

/* Episode structure (for TV seasons) */
typedef struct _Episode {
  gint64 id;
  gint64 season_id; /* References Film.id where media_type=TV_SEASON */
  gint episode_number;
  gchar *title;
  gchar *file_path;
  gint runtime_minutes;
  gchar *plot;
  gint tmdb_id;
  gchar *air_date;
} Episode;

/* Film structure (also used for TV seasons) */
struct _Film {
  gint64 id;
  gchar *file_path; /* For films: video file; for TV: season folder path */
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
  MediaType media_type;
  gint season_number; /* For TV seasons only */

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
  gchar *plot_text;
  gchar *sort_by;
  gboolean sort_ascending;
};

/* Main application state */
struct _ReelApp {
  GtkApplication *app;
  GtkWidget *window;
  GtkWidget *grid_scrolled;
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
  gboolean system_prefer_dark;
  gchar *gtk_theme_name;         /* Optional override */
  gchar *system_gtk_theme_name;  /* Captured at startup */
  gdouble scale_factor; /* DPI scale factor */

  /* Window geometry (persisted) */
  gboolean window_geometry_valid;
  gint window_width;
  gint window_height;
  gint window_x;
  gint window_y;
  gboolean window_maximized;

  /* Async */
  GThreadPool *thread_pool;
  GAsyncQueue *ui_queue;
  guint ui_update_source;

  /* Async film loading / grid population */
  guint films_refresh_gen;
  gboolean films_loading;
  gint films_next_offset;
  gboolean films_end_reached;
  GList *grid_pending; /* List of Film* (nodes owned by grid) */
  guint grid_idle_source;
  gboolean genres_dirty;

  /* Debug/metrics */
  gint grid_posters_loaded;
  guint mem_debug_source;
};

/* Function prototypes - app lifecycle */
ReelApp *reel_app_new(void);
void reel_app_free(ReelApp *app);
gboolean reel_app_init_paths(ReelApp *app);

/* Film memory management */
Film *film_new(void);
void film_free(Film *film);
Film *film_copy(const Film *film);

/* Episode memory management */
Episode *episode_new(void);
void episode_free(Episode *episode);

/* Filter state */
void filter_state_init(FilterState *filter);
void filter_state_clear(FilterState *filter);

#endif /* REELGTK_APP_H */
