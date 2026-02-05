/*
 * ReelGTK - Main Window
 * Application window with poster grid, filter bar, and status bar
 */

#include "window.h"
#include "config.h"
#include "db.h"
#include "filter.h"
#include "grid.h"
#include "scanner.h"
#include "scraper.h"
#include <stdarg.h>
#include <string.h>
#include <gdk/gdkkeysyms.h>
#ifdef __GLIBC__
#include <malloc.h>
#endif

static guint64 read_rss_kb(void) {
  gchar *contents = NULL;
  gsize len = 0;
  if (!g_file_get_contents("/proc/self/status", &contents, &len, NULL) ||
      !contents) {
    g_free(contents);
    return 0;
  }

  guint64 rss_kb = 0;
  gchar **lines = g_strsplit(contents, "\n", -1);
  for (gchar **p = lines; p && *p; p++) {
    if (g_str_has_prefix(*p, "VmRSS:")) {
      gchar **parts = g_strsplit_set(*p, " \t", -1);
      for (gchar **q = parts; q && *q; q++) {
        if (**q && g_ascii_isdigit(**q)) {
          rss_kb = (guint64)g_ascii_strtoull(*q, NULL, 10);
          break;
        }
      }
      g_strfreev(parts);
      break;
    }
  }
  g_strfreev(lines);
  g_free(contents);
  return rss_kb;
}

static void maybe_malloc_trim(void) {
#ifdef __GLIBC__
  const gchar *env = g_getenv("REELVAULT_MALLOC_TRIM");
  if (env && *env && g_strcmp0(env, "0") != 0) {
    malloc_trim(0);
  }
#endif
}

static gboolean mem_debug_tick(gpointer data) {
  ReelApp *app = (ReelApp *)data;
  if (!app)
    return G_SOURCE_REMOVE;
  g_printerr("[mem] rss=%lukB films_loaded=%d posters_loaded=%d\n",
             (unsigned long)read_rss_kb(), app->films_next_offset,
             app->grid_posters_loaded);
  return G_SOURCE_CONTINUE;
}

/* Forward declarations */
static void apply_theme_css(ReelApp *app);
static gboolean on_main_window_configure(GtkWidget *widget,
                                        GdkEventConfigure *event,
                                        gpointer user_data);
static gboolean on_main_window_state(GtkWidget *widget,
                                     GdkEventWindowState *event,
                                     gpointer user_data);
static gboolean on_main_window_key_press(GtkWidget *widget, GdkEventKey *event,
                                        gpointer user_data);

static gboolean startup_debug_enabled(void) {
  static gint inited = 0;
  static gboolean enabled = FALSE;
  if (!inited) {
    const gchar *env = g_getenv("REELVAULT_STARTUP_DEBUG");
    enabled = (env && *env && g_strcmp0(env, "0") != 0);
    inited = 1;
  }
  return enabled;
}

static void startup_log(const char *fmt, ...) {
  if (!startup_debug_enabled())
    return;
  static gint64 t0 = 0;
  if (!t0)
    t0 = g_get_monotonic_time();
  gint64 ms = (g_get_monotonic_time() - t0) / 1000;

  va_list ap;
  va_start(ap, fmt);
  gchar *msg = g_strdup_vprintf(fmt, ap);
  va_end(ap);

  g_printerr("[startup +%ldms] %s\n", (long)ms, msg ? msg : "");
  g_free(msg);
}

typedef struct {
  ReelApp *app;
  guint gen;
  gchar *db_path;
  FilterState filter;
  gint offset_start;
  gint page_size;
  gboolean include_counts;
} FilmsLoadRequest;

typedef struct {
  ReelApp *app;
  guint gen;
  GList *films; /* owned */
} FilmsPagePayload;

typedef struct {
  ReelApp *app;
  guint gen;
  gint total;
  gint unmatched;
} FilmsCountsPayload;

static void filter_state_clone(FilterState *dst, const FilterState *src);
static void filter_state_free_members(FilterState *f);
static gboolean films_counts_idle(gpointer data);
static gboolean films_page_idle(gpointer data);
static gboolean films_done_idle(gpointer data);
static gpointer films_load_thread(gpointer data);
static void request_next_page(ReelApp *app);
static void maybe_request_next_page(ReelApp *app);
static void on_grid_scroll_changed(GtkAdjustment *adj, gpointer user_data);

void window_apply_theme(ReelApp *app, GtkWidget *toplevel) {
  if (!toplevel)
    return;

  GtkSettings *settings = gtk_settings_get_default();

  /* Apply GTK theme override (if set) */
  if (app->gtk_theme_name && strlen(app->gtk_theme_name) > 0) {
    g_object_set(settings, "gtk-theme-name", app->gtk_theme_name, NULL);
  } else if (app->system_gtk_theme_name) {
    g_object_set(settings, "gtk-theme-name", app->system_gtk_theme_name, NULL);
  }

  gboolean prefer_dark = app->system_prefer_dark;
  if (app->theme_preference == THEME_DARK) {
    prefer_dark = TRUE;
  } else if (app->theme_preference == THEME_LIGHT) {
    prefer_dark = FALSE;
  }

  g_object_set(settings, "gtk-application-prefer-dark-theme", prefer_dark, NULL);

  gtk_widget_queue_draw(toplevel);
}

