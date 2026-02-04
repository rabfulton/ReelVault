/*
 * ReelGTK - Poster Grid
 * GtkFlowBox-based poster grid with DPI-aware sizing
 */

#include "grid.h"
#include "detail.h"
#include "utils.h"
#include <stdarg.h>
#include <string.h>
#include <glib/gstdio.h>
#include <unistd.h>

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

  g_printerr("[grid +%ldms] %s\n", (long)ms, msg ? msg : "");
  g_free(msg);
}

typedef struct {
  ReelApp *app;
  gchar *path;
  gint width;
  gint height;
  GWeakRef widget_ref;
  GdkPixbuf *pixbuf;
} PosterLoadTask;

static void poster_area_destroy(GtkWidget *widget, gpointer user_data) {
  ReelApp *app = (ReelApp *)user_data;
  if (!app)
    return;
  if (g_object_get_data(G_OBJECT(widget), "poster_loaded")) {
    app->grid_posters_loaded--;
  }
}

static gchar *grid_make_tmp_path_in_dir(const gchar *dest_path) {
  if (!dest_path)
    return NULL;
  gchar *dir = g_path_get_dirname(dest_path);
  if (!dir)
    return NULL;
  gchar *tmpl = g_build_filename(dir, ".reelvault_tmp_XXXXXX", NULL);
  g_free(dir);
  return tmpl;
}

static void grid_save_pixbuf_jpeg_atomic(GdkPixbuf *pixbuf,
                                        const gchar *dest_path) {
  if (!pixbuf || !dest_path)
    return;

  gchar *tmp_template = grid_make_tmp_path_in_dir(dest_path);
  if (!tmp_template)
    return;

  int fd = g_mkstemp(tmp_template);
  if (fd < 0) {
    g_free(tmp_template);
    return;
  }
  close(fd);

  GError *error = NULL;
  gboolean ok = gdk_pixbuf_save(pixbuf, tmp_template, "jpeg", &error, "quality",
                                "85", NULL);
  if (error)
    g_error_free(error);
  if (!ok) {
    g_unlink(tmp_template);
    g_free(tmp_template);
    return;
  }

  if (g_rename(tmp_template, dest_path) != 0) {
    g_unlink(tmp_template);
  }
  g_free(tmp_template);
}

static gboolean poster_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
  (void)user_data;
  GtkAllocation alloc;
  gtk_widget_get_allocation(widget, &alloc);
  if (alloc.width <= 0 || alloc.height <= 0)
    return FALSE;

  GdkPixbuf *pixbuf = g_object_get_data(G_OBJECT(widget), "poster_pixbuf");
  if (!pixbuf) {
    cairo_set_source_rgb(cr, 0.15, 0.15, 0.15);
    cairo_rectangle(cr, 0, 0, alloc.width, alloc.height);
    cairo_fill(cr);
    return FALSE;
  }

  gint pw = gdk_pixbuf_get_width(pixbuf);
  gint ph = gdk_pixbuf_get_height(pixbuf);
  if (pw <= 0 || ph <= 0)
    return FALSE;

  gdouble sx = (gdouble)alloc.width / (gdouble)pw;
  gdouble sy = (gdouble)alloc.height / (gdouble)ph;

  cairo_save(cr);
  cairo_scale(cr, sx, sy);
  gdk_cairo_set_source_pixbuf(cr, pixbuf, 0, 0);
  cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
  cairo_paint(cr);
  cairo_restore(cr);
  return FALSE;
}

static gboolean thumb_is_fresh(const gchar *original_path,
                               const gchar *thumb_path) {
  if (!original_path || !thumb_path)
    return FALSE;

  GStatBuf st_original;
  GStatBuf st_thumb;
  if (g_stat(original_path, &st_original) != 0)
    return FALSE;
  if (g_stat(thumb_path, &st_thumb) != 0)
    return FALSE;

  return st_thumb.st_mtime >= st_original.st_mtime;
}

static gchar *grid_thumb_path_for_original(const gchar *poster_path) {
  if (!poster_path)
    return NULL;
  const gchar *dot = strrchr(poster_path, '.');
  if (!dot)
    return NULL;
  return g_strdup_printf("%.*s_thumb%s", (int)(dot - poster_path), poster_path,
                         dot);
}

