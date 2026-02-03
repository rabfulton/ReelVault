#ifndef REELGTK_GRID_H
#define REELGTK_GRID_H

#include "app.h"

/* Create the poster grid widget */
GtkWidget *grid_create(ReelApp *app);

/* Populate/refresh the grid with films */
void grid_populate(ReelApp *app);

/* Append a batch of films to the grid (Film* list, not owned) */
void grid_append_films(ReelApp *app, GList *films);

/* Replace a single film's grid item if present */
void grid_update_film(ReelApp *app, const Film *film);

/* Clear all items from the grid */
void grid_clear(ReelApp *app);

#endif /* REELGTK_GRID_H */
