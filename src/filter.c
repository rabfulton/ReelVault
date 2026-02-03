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
static void parse_search_text(ReelApp *app, const gchar *text);

GtkWidget *filter_bar_create(ReelApp *app) {
  /* Use a 3-column grid so the search entry is truly centered regardless of
     left/right control widths. */
  GtkWidget *bar = gtk_grid_new();
  gtk_widget_set_margin_start(bar, 12);
  gtk_widget_set_margin_end(bar, 12);
  gtk_widget_set_margin_top(bar, 8);
  gtk_widget_set_margin_bottom(bar, 8);
  gtk_grid_set_column_spacing(GTK_GRID(bar), 12);
  gtk_grid_set_row_spacing(GTK_GRID(bar), 0);

  GtkStyleContext *ctx = gtk_widget_get_style_context(bar);
  gtk_style_context_add_class(ctx, "filter-bar");

  FilterWidgets *widgets = g_new0(FilterWidgets, 1);
  g_object_set_data_full(G_OBJECT(bar), "widgets", widgets, g_free);
  g_object_set_data(G_OBJECT(bar), "app", app);

  /* Layout: left controls, centered search, right controls */
  GtkWidget *left = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_hexpand(left, TRUE);
  gtk_widget_set_halign(left, GTK_ALIGN_FILL);
  gtk_widget_set_halign(left, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(bar), left, 0, 0, 1, 1);

  GtkWidget *right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_hexpand(right, TRUE);
  gtk_widget_set_halign(right, GTK_ALIGN_FILL);
  gtk_widget_set_halign(right, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(bar), right, 2, 0, 1, 1);

  /* Ensure the left and right areas take equal width so the search entry is
     visually centered regardless of control widths. */
  GtkSizeGroup *side_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
  gtk_size_group_add_widget(side_group, left);
  gtk_size_group_add_widget(side_group, right);
  g_object_unref(side_group);

  /* Centered search entry */
  GtkWidget *search_entry = gtk_search_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry), "Search films...");
  gtk_widget_set_size_request(search_entry, 420, -1);
  gtk_widget_set_halign(search_entry, GTK_ALIGN_CENTER);
  gtk_widget_set_hexpand(search_entry, TRUE);
  g_signal_connect(search_entry, "search-changed",
                   G_CALLBACK(on_search_changed), app);
  gtk_grid_attach(GTK_GRID(bar), search_entry, 1, 0, 1, 1);
  widgets->search_entry = search_entry;

  /* Genre filter */
  GtkWidget *genre_label = gtk_label_new("Genre:");
  gtk_box_pack_start(GTK_BOX(left), genre_label, FALSE, FALSE, 0);

  GtkWidget *genre_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(genre_combo), "", "All Genres");
  gtk_combo_box_set_active(GTK_COMBO_BOX(genre_combo), 0);
  g_signal_connect(genre_combo, "changed", G_CALLBACK(on_filter_changed), app);
  gtk_box_pack_start(GTK_BOX(left), genre_combo, FALSE, FALSE, 0);
  widgets->genre_combo = genre_combo;

  /* Year filter */
  GtkWidget *year_label = gtk_label_new("Year:");
  gtk_box_pack_start(GTK_BOX(left), year_label, FALSE, FALSE, 0);

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
  gtk_box_pack_start(GTK_BOX(left), year_combo, FALSE, FALSE, 0);
  widgets->year_combo = year_combo;

  /* Sort controls */
  GtkWidget *sort_label = gtk_label_new("Sort:");
  gtk_box_pack_start(GTK_BOX(right), sort_label, FALSE, FALSE, 0);

  GtkWidget *sort_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sort_combo), "title", "Title");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sort_combo), "year", "Year");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sort_combo), "rating", "Rating");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sort_combo), "added",
                            "Date Added");
  gtk_combo_box_set_active(GTK_COMBO_BOX(sort_combo), 0);
  g_signal_connect(sort_combo, "changed", G_CALLBACK(on_filter_changed), app);
  gtk_box_pack_start(GTK_BOX(right), sort_combo, FALSE, FALSE, 0);
  widgets->sort_combo = sort_combo;

  /* Sort order button */
  GtkWidget *sort_order_btn = gtk_button_new_from_icon_name(
      "view-sort-ascending-symbolic", GTK_ICON_SIZE_BUTTON);
  gtk_widget_set_tooltip_text(sort_order_btn, "Toggle sort order");
  g_signal_connect(sort_order_btn, "clicked", G_CALLBACK(on_sort_order_clicked),
                   app);
  gtk_box_pack_start(GTK_BOX(right), sort_order_btn, FALSE, FALSE, 0);
  widgets->sort_order_btn = sort_order_btn;

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

  /* Sort */
  const gchar *sort_id =
      gtk_combo_box_get_active_id(GTK_COMBO_BOX(widgets->sort_combo));
  app->filter.sort_by = g_strdup(sort_id ? sort_id : "title");
}

