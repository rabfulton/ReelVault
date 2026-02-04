/*
 * ReelGTK - Utility Functions
 */

#include "utils.h"
#include <ctype.h>
#include <string.h>

/* TurboJPEG header location varies by distro. */
#if __has_include(<turbojpeg.h>)
#  include <turbojpeg.h>
#  define REELVAULT_HAVE_TURBOJPEG 1
#elif __has_include(<turbojpeg/turbojpeg.h>)
#  include <turbojpeg/turbojpeg.h>
#  define REELVAULT_HAVE_TURBOJPEG 1
#else
#  define REELVAULT_HAVE_TURBOJPEG 0
#endif

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

static GdkPixbuf *pixbuf_scale_fit(GdkPixbuf *pixbuf, gint width, gint height,
                                   gboolean preserve_aspect) {
  if (!pixbuf || width <= 0 || height <= 0)
    return NULL;

  gint ow = gdk_pixbuf_get_width(pixbuf);
  gint oh = gdk_pixbuf_get_height(pixbuf);
  if (ow <= 0 || oh <= 0)
    return NULL;

  gint tw = width;
  gint th = height;
  if (preserve_aspect) {
    gdouble sx = (gdouble)width / (gdouble)ow;
    gdouble sy = (gdouble)height / (gdouble)oh;
    gdouble s = (sx < sy) ? sx : sy;
    if (s <= 0.0)
      return NULL;
    tw = (gint)(ow * s + 0.5);
    th = (gint)(oh * s + 0.5);
    if (tw < 1)
      tw = 1;
    if (th < 1)
      th = 1;
  }

  if (tw == ow && th == oh)
    return g_object_ref(pixbuf);

  return gdk_pixbuf_scale_simple(pixbuf, tw, th, GDK_INTERP_BILINEAR);
}

static void utils_pixbuf_free_pixels(guchar *pixels, gpointer user_data) {
  (void)user_data;
  g_free(pixels);
}

static gboolean utils_path_is_jpeg(const gchar *path) {
  if (!path)
    return FALSE;
  return g_str_has_suffix(path, ".jpg") || g_str_has_suffix(path, ".JPG") ||
         g_str_has_suffix(path, ".jpeg") || g_str_has_suffix(path, ".JPEG");
}

static GdkPixbuf *utils_pixbuf_new_from_jpeg_turbo(const gchar *path,
                                                   GError **error) {
#if !REELVAULT_HAVE_TURBOJPEG
  (void)path;
  (void)error;
  return NULL;
#else
  if (!path)
    return NULL;

  guchar *jpeg_buf = NULL;
  gsize jpeg_len = 0;
  if (!g_file_get_contents(path, (gchar **)&jpeg_buf, &jpeg_len, error)) {
    return NULL;
  }

  tjhandle handle = tjInitDecompress();
  if (!handle) {
    g_free(jpeg_buf);
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                "turbojpeg init failed");
    return NULL;
  }

  int width = 0, height = 0, subsamp = 0, cs = 0;
  if (tjDecompressHeader3(handle, jpeg_buf, (unsigned long)jpeg_len, &width,
                          &height, &subsamp, &cs) != 0) {
    const char *msg = tjGetErrorStr2(handle);
    tjDestroy(handle);
    g_free(jpeg_buf);
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                "turbojpeg header failed: %s", msg ? msg : "unknown");
    return NULL;
  }

  if (width <= 0 || height <= 0) {
    tjDestroy(handle);
    g_free(jpeg_buf);
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                "turbojpeg invalid dimensions");
    return NULL;
  }

  gsize stride = (gsize)width * 3;
  gsize buf_sz = (gsize)height * stride;
  guchar *rgb = g_malloc(buf_sz);

  if (tjDecompress2(handle, jpeg_buf, (unsigned long)jpeg_len, rgb, width,
                    (int)stride, height, TJPF_RGB, TJFLAG_ACCURATEDCT) != 0) {
    const char *msg = tjGetErrorStr2(handle);
    tjDestroy(handle);
    g_free(jpeg_buf);
    g_free(rgb);
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                "turbojpeg decompress failed: %s", msg ? msg : "unknown");
    return NULL;
  }

  tjDestroy(handle);
  g_free(jpeg_buf);

  return gdk_pixbuf_new_from_data(rgb, GDK_COLORSPACE_RGB, FALSE, 8, width,
                                 height, (int)stride, utils_pixbuf_free_pixels,
                                 NULL);
#endif
}

GdkPixbuf *utils_pixbuf_new_from_file_at_scale_safe(const gchar *path, gint width,
                                                    gint height,
                                                    gboolean preserve_aspect,
                                                    GError **error) {
  if (!path)
    return NULL;

  /* Work around rare JPEG decode issues by using libjpeg-turbo when possible. */
  GdkPixbuf *full = NULL;
  if (utils_path_is_jpeg(path)) {
    GError *tj_err = NULL;
    full = utils_pixbuf_new_from_jpeg_turbo(path, &tj_err);
    if (!full && tj_err) {
      g_error_free(tj_err);
    }
  }

  if (!full) {
    full = gdk_pixbuf_new_from_file(path, error);
  }
  if (!full)
    return NULL;

  if (width <= 0 || height <= 0)
    return full;

  GdkPixbuf *scaled = pixbuf_scale_fit(full, width, height, preserve_aspect);
  g_object_unref(full);
  return scaled;
}
