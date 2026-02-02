#ifndef REELGTK_SCANNER_H
#define REELGTK_SCANNER_H

#include "app.h"

/* Scan a directory for video files and add to database */
gint scanner_scan_directory(ReelApp *app, const gchar *path);

/* Parse a filename to extract title and year */
gboolean scanner_parse_filename(const gchar *filename, gchar **title,
                                gint *year);

#endif /* REELGTK_SCANNER_H */