static void filter_state_clone(FilterState *dst, const FilterState *src) {
  memset(dst, 0, sizeof(*dst));
  if (!src)
    return;
  dst->genre = g_strdup(src->genre);
  dst->year_from = src->year_from;
  dst->year_to = src->year_to;
  dst->actor = g_strdup(src->actor);
  dst->director = g_strdup(src->director);
  dst->search_text = g_strdup(src->search_text);
  dst->plot_text = g_strdup(src->plot_text);
  dst->sort_by = g_strdup(src->sort_by);
  dst->sort_ascending = src->sort_ascending;
}

static void filter_state_free_members(FilterState *f) {
  if (!f)
    return;
  g_free(f->genre);
  g_free(f->actor);
  g_free(f->director);
  g_free(f->search_text);
  g_free(f->sort_by);
  memset(f, 0, sizeof(*f));
}

static gboolean films_counts_idle(gpointer data) {
  FilmsCountsPayload *p = (FilmsCountsPayload *)data;
  if (p->gen != p->app->films_refresh_gen) {
    g_free(p);
    return G_SOURCE_REMOVE;
  }
  p->app->total_films = p->total;
  p->app->unmatched_films = p->unmatched;
  window_update_status_bar(p->app);
  g_free(p);
  return G_SOURCE_REMOVE;
}

static gboolean films_page_idle(gpointer data) {
  FilmsPagePayload *p = (FilmsPagePayload *)data;
  if (p->gen != p->app->films_refresh_gen) {
    g_list_free_full(p->films, (GDestroyNotify)film_free);
    g_free(p);
    return G_SOURCE_REMOVE;
  }

  if (!p->films) {
    p->app->films_end_reached = TRUE;
    g_free(p);
    return G_SOURCE_REMOVE;
  }

  gint added = g_list_length(p->films);
  p->app->films = g_list_concat(p->app->films, p->films);
  grid_append_films(p->app, p->films);
  p->app->films_next_offset += added;
  if (added < 250)
    p->app->films_end_reached = TRUE;

  maybe_request_next_page(p->app);
  g_free(p);
  return G_SOURCE_REMOVE;
}

static gboolean films_done_idle(gpointer data) {
  FilmsLoadRequest *req = (FilmsLoadRequest *)data;
  if (req->gen == req->app->films_refresh_gen) {
    if (req->page_size > 0)
      req->app->films_loading = FALSE;
    if (req->app->genres_dirty) {
      filter_bar_refresh(req->app);
      req->app->genres_dirty = FALSE;
    }
  }

  filter_state_free_members(&req->filter);
  g_free(req->db_path);
  g_free(req);
  return G_SOURCE_REMOVE;
}

static gpointer films_load_thread(gpointer data) {
  FilmsLoadRequest *req = (FilmsLoadRequest *)data;
  startup_log("films_load_thread: open db (offset_start=%d)", req->offset_start);
  sqlite3 *db = db_open_readonly(req->db_path);
  if (!db) {
    startup_log("films_load_thread: failed to open db");
    g_idle_add(films_done_idle, req);
    return NULL;
  }

  if (req->include_counts) {
    FilmsCountsPayload *counts = g_new0(FilmsCountsPayload, 1);
    counts->app = req->app;
    counts->gen = req->gen;
    counts->total = db_films_count_db(db);
    counts->unmatched = db_films_count_unmatched_db(db);
    startup_log("films_load_thread: counts total=%d unmatched=%d", counts->total,
                counts->unmatched);
    g_idle_add(films_counts_idle, counts);
  }

  if (req->page_size > 0 && req->gen == req->app->films_refresh_gen) {
    gint64 t_page0 = g_get_monotonic_time();
    GList *page =
        db_films_get_page_db(db, &req->filter, req->page_size, req->offset_start);
    gint64 page_ms = (g_get_monotonic_time() - t_page0) / 1000;
    int page_len = page ? g_list_length(page) : 0;
    startup_log("films_load_thread: loaded page offset=%d size=%d (%ldms)",
                req->offset_start, page_len, (long)page_ms);

    FilmsPagePayload *p = g_new0(FilmsPagePayload, 1);
    p->app = req->app;
    p->gen = req->gen;
    p->films = page; /* may be NULL */
    g_idle_add(films_page_idle, p);
  }

  db_close_handle(db);
  startup_log("films_load_thread: done");
  g_idle_add(films_done_idle, req);
  return NULL;
}

