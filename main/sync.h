#pragma once
#include <stdbool.h>
#include <stdio.h>
#include "sync_progress.h"
#ifdef PEARL_UI_HOST
static inline void pearl_sync_start(void) {}
static bool host_sync_busy;
static const char *host_sync_message;
static pearl_sync_view host_sync_view;
static inline pearl_sync_view pearl_sync_snapshot(void) { return host_sync_view; }
static inline bool pearl_sync_busy(void) { return host_sync_busy; }
static inline void pearl_sync_cancel(void) {}
static inline void pearl_sync_status(char *out, unsigned size) {
  snprintf(out, size, "%s",host_sync_message?host_sync_message:"Ready to sync\n\nAdd PP: playlists in Plex. Keep the library computer running. Pause music before starting.");
}
#else
void pearl_sync_start(void);
void pearl_sync_status(char *out, unsigned size);
pearl_sync_view pearl_sync_snapshot(void);
bool pearl_sync_busy(void);
bool pearl_sync_shutdown(void);
void pearl_sync_cancel(void);
void pearl_sync_trace(char *out,unsigned size);
bool pearl_sync_probe_start(const char *url);
bool pearl_sync_source(const char *url);
#endif
