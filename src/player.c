/*
 * ReelGTK - External Player
 * Launches films in the configured video player
 */

#include "player.h"

void player_launch(ReelApp *app, const gchar *file_path) {
  if (!file_path) {
    g_printerr("No file path provided\n");
    return;
  }

  const gchar *player = app->player_command ? app->player_command : "xdg-open";

  gchar *argv[] = {(gchar *)player, (gchar *)file_path, NULL};

  GError *error = NULL;
  gboolean success = g_spawn_async(NULL, /* working directory */
                                   argv, /* argv */
                                   NULL, /* envp */
                                   G_SPAWN_SEARCH_PATH, NULL, /* child_setup */
                                   NULL,                      /* user_data */
                                   NULL,                      /* child_pid */
                                   &error);

  if (!success) {
    g_printerr("Failed to launch player: %s\n", error->message);
    g_error_free(error);

    /* Show error dialog */
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(app->window), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
        GTK_BUTTONS_OK,
        "Failed to launch video player '%s'.\n\nCheck your settings to ensure "
        "the player command is correct.",
        player);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
  } else {
    g_print("Launched %s with: %s\n", player, file_path);
  }
}
