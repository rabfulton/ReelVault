/*
 * ReelGTK - Match Dialog
 * Manual matching of films to TMDB entries
 */

#include "match.h"
#include "db.h"
#include "scanner.h"
#include "scraper.h"
#include "utils.h"
#include "window.h"
#include <string.h>

typedef struct {
  ReelApp *app;
  gint64 film_id;
  MediaType media_type;
  GtkWidget *dialog;
  GtkWidget *search_entry;
  GtkWidget *tv_checkbox;
  GtkWidget *results_list;
  GList *search_results;
} MatchDialogContext;

static void on_search_clicked(GtkButton *button, gpointer user_data);
static void on_apply_clicked(GtkButton *button, gpointer user_data);
static void context_free(MatchDialogContext *ctx);
static void on_tv_toggle(GtkToggleButton *button, gpointer user_data);
static void reset_film_to_unmatched(ReelApp *app, gint64 film_id);

typedef struct {
  ReelApp *app;
  gint64 film_id;
  gint tmdb_id;
  gboolean convert_to_tv_season;
  gboolean success;
  GWeakRef dialog_ref;
  GWeakRef busy_ref;
  GWeakRef apply_button_ref;
} ApplyMatchTask;

static gboolean apply_match_done_idle(gpointer data) {
  ApplyMatchTask *task = (ApplyMatchTask *)data;

  GtkWidget *busy = g_weak_ref_get(&task->busy_ref);
  if (busy) {
    gtk_widget_destroy(busy);
    g_object_unref(busy);
  }

  GtkWidget *apply_btn = g_weak_ref_get(&task->apply_button_ref);
  if (apply_btn) {
    gtk_widget_set_sensitive(apply_btn, TRUE);
    g_object_unref(apply_btn);
  }

  GtkWidget *dialog = g_weak_ref_get(&task->dialog_ref);
  if (task->success) {
    window_refresh_film(task->app, task->film_id);
    if (dialog) {
      gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    }
  } else {
    if (dialog) {
      GtkWidget *err = gtk_message_dialog_new(
          GTK_WINDOW(dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
          "Failed to fetch film details from TMDB.");
      window_apply_theme(task->app, err);
      gtk_dialog_run(GTK_DIALOG(err));
      gtk_widget_destroy(err);
    }
  }

  if (dialog)
    g_object_unref(dialog);

  g_weak_ref_clear(&task->dialog_ref);
  g_weak_ref_clear(&task->busy_ref);
  g_weak_ref_clear(&task->apply_button_ref);
  g_free(task);
  return G_SOURCE_REMOVE;
}

static gpointer apply_match_thread(gpointer data) {
  ApplyMatchTask *task = (ApplyMatchTask *)data;

  sqlite3 *db = NULL;
  if (sqlite3_open(task->app->db_path, &db) != SQLITE_OK) {
    if (db)
      sqlite3_close(db);
    task->success = FALSE;
    g_idle_add(apply_match_done_idle, task);
    return NULL;
  }
  sqlite3_exec(db, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL);

  ReelApp thread_app = *task->app;
  thread_app.db = db;

  if (task->convert_to_tv_season) {
    Film *film = db_film_get_by_id(&thread_app, task->film_id);
    if (film) {
      film->media_type = MEDIA_TV_SEASON;
      if (film->season_number <= 0)
        film->season_number = 1;
      db_film_update(&thread_app, film);
      film_free(film);
    }
  } else {
    /* If the user is matching as a movie, ensure the entry is treated as a film. */
    Film *film = db_film_get_by_id(&thread_app, task->film_id);
    if (film) {
      film->media_type = MEDIA_FILM;
      db_film_update(&thread_app, film);
      film_free(film);
    }
  }

  task->success = scraper_fetch_and_update(&thread_app, task->film_id, task->tmdb_id);

  /* Mark as manual after applying a specific selection. */
  if (task->success) {
    Film *film = db_film_get_by_id(&thread_app, task->film_id);
    if (film) {
      film->match_status = MATCH_STATUS_MANUAL;
      db_film_update(&thread_app, film);
      film_free(film);
    }
  }

  sqlite3_close(db);

  g_idle_add(apply_match_done_idle, task);
  return NULL;
}

/* Callback for search entry activate */
static void on_search_entry_activate(GtkEntry *entry, gpointer user_data) {
  (void)entry;
  MatchDialogContext *ctx = (MatchDialogContext *)user_data;
  on_search_clicked(NULL, ctx);
}

void match_show(ReelApp *app, gint64 film_id) {
  Film *film = db_film_get_by_id(app, film_id);
  if (!film)
    return;

  /* Create context */
  MatchDialogContext *ctx = g_new0(MatchDialogContext, 1);
  ctx->app = app;
  ctx->film_id = film_id;
  ctx->media_type = film->media_type;

  /* Create dialog */
  const gchar *title =
      (film->media_type == MEDIA_TV_SEASON) ? "Match TV Season" : "Match Film";
  ctx->dialog = gtk_dialog_new_with_buttons(
      title, GTK_WINDOW(app->window),
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel",
      GTK_RESPONSE_CANCEL, NULL);
  window_apply_theme(app, ctx->dialog);

  gtk_window_set_default_size(GTK_WINDOW(ctx->dialog), 600, 400);

  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(ctx->dialog));
  gtk_container_set_border_width(GTK_CONTAINER(content), 12);
  gtk_box_set_spacing(GTK_BOX(content), 8);

  /* File info */
  GtkWidget *file_label = gtk_label_new(NULL);
  gchar *file_markup =
      g_strdup_printf("<b>File:</b> %s", g_path_get_basename(film->file_path));
  gtk_label_set_markup(GTK_LABEL(file_label), file_markup);
  gtk_label_set_xalign(GTK_LABEL(file_label), 0);
  gtk_label_set_ellipsize(GTK_LABEL(file_label), PANGO_ELLIPSIZE_MIDDLE);
  gtk_box_pack_start(GTK_BOX(content), file_label, FALSE, FALSE, 0);
  g_free(file_markup);

  /* Search box */
  GtkWidget *search_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_pack_start(GTK_BOX(content), search_box, FALSE, FALSE, 0);

  ctx->search_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(
      GTK_ENTRY(ctx->search_entry),
      (film->media_type == MEDIA_TV_SEASON) ? "Search TMDB TV..." : "Search TMDB...");
  if (film->title) {
    gtk_entry_set_text(GTK_ENTRY(ctx->search_entry), film->title);
  }
  gtk_widget_set_hexpand(ctx->search_entry, TRUE);
  gtk_box_pack_start(GTK_BOX(search_box), ctx->search_entry, TRUE, TRUE, 0);

  GtkWidget *search_btn = gtk_button_new_with_label("Search");
  g_signal_connect(search_btn, "clicked", G_CALLBACK(on_search_clicked), ctx);
  gtk_box_pack_start(GTK_BOX(search_box), search_btn, FALSE, FALSE, 0);

  /* Always allow switching search type to fix mischaracterized entries. */
  GtkWidget *tv_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_pack_start(GTK_BOX(content), tv_box, FALSE, FALSE, 0);

  ctx->tv_checkbox = gtk_check_button_new_with_label("Search TV series");
  gtk_box_pack_start(GTK_BOX(tv_box), ctx->tv_checkbox, FALSE, FALSE, 0);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ctx->tv_checkbox),
                               film->media_type == MEDIA_TV_SEASON);
  g_signal_connect(ctx->tv_checkbox, "toggled", G_CALLBACK(on_tv_toggle), ctx);

  /* Allow Enter to search */
  g_signal_connect(ctx->search_entry, "activate",
                   G_CALLBACK(on_search_entry_activate), ctx);

  /* Results list */
  GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(scroll, TRUE);
  gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 0);

  ctx->results_list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(ctx->results_list),
                                  GTK_SELECTION_SINGLE);
  gtk_container_add(GTK_CONTAINER(scroll), ctx->results_list);

  /* Apply button */
  GtkWidget *apply_btn = gtk_button_new_with_label("Apply Match");
  GtkStyleContext *apply_ctx = gtk_widget_get_style_context(apply_btn);
  gtk_style_context_add_class(apply_ctx, "suggested-action");
  gtk_dialog_add_action_widget(GTK_DIALOG(ctx->dialog), apply_btn,
                               GTK_RESPONSE_ACCEPT);
  g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_apply_clicked), ctx);

  /* Ignore button */
  GtkWidget *ignore_btn = gtk_button_new_with_label("Mark as No Match");
  gtk_dialog_add_action_widget(GTK_DIALOG(ctx->dialog), ignore_btn,
                               GTK_RESPONSE_REJECT);

  gtk_widget_show_all(ctx->dialog);

  gint response = gtk_dialog_run(GTK_DIALOG(ctx->dialog));

  if (response == GTK_RESPONSE_REJECT) {
    reset_film_to_unmatched(app, film_id);
  }

  gtk_widget_destroy(ctx->dialog);
  context_free(ctx);
  film_free(film);

  /* Refresh main window */
  window_refresh_films(app);
}

