#include "managed.h"
#include "cJSON.h"
#include "mbedtls/sha256.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
#include "nvs.h"
#include "playlist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef PEARL_MANAGED_ROOT
#define PEARL_MANAGED_ROOT "/sdcard/music/.pearl"
#endif
#define ROOT PEARL_MANAGED_ROOT
typedef struct {
  char active[65], previous[65];
} activation;

static bool hashname(const char *s) {
  if (!s || strlen(s) < 64)
    return false;
  for (unsigned i = 0; i < 64; i++)
    if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
      return false;
  return true;
}
static bool object(const char *s) {
  if (!hashname(s) || strlen(s) > 70)
    return false;
  const char *ext = s + 64;
  return !strcmp(ext, ".mp3") || !strcmp(ext, ".jpg") || !strcmp(ext, ".lrc") ||
         !strcmp(ext, ".txt") || !strcmp(ext, ".srt") || !strcmp(ext, ".vtt") ||
         !strcmp(ext, ".m3u8");
}
static const char *string(cJSON *row, const char *key) {
  cJSON *v = cJSON_GetObjectItemCaseSensitive(row, key);
  return cJSON_IsString(v) ? v->valuestring : NULL;
}
static bool checksum(const char *path, const char *sha) {
  FILE *f = fopen(path, "rb");

  if (!f)
    return false;
#ifdef ESP_PLATFORM
  unsigned char *buf =
      heap_caps_malloc(16384, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  unsigned char *buf = malloc(16384);
#endif

  if (!buf) {
    fclose(f);
    return false;
  }
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  size_t n;

  while ((n = fread(buf, 1, 16384, f)))
    mbedtls_sha256_update(&ctx, buf, n);

  unsigned char digest[32];
  mbedtls_sha256_finish(&ctx, digest);
  mbedtls_sha256_free(&ctx);
  bool ok = !ferror(f);
  fclose(f);
  free(buf);

  char hex[65];
  for (int i = 0; i < 32; i++)
    snprintf(hex + i * 2, 3, "%02x", digest[i]);
  return ok && !strncmp(hex, sha, 64);
}
static bool read_activation(activation *record) {
  memset(record, 0, sizeof(*record));
  nvs_handle_t h;

  if (nvs_open("pearl_sync", NVS_READONLY, &h) != ESP_OK)
    return false;
  size_t n = sizeof(*record);
  esp_err_t e = nvs_get_blob(h, "active", record, &n);
  nvs_close(h);
  return e == ESP_OK && n == sizeof(*record) && record->active[64] == 0 &&
         record->previous[64] == 0;
}
/* Validate everything before changing NVS. Full hashing runs only during sync.
 */
static bool catalog(const char *sha, bool full, pearl_library *l,
                    pearl_add_track_fn add) {
  if (!hashname(sha) || strlen(sha) != 64)
    return false;

  char path[PEARL_PATH];
  snprintf(path, sizeof(path), ROOT "/catalogs/%s", sha);

  struct stat st;

  if (stat(path, &st) || st.st_size <= 0 || st.st_size > 512 * 1024 ||
      !checksum(path, sha))
    return false;

  FILE *f = fopen(path, "rb");

  if (!f)
    return false;
  char *line = malloc(4096);

  if (!line) {
    fclose(f);
    return false;
  }
  bool ok = true;

  while (ok && fgets(line, 4096, f)) {
    if (!strchr(line, '\n')) {
      ok = false;
      break;
    }
    bool quoted = false, escaped = false;
    unsigned depth = 0;
    for (char *p = line; *p; p++) {
      if (quoted) {
        if (escaped)
          escaped = false;
        else if (*p == '\\')
          escaped = true;
        else if (*p == '"')
          quoted = false;
      } else if (*p == '"')
        quoted = true;
      else if (*p == '{' || *p == '[') {
        if (++depth > 8) {
          ok = false;
          break;
        }
      } else if (*p == '}' || *p == ']') {
        if (!depth) {
          ok = false;
          break;
        }
        depth--;
      }
    }
    if (!ok || depth || quoted) {
      ok = false;
      break;
    }
    cJSON *row = cJSON_ParseWithOpts(line, NULL, true);

    if (!row) {
      ok = false;
      break;
    }
    const char *name = string(row, "file"), *track = string(row, "track"),
               *playlist = string(row, "playlist");

    if (name) {
      cJSON *size = cJSON_GetObjectItem(row, "bytes");
      snprintf(path, sizeof(path), ROOT "/objects/%s",
               object(name) ? name : "invalid");

      ok = object(name) && cJSON_IsNumber(size) && size->valuedouble >= 0 &&
           size->valuedouble <= 1024 * 1024 * 1024 &&
           size->valuedouble == (double)(size_t)size->valuedouble &&
           !stat(path, &st) && st.st_size == (off_t)size->valuedouble;

      if (ok && full && !checksum(path, name)) {
        unlink(path);
        ok = false;
      }
    } else if (track) {
      ok = object(track) && !strcmp(track + 64, ".mp3");
      if (ok) {
        snprintf(path, sizeof(path), ROOT "/objects/%s", track);
        ok = !stat(path, &st) && st.st_size > 0;
        if (ok && full)
          ok = checksum(path, track);
      }

      const char *album = string(row, "album_id");

      if (!album || strlen(album) > 48 ||
          strspn(album, "0123456789") != strlen(album))
        ok = false;

      const char *lyrics = NULL;
      cJSON *variants = cJSON_GetObjectItem(row, "lyrics");

      if (cJSON_IsArray(variants) && cJSON_GetArraySize(variants)) {
        lyrics = string(cJSON_GetArrayItem(variants, 0), "file");

        if (!object(lyrics))
          ok = false;
        if (ok && lyrics) {
          snprintf(path, sizeof(path), ROOT "/objects/%s", lyrics);
          ok = !stat(path, &st) && st.st_size > 0 && st.st_size <= 256 * 1024;
          if (ok && full)
            ok = checksum(path, lyrics);
        }
      }
      if (ok && l) {
        snprintf(path, sizeof(path), ROOT "/objects/%s", track);
        char dir[PEARL_PATH];
        snprintf(dir, sizeof(dir), ROOT "/album_%s", album);

        unsigned before = l->track_count;
        add(l, path, dir, track);

        if (l->track_count == before + 1 && lyrics) {
          pearl_track *t = &l->tracks[before];
          free(t->lyrics);
          snprintf(path, sizeof(path), ROOT "/objects/%s", lyrics);
#ifdef ESP_PLATFORM
          t->lyrics = heap_caps_malloc(strlen(path) + 1,
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
          if (t->lyrics)
            strcpy(t->lyrics, path);
#else
          t->lyrics = strdup(path);
#endif

          if (!t->lyrics)
            l->truncated = true;
        }
      }
    } else if (playlist) {
      const char *title = string(row, "title");
      ok = object(playlist) && !strcmp(playlist + 64, ".m3u8") && title &&
           strlen(title) < PEARL_NAME;

      if (ok) {
        snprintf(path, sizeof(path), ROOT "/objects/%s", playlist);
        ok = !stat(path, &st) && st.st_size > 0;
        if (ok && full)
          ok = checksum(path, playlist);
      }
      if (ok && l) {
        snprintf(path, sizeof(path), ROOT "/objects/%s", playlist);
        pearl_collection_add(l, PEARL_PLAYLISTS, title, path);
      }
    } else
      ok = false;

    cJSON_Delete(row);
  }
  if (ferror(f))
    ok = false;

  free(line);
  fclose(f);
  return ok;
}
bool pearl_managed_activate(const char *sha) {
  activation record = {0};
  read_activation(&record);
  if (sha && !strcmp(record.active, sha))
    return catalog(sha, false, NULL, NULL);
  if (!catalog(sha, true, NULL, NULL) || !pearl_library_validate_sync(sha))
    return false;

  snprintf(record.previous, sizeof(record.previous), "%s", record.active);
  snprintf(record.active, sizeof(record.active), "%s", sha);

  nvs_handle_t h;

  if (nvs_open("pearl_sync", NVS_READWRITE, &h) != ESP_OK)
    return false;

  esp_err_t e = nvs_set_blob(h, "active", &record, sizeof(record));

  if (e == ESP_OK)
    e = nvs_commit(h);
  nvs_close(h);
  return e == ESP_OK;
}
bool pearl_managed_load(pearl_library *l, const char *root,
                        pearl_add_track_fn add) {
  (void)root;
  activation record;

  if (!read_activation(&record))
    return true;

  const char *selected =
      catalog(record.active, false, NULL, NULL)     ? record.active
      : catalog(record.previous, false, NULL, NULL) ? record.previous
                                                    : NULL;

  return !selected || catalog(selected, false, l, add);
}

bool pearl_managed_candidate(pearl_library *l, const char *sha,
                             pearl_add_track_fn add) {
  return catalog(sha, false, l, add);
}
