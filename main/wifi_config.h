#pragma once
#include <stdbool.h>
#include <stddef.h>
#define PEARL_WIFI_PROFILE_LIMIT 4
#define PEARL_WIFI_CONFIG_LIMIT 8192
typedef struct {
  char ssid[33], password[65];
} pearl_wifi_profile;
typedef enum {
  WIFI_IMPORT_INVALID = -1,
  WIFI_IMPORT_IO = -2,
  WIFI_IMPORT_STORAGE = -3,
  WIFI_IMPORT_MISSING = 0,
  WIFI_IMPORT_DONE = 1,
  WIFI_IMPORT_RETAINED = 2
} pearl_wifi_import_result;
bool pearl_wifi_config_parse(const char *data, size_t bytes,
                             pearl_wifi_profile out[PEARL_WIFI_PROFILE_LIMIT],
                             unsigned *count);
typedef bool (*pearl_wifi_persist)(const pearl_wifi_profile *profiles,
                                   unsigned count, void *context);
pearl_wifi_import_result pearl_wifi_config_import(
    const char *path, pearl_wifi_persist persist, void *context,
    pearl_wifi_profile out[PEARL_WIFI_PROFILE_LIMIT], unsigned *count);
