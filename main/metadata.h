#pragma once
#include "player.h"
#include <stdio.h>
typedef struct {char title[PEARL_NAME],artist[PEARL_NAME],album[PEARL_NAME],album_artist[PEARL_NAME];unsigned track,disc;} pearl_tags;
void pearl_metadata(const char *path,pearl_tags *tags);
/* Offset of native audio after a bounded ID3v2 prefix, zero for untagged files. */
long pearl_audio_offset(FILE *file);