void window_create(ReelApp *app) {
  startup_log("window_create: start");
  if (app->system_prefer_dark == FALSE) {
    g_object_get(gtk_settings_get_default(),
                 "gtk-application-prefer-dark-theme", &app->system_prefer_dark,
                 NULL);
  }

  if (app->system_gtk_theme_name == NULL) {
    gchar *theme_name = NULL;
    g_object_get(gtk_settings_get_default(), "gtk-theme-name", &theme_name, NULL);
    app->system_gtk_theme_name = theme_name;
  }

  /* Get DPI scale factor from GDK */
  GdkScreen *screen = gdk_screen_get_default();
  if (screen) {
    gdouble dpi = gdk_screen_get_resolution(screen);
    app->scale_factor = (dpi > 0) ? dpi / 96.0 : 1.0;
    /* Clamp to reasonable range */
    if (app->scale_factor < 1.0)
      app->scale_factor = 1.0;
    if (app->scale_factor > 3.0)
      app->scale_factor = 3.0;
  } else {
    app->scale_factor = 1.0;
  }
  g_print("DPI scale factor: %.2f\n", app->scale_factor);

  /* Create main window */
  app->window = gtk_application_window_new(app->app);
  gtk_window_set_title(GTK_WINDOW(app->window), APP_NAME);
  if (app->window_geometry_valid && app->window_width > 0 &&
      app->window_height > 0) {
    /* Use resize (not default_size) to avoid size drift from mismatched
       decoration/client-area measurements across sessions. */
    gtk_window_resize(GTK_WINDOW(app->window), app->window_width,
                      app->window_height);
  } else {
    gtk_window_set_default_size(GTK_WINDOW(app->window), 1200, 800);
    gtk_window_set_position(GTK_WINDOW(app->window), GTK_WIN_POS_CENTER);
  }

  if (app->window_geometry_valid && !app->window_maximized) {
    gtk_window_move(GTK_WINDOW(app->window), app->window_x, app->window_y);
  }

  if (app->window_maximized) {
    gtk_window_maximize(GTK_WINDOW(app->window));
  }

  g_signal_connect(app->window, "configure-event",
                   G_CALLBACK(on_main_window_configure), app);
  g_signal_connect(app->window, "window-state-event",
                   G_CALLBACK(on_main_window_state), app);
  g_signal_connect(app->window, "key-press-event",
                   G_CALLBACK(on_main_window_key_press), app);

  /* Main vertical box */
  GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add(GTK_CONTAINER(app->window), main_box);

  /* Header bar */
  GtkWidget *header = gtk_header_bar_new();
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(header), APP_NAME);
  gtk_window_set_titlebar(GTK_WINDOW(app->window), header);

  /* Filter bar */
  app->filter_bar = filter_bar_create(app);
  gtk_box_pack_start(GTK_BOX(main_box), app->filter_bar, FALSE, FALSE, 0);

  /* Scrolled window for poster grid */
  app->grid_scrolled = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(app->grid_scrolled),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start(GTK_BOX(main_box), app->grid_scrolled, TRUE, TRUE, 0);

  /* Poster grid */
  app->grid_view = grid_create(app);
  gtk_container_add(GTK_CONTAINER(app->grid_scrolled), app->grid_view);

  GtkAdjustment *vadj =
      gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(app->grid_scrolled));
  if (vadj) {
    g_signal_connect(vadj, "value-changed", G_CALLBACK(on_grid_scroll_changed),
                     app);
  }

  /* Status bar */
  app->status_bar = NULL;

  /* Initial load */
  startup_log("window_create: initial window_refresh_films()");
  window_refresh_films(app);
  window_update_status_bar(app);
  filter_bar_refresh(app);

  if (!app->mem_debug_source) {
    const gchar *env = g_getenv("REELVAULT_MEM_DEBUG");
    if (env && *env && g_strcmp0(env, "0") != 0) {
      app->mem_debug_source = g_timeout_add_seconds(5, mem_debug_tick, app);
    }
  }

  /* Apply theme CSS */
  apply_theme_css(app);
  startup_log("window_create: done");
}

static gboolean on_main_window_configure(GtkWidget *widget,
                                        GdkEventConfigure *event,
                                        gpointer user_data) {
  ReelApp *app = (ReelApp *)user_data;
  if (!app || !event)
    return GDK_EVENT_PROPAGATE;

  if (!GTK_IS_WINDOW(widget))
    return GDK_EVENT_PROPAGATE;

  if (app->window_maximized)
    return GDK_EVENT_PROPAGATE;

  /* Use GtkWindow getters rather than raw configure-event values to avoid
     decoration/scale-factor drift (which can cause the window to grow on each
     launch). */
  gint w = 0, h = 0;
  gtk_window_get_size(GTK_WINDOW(widget), &w, &h);

  if (w > 0 && h > 0) {
    app->window_width = w;
    app->window_height = h;
    app->window_geometry_valid = TRUE;
  }
  /* For position, prefer the configure-event coordinates; using
     gtk_window_get_position() can introduce a constant decoration offset that
     accumulates across restarts on some WMs. */
  app->window_x = event->x;
  app->window_y = event->y;

  return GDK_EVENT_PROPAGATE;
}

static gboolean on_main_window_key_press(GtkWidget *widget, GdkEventKey *event,
                                        gpointer user_data) {
  (void)widget;
  ReelApp *app = (ReelApp *)user_data;
  if (!app || !event)
    return GDK_EVENT_PROPAGATE;

  if ((event->state & GDK_CONTROL_MASK) &&
      (event->keyval == GDK_KEY_f || event->keyval == GDK_KEY_F)) {
    filter_bar_focus_search(app);
    return GDK_EVENT_STOP;
  }

  return GDK_EVENT_PROPAGATE;
}

static gboolean on_main_window_state(GtkWidget *widget,
                                     GdkEventWindowState *event,
                                     gpointer user_data) {
  (void)widget;
  ReelApp *app = (ReelApp *)user_data;
  if (!app || !event)
    return GDK_EVENT_PROPAGATE;

  app->window_maximized =
      (event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) != 0;
  return GDK_EVENT_PROPAGATE;
}