static void clear_results(MatchDialogContext *ctx) {
  /* Clear list box */
  GList *children =
      gtk_container_get_children(GTK_CONTAINER(ctx->results_list));
  for (GList *l = children; l != NULL; l = l->next) {
    gtk_widget_destroy(GTK_WIDGET(l->data));
  }
  g_list_free(children);

  /* Free previous results */
  if (ctx->search_results) {
    g_list_free_full(ctx->search_results,
                     (GDestroyNotify)tmdb_search_result_free);
    ctx->search_results = NULL;
  }
}

static void on_tv_toggle(GtkToggleButton *button, gpointer user_data) {
  MatchDialogContext *ctx = (MatchDialogContext *)user_data;
  gboolean enabled = gtk_toggle_button_get_active(button);

  gtk_entry_set_placeholder_text(GTK_ENTRY(ctx->search_entry),
                                 enabled ? "Search TMDB TV..." : "Search TMDB...");
  clear_results(ctx);
}

static void on_search_clicked(GtkButton *button, gpointer user_data) {
  (void)button;
  MatchDialogContext *ctx = (MatchDialogContext *)user_data;

  const gchar *query = gtk_entry_get_text(GTK_ENTRY(ctx->search_entry));
  if (!query || strlen(query) == 0)
    return;

  clear_results(ctx);

  /* Search TMDB */
  if (ctx->media_type == MEDIA_TV_SEASON ||
      (ctx->tv_checkbox &&
       gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ctx->tv_checkbox)))) {
    ctx->search_results = scraper_search_tv(ctx->app, query, 0);
  } else {
    ctx->search_results = scraper_search_tmdb(ctx->app, query, 0);
  }

  if (!ctx->search_results) {
    GtkWidget *label = gtk_label_new("No results found");
    gtk_list_box_insert(GTK_LIST_BOX(ctx->results_list), label, -1);
    gtk_widget_show(label);
    return;
  }

  /* Populate results */
  for (GList *l = ctx->search_results; l != NULL; l = l->next) {
    TmdbSearchResult *result = (TmdbSearchResult *)l->data;

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(row), 8);

    /* Title and year */
    gchar *text = g_strdup_printf("%s (%d)", result->title, result->year);
    GtkWidget *title_label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(title_label), 0);
    gtk_widget_set_hexpand(title_label, TRUE);
    gtk_box_pack_start(GTK_BOX(row), title_label, TRUE, TRUE, 0);
    g_free(text);

    /* Rating */
    if (result->vote_average > 0) {
      gchar *rating = g_strdup_printf("★ %.1f", result->vote_average);
      GtkWidget *rating_label = gtk_label_new(rating);
      gtk_box_pack_end(GTK_BOX(row), rating_label, FALSE, FALSE, 0);
      g_free(rating);
    }

    /* Store TMDB ID on the row */
    g_object_set_data(G_OBJECT(row), "tmdb_id",
                      GINT_TO_POINTER(result->tmdb_id));

    gtk_list_box_insert(GTK_LIST_BOX(ctx->results_list), row, -1);
  }

  gtk_widget_show_all(ctx->results_list);
}

