#ifndef REELGTK_CONFIG_H
#define REELGTK_CONFIG_H

#include "app.h"

/* Load configuration from file */
gboolean config_load(ReelApp *app);

/* Save configuration to file */
gboolean config_save(ReelApp *app);

/* Individual setting getters/setters */
void config_set_api_key(ReelApp *app, const gchar *api_key);
void config_set_player_command(ReelApp *app, const gchar *command);
void config_add_library_path(ReelApp *app, const gchar *path);
void config_remove_library_path(ReelApp *app, const gchar *path);

#endif /* REELGTK_CONFIG_H */
