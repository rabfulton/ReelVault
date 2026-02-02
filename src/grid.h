#ifndef REELGTK_GRID_H
#define REELGTK_GRID_H

#include "app.h"

/* Create the poster grid widget */
GtkWidget *grid_create(ReelApp *app);

/* Populate/refresh the grid with films */
void grid_populate(ReelApp *app);

/* Clear all items from the grid */
void grid_clear(ReelApp *app);

#endif /* REELGTK_GRID_H */