static void on_apply_clicked(GtkButton *button, gpointer user_data) {
  (void)button;
  MatchDialogContext *ctx = (MatchDialogContext *)user_data;

  GtkListBoxRow *selected =
      gtk_list_box_get_selected_row(GTK_LIST_BOX(ctx->results_list));
  if (!selected) {
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(ctx->dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
        GTK_BUTTONS_OK, "Please select a match from the list.");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return;
  }

  GtkWidget *row_box = gtk_bin_get_child(GTK_BIN(selected));
  gint tmdb_id =
      GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row_box), "tmdb_id"));

  /* Fetch and update on a worker thread to keep UI responsive */
  ApplyMatchTask *task = g_new0(ApplyMatchTask, 1);
  task->app = ctx->app;
  task->film_id = ctx->film_id;
  task->tmdb_id = tmdb_id;
  task->convert_to_tv_season =
      (ctx->tv_checkbox &&
       gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ctx->tv_checkbox)));

  GtkWidget *busy_dialog = gtk_message_dialog_new(
      GTK_WINDOW(ctx->dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_NONE,
      "Fetching details from TMDB...");
  window_apply_theme(ctx->app, busy_dialog);
  gtk_widget_show(busy_dialog);

  g_weak_ref_init(&task->dialog_ref, ctx->dialog);
  g_weak_ref_init(&task->busy_ref, busy_dialog);
  g_weak_ref_init(&task->apply_button_ref, GTK_WIDGET(button));

  gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);

  g_thread_new("apply-match", apply_match_thread, task);
}

