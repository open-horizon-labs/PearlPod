#pragma once
#include "player.h"
/* Overlay typed catalog metadata after ordinary card/index loading. */
bool pearl_media_apply(pearl_library *library,const char *root);
const media_item *pearl_media_item(const pearl_library *library,int track);
uint32_t pearl_media_start(const pearl_library *library,int track);
int pearl_media_locate(const pearl_library *library,int track,uint32_t position,uint32_t *offset);
bool pearl_media_progress_load(const pearl_library *library,int track,media_progress *out);
bool pearl_media_progress_save(const pearl_library *library,int track,uint32_t position,bool completed);
