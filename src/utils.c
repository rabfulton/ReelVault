/*
 * ReelGTK - Utility Functions
 */

#include "utils.h"
#include <ctype.h>
#include <string.h>

/* Quality/release tags to strip from titles */
static const char *STRIP_TAGS[] = {
    "1080p",    "720p",    "480p",          "2160p",      "4k",     "uhd",
    "bluray",   "blu-ray", "bdrip",         "brrip",      "dvdrip", "dvdscr",
    "hdtv",     "webrip",  "web-dl",        "webdl",      "x264",   "x265",
    "h264",     "h265",    "hevc",          "avc",        "aac",    "ac3",
    "dts",      "truehd",  "atmos",         "remux",      "proper", "repack",
    "extended", "unrated", "directors cut", "theatrical", "imax",   "yify",
    "yts",      "rarbg",   "ettv",          "eztv",       NULL};

gchar *utils_normalize_title(const gchar *raw) {
  if (!raw)
    return NULL;

  /* Replace dots and underscores with spaces */
  gchar *result = g_strdup(raw);
  for (gchar *p = result; *p; p++) {
    if (*p == '.' || *p == '_') {
      *p = ' ';
    }
  }

  /* Convert to lowercase for tag matching */
  gchar *lower = g_ascii_strdown(result, -1);

  /* Remove quality tags */
  for (int i = 0; STRIP_TAGS[i] != NULL; i++) {
    gchar *pos = strstr(lower, STRIP_TAGS[i]);
    if (pos) {
      /* Find corresponding position in result */
      gsize offset = pos - lower;
      if (offset < strlen(result)) {
        result[offset] = '\0';
      }
      lower[offset] = '\0';
    }
  }

  g_free(lower);

  /* Trim whitespace */
  g_strstrip(result);

  /* Remove trailing dashes/spaces */
  gsize len = strlen(result);
  while (len > 0 && (result[len - 1] == '-' || result[len - 1] == ' ')) {
    result[--len] = '\0';
  }

  /* Capitalize first letter of each word */
  gboolean cap_next = TRUE;
  for (gchar *p = result; *p; p++) {
    if (cap_next && *p >= 'a' && *p <= 'z') {
      *p = *p - 'a' + 'A';
    }
    cap_next = (*p == ' ');
  }

  return result;
}

gchar *utils_format_runtime(gint minutes) {
  if (minutes <= 0)
    return g_strdup("Unknown");

  gint hours = minutes / 60;
  gint mins = minutes % 60;

  if (hours > 0) {
    return g_strdup_printf("%dh %dm", hours, mins);
  } else {
    return g_strdup_printf("%dm", mins);
  }
}