static void on_filter_changed(GtkWidget *widget, gpointer user_data) {
  (void)widget;
  ReelApp *app = (ReelApp *)user_data;
  update_filter_state(app);
  FilterWidgets *widgets =
      g_object_get_data(G_OBJECT(app->filter_bar), "widgets");
  if (widgets && widgets->search_entry) {
    parse_search_text(
        app, gtk_entry_get_text(GTK_ENTRY(widgets->search_entry)));
  }
  window_refresh_films(app);
}

static void on_search_changed(GtkSearchEntry *entry, gpointer user_data) {
  ReelApp *app = (ReelApp *)user_data;
  update_filter_state(app);
  parse_search_text(app, gtk_entry_get_text(GTK_ENTRY(entry)));
  window_refresh_films(app);
}

static void parse_search_text(ReelApp *app, const gchar *text) {
  if (!app)
    return;

  g_free(app->filter.search_text);
  app->filter.search_text = NULL;
  g_free(app->filter.actor);
  app->filter.actor = NULL;
  g_free(app->filter.plot_text);
  app->filter.plot_text = NULL;

  if (!text || !*text)
    return;

  gint argc = 0;
  gchar **argv = NULL;
  if (!g_shell_parse_argv(text, &argc, &argv, NULL) || argc <= 0) {
    app->filter.search_text = g_strdup(text);
    return;
  }

  GString *title = g_string_new("");

  for (gint i = 0; i < argc; i++) {
    const gchar *tok = argv[i];
    if (!tok || !*tok)
      continue;

    const gchar *value = NULL;
    const gchar *key = NULL;
    if (g_str_has_prefix(tok, "actor:") || g_str_has_prefix(tok, "cast:") ||
        g_str_has_prefix(tok, "plot:") || g_str_has_prefix(tok, "title:")) {
      key = tok;
      value = strchr(tok, ':');
      value = value ? value + 1 : NULL;
      if (value && *value) {
        /* ok */
      } else if ((i + 1) < argc) {
        value = argv[++i];
      }
    }

    if (key && value && *value) {
      if (g_str_has_prefix(key, "actor:") || g_str_has_prefix(key, "cast:")) {
        g_free(app->filter.actor);
        app->filter.actor = g_strdup(value);
        continue;
      }
      if (g_str_has_prefix(key, "plot:")) {
        g_free(app->filter.plot_text);
        app->filter.plot_text = g_strdup(value);
        continue;
      }
      if (g_str_has_prefix(key, "title:")) {
        if (title->len > 0)
          g_string_append_c(title, ' ');
        g_string_append(title, value);
        continue;
      }
    }

    if (title->len > 0)
      g_string_append_c(title, ' ');
    g_string_append(title, tok);
  }

  if (title->len > 0)
    app->filter.search_text = g_strdup(title->str);

  g_string_free(title, TRUE);
  g_strfreev(argv);
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
