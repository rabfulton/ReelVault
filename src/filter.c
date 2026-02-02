/*
 * ReelGTK - Filter Bar
 * Filter and sort controls for the film grid
 */

#include "filter.h"
#include "db.h"
#include "window.h"
#include <string.h>

/* Widget references stored on filter bar */
typedef struct {
  GtkWidget *genre_combo;
  GtkWidget *year_combo;
  GtkWidget *search_entry;
  GtkWidget *sort_combo;
  GtkWidget *sort_order_btn;
} FilterWidgets;

static void on_filter_changed(GtkWidget *widget, gpointer user_data);
static void on_search_changed(GtkSearchEntry *entry, gpointer user_data);
static void on_sort_order_clicked(GtkButton *button, gpointer user_data);

GtkWidget *filter_bar_create(ReelApp *app) {
  GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_margin_start(bar, 12);
  gtk_widget_set_margin_end(bar, 12);
  gtk_widget_set_margin_top(bar, 8);
  gtk_widget_set_margin_bottom(bar, 8);

  GtkStyleContext *ctx = gtk_widget_get_style_context(bar);
  gtk_style_context_add_class(ctx, "filter-bar");

  FilterWidgets *widgets = g_new0(FilterWidgets, 1);
  g_object_set_data_full(G_OBJECT(bar), "widgets", widgets, g_free);
  g_object_set_data(G_OBJECT(bar), "app", app);

  /* Search entry */
  GtkWidget *search_entry = gtk_search_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry), "Search films...");
  gtk_widget_set_size_request(search_entry, 200, -1);
  g_signal_connect(search_entry, "search-changed",
                   G_CALLBACK(on_search_changed), app);
  gtk_box_pack_start(GTK_BOX(bar), search_entry, FALSE, FALSE, 0);
  widgets->search_entry = search_entry;

  /* Separator */
  gtk_box_pack_start(GTK_BOX(bar), gtk_separator_new(GTK_ORIENTATION_VERTICAL),
                     FALSE, FALSE, 0);

  /* Genre filter */
  GtkWidget *genre_label = gtk_label_new("Genre:");
  gtk_box_pack_start(GTK_BOX(bar), genre_label, FALSE, FALSE, 0);

  GtkWidget *genre_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(genre_combo), "", "All Genres");
  gtk_combo_box_set_active(GTK_COMBO_BOX(genre_combo), 0);
  g_signal_connect(genre_combo, "changed", G_CALLBACK(on_filter_changed), app);
  gtk_box_pack_start(GTK_BOX(bar), genre_combo, FALSE, FALSE, 0);
  widgets->genre_combo = genre_combo;

  /* Year filter */
  GtkWidget *year_label = gtk_label_new("Year:");
  gtk_box_pack_start(GTK_BOX(bar), year_label, FALSE, FALSE, 0);

  GtkWidget *year_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(year_combo), "", "All Years");
  gtk_combo_box_set_active(GTK_COMBO_BOX(year_combo), 0);

  /* Add decade options */
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(year_combo), "2020s", "2020s");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(year_combo), "2010s", "2010s");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(year_combo), "2000s", "2000s");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(year_combo), "1990s", "1990s");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(year_combo), "1980s", "1980s");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(year_combo), "older",
                            "Before 1980");

  g_signal_connect(year_combo, "changed", G_CALLBACK(on_filter_changed), app);
  gtk_box_pack_start(GTK_BOX(bar), year_combo, FALSE, FALSE, 0);
  widgets->year_combo = year_combo;

  /* Spacer */
  GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand(spacer, TRUE);
  gtk_box_pack_start(GTK_BOX(bar), spacer, TRUE, TRUE, 0);

  /* Sort controls */
  GtkWidget *sort_label = gtk_label_new("Sort:");
  gtk_box_pack_start(GTK_BOX(bar), sort_label, FALSE, FALSE, 0);

  GtkWidget *sort_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sort_combo), "title", "Title");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sort_combo), "year", "Year");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sort_combo), "rating", "Rating");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sort_combo), "added",
                            "Date Added");
  gtk_combo_box_set_active(GTK_COMBO_BOX(sort_combo), 0);
  g_signal_connect(sort_combo, "changed", G_CALLBACK(on_filter_changed), app);
  gtk_box_pack_start(GTK_BOX(bar), sort_combo, FALSE, FALSE, 0);
  widgets->sort_combo = sort_combo;

  /* Sort order button */
  GtkWidget *sort_order_btn = gtk_button_new_from_icon_name(
      "view-sort-ascending-symbolic", GTK_ICON_SIZE_BUTTON);
  gtk_widget_set_tooltip_text(sort_order_btn, "Toggle sort order");
  g_signal_connect(sort_order_btn, "clicked", G_CALLBACK(on_sort_order_clicked),
                   app);
  gtk_box_pack_start(GTK_BOX(bar), sort_order_btn, FALSE, FALSE, 0);
  widgets->sort_order_btn = sort_order_btn;

  /* Populate genres from database */
  filter_bar_refresh(app);

  return bar;
}

