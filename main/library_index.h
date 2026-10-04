#pragma once
#include "player.h"
/* Versioned, checksummed snapshot. Never stats or opens individual music files. */
bool pearl_index_load(pearl_library *out,const char *root,const char *revision);
bool pearl_index_save(const pearl_library *lib,const char *root,const char *revision);
bool pearl_index_invalidate(const char *root);
