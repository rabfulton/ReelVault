#ifndef REELGTK_WINDOW_H
#define REELGTK_WINDOW_H

#include "app.h"

/* Create the main application window */
void window_create(ReelApp *app);

/* Apply current theme to a toplevel widget (window/dialog) */
void window_apply_theme(ReelApp *app, GtkWidget *toplevel);

/* Update window state */
void window_refresh_films(ReelApp *app);
void window_refresh_film(ReelApp *app, gint64 film_id);
void window_update_status_bar(ReelApp *app);

/* Menu/toolbar actions */
void window_show_settings(ReelApp *app);
void window_scan_library(ReelApp *app);

#endif /* REELGTK_WINDOW_H */