static gboolean poster_apply_idle(gpointer data) {
  PosterLoadTask *task = (PosterLoadTask *)data;
  GtkWidget *widget = g_weak_ref_get(&task->widget_ref);
  if (widget) {
    if (task->pixbuf) {
      if (!g_object_get_data(G_OBJECT(widget), "poster_loaded")) {
        g_object_set_data(G_OBJECT(widget), "poster_loaded",
                          GINT_TO_POINTER(1));
        if (task->app)
          task->app->grid_posters_loaded++;
      }
      g_object_set_data_full(G_OBJECT(widget), "poster_pixbuf",
                             g_object_ref(task->pixbuf),
                             (GDestroyNotify)g_object_unref);
      gtk_widget_queue_draw(widget);
    }
    g_object_unref(widget);
  }
  if (task->pixbuf)
    g_object_unref(task->pixbuf);
  g_weak_ref_clear(&task->widget_ref);
  g_free(task->path);
  g_free(task);
  return G_SOURCE_REMOVE;
}

static void poster_load_worker(gpointer data, gpointer user_data) {
  (void)user_data;
  PosterLoadTask *task = (PosterLoadTask *)data;

  static gint log_n = 0;
  if (startup_debug_enabled() && log_n < 15) {
    startup_log("poster_load_worker: start %s", task->path);
    log_n++;
  }

  /* Best effort: if we were asked to load the original and the thumb doesn't
     exist yet or is stale, generate it once in the background. For display,
     always prefer the thumb to keep memory bounded. */
  gchar *thumb_path = NULL;

  if (!g_str_has_suffix(task->path, "_thumb.jpg")) {
    thumb_path = grid_thumb_path_for_original(task->path);
    if (thumb_path) {
      gboolean need_thumb = (!g_file_test(thumb_path, G_FILE_TEST_EXISTS) ||
                             !thumb_is_fresh(task->path, thumb_path));

      if (need_thumb) {
        GError *thumb_err = NULL;
        GdkPixbuf *thumb = utils_pixbuf_new_from_file_at_scale_safe(
            task->path, POSTER_THUMB_WIDTH, POSTER_THUMB_HEIGHT, TRUE, &thumb_err);
        if (thumb) {
          grid_save_pixbuf_jpeg_atomic(thumb, thumb_path);
          g_object_unref(thumb);
        }
        if (thumb_err)
          g_error_free(thumb_err);
      }
    }
  }

  /* Prefer the thumb for display, but keep a robust fallback for multi-scan
     baseline JPEGs. */
  if (thumb_path && g_file_test(thumb_path, G_FILE_TEST_EXISTS)) {
    GError *error = NULL;
    task->pixbuf = gdk_pixbuf_new_from_file(thumb_path, &error);
    if (!task->pixbuf && error) {
      g_error_free(error);
    }
  } else {
    task->pixbuf = utils_pixbuf_new_from_file_at_scale_safe(
        task->path, POSTER_THUMB_WIDTH, POSTER_THUMB_HEIGHT, TRUE, NULL);
  }

  g_free(thumb_path);

  if (startup_debug_enabled() && log_n < 15) {
    startup_log("poster_load_worker: loaded pixbuf=%s", task->pixbuf ? "yes" : "no");
  }

  g_idle_add(poster_apply_idle, task);
}

/* Get DPI-scaled dimensions */
static gint get_scaled_width(ReelApp *app) {
  gdouble scale = (app->scale_factor > 0) ? app->scale_factor : 1.0;
  return (gint)(POSTER_BASE_WIDTH * scale);
}

static gint get_scaled_height(ReelApp *app) {
  gdouble scale = (app->scale_factor > 0) ? app->scale_factor : 1.0;
  return (gint)(POSTER_BASE_HEIGHT * scale);
}

/* Get DPI-scaled font size for poster labels */
static gint get_scaled_font_size(ReelApp *app) {
  gdouble scale = (app->scale_factor > 0) ? app->scale_factor : 1.0;
  return (gint)(11 * scale); /* Base font size 11pt */
}

