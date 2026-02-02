/*
 * ReelGTK - Poster Grid
 * GtkFlowBox-based poster grid with DPI-aware sizing
 */

#include "grid.h"
#include "detail.h"
#include <string.h>

/* Placeholder poster path */
static const char *PLACEHOLDER_ICON = "video-x-generic";

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
  GtkWidget *image;
  if (film->poster_path && g_file_test(film->poster_path, G_FILE_TEST_EXISTS)) {
    /* Load thumbnail from cache */
    GError *error = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(
        film->poster_path, poster_width, poster_height, TRUE, &error);

    if (pixbuf) {
      image = gtk_image_new_from_pixbuf(pixbuf);
      g_object_unref(pixbuf);
    } else {
      g_print("Failed to load poster %s: %s\n", film->poster_path,
              error->message);
      g_error_free(error);
      image =
          gtk_image_new_from_icon_name(PLACEHOLDER_ICON, GTK_ICON_SIZE_DIALOG);
      gtk_image_set_pixel_size(GTK_IMAGE(image), poster_height);
    }
  } else {
    /* Use placeholder */
    image =
        gtk_image_new_from_icon_name(PLACEHOLDER_ICON, GTK_ICON_SIZE_DIALOG);
    gtk_image_set_pixel_size(GTK_IMAGE(image), poster_height);
  }

  gtk_widget_set_size_request(image, poster_width, poster_height);
  gtk_widget_set_halign(image, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(image, GTK_ALIGN_START);
  gtk_container_add(GTK_CONTAINER(overlay), image);

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
  GList *children = gtk_container_get_children(GTK_CONTAINER(app->grid_view));
  for (GList *l = children; l != NULL; l = l->next) {
    gtk_widget_destroy(GTK_WIDGET(l->data));
  }
  g_list_free(children);
}

void grid_populate(ReelApp *app) {
  /* Clear existing */
  grid_clear(app);

  /* Add poster for each film */
  for (GList *l = app->films; l != NULL; l = l->next) {
    Film *film = (Film *)l->data;
    GtkWidget *poster = create_poster_widget(app, film);
    gtk_flow_box_insert(GTK_FLOW_BOX(app->grid_view), poster, -1);
  }

  gtk_widget_show_all(app->grid_view);
}
