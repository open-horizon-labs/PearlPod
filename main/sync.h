#pragma once
#include <stdbool.h>
#include <stdio.h>
#ifdef PEARL_UI_HOST
static inline void pearl_sync_start(void) {}
static bool host_sync_busy;
static const char *host_sync_message;
static inline bool pearl_sync_busy(void) { return host_sync_busy; }
static inline void pearl_sync_cancel(void) {}
static inline void pearl_sync_status(char *out, unsigned size) {
  snprintf(out, size, "%s",host_sync_message?host_sync_message:"Ready to sync\n\nAdd PP: playlists in Plex. Keep the library computer running. Pause music before starting.");
}
#else
void pearl_sync_start(void);
void pearl_sync_status(char *out, unsigned size);
bool pearl_sync_busy(void);
bool pearl_sync_shutdown(void);
void pearl_sync_cancel(void);
void pearl_sync_trace(char *out,unsigned size);
bool pearl_sync_source(const char *url);
#endif
