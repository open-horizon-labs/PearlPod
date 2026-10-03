#include "wifi_config.h"
#include "tomlc17.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static bool known_keys(toml_datum_t table, const char *const *keys,
                       unsigned count) {
  if (table.type != TOML_TABLE)
    return false;
  for (int i = 0; i < table.u.tab.size; i++) {
    bool found = false;
    for (unsigned j = 0; j < count; j++)
      if (strlen(keys[j]) == (size_t)table.u.tab.len[i] &&
          !memcmp(keys[j], table.u.tab.key[i], table.u.tab.len[i]))
        found = true;
    if (!found)
      return false;
  }
  return true;
}
static bool string_ok(toml_datum_t value, size_t minimum, size_t maximum) {
  return value.type == TOML_STRING && value.u.str.len >= (int)minimum &&
         value.u.str.len <= (int)maximum &&
         strlen(value.u.s) == (size_t)value.u.str.len;
}
static bool read_profile(toml_datum_t table, pearl_wifi_profile *out) {
  const char *keys[] = {"ssid", "password", "hidden", "password_encrypted"};
  if (!known_keys(table, keys, 4))
    return false;
  toml_datum_t ssid = toml_get(table, "ssid"),
               pass = toml_get(table, "password"),
               hidden = toml_get(table, "hidden"),
               encrypted = toml_get(table, "password_encrypted");
  if (!string_ok(ssid, 1, 32) || !string_ok(pass, 0, 63) ||
      (pass.u.str.len && pass.u.str.len < 8) ||
      (hidden.type != TOML_UNKNOWN && hidden.type != TOML_BOOLEAN) ||
      (encrypted.type != TOML_UNKNOWN &&
       (encrypted.type != TOML_BOOLEAN || encrypted.u.boolean)))
    return false;
  memcpy(out->ssid, ssid.u.s, ssid.u.str.len);
  memcpy(out->password, pass.u.s, pass.u.str.len);
  return true;
}
bool pearl_wifi_config_parse(const char *data, size_t bytes,
                             pearl_wifi_profile out[PEARL_WIFI_PROFILE_LIMIT],
                             unsigned *count) {
  if (!data || !out || !count || !bytes || bytes > PEARL_WIFI_CONFIG_LIMIT ||
      memchr(data, 0, bytes))
    return false;
  toml_option_t options = toml_default_option();
  options.check_utf8 = true;
  toml_set_option(options);
  toml_result_t result = toml_parse(data, bytes);
  if (!result.ok) {
    toml_free(result);
    return false;
  }
  bool valid = false;
  pearl_wifi_profile next[PEARL_WIFI_PROFILE_LIMIT] = {0};
  unsigned total = 0;
  const char *keys[] = {"config_version", "networks", "wlan"};
  if (!known_keys(result.toptab, keys, 3))
    goto finish;
  toml_datum_t version = toml_get(result.toptab, "config_version");
  if (version.type != TOML_UNKNOWN &&
      (version.type != TOML_INT64 || version.u.int64 != 1))
    goto finish;
  toml_datum_t networks = toml_get(result.toptab, "networks"),
               wlan = toml_get(result.toptab, "wlan");
  if (networks.type == TOML_ARRAY && wlan.type == TOML_UNKNOWN) {
    if (networks.u.arr.size < 1 ||
        networks.u.arr.size > PEARL_WIFI_PROFILE_LIMIT)
      goto finish;
    total = networks.u.arr.size;
    for (unsigned i = 0; i < total; i++)
      if (!read_profile(networks.u.arr.elem[i], &next[i]))
        goto finish;
  } else if (networks.type == TOML_UNKNOWN && wlan.type == TOML_TABLE) {
    total = 1;
    if (!read_profile(wlan, &next[0]))
      goto finish;
  } else
    goto finish;
  for (unsigned i = 0; i < total; i++)
    for (unsigned j = 0; j < i; j++)
      if (!strcmp(next[i].ssid, next[j].ssid))
        goto finish;
  memcpy(out, next, sizeof(next));
  *count = total;
  valid = true;
finish:
  memset(next, 0, sizeof(next));
  toml_free(result);
  return valid;
}
pearl_wifi_import_result pearl_wifi_config_import(
    const char *path, pearl_wifi_persist persist, void *context,
    pearl_wifi_profile out[PEARL_WIFI_PROFILE_LIMIT], unsigned *count) {
  if (!path || !persist || !out || !count)
    return WIFI_IMPORT_INVALID;
  FILE *file = fopen(path, "rb");
  if (!file)
    return errno == ENOENT ? WIFI_IMPORT_MISSING : WIFI_IMPORT_IO;
  char *data = malloc(PEARL_WIFI_CONFIG_LIMIT + 1);
  if (!data) {
    fclose(file);
    return WIFI_IMPORT_IO;
  }
  size_t bytes = fread(data, 1, PEARL_WIFI_CONFIG_LIMIT + 1, file);
  bool io = ferror(file);
  fclose(file);
  pearl_wifi_profile next[PEARL_WIFI_PROFILE_LIMIT] = {0};
  unsigned total = 0;
  bool valid = !io && pearl_wifi_config_parse(data, bytes, next, &total);
  memset(data, 0, PEARL_WIFI_CONFIG_LIMIT + 1);
  free(data);
  if (io)
    return WIFI_IMPORT_IO;
  if (!valid)
    return WIFI_IMPORT_INVALID;
  if (!persist(next, total, context)) {
    memset(next, 0, sizeof(next));
    return WIFI_IMPORT_STORAGE;
  }
  memcpy(out, next, sizeof(next));
  *count = total;
  memset(next, 0, sizeof(next));
  return unlink(path) == 0 ? WIFI_IMPORT_DONE : WIFI_IMPORT_RETAINED;
}