static void scan_theme_root(GHashTable *set, const gchar *root) {
  if (!root || !g_file_test(root, G_FILE_TEST_IS_DIR))
    return;

  GDir *dir = g_dir_open(root, 0, NULL);
  if (!dir)
    return;

  const gchar *name;
  while ((name = g_dir_read_name(dir)) != NULL) {
    if (name[0] == '.')
      continue;

    gchar *gtk3_dir = g_build_filename(root, name, "gtk-3.0", NULL);
    if (g_file_test(gtk3_dir, G_FILE_TEST_IS_DIR)) {
      g_hash_table_add(set, g_strdup(name));
    }
    g_free(gtk3_dir);
  }

  g_dir_close(dir);
}

static gint theme_name_cmp(gconstpointer a, gconstpointer b) {
  return g_ascii_strcasecmp((const gchar *)a, (const gchar *)b);
}

static GList *discover_gtk_themes(void) {
  GHashTable *set = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

  scan_theme_root(set, "/usr/share/themes");
  scan_theme_root(set, "/usr/local/share/themes");
  gchar *home_themes = g_build_filename(g_get_home_dir(), ".themes", NULL);
  scan_theme_root(set, home_themes);
  g_free(home_themes);

  gchar *user_themes = g_build_filename(g_get_user_data_dir(), "themes", NULL);
  scan_theme_root(set, user_themes);
  g_free(user_themes);

  GList *names = NULL;
  GHashTableIter iter;
  gpointer key;
  g_hash_table_iter_init(&iter, set);
  while (g_hash_table_iter_next(&iter, &key, NULL)) {
    names = g_list_prepend(names, g_strdup((const gchar *)key));
  }
  names = g_list_sort(names, theme_name_cmp);

  g_hash_table_destroy(set);
  return names; /* caller frees list + strings */
}

/* Apply comprehensive dark/light theme CSS */
static void apply_theme_css(ReelApp *app) {
  GtkCssProvider *css = gtk_css_provider_new();

  /* Calculate scaled font sizes */
  gint base_font = (gint)(11 * app->scale_factor);
  gint small_font = (gint)(10 * app->scale_factor);
  gint badge_font = (gint)(10 * app->scale_factor);

  gchar *css_str = g_strdup_printf(
      /* ===== LIGHT/DARK ADAPTIVE THEME ===== */

      /* Poster grid items - tight spacing */
      "flowbox > flowboxchild {"
      "    margin: 1px;"
      "    padding: 3px;"
      "    border-radius: 6px;"
      "    transition: all 200ms ease;"
      "}"
      "flowbox > flowboxchild:hover {"
      "    background-color: alpha(@theme_selected_bg_color, 0.15);"
      "    box-shadow: 0 2px 8px alpha(black, 0.15);"
      "}"
      "flowbox > flowboxchild:selected {"
      "    background-color: alpha(@theme_selected_bg_color, 0.3);"
      "}"

      /* Poster title - DPI-aware sizing */
      ".poster-title {"
      "    font-size: %dpt;"
      "    font-weight: 600;"
      "    margin-top: 6px;"
      "    color: @theme_fg_color;"
      "}"

      /* Poster year - DPI-aware sizing */
      ".poster-year {"
      "    font-size: %dpt;"
      "    color: alpha(@theme_fg_color, 0.6);"
      "}"

      /* Unmatched badge */
      ".unmatched-badge {"
      "    background-color: #e67e22;"
      "    color: white;"
      "    border-radius: 12px;"
      "    padding: 4px 8px;"
      "    font-size: %dpt;"
      "    font-weight: bold;"
      "    box-shadow: 0 1px 3px alpha(black, 0.3);"
      "}"

      /* Status bar styling */
      "statusbar {"
      "    padding: 4px 12px;"
      "    font-size: %dpt;"
      "}"

      /* Filter bar styling */
      ".filter-bar {"
      "    padding: 8px 12px;"
      "    background-color: alpha(@theme_bg_color, 0.97);"
      "    border-bottom: 1px solid alpha(@theme_fg_color, 0.1);"
      "}"
      ".filter-bar entry {"
      "    min-width: 200px;"
      "}"
      ,
      base_font, small_font, badge_font, small_font);

  gtk_css_provider_load_from_data(css, css_str, -1, NULL);
  g_free(css_str);

  gtk_style_context_add_provider_for_screen(
      gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(css);

  /* Apply initial theme based on preference */
  window_apply_theme(app, app->window);
}

void window_refresh_films(ReelApp *app) {
  startup_log("window_refresh_films: begin gen=%u", app->films_refresh_gen + 1);
  /* Cancel any existing grid population and clear current UI quickly. */
  app->films_refresh_gen++;
  app->films_loading = FALSE;
  app->films_end_reached = FALSE;
  app->films_next_offset = 0;

  if (app->films) {
    g_list_free_full(app->films, (GDestroyNotify)film_free);
    app->films = NULL;
  }
  grid_clear(app);
  if (app->grid_scrolled) {
    GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(
        GTK_SCROLLED_WINDOW(app->grid_scrolled));
    if (adj)
      gtk_adjustment_set_value(adj, 0.0);
  }
  app->grid_posters_loaded = 0;
  maybe_malloc_trim();

  /* Fast first paint: load a small first page synchronously on the UI thread. */
  const int first_page = 80;
  gint64 t0 = g_get_monotonic_time();
  GList *initial = db_films_get_page_db(app->db, &app->filter, first_page, 0);
  gint64 ms = (g_get_monotonic_time() - t0) / 1000;
  startup_log("window_refresh_films: initial page size=%d (%ldms)",
              initial ? g_list_length(initial) : 0, (long)ms);
  if (initial) {
    app->films = initial;
    grid_append_films(app, initial);
  }
  app->films_next_offset = initial ? g_list_length(initial) : 0;
  app->films_end_reached = (!initial || app->films_next_offset < first_page);

  FilmsLoadRequest *req = g_new0(FilmsLoadRequest, 1);
  req->app = app;
  req->gen = app->films_refresh_gen;
  req->db_path = g_strdup(app->db_path);
  filter_state_clone(&req->filter, &app->filter);
  req->offset_start = 0;
  req->page_size = 0; /* counts only */
  req->include_counts = TRUE;

  startup_log("window_refresh_films: start films_load_thread (counts only)");
  g_thread_new("films-load", films_load_thread, req);

  /* If the first page doesn't fill the viewport, load more lazily. */
  maybe_request_next_page(app);
}

static void request_next_page(ReelApp *app) {
  if (!app || app->films_loading || app->films_end_reached)
    return;

  app->films_loading = TRUE;

  FilmsLoadRequest *req = g_new0(FilmsLoadRequest, 1);
  req->app = app;
  req->gen = app->films_refresh_gen;
  req->db_path = g_strdup(app->db_path);
  filter_state_clone(&req->filter, &app->filter);
  req->offset_start = app->films_next_offset;
  req->page_size = 250;
  req->include_counts = FALSE;

  startup_log("request_next_page: offset=%d", req->offset_start);
  g_thread_new("films-load", films_load_thread, req);
}

static void maybe_request_next_page(ReelApp *app) {
  if (!app || app->films_loading || app->films_end_reached)
    return;
  if (!app->grid_scrolled)
    return;

  GtkAdjustment *adj =
      gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(app->grid_scrolled));
  if (!adj)
    return;

  gdouble value = gtk_adjustment_get_value(adj);
  gdouble page = gtk_adjustment_get_page_size(adj);
  gdouble upper = gtk_adjustment_get_upper(adj);

  /* Load more when near bottom or when there's no real scrolling yet. */
  gdouble remaining = upper - (value + page);
  if (remaining < 400 || upper <= page + 1) {
    request_next_page(app);
  }
}

