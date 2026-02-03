/*
 * ReelGTK - Film Detail Dialog
 * Displays full film information with play button
 */

#include "detail.h"
#include "db.h"
#include "match.h"
#include "player.h"
#include "window.h"
#include <string.h>

/* Callback for edit button */
static void on_edit_match_clicked(GtkButton *btn, gpointer data) {
  (void)data;
  ReelApp *app = g_object_get_data(G_OBJECT(btn), "app");
  gint64 film_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "film_id"));
  GtkWidget *parent = g_object_get_data(G_OBJECT(btn), "dialog");
  gtk_widget_destroy(parent);
  match_show(app, film_id);
}

/* Callback for play button */
static void on_play_clicked(GtkButton *btn, gpointer data) {
  (void)data;
  ReelApp *app = g_object_get_data(G_OBJECT(btn), "app");
  const gchar *path = g_object_get_data(G_OBJECT(btn), "file_path");
  player_launch(app, path);
}

static void refresh_detail(ReelApp *app, GtkWidget *dialog, gint64 film_id) {
  gtk_widget_destroy(dialog);
  window_refresh_films(app);
  detail_show(app, film_id);
}

static void on_delete_clicked(GtkButton *btn, gpointer data) {
  (void)data;
  ReelApp *app = g_object_get_data(G_OBJECT(btn), "app");
  gint64 film_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "film_id"));
  GtkWidget *dialog = g_object_get_data(G_OBJECT(btn), "dialog");

  GtkWidget *confirm = gtk_message_dialog_new(
      GTK_WINDOW(dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
      GTK_BUTTONS_OK_CANCEL, "Delete this entry from the database?");
  gtk_message_dialog_format_secondary_text(
      GTK_MESSAGE_DIALOG(confirm),
      "This removes the film/season and any associated data from the library.");

  gint response = gtk_dialog_run(GTK_DIALOG(confirm));
  gtk_widget_destroy(confirm);
  if (response != GTK_RESPONSE_OK)
    return;

  if (!db_film_delete(app, film_id)) {
    GtkWidget *err = gtk_message_dialog_new(GTK_WINDOW(dialog),
                                           GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                                           GTK_BUTTONS_OK,
                                           "Failed to delete entry.");
    gtk_dialog_run(GTK_DIALOG(err));
    gtk_widget_destroy(err);
    return;
  }

  gtk_widget_destroy(dialog);
  window_refresh_films(app);
}

static void on_remove_file_clicked(GtkButton *btn, gpointer data) {
  (void)data;
  ReelApp *app = g_object_get_data(G_OBJECT(btn), "app");
  GtkWidget *dialog = g_object_get_data(G_OBJECT(btn), "dialog");
  gint64 film_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "film_id"));
  gint64 file_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "file_id"));

  GtkWidget *confirm = gtk_message_dialog_new(
      GTK_WINDOW(dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
      GTK_BUTTONS_OK_CANCEL, "Remove this file from the entry?");

  gint response = gtk_dialog_run(GTK_DIALOG(confirm));
  gtk_widget_destroy(confirm);
  if (response != GTK_RESPONSE_OK)
    return;

  if (!db_film_file_delete(app, file_id)) {
    GtkWidget *err = gtk_message_dialog_new(GTK_WINDOW(dialog),
                                           GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                                           GTK_BUTTONS_OK,
                                           "Failed to remove file.");
    gtk_dialog_run(GTK_DIALOG(err));
    gtk_widget_destroy(err);
    return;
  }

  refresh_detail(app, dialog, film_id);
}

