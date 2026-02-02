#ifndef REELGTK_UTILS_H
#define REELGTK_UTILS_H

#include <glib.h>

/* Normalize a title string (replace dots/underscores, remove quality tags,
 * trim) */
gchar *utils_normalize_title(const gchar *raw);

/* Format runtime as "Xh Ym" */
gchar *utils_format_runtime(gint minutes);

#endif /* REELGTK_UTILS_H */
