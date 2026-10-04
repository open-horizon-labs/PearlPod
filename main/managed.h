#pragma once
#include "player.h"
typedef void (*pearl_add_track_fn)(pearl_library *, const char *, const char *,
                                   const char *);
#if defined(ESP_PLATFORM) || defined(PEARL_MANAGED_HOST)
bool pearl_managed_load(pearl_library *l, const char *root,
                        pearl_add_track_fn add);
bool pearl_managed_activate(const char *sha);
bool pearl_managed_is_active(const char *sha);
/* Conservative cleanup: only managed hash names outside active/previous catalogs. */
bool pearl_managed_collect(void);
bool pearl_managed_candidate(pearl_library *l, const char *sha,
                             pearl_add_track_fn add);
bool pearl_library_validate_sync(const char *sha);
#else
static inline bool pearl_managed_load(pearl_library *l, const char *root,
                                      pearl_add_track_fn add) {
  (void)l;
  (void)root;
  (void)add;
  return true;
}
#endif