static void on_add_file_clicked(GtkButton *btn, gpointer data) {
  (void)data;
  ReelApp *app = g_object_get_data(G_OBJECT(btn), "app");
  GtkWidget *dialog = g_object_get_data(G_OBJECT(btn), "dialog");
  gint64 film_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "film_id"));

  GtkWidget *chooser = gtk_file_chooser_dialog_new(
      "Attach File", GTK_WINDOW(dialog), GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel",
      GTK_RESPONSE_CANCEL, "_Attach", GTK_RESPONSE_ACCEPT, NULL);

  Film *film = db_film_get_by_id(app, film_id);
  if (film && film->file_path) {
    gchar *dir = g_path_get_dirname(film->file_path);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(chooser), dir);
    g_free(dir);
  }
  film_free(film);

  gint response = gtk_dialog_run(GTK_DIALOG(chooser));
  if (response != GTK_RESPONSE_ACCEPT) {
    gtk_widget_destroy(chooser);
    return;
  }

  gchar *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
  gtk_widget_destroy(chooser);
  if (!path)
    return;

  Film *existing = db_film_get_by_path(app, path);
  if (existing && existing->id != film_id) {
    GtkWidget *confirm = gtk_message_dialog_new(
        GTK_WINDOW(dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_OK_CANCEL,
        "That file already exists as a separate entry.\nMerge it into this one?");
    gtk_message_dialog_format_secondary_text(
        GTK_MESSAGE_DIALOG(confirm),
        "The other entry will be removed and its file will be attached here.");
    gint merge_resp = gtk_dialog_run(GTK_DIALOG(confirm));
    gtk_widget_destroy(confirm);
    if (merge_resp != GTK_RESPONSE_OK) {
      film_free(existing);
      g_free(path);
      return;
    }
  }

  if (!db_film_file_attach(app, film_id, path, NULL, 0)) {
    GtkWidget *err = gtk_message_dialog_new(
        GTK_WINDOW(dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
        "Failed to attach file.");
    gtk_dialog_run(GTK_DIALOG(err));
    gtk_widget_destroy(err);
    if (existing)
      film_free(existing);
    g_free(path);
    return;
  }

  if (existing && existing->id != film_id) {
    db_film_delete(app, existing->id);
    film_free(existing);
  } else if (existing) {
    film_free(existing);
  }

  g_free(path);
  refresh_detail(app, dialog, film_id);
}

void detail_show(ReelApp *app, gint64 film_id) {
  Film *film = db_film_get_by_id(app, film_id);
  if (film == NULL) {
    g_printerr("Film not found: %ld\n", film_id);
    return;
  }

  /* Create dialog */
  gchar *dialog_title = film->title ? film->title : "Film Details";
  GtkWidget *dialog = gtk_dialog_new_with_buttons(
      dialog_title, GTK_WINDOW(app->window),
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, NULL, NULL);
  window_apply_theme(app, dialog);

  gtk_window_set_default_size(GTK_WINDOW(dialog), 700, 500);

  /* Use a header bar to match the main window style. */
  GtkWidget *header = gtk_header_bar_new();
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(header), dialog_title);
  gtk_window_set_titlebar(GTK_WINDOW(dialog), header);

  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  gtk_container_set_border_width(GTK_CONTAINER(content), 16);

  /* Main horizontal layout */
  GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
  gtk_box_pack_start(GTK_BOX(content), hbox, TRUE, TRUE, 0);

  /* Poster */
  GtkWidget *poster_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_box_pack_start(GTK_BOX(hbox), poster_box, FALSE, FALSE, 0);

  GtkWidget *poster_image;
  if (film->poster_path && g_file_test(film->poster_path, G_FILE_TEST_EXISTS)) {
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(film->poster_path,
                                                          250, 375, TRUE, NULL);
    if (pixbuf) {
      poster_image = gtk_image_new_from_pixbuf(pixbuf);
      g_object_unref(pixbuf);
    } else {
      poster_image =
          gtk_image_new_from_icon_name("video-x-generic", GTK_ICON_SIZE_DIALOG);
      gtk_image_set_pixel_size(GTK_IMAGE(poster_image), 200);
    }
  } else {
    poster_image =
        gtk_image_new_from_icon_name("video-x-generic", GTK_ICON_SIZE_DIALOG);
    gtk_image_set_pixel_size(GTK_IMAGE(poster_image), 200);
  }
  gtk_box_pack_start(GTK_BOX(poster_box), poster_image, FALSE, FALSE, 0);

  /* Info section */
  GtkWidget *info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_hexpand(info_box, TRUE);
  gtk_box_pack_start(GTK_BOX(hbox), info_box, TRUE, TRUE, 0);

  /* Title + Year */
  gchar *title_text;
  if (film->year > 0) {
    title_text =
        g_strdup_printf("<span size='x-large' weight='bold'>%s</span> <span "
                        "size='large'>(%d)</span>",
                        film->title ? film->title : "Unknown", film->year);
  } else {
    title_text = g_strdup_printf("<span size='x-large' weight='bold'>%s</span>",
                                 film->title ? film->title : "Unknown");
  }
  GtkWidget *title_label = gtk_label_new(NULL);
  gtk_label_set_markup(GTK_LABEL(title_label), title_text);
  gtk_label_set_xalign(GTK_LABEL(title_label), 0);
  gtk_box_pack_start(GTK_BOX(info_box), title_label, FALSE, FALSE, 0);
  g_free(title_text);

  /* Rating + Runtime + Genres */
  GString *meta = g_string_new("");

  if (film->rating > 0) {
    g_string_append_printf(meta, "★ %.1f/10", film->rating);
  }

  if (film->runtime_minutes > 0) {
    if (meta->len > 0)
      g_string_append(meta, "  │  ");
    int hours = film->runtime_minutes / 60;
    int mins = film->runtime_minutes % 60;
    if (hours > 0) {
      g_string_append_printf(meta, "%dh %dm", hours, mins);
    } else {
      g_string_append_printf(meta, "%dm", mins);
    }
  }

  /* Genres */
  GList *genres = db_genres_get_for_film(app, film_id);
  if (genres) {
    if (meta->len > 0)
      g_string_append(meta, "  │  ");
    for (GList *l = genres; l != NULL; l = l->next) {
      if (l != genres)
        g_string_append(meta, ", ");
      g_string_append(meta, (gchar *)l->data);
    }
    g_list_free_full(genres, g_free);
  }

  if (meta->len > 0) {
    GtkWidget *meta_label = gtk_label_new(meta->str);
    gtk_label_set_xalign(GTK_LABEL(meta_label), 0);
    gtk_box_pack_start(GTK_BOX(info_box), meta_label, FALSE, FALSE, 0);
  }
  g_string_free(meta, TRUE);

  /* Directors */
  GList *directors = db_directors_get_for_film(app, film_id);
  if (directors) {
    GString *dir_str = g_string_new("<b>Director:</b> ");
    for (GList *l = directors; l != NULL; l = l->next) {
      DbPerson *person = (DbPerson *)l->data;
      if (l != directors)
        g_string_append(dir_str, ", ");
      g_string_append(dir_str, person->name);
    }

    GtkWidget *dir_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(dir_label), dir_str->str);
    gtk_label_set_xalign(GTK_LABEL(dir_label), 0);
    gtk_box_pack_start(GTK_BOX(info_box), dir_label, FALSE, FALSE, 0);

    g_string_free(dir_str, TRUE);
    g_list_free_full(directors, (GDestroyNotify)db_person_free);
  }

  /* Cast */
  GList *cast = db_actors_get_for_film(app, film_id);
  if (cast) {
    GtkWidget *cast_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(cast_label), "<b>Cast:</b>");
    gtk_label_set_xalign(GTK_LABEL(cast_label), 0);
    gtk_box_pack_start(GTK_BOX(info_box), cast_label, FALSE, FALSE, 0);

    int count = 0;
    for (GList *l = cast; l != NULL && count < 6; l = l->next, count++) {
      DbCastMember *member = (DbCastMember *)l->data;
      gchar *cast_text;
      if (member->role && strlen(member->role) > 0) {
        cast_text = g_strdup_printf("  • %s as %s", member->name, member->role);
      } else {
        cast_text = g_strdup_printf("  • %s", member->name);
      }

      GtkWidget *cast_item = gtk_label_new(cast_text);
      gtk_label_set_xalign(GTK_LABEL(cast_item), 0);
      gtk_box_pack_start(GTK_BOX(info_box), cast_item, FALSE, FALSE, 0);
      g_free(cast_text);
    }

    g_list_free_full(cast, (GDestroyNotify)db_cast_member_free);
  }

  /* Plot */
  if (film->plot && strlen(film->plot) > 0) {
    GtkWidget *plot_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(plot_label), "<b>Plot:</b>");
    gtk_label_set_xalign(GTK_LABEL(plot_label), 0);
    gtk_widget_set_margin_top(plot_label, 8);
    gtk_box_pack_start(GTK_BOX(info_box), plot_label, FALSE, FALSE, 0);

    GtkWidget *plot_text = gtk_label_new(film->plot);
    gtk_label_set_xalign(GTK_LABEL(plot_text), 0);
    gtk_label_set_line_wrap(GTK_LABEL(plot_text), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(plot_text), 60);
    gtk_box_pack_start(GTK_BOX(info_box), plot_text, FALSE, FALSE, 0);
  }

  /* Episodes (for TV Season) */
  if (film->media_type == MEDIA_TV_SEASON) {
    GtkWidget *ep_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(ep_label), "<b>Episodes:</b>");
    gtk_label_set_xalign(GTK_LABEL(ep_label), 0);
    gtk_widget_set_margin_top(ep_label, 12);
    gtk_box_pack_start(GTK_BOX(info_box), ep_label, FALSE, FALSE, 0);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolled, -1, 150);
    gtk_box_pack_start(GTK_BOX(info_box), scrolled, TRUE, TRUE, 4);

    GtkWidget *ep_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_add(GTK_CONTAINER(scrolled), ep_list);

    GList *episodes = db_episodes_get_for_season(app, film->id);
    if (episodes) {
      for (GList *l = episodes; l != NULL; l = l->next) {
        Episode *ep = (Episode *)l->data;
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

        gchar *ep_text = g_strdup_printf("<b>%d.</b> %s", ep->episode_number,
                                         ep->title ? ep->title : "Episode");
        GtkWidget *lbl = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(lbl), ep_text);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0);
        gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
        gtk_box_pack_start(GTK_BOX(row), lbl, TRUE, TRUE, 0);
        g_free(ep_text);

        GtkWidget *play_ep = gtk_button_new_from_icon_name(
            "media-playback-start-symbolic", GTK_ICON_SIZE_BUTTON);
        gtk_widget_set_tooltip_text(play_ep, "Play Episode");
        g_object_set_data_full(G_OBJECT(play_ep), "file_path",
                               g_strdup(ep->file_path), g_free);
        g_object_set_data(G_OBJECT(play_ep), "app", app);
        g_signal_connect(play_ep, "clicked", G_CALLBACK(on_play_clicked), NULL);
        gtk_box_pack_end(GTK_BOX(row), play_ep, FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(ep_list), row, FALSE, FALSE, 2);
      }
      g_list_free_full(episodes, (GDestroyNotify)episode_free);
    } else {
      GtkWidget *no_eps = gtk_label_new("No episodes found.");
      gtk_label_set_xalign(GTK_LABEL(no_eps), 0);
      gtk_box_pack_start(GTK_BOX(ep_list), no_eps, FALSE, FALSE, 0);
    }
  }

  /* Spacer */
  GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_vexpand(spacer, TRUE);
  gtk_box_pack_start(GTK_BOX(info_box), spacer, TRUE, TRUE, 0);

  /* File(s) */
  GtkWidget *file_frame =
      gtk_frame_new(film->media_type == MEDIA_FILM ? "Files" : "File");
  gtk_box_pack_start(GTK_BOX(content), file_frame, FALSE, FALSE, 0);

  GtkWidget *files_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_container_add(GTK_CONTAINER(file_frame), files_box);
  gtk_container_set_border_width(GTK_CONTAINER(file_frame), 6);

  /* Primary file row */
  GtkWidget *primary_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_pack_start(GTK_BOX(files_box), primary_row, FALSE, FALSE, 0);

  GtkWidget *file_label = gtk_label_new(film->file_path);
  gtk_label_set_selectable(GTK_LABEL(file_label), TRUE);
  gtk_label_set_ellipsize(GTK_LABEL(file_label), PANGO_ELLIPSIZE_MIDDLE);
  gtk_label_set_xalign(GTK_LABEL(file_label), 0);
  gtk_box_pack_start(GTK_BOX(primary_row), file_label, TRUE, TRUE, 0);

  if (film->media_type == MEDIA_FILM) {
    GtkWidget *play_primary = gtk_button_new_with_label("Play");
    g_object_set_data_full(G_OBJECT(play_primary), "file_path",
                           g_strdup(film->file_path), g_free);
    g_object_set_data(G_OBJECT(play_primary), "app", app);
    g_signal_connect(play_primary, "clicked", G_CALLBACK(on_play_clicked), NULL);
    gtk_box_pack_end(GTK_BOX(primary_row), play_primary, FALSE, FALSE, 0);
  }

  /* Attached files (movies only) */
  if (film->media_type == MEDIA_FILM) {
    GList *files = db_film_files_get(app, film_id);
    for (GList *l = files; l != NULL; l = l->next) {
      FilmFile *ff = (FilmFile *)l->data;

      GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
      gtk_box_pack_start(GTK_BOX(files_box), row, FALSE, FALSE, 0);

      const gchar *display = ff->label && strlen(ff->label) > 0
                                 ? ff->label
                                 : ff->file_path;
      GtkWidget *lbl = gtk_label_new(display);
      gtk_label_set_selectable(GTK_LABEL(lbl), TRUE);
      gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_MIDDLE);
      gtk_label_set_xalign(GTK_LABEL(lbl), 0);
      gtk_box_pack_start(GTK_BOX(row), lbl, TRUE, TRUE, 0);

      GtkWidget *play_btn = gtk_button_new_with_label("Play");
      g_object_set_data_full(G_OBJECT(play_btn), "file_path",
                             g_strdup(ff->file_path), g_free);
      g_object_set_data(G_OBJECT(play_btn), "app", app);
      g_signal_connect(play_btn, "clicked", G_CALLBACK(on_play_clicked), NULL);
      gtk_box_pack_end(GTK_BOX(row), play_btn, FALSE, FALSE, 0);

      GtkWidget *remove_btn = gtk_button_new_with_label("Remove");
      g_object_set_data(G_OBJECT(remove_btn), "app", app);
      g_object_set_data(G_OBJECT(remove_btn), "dialog", dialog);
      g_object_set_data(G_OBJECT(remove_btn), "film_id", GINT_TO_POINTER(film_id));
      g_object_set_data(G_OBJECT(remove_btn), "file_id",
                        GINT_TO_POINTER(ff->id));
      g_signal_connect(remove_btn, "clicked", G_CALLBACK(on_remove_file_clicked),
                       NULL);
      gtk_box_pack_end(GTK_BOX(row), remove_btn, FALSE, FALSE, 0);
    }
    g_list_free_full(files, (GDestroyNotify)film_file_free);
  }

  /* Button box */
  GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_margin_top(btn_box, 12);
  gtk_box_pack_end(GTK_BOX(content), btn_box, FALSE, FALSE, 0);

  /* Close button */
  GtkWidget *close_btn = gtk_button_new_with_label("Close");
  gtk_box_pack_end(GTK_BOX(btn_box), close_btn, FALSE, FALSE, 0);
  g_signal_connect_swapped(close_btn, "clicked", G_CALLBACK(gtk_widget_destroy),
                           dialog);

  /* Edit Match button */
  GtkWidget *edit_btn = gtk_button_new_with_label("Edit Match...");
  gtk_box_pack_end(GTK_BOX(btn_box), edit_btn, FALSE, FALSE, 0);
  g_object_set_data(G_OBJECT(edit_btn), "film_id", GINT_TO_POINTER(film_id));
  g_object_set_data(G_OBJECT(edit_btn), "app", app);
  g_object_set_data(G_OBJECT(edit_btn), "dialog", dialog);
  g_signal_connect(edit_btn, "clicked", G_CALLBACK(on_edit_match_clicked),
                   NULL);

  /* Play button (Only for Movies) */
  if (film->media_type == MEDIA_FILM) {
    GtkWidget *add_btn = gtk_button_new_with_label("Add File...");
    gtk_box_pack_start(GTK_BOX(btn_box), add_btn, FALSE, FALSE, 0);
    g_object_set_data(G_OBJECT(add_btn), "app", app);
    g_object_set_data(G_OBJECT(add_btn), "dialog", dialog);
    g_object_set_data(G_OBJECT(add_btn), "film_id", GINT_TO_POINTER(film_id));
    g_signal_connect(add_btn, "clicked", G_CALLBACK(on_add_file_clicked), NULL);
  }

  /* Delete button */
  GtkWidget *delete_btn = gtk_button_new_with_label("Delete");
  gtk_box_pack_start(GTK_BOX(btn_box), delete_btn, FALSE, FALSE, 0);
  g_object_set_data(G_OBJECT(delete_btn), "app", app);
  g_object_set_data(G_OBJECT(delete_btn), "dialog", dialog);
  g_object_set_data(G_OBJECT(delete_btn), "film_id", GINT_TO_POINTER(film_id));
  g_signal_connect(delete_btn, "clicked", G_CALLBACK(on_delete_clicked), NULL);

  gtk_widget_show_all(dialog);

  film_free(film);
}
