#ifndef REELGTK_FILTER_H
#define REELGTK_FILTER_H

#include "app.h"

/* Create the filter bar widget */
GtkWidget *filter_bar_create(ReelApp *app);

/* Refresh filter dropdowns (after DB changes) */
void filter_bar_refresh(ReelApp *app);

/* Clear all filters */
void filter_bar_reset(ReelApp *app);

/* Focus the search entry (for keyboard shortcuts like Ctrl+F). */
void filter_bar_focus_search(ReelApp *app);

#endif /* REELGTK_FILTER_H */
