#pragma once
#include "player.h"
int pearl_collection_add(pearl_library *lib,pearl_view kind,const char *title,const char *path);
bool pearl_collection_append(pearl_library *lib,int collection,unsigned track);
void pearl_playlist_read(pearl_library *lib,unsigned collection,const char *root);