/* Create a poster widget for a film */
static GtkWidget *create_poster_widget(ReelApp *app, Film *film) {
  gint poster_width = get_scaled_width(app);
  gint poster_height = get_scaled_height(app);
  gint font_size = get_scaled_font_size(app);

  /* Outer box - tight spacing */
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(box, GTK_ALIGN_START);
  gint extra_height =
      (gint)(52 * ((app->scale_factor > 0) ? app->scale_factor : 1.0));
  gtk_widget_set_size_request(box, poster_width, poster_height + extra_height);

  /* Overlay for poster + badge */
  GtkWidget *overlay = gtk_overlay_new();
  gtk_widget_set_halign(overlay, GTK_ALIGN_CENTER);
  gtk_box_pack_start(GTK_BOX(box), overlay, FALSE, FALSE, 0);

  /* Poster image */
  GtkWidget *poster_area = gtk_drawing_area_new();
  gtk_widget_set_size_request(poster_area, poster_width, poster_height);
  gtk_widget_set_halign(poster_area, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(poster_area, GTK_ALIGN_START);
  g_signal_connect(poster_area, "draw", G_CALLBACK(poster_draw), NULL);
  g_signal_connect(poster_area, "destroy", G_CALLBACK(poster_area_destroy), app);
  gtk_container_add(GTK_CONTAINER(overlay), poster_area);

  if (film->poster_path && g_file_test(film->poster_path, G_FILE_TEST_EXISTS)) {
    gchar *poster_path = g_strdup(film->poster_path);
    if (poster_path) {
      if (!app->thread_pool) {
        app->thread_pool = g_thread_pool_new(poster_load_worker, NULL, 4, FALSE,
                                             NULL);
      }

      PosterLoadTask *task = g_new0(PosterLoadTask, 1);
      task->app = app;
      task->path = poster_path;
      task->width = poster_width;
      task->height = poster_height;
      g_weak_ref_init(&task->widget_ref, poster_area);
      g_thread_pool_push(app->thread_pool, task, NULL);
    }
  }

  /* Unmatched badge */
  if (film->match_status == MATCH_STATUS_UNMATCHED) {
    GtkWidget *badge = gtk_label_new("?");
    gtk_widget_set_halign(badge, GTK_ALIGN_END);
    gtk_widget_set_valign(badge, GTK_ALIGN_START);
    gtk_widget_set_margin_top(badge, 6);
    gtk_widget_set_margin_end(badge, 6);

    GtkStyleContext *ctx = gtk_widget_get_style_context(badge);
    gtk_style_context_add_class(ctx, "unmatched-badge");

    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), badge);
  }

  /* Title label with DPI-aware font */
  gchar *display_title =
      film->title ? film->title : g_path_get_basename(film->file_path);
  GtkWidget *title_label = gtk_label_new(NULL);
  gtk_label_set_lines(GTK_LABEL(title_label), 1);

  /* Set title with size markup */
  gchar *title_markup = g_strdup_printf(
      "<span size='%d' weight='bold'>%s</span>", font_size * PANGO_SCALE,
      g_markup_escape_text(display_title, -1));
  gtk_label_set_markup(GTK_LABEL(title_label), title_markup);
  g_free(title_markup);

  gtk_label_set_max_width_chars(GTK_LABEL(title_label), 18);
  gtk_label_set_ellipsize(GTK_LABEL(title_label), PANGO_ELLIPSIZE_END);
  gtk_label_set_line_wrap(GTK_LABEL(title_label), FALSE);
  gtk_label_set_xalign(GTK_LABEL(title_label), 0.5); /* Center */

  GtkStyleContext *title_ctx = gtk_widget_get_style_context(title_label);
  gtk_style_context_add_class(title_ctx, "poster-title");

  gtk_box_pack_start(GTK_BOX(box), title_label, FALSE, FALSE, 0);

  if (!film->title) {
    g_free(display_title);
  }

  /* Year label */
  gchar *year_markup = NULL;
  if (film->year > 0) {
    year_markup =
        g_strdup_printf("<span size='%d'>(%d)</span>",
                        (gint)(font_size * 0.9 * PANGO_SCALE), film->year);
  } else {
    /* Keep row height consistent even when year is missing. */
    year_markup =
        g_strdup_printf("<span size='%d'> </span>",
                        (gint)(font_size * 0.9 * PANGO_SCALE));
  }

  GtkWidget *year_label = gtk_label_new(NULL);
  gtk_label_set_lines(GTK_LABEL(year_label), 1);
  gtk_label_set_markup(GTK_LABEL(year_label), year_markup);
  gtk_label_set_xalign(GTK_LABEL(year_label), 0.5); /* Center */
  g_free(year_markup);

  GtkStyleContext *year_ctx = gtk_widget_get_style_context(year_label);
  gtk_style_context_add_class(year_ctx, "poster-year");

  gtk_box_pack_start(GTK_BOX(box), year_label, FALSE, FALSE, 0);

  /* Store film ID for click handling */
  g_object_set_data(G_OBJECT(box), "film_id", GINT_TO_POINTER(film->id));
  g_object_set_data(G_OBJECT(box), "app", app);

  return box;
}

/* Click handler for poster */
static void on_poster_activated(GtkFlowBox *flowbox, GtkFlowBoxChild *child,
                                gpointer user_data) {
  (void)flowbox;
  ReelApp *app = (ReelApp *)user_data;

  GtkWidget *box = gtk_bin_get_child(GTK_BIN(child));
  gint64 film_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(box), "film_id"));

  /* Show detail dialog */
  detail_show(app, film_id);
}