static void context_free(MatchDialogContext *ctx) {
  if (ctx->search_results) {
    g_list_free_full(ctx->search_results,
                     (GDestroyNotify)tmdb_search_result_free);
  }
  g_free(ctx);
}

static void reset_film_to_unmatched(ReelApp *app, gint64 film_id) {
  if (!app)
    return;

  Film *film = db_film_get_by_id(app, film_id);
  if (!film)
    return;

  /* Remove any scraped associations. */
  db_film_clear_associations(app, film_id);
  app->genres_dirty = TRUE;

  /* Reset scraped fields. */
  film->match_status = MATCH_STATUS_UNMATCHED;
  film->tmdb_id = 0;
  g_free(film->imdb_id);
  film->imdb_id = NULL;
  film->rating = 0.0;
  film->runtime_minutes = 0;
  g_free(film->plot);
  film->plot = NULL;
  g_free(film->poster_path);
  film->poster_path = NULL;

  /* Restore a reasonable "unknown" title/year from the filename/path. */
  g_free(film->title);
  film->title = NULL;
  film->year = 0;

  if (film->media_type == MEDIA_TV_SEASON) {
    gchar *parent = g_path_get_dirname(film->file_path ? film->file_path : "");
    gchar *show_base = parent ? g_path_get_basename(parent) : NULL;
    gchar *show_name = utils_normalize_title(show_base ? show_base : "");
    g_free(show_base);
    g_free(parent);

    if (!show_name || strlen(show_name) == 0) {
      g_free(show_name);
      show_name = g_strdup("Unknown Show");
    }

    if (film->season_number == 0) {
      film->title = g_strdup_printf("%s - Specials", show_name);
    } else {
      if (film->season_number < 0)
        film->season_number = 1;
      film->title = g_strdup_printf("%s - Season %d", show_name,
                                    film->season_number);
    }
    g_free(show_name);
  } else {
    gchar *base = g_path_get_basename(film->file_path ? film->file_path : "");
    gchar *parsed_title = NULL;
    gint parsed_year = 0;
    if (base && scanner_parse_filename(base, &parsed_title, &parsed_year) &&
        parsed_title && *parsed_title) {
      film->title = parsed_title;
      film->year = parsed_year;
    } else {
      g_free(parsed_title);
      film->title = utils_normalize_title(base ? base : "Unknown");
      film->year = 0;
    }
    g_free(base);
  }

  db_film_update(app, film);
  film_free(film);
}
