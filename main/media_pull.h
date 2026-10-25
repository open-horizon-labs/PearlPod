#pragma once
#include <stdbool.h>
#include "sync_progress.h"
typedef enum {PEARL_PULL_OK,PEARL_PULL_UNSUPPORTED,PEARL_PULL_CANCELED,PEARL_PULL_FAILED} pearl_pull_result;
/* Audio must be stopped/released by the caller; never starts playback or WiFi. */
pearl_pull_result pearl_media_pull(const char *base,const char *root,
 bool (*cancelled)(void),void (*progress)(const pearl_transfer_info *),
 char *error,unsigned error_bytes);