GtkWidget *grid_create(ReelApp *app) {
  GtkWidget *flowbox = gtk_flow_box_new();

  /* Non-homogeneous keeps cells tight to portrait poster width, avoiding
     wide empty gutters caused by equal-width columns on large windows. */
  gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flowbox), FALSE);
  /* Prevent sparse results from being vertically distributed to fill the
     scrolled window; keep the grid anchored at the top. */
  gtk_widget_set_valign(flowbox, GTK_ALIGN_START);
  gtk_widget_set_vexpand(flowbox, FALSE);
  gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flowbox), GTK_SELECTION_SINGLE);
  gtk_flow_box_set_activate_on_single_click(GTK_FLOW_BOX(flowbox), TRUE);
  gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flowbox), 4);
  gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flowbox), 8);
  gtk_widget_set_margin_start(flowbox, 8);
  gtk_widget_set_margin_end(flowbox, 8);
  gtk_widget_set_margin_top(flowbox, 8);
  gtk_widget_set_margin_bottom(flowbox, 8);

  /* Reasonable limits for grid columns */
  gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flowbox), 2);
  gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flowbox), 12);

  g_signal_connect(flowbox, "child-activated", G_CALLBACK(on_poster_activated),
                   app);

  return flowbox;
}

void grid_clear(ReelApp *app) {
  if (app->grid_idle_source) {
    g_source_remove(app->grid_idle_source);
    app->grid_idle_source = 0;
  }
  if (app->grid_pending) {
    g_list_free(app->grid_pending);
    app->grid_pending = NULL;
  }

  GList *children = gtk_container_get_children(GTK_CONTAINER(app->grid_view));
  for (GList *l = children; l != NULL; l = l->next) {
    gtk_widget_destroy(GTK_WIDGET(l->data));
  }
  g_list_free(children);
}

static gboolean grid_append_idle(gpointer data) {
  ReelApp *app = (ReelApp *)data;

  const int chunk = 40;
  int inserted = 0;
  while (app->grid_pending && inserted < chunk) {
    Film *film = (Film *)app->grid_pending->data;
    GtkWidget *poster = create_poster_widget(app, film);
    GtkWidget *child = gtk_flow_box_child_new();
    gtk_container_add(GTK_CONTAINER(child), poster);
    gtk_widget_show_all(child);
    gtk_flow_box_insert(GTK_FLOW_BOX(app->grid_view), child, -1);

    GList *link = app->grid_pending;
    app->grid_pending = app->grid_pending->next;
    g_list_free_1(link);
    inserted++;
  }

  if (startup_debug_enabled()) {
    startup_log("grid_append_idle: inserted=%d pending=%s", inserted,
                app->grid_pending ? "yes" : "no");
  }

  if (!app->grid_pending) {
    app->grid_idle_source = 0;
    return G_SOURCE_REMOVE;
  }

  return G_SOURCE_CONTINUE;
}

void grid_append_films(ReelApp *app, GList *films) {
  if (!films)
    return;

  app->grid_pending = g_list_concat(app->grid_pending, g_list_copy(films));
  if (!app->grid_idle_source) {
    app->grid_idle_source = g_idle_add(grid_append_idle, app);
  }
}

void grid_update_film(ReelApp *app, const Film *film) {
  if (!app || !app->grid_view || !film)
    return;

  GList *children = gtk_container_get_children(GTK_CONTAINER(app->grid_view));
  for (GList *l = children; l != NULL; l = l->next) {
    GtkFlowBoxChild *child = GTK_FLOW_BOX_CHILD(l->data);
    GtkWidget *box = gtk_bin_get_child(GTK_BIN(child));
    if (!box)
      continue;

    gint64 id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(box), "film_id"));
    if (id != film->id)
      continue;

    int index = gtk_flow_box_child_get_index(child);
    gtk_widget_destroy(GTK_WIDGET(child));

    GtkWidget *new_box = create_poster_widget(app, (Film *)film);
    GtkWidget *new_child = gtk_flow_box_child_new();
    gtk_container_add(GTK_CONTAINER(new_child), new_box);
    gtk_widget_show_all(new_child);
    gtk_flow_box_insert(GTK_FLOW_BOX(app->grid_view), new_child, index);
    break;
  }
  g_list_free(children);
}

void grid_populate(ReelApp *app) {
  grid_clear(app);
  grid_append_films(app, app->films);
}
