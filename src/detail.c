/*
 * ReelGTK - Film Detail Dialog
 * Displays full film information with play button
 */

#include "detail.h"
#include "db.h"
#include "match.h"
#include "player.h"
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

  gtk_window_set_default_size(GTK_WINDOW(dialog), 700, 500);

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

  /* Spacer */
  GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_vexpand(spacer, TRUE);
  gtk_box_pack_start(GTK_BOX(info_box), spacer, TRUE, TRUE, 0);

  /* File path */
  GtkWidget *file_frame = gtk_frame_new("File");
  gtk_box_pack_start(GTK_BOX(content), file_frame, FALSE, FALSE, 0);

  GtkWidget *file_label = gtk_label_new(film->file_path);
  gtk_label_set_selectable(GTK_LABEL(file_label), TRUE);
  gtk_label_set_ellipsize(GTK_LABEL(file_label), PANGO_ELLIPSIZE_START);
  gtk_container_add(GTK_CONTAINER(file_frame), file_label);
  gtk_container_set_border_width(GTK_CONTAINER(file_frame), 6);

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

  /* Play button */
  GtkWidget *play_btn = gtk_button_new_with_label("▶ Play");
  GtkStyleContext *play_ctx = gtk_widget_get_style_context(play_btn);
  gtk_style_context_add_class(play_ctx, "suggested-action");
  gtk_box_pack_end(GTK_BOX(btn_box), play_btn, FALSE, FALSE, 0);

  g_object_set_data_full(G_OBJECT(play_btn), "file_path",
                         g_strdup(film->file_path), g_free);
  g_object_set_data(G_OBJECT(play_btn), "app", app);
  g_signal_connect(play_btn, "clicked", G_CALLBACK(on_play_clicked), NULL);

  gtk_widget_show_all(dialog);

  film_free(film);
}
