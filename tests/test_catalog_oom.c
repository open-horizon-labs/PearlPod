#undef malloc
#undef realloc
#include "player.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static int fail_after=-1;
void *pearl_test_malloc(size_t n){if(fail_after==0)return NULL;if(fail_after>0)fail_after--;return malloc(n);}
void *pearl_test_realloc(void *p,size_t n){if(fail_after==0)return NULL;if(fail_after>0)fail_after--;return realloc(p,n);}
int main(void){char root[]="/tmp/pearl-oom-XXXXXX";assert(mkdtemp(root));char file[512];snprintf(file,sizeof(file),"%s/track.mp3",root);FILE *f=fopen(file,"wb");assert(f);fclose(f);pearl_library old;assert(!pearl_library_scan(&old,root));unsigned failures=0;for(int i=0;i<60;i++){fail_after=i;pearl_library next;int rc=pearl_library_scan(&next,root);if(rc)failures++;else pearl_library_free(&next);assert(old.track_count==1&&!strcmp(old.tracks[0].path,file));}fail_after=-1;pearl_library_free(&old);assert(failures>10);remove(file);rmdir(root);puts("Allocation failure at each catalog construction stage cleans up and leaves the previous snapshot intact.");}