static void on_grid_scroll_changed(GtkAdjustment *adj, gpointer user_data) {
  (void)adj;
  ReelApp *app = (ReelApp *)user_data;
  maybe_request_next_page(app);
}

void window_update_status_bar(ReelApp *app) {
  if (!app->status_bar)
    return;
  gchar *status = g_strdup_printf("%d films | %d unmatched", app->total_films,
                                  app->unmatched_films);
  gtk_statusbar_pop(GTK_STATUSBAR(app->status_bar), 0);
  gtk_statusbar_push(GTK_STATUSBAR(app->status_bar), 0, status);
  g_free(status);
}

void window_refresh_film(ReelApp *app, gint64 film_id) {
  if (!app)
    return;

  Film *updated = db_film_get_by_id(app, film_id);
  if (!updated)
    return;

  gboolean replaced = FALSE;
  for (GList *l = app->films; l != NULL; l = l->next) {
    Film *existing = (Film *)l->data;
    if (existing && existing->id == film_id) {
      film_free(existing);
      l->data = updated;
      replaced = TRUE;
      break;
    }
  }

  if (replaced) {
    grid_update_film(app, updated);
  } else {
    /* Film not in current view (filtered out); just drop it. */
    film_free(updated);
  }

  if (app->genres_dirty) {
    filter_bar_refresh(app);
    app->genres_dirty = FALSE;
  }
}

typedef struct {
  ReelApp *app;
  GtkWidget *dialog;
  GtkWidget *label;
  GtkWidget *progress;
  gboolean canceled;
} ImportProgressUi;

static void import_progress_destroy(ImportProgressUi *ui) {
  if (!ui)
    return;
  if (ui->dialog)
    gtk_widget_destroy(ui->dialog);
  g_free(ui);
}

static void on_import_dialog_response(GtkDialog *dialog, gint response_id,
                                      gpointer user_data) {
  (void)dialog;
  ImportProgressUi *ui = (ImportProgressUi *)user_data;
  if (response_id == GTK_RESPONSE_CANCEL) {
    ui->canceled = TRUE;
    scraper_stop(ui->app);
  }
}

static void scraper_progress_cb(ReelApp *app, gint done, gint total,
                                const gchar *current_title,
                                gpointer user_data) {
  (void)app;
  ImportProgressUi *ui = (ImportProgressUi *)user_data;
  if (!ui || ui->canceled)
    return;

  if (total <= 0)
    total = 1;

  gchar *text = g_strdup_printf("Fetching metadata (%d/%d): %s", done, total,
                                current_title ? current_title : "");
  gtk_label_set_text(GTK_LABEL(ui->label), text);
  g_free(text);

  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui->progress),
                                (gdouble)done / (gdouble)total);
}

