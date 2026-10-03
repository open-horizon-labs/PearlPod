#include "managed.h"
#include "nvs.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static unsigned char persisted[130],pending[130];static bool saved,fail;
int nvs_open(const char *s,int mode,nvs_handle_t *h){(void)s;(void)mode;*h=1;return 0;}
int nvs_get_blob(nvs_handle_t h,const char *k,void *p,size_t *n){(void)h;(void)k;if(!saved||*n!=sizeof(persisted))return 1;memcpy(p,persisted,*n);return 0;}
int nvs_set_blob(nvs_handle_t h,const char *k,const void *p,size_t n){(void)h;(void)k;if(n!=sizeof(pending))return 1;memcpy(pending,p,n);return 0;}
int nvs_commit(nvs_handle_t h){(void)h;if(fail)return 1;memcpy(persisted,pending,sizeof(persisted));saved=true;return 0;}
void nvs_close(nvs_handle_t h){(void)h;}
int main(int argc,char **argv){assert(argc==4);assert(pearl_managed_activate(argv[1]));char active[65];memcpy(active,persisted,65);fail=true;assert(!pearl_managed_activate(argv[2]));assert(!memcmp(active,persisted,65));fail=false;assert(!pearl_managed_activate(argv[3]));assert(!memcmp(active,persisted,65));assert(pearl_managed_activate(argv[2]));assert(!memcmp(persisted+65,active,65));pearl_library l={0};assert(!pearl_library_scan(&l,"/tmp/pearl-managed-tests/music"));assert(l.track_count==2&&l.album_count==1);unsigned playlists=0;for(unsigned i=0;i<l.collection_count;i++)if(l.collections[i].kind==PEARL_PLAYLISTS){playlists++;assert(l.collections[i].count==2);assert(strncmp(l.collections[i].title,"PP:",3));}assert(playlists==1);assert(l.tracks[0].lyrics&&l.tracks[1].lyrics);pearl_library_free(&l);puts("Activation commits once, preserves old generation on NVS failure and rejects corrupt input");}
