#ifndef REELGTK_UTILS_H
#define REELGTK_UTILS_H

#include <glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

/* Normalize a title string (replace dots/underscores, remove quality tags,
 * trim) */
gchar *utils_normalize_title(const gchar *raw);

/* Format runtime as "Xh Ym" */
gchar *utils_format_runtime(gint minutes);

/* Load an image at scale, with a fallback path for rare multi-scan baseline
 * JPEGs. If preserve_aspect is TRUE, scales to fit within width/height. */
GdkPixbuf *utils_pixbuf_new_from_file_at_scale_safe(const gchar *path,
                                                    gint width, gint height,
                                                    gboolean preserve_aspect,
                                                    GError **error);

#endif /* REELGTK_UTILS_H */