void filter_bar_refresh(ReelApp *app) {
  if (!app->filter_bar)
    return;

  FilterWidgets *widgets =
      g_object_get_data(G_OBJECT(app->filter_bar), "widgets");
  if (!widgets)
    return;

  /* Block signals while updating */
  g_signal_handlers_block_matched(widgets->genre_combo, G_SIGNAL_MATCH_FUNC, 0,
                                  0, NULL, G_CALLBACK(on_filter_changed), NULL);

  /* Clear and repopulate genre combo */
  gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(widgets->genre_combo));
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(widgets->genre_combo), "",
                            "All Genres");

  GList *genres = db_genres_get_all(app);
  for (GList *l = genres; l != NULL; l = l->next) {
    gchar *genre = (gchar *)l->data;
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(widgets->genre_combo), genre,
                              genre);
  }
  g_list_free_full(genres, g_free);

  gtk_combo_box_set_active(GTK_COMBO_BOX(widgets->genre_combo), 0);

  /* Unblock signals */
  g_signal_handlers_unblock_matched(widgets->genre_combo, G_SIGNAL_MATCH_FUNC,
                                    0, 0, NULL, G_CALLBACK(on_filter_changed),
                                    NULL);
}

void filter_bar_reset(ReelApp *app) {
  if (!app->filter_bar)
    return;

  FilterWidgets *widgets =
      g_object_get_data(G_OBJECT(app->filter_bar), "widgets");
  if (!widgets)
    return;

  /* Reset all controls */
  gtk_combo_box_set_active(GTK_COMBO_BOX(widgets->genre_combo), 0);
  gtk_combo_box_set_active(GTK_COMBO_BOX(widgets->year_combo), 0);
  gtk_combo_box_set_active(GTK_COMBO_BOX(widgets->sort_combo), 0);
  gtk_entry_set_text(GTK_ENTRY(widgets->search_entry), "");

  /* Clear filter state */
  filter_state_clear(&app->filter);
  filter_state_init(&app->filter);

  window_refresh_films(app);
}

static void update_filter_state(ReelApp *app) {
  FilterWidgets *widgets =
      g_object_get_data(G_OBJECT(app->filter_bar), "widgets");
  if (!widgets)
    return;

  /* Clear current filter */
  g_free(app->filter.genre);
  g_free(app->filter.search_text);
  g_free(app->filter.sort_by);

  /* Genre */
  gchar *genre_id = gtk_combo_box_text_get_active_text(
      GTK_COMBO_BOX_TEXT(widgets->genre_combo));
  if (genre_id && strlen(genre_id) > 0 &&
      g_strcmp0(genre_id, "All Genres") != 0) {
    app->filter.genre = genre_id;
  } else {
    app->filter.genre = NULL;
    g_free(genre_id);
  }

  /* Year range */
  const gchar *year_id =
      gtk_combo_box_get_active_id(GTK_COMBO_BOX(widgets->year_combo));
  app->filter.year_from = 0;
  app->filter.year_to = 0;

  if (year_id) {
    if (g_strcmp0(year_id, "2020s") == 0) {
      app->filter.year_from = 2020;
      app->filter.year_to = 2029;
    } else if (g_strcmp0(year_id, "2010s") == 0) {
      app->filter.year_from = 2010;
      app->filter.year_to = 2019;
    } else if (g_strcmp0(year_id, "2000s") == 0) {
      app->filter.year_from = 2000;
      app->filter.year_to = 2009;
    } else if (g_strcmp0(year_id, "1990s") == 0) {
      app->filter.year_from = 1990;
      app->filter.year_to = 1999;
    } else if (g_strcmp0(year_id, "1980s") == 0) {
      app->filter.year_from = 1980;
      app->filter.year_to = 1989;
    } else if (g_strcmp0(year_id, "older") == 0) {
      app->filter.year_to = 1979;
    }
  }

  /* Search text */
  const gchar *search = gtk_entry_get_text(GTK_ENTRY(widgets->search_entry));
  if (search && strlen(search) > 0) {
    app->filter.search_text = g_strdup(search);
  } else {
    app->filter.search_text = NULL;
  }

  /* Sort */
  const gchar *sort_id =
      gtk_combo_box_get_active_id(GTK_COMBO_BOX(widgets->sort_combo));
  app->filter.sort_by = g_strdup(sort_id ? sort_id : "title");
}

static void on_filter_changed(GtkWidget *widget, gpointer user_data) {
  (void)widget;
  ReelApp *app = (ReelApp *)user_data;
  update_filter_state(app);
  window_refresh_films(app);
}

static void on_search_changed(GtkSearchEntry *entry, gpointer user_data) {
  (void)entry;
  ReelApp *app = (ReelApp *)user_data;
  update_filter_state(app);
  window_refresh_films(app);
}

static void on_sort_order_clicked(GtkButton *button, gpointer user_data) {
  ReelApp *app = (ReelApp *)user_data;

  app->filter.sort_ascending = !app->filter.sort_ascending;

  /* Update icon */
  GtkWidget *image = gtk_button_get_image(button);
  if (image) {
    gtk_image_set_from_icon_name(GTK_IMAGE(image),
                                 app->filter.sort_ascending
                                     ? "view-sort-ascending-symbolic"
                                     : "view-sort-descending-symbolic",
                                 GTK_ICON_SIZE_BUTTON);
  }

  window_refresh_films(app);
}