static void scraper_done_cb(ReelApp *app, gboolean canceled,
                            gpointer user_data) {
  ImportProgressUi *ui = (ImportProgressUi *)user_data;
  if (!ui)
    return;

  window_refresh_films(app);

  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui->progress), 1.0);
  gtk_label_set_text(GTK_LABEL(ui->label),
                     canceled ? "Import canceled." : "Import complete.");

  import_progress_destroy(ui);
}

static void window_scan_paths(ReelApp *app, gchar **paths, gint paths_count) {
  if (!app || !paths || paths_count <= 0)
    return;

  ImportProgressUi *ui = g_new0(ImportProgressUi, 1);
  ui->app = app;

  ui->dialog = gtk_dialog_new_with_buttons(
      "Import Library", GTK_WINDOW(app->window),
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel",
      GTK_RESPONSE_CANCEL, NULL);
  window_apply_theme(app, ui->dialog);
  gtk_window_set_default_size(GTK_WINDOW(ui->dialog), 520, 160);

  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(ui->dialog));
  gtk_container_set_border_width(GTK_CONTAINER(content), 12);
  gtk_box_set_spacing(GTK_BOX(content), 10);

  ui->label = gtk_label_new("Scanning library...");
  gtk_label_set_xalign(GTK_LABEL(ui->label), 0);
  gtk_box_pack_start(GTK_BOX(content), ui->label, FALSE, FALSE, 0);

  ui->progress = gtk_progress_bar_new();
  gtk_progress_bar_pulse(GTK_PROGRESS_BAR(ui->progress));
  gtk_box_pack_start(GTK_BOX(content), ui->progress, FALSE, FALSE, 0);

  g_signal_connect(ui->dialog, "response", G_CALLBACK(on_import_dialog_response),
                   ui);

  gtk_widget_show_all(ui->dialog);

  while (gtk_events_pending())
    gtk_main_iteration();

  int new_films = 0;
  for (int i = 0; i < paths_count; i++) {
    if (ui->canceled)
      break;
    if (!paths[i] || !*paths[i])
      continue;

    gchar *text = g_strdup_printf("Scanning: %s", paths[i]);
    gtk_label_set_text(GTK_LABEL(ui->label), text);
    g_free(text);

    gtk_progress_bar_pulse(GTK_PROGRESS_BAR(ui->progress));
    while (gtk_events_pending())
      gtk_main_iteration();

    new_films += scanner_scan_directory(app, paths[i]);
  }

  window_refresh_films(app);

  if (ui->canceled) {
    gtk_label_set_text(GTK_LABEL(ui->label), "Import canceled.");
    import_progress_destroy(ui);
    return;
  }

  if (app->tmdb_api_key && strlen(app->tmdb_api_key) > 0 && new_films > 0) {
    gtk_label_set_text(GTK_LABEL(ui->label), "Fetching metadata from TMDB...");
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui->progress), 0.0);
    scraper_start_background_with_progress(app, scraper_progress_cb,
                                          scraper_done_cb, ui);
    return;
  }

  gtk_label_set_text(GTK_LABEL(ui->label), "Import complete.");
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui->progress), 1.0);
  import_progress_destroy(ui);
}

typedef struct {
  ReelApp *app;
  gchar **paths; /* owned strv */
  gint count;
} SettingsAutoScanTask;

static gboolean settings_auto_scan_idle(gpointer data) {
  SettingsAutoScanTask *task = (SettingsAutoScanTask *)data;
  if (task && task->app && task->paths && task->count > 0) {
    window_scan_paths(task->app, task->paths, task->count);
  }
  if (task) {
    g_strfreev(task->paths);
    g_free(task);
  }
  return G_SOURCE_REMOVE;
}

static gboolean library_path_exists(ReelApp *app, const gchar *path) {
  if (!app || !path || !*path)
    return FALSE;
  for (int i = 0; i < app->library_paths_count; i++) {
    if (g_strcmp0(app->library_paths[i], path) == 0)
      return TRUE;
  }
  return FALSE;
}

/* Callback for add folder button in settings */
static void on_add_folder_clicked(GtkButton *btn, gpointer user_data) {
  GtkWidget *settings_dialog = GTK_WIDGET(user_data);
  ReelApp *app = g_object_get_data(G_OBJECT(settings_dialog), "app");
  GtkWidget *lib_list =
      g_object_get_data(G_OBJECT(settings_dialog), "lib_list");

  GtkWidget *chooser = gtk_file_chooser_dialog_new(
      "Select Film Directory", GTK_WINDOW(settings_dialog),
      GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Cancel", GTK_RESPONSE_CANCEL,
      "_Select", GTK_RESPONSE_ACCEPT, NULL);

  if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
    gchar *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
    if (!folder || !*folder) {
      g_free(folder);
      gtk_widget_destroy(chooser);
      (void)btn;
      return;
    }

    if (library_path_exists(app, folder)) {
      g_free(folder);
      gtk_widget_destroy(chooser);
      (void)btn;
      return;
    }

    config_add_library_path(app, folder);

    GtkWidget *row = gtk_label_new(folder);
    gtk_label_set_xalign(GTK_LABEL(row), 0);
    gtk_widget_show(row);
    gtk_list_box_insert(GTK_LIST_BOX(lib_list), row, -1);

    GPtrArray *added = g_object_get_data(G_OBJECT(settings_dialog), "added_paths");
    if (added) {
      g_ptr_array_add(added, g_strdup(folder));
    }

    g_free(folder);
  }

  gtk_widget_destroy(chooser);
  (void)btn;
}

