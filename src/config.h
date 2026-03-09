#pragma once

#define CONFIG_DIR  "/.config/retypex"
#define CONFIG_FILE "/.config/retypex/config"

typedef struct {
    char keyboard[64];  /* device name for hyprctl, default "all" */
} Config;

extern Config g_config;

void        config_load(void);
const char *config_socket_path(void);
