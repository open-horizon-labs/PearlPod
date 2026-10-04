#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  char playlist[65], song[65], album[65], artist[65], kind[16];
  uint64_t total_bytes, completed_bytes, file_bytes;
  unsigned songs_done, songs_total, playlist_index, playlist_count, playlists_ready;
} pearl_transfer_info;

typedef struct {
  pearl_transfer_info info;
  uint64_t received, sample_bytes, started_bytes;
  uint32_t sample_ms, last_data_ms, started_ms;
  double rate;
  bool known, sampled;
} pearl_transfer_progress;

typedef struct {
  char title[96], detail[96], context[160], count[80], timing[96];
  unsigned percent, playlists_ready;
  bool determinate, busy, complete;
} pearl_sync_view;

void pearl_progress_reset(pearl_transfer_progress *p);
bool pearl_progress_parse(const char *json, pearl_transfer_info *out);
bool pearl_progress_update(pearl_transfer_progress *p, const pearl_transfer_info *info, uint32_t now);
void pearl_progress_sample(pearl_transfer_progress *p, uint64_t file_bytes, uint32_t now);
void pearl_progress_view(const pearl_transfer_progress *p, uint32_t now, pearl_sync_view *out);