static void on_remove_folder_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  GtkWidget *settings_dialog = GTK_WIDGET(user_data);
  ReelApp *app = g_object_get_data(G_OBJECT(settings_dialog), "app");
  GtkWidget *lib_list =
      g_object_get_data(G_OBJECT(settings_dialog), "lib_list");

  GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(lib_list));
  if (!row)
    return;

  GtkWidget *child = gtk_bin_get_child(GTK_BIN(row));
  if (!GTK_IS_LABEL(child))
    return;

  const gchar *path = gtk_label_get_text(GTK_LABEL(child));
  if (path && strlen(path) > 0) {
    config_remove_library_path(app, path);
  }

  gtk_widget_destroy(GTK_WIDGET(row));
}

void window_show_settings(ReelApp *app) {
  GtkWidget *dialog = gtk_dialog_new_with_buttons(
      "Settings", GTK_WINDOW(app->window),
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel",
      GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);

  gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 400);
  window_apply_theme(app, dialog);

  GPtrArray *added_paths = g_ptr_array_new_with_free_func(g_free);
  g_object_set_data_full(G_OBJECT(dialog), "added_paths", added_paths,
                         (GDestroyNotify)g_ptr_array_unref);

  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  gtk_container_set_border_width(GTK_CONTAINER(content), 12);
  gtk_box_set_spacing(GTK_BOX(content), 12);

  /* Appearance */
  GtkWidget *appearance_frame = gtk_frame_new("Appearance");
  gtk_box_pack_start(GTK_BOX(content), appearance_frame, FALSE, FALSE, 0);
  gtk_container_set_border_width(GTK_CONTAINER(appearance_frame), 6);

  GtkWidget *appearance_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(appearance_grid), 8);
  gtk_grid_set_column_spacing(GTK_GRID(appearance_grid), 12);
  gtk_container_add(GTK_CONTAINER(appearance_frame), appearance_grid);

  GtkWidget *scheme_label = gtk_label_new("Color scheme:");
  gtk_label_set_xalign(GTK_LABEL(scheme_label), 0);
  gtk_widget_set_halign(scheme_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(appearance_grid), scheme_label, 0, 0, 1, 1);

  GtkWidget *scheme_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(scheme_combo), "system",
                            "System");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(scheme_combo), "light", "Light");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(scheme_combo), "dark", "Dark");
  const gchar *scheme_id = "system";
  if (app->theme_preference == THEME_LIGHT) {
    scheme_id = "light";
  } else if (app->theme_preference == THEME_DARK) {
    scheme_id = "dark";
  }
  gtk_combo_box_set_active_id(GTK_COMBO_BOX(scheme_combo), scheme_id);
  gtk_widget_set_hexpand(scheme_combo, TRUE);
  gtk_grid_attach(GTK_GRID(appearance_grid), scheme_combo, 1, 0, 1, 1);

  GtkWidget *theme_label = gtk_label_new("GTK theme:");
  gtk_label_set_xalign(GTK_LABEL(theme_label), 0);
  gtk_widget_set_halign(theme_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(appearance_grid), theme_label, 0, 1, 1, 1);

  GtkWidget *theme_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme_combo),
                            "__system_default__", "System Default");
  GList *themes = discover_gtk_themes();
  for (GList *l = themes; l != NULL; l = l->next) {
    const gchar *name = (const gchar *)l->data;
    if (name && *name) {
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme_combo), name, name);
    }
  }

  if (app->gtk_theme_name && strlen(app->gtk_theme_name) > 0) {
    if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(theme_combo),
                                     app->gtk_theme_name)) {
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme_combo),
                                app->gtk_theme_name, app->gtk_theme_name);
      gtk_combo_box_set_active_id(GTK_COMBO_BOX(theme_combo),
                                  app->gtk_theme_name);
    }
  } else {
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(theme_combo), "__system_default__");
  }
  gtk_widget_set_hexpand(theme_combo, TRUE);
  gtk_grid_attach(GTK_GRID(appearance_grid), theme_combo, 1, 1, 1, 1);

  g_list_free_full(themes, g_free);

  /* TMDB API Key */
  GtkWidget *api_frame = gtk_frame_new("TMDB API Key");
  gtk_box_pack_start(GTK_BOX(content), api_frame, FALSE, FALSE, 0);

  GtkWidget *api_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(api_entry),
                                 "Enter your TMDB API key");
  if (app->tmdb_api_key) {
    gtk_entry_set_text(GTK_ENTRY(api_entry), app->tmdb_api_key);
  }
  gtk_container_add(GTK_CONTAINER(api_frame), api_entry);
  gtk_container_set_border_width(GTK_CONTAINER(api_frame), 6);

  /* Player Command */
  GtkWidget *player_frame = gtk_frame_new("Video Player");
  gtk_box_pack_start(GTK_BOX(content), player_frame, FALSE, FALSE, 0);

  GtkWidget *player_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(
      GTK_ENTRY(player_entry),
      "Command to launch video player (e.g., mpv, vlc)");
  if (app->player_command) {
    gtk_entry_set_text(GTK_ENTRY(player_entry), app->player_command);
  }
  gtk_container_add(GTK_CONTAINER(player_frame), player_entry);
  gtk_container_set_border_width(GTK_CONTAINER(player_frame), 6);

  /* Library Paths */
  GtkWidget *lib_frame = gtk_frame_new("Library Paths");
  gtk_box_pack_start(GTK_BOX(content), lib_frame, TRUE, TRUE, 0);

  GtkWidget *lib_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_container_add(GTK_CONTAINER(lib_frame), lib_box);
  gtk_container_set_border_width(GTK_CONTAINER(lib_frame), 6);

  GtkWidget *lib_scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(lib_scroll),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  /* Make the list tall enough to comfortably show ~4 rows. */
  gtk_widget_set_size_request(lib_scroll, -1, (gint)(4 * 32 * app->scale_factor));
  gtk_box_pack_start(GTK_BOX(lib_box), lib_scroll, TRUE, TRUE, 0);

  GtkWidget *lib_list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(lib_list),
                                  GTK_SELECTION_SINGLE);
  gtk_container_add(GTK_CONTAINER(lib_scroll), lib_list);

  /* Populate library paths */
  for (int i = 0; i < app->library_paths_count; i++) {
    GtkWidget *row = gtk_label_new(app->library_paths[i]);
    gtk_label_set_xalign(GTK_LABEL(row), 0);
    gtk_list_box_insert(GTK_LIST_BOX(lib_list), row, -1);
  }

  /* Add/Remove buttons */
  GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_pack_start(GTK_BOX(lib_box), btn_box, FALSE, FALSE, 0);

  GtkWidget *add_btn = gtk_button_new_with_label("Add Folder...");
  gtk_box_pack_start(GTK_BOX(btn_box), add_btn, FALSE, FALSE, 0);

  GtkWidget *remove_btn = gtk_button_new_with_label("Remove Selected");
  gtk_box_pack_start(GTK_BOX(btn_box), remove_btn, FALSE, FALSE, 0);

  /* Store references for callback */
  g_object_set_data(G_OBJECT(dialog), "api_entry", api_entry);
  g_object_set_data(G_OBJECT(dialog), "player_entry", player_entry);
  g_object_set_data(G_OBJECT(dialog), "lib_list", lib_list);
  g_object_set_data(G_OBJECT(dialog), "app", app);

  /* Add folder callback */
  g_signal_connect(add_btn, "clicked", G_CALLBACK(on_add_folder_clicked),
                   dialog);
  g_signal_connect(remove_btn, "clicked", G_CALLBACK(on_remove_folder_clicked),
                   dialog);

  gtk_widget_show_all(dialog);

  if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
    /* Save settings */
    const gchar *api_text = gtk_entry_get_text(GTK_ENTRY(api_entry));
    const gchar *player_text = gtk_entry_get_text(GTK_ENTRY(player_entry));
    const gchar *scheme_selected =
        gtk_combo_box_get_active_id(GTK_COMBO_BOX(scheme_combo));
    const gchar *gtk_theme_selected =
        gtk_combo_box_get_active_id(GTK_COMBO_BOX(theme_combo));

    if (scheme_selected) {
      if (g_strcmp0(scheme_selected, "light") == 0) {
        app->theme_preference = THEME_LIGHT;
      } else if (g_strcmp0(scheme_selected, "dark") == 0) {
        app->theme_preference = THEME_DARK;
      } else {
        app->theme_preference = THEME_SYSTEM;
      }
    }

    g_free(app->gtk_theme_name);
    app->gtk_theme_name = NULL;
    if (gtk_theme_selected &&
        g_strcmp0(gtk_theme_selected, "__system_default__") != 0 &&
        strlen(gtk_theme_selected) > 0) {
      app->gtk_theme_name = g_strdup(gtk_theme_selected);
    }

    if (api_text && strlen(api_text) > 0) {
      g_free(app->tmdb_api_key);
      app->tmdb_api_key = g_strdup(api_text);
    }

    if (player_text && strlen(player_text) > 0) {
      g_free(app->player_command);
      app->player_command = g_strdup(player_text);
    }

    window_apply_theme(app, app->window);
    config_save(app);

    /* Auto-scan when new folders were added in this dialog session. */
    GPtrArray *added = g_object_get_data(G_OBJECT(dialog), "added_paths");
    gint added_n = added ? (gint)added->len : 0;
    if (added_n > 0) {
      SettingsAutoScanTask *task = g_new0(SettingsAutoScanTask, 1);
      task->app = app;
      task->count = added_n;
      task->paths = g_new0(gchar *, added_n + 1); /* strv */
      for (gint i = 0; i < added_n; i++) {
        const gchar *p = (const gchar *)g_ptr_array_index(added, i);
        task->paths[i] = g_strdup(p ? p : "");
      }
      gtk_widget_destroy(dialog);
      /* Defer scan to idle so the UI can settle after closing settings. */
      g_idle_add(settings_auto_scan_idle, task);
      return;
    }
  }

  gtk_widget_destroy(dialog);
}

void window_scan_library(ReelApp *app) {
  window_scan_paths(app, app->library_paths, app->library_paths_count);
}
