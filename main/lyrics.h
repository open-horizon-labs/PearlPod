#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
  uint32_t milliseconds;
  char *text;
} pearl_cue;
typedef struct {
  char *text;
  pearl_cue *cues;
  unsigned count;
  bool timed;
} pearl_lyrics;
bool pearl_lyrics_load(const char *path, pearl_lyrics *lyrics);
void pearl_lyrics_free(pearl_lyrics *lyrics);
int pearl_lyrics_at(const pearl_lyrics *lyrics, uint32_t milliseconds);
