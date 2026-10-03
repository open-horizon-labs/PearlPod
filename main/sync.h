#pragma once
#include <stdbool.h>
#include <stdio.h>
#ifdef PEARL_UI_HOST
static inline void pearl_sync_start(void) {}
static inline void pearl_sync_status(char *out, unsigned size) {
  snprintf(out, size, "Sync is off");
}
#else
void pearl_sync_start(void);
void pearl_sync_status(char *out, unsigned size);
bool pearl_sync_busy(void);
bool pearl_sync_shutdown(void);
bool pearl_sync_source(const char *url);
#endif
