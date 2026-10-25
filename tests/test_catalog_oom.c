#undef malloc
#undef realloc
#include "player.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
static int fail_after=-1;
void *pearl_test_malloc(size_t n){if(fail_after==0)return NULL;if(fail_after>0)fail_after--;return malloc(n);}
void *pearl_test_realloc(void *p,size_t n){if(fail_after==0)return NULL;if(fail_after>0)fail_after--;return realloc(p,n);}
int main(void){char root[]="/tmp/pearl-oom-XXXXXX";assert(mkdtemp(root));char file[512];snprintf(file,sizeof(file),"%s/track.mp3",root);FILE *f=fopen(file,"wb");assert(f);fclose(f);char hidden[512];snprintf(hidden,sizeof(hidden),"%s/.pearl",root);assert(!mkdir(hidden,0700));snprintf(hidden,sizeof(hidden),"%s/.pearl/media-library.json",root);FILE *catalog=fopen(hidden,"wb");assert(catalog);fputs("{\"version\":1,\"revision\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"items\":[{\"id\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"kind\":\"audiobook\",\"source\":{\"type\":\"abs\",\"scope\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"id\":\"book\"},\"title\":\"Book\",\"artist\":\"Reader\",\"duration_ms\":1000,\"representations\":[{\"id\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"codec\":\"mp3\",\"segments\":[{\"path\":\"track.mp3\",\"title\":\"Book\",\"start_ms\":0,\"duration_ms\":1000,\"bytes\":1,\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}]}],\"chapters\":[]}],\"collections\":[]}",catalog);fclose(catalog);pearl_library old;assert(!pearl_library_scan(&old,root));unsigned failures=0;for(int i=0;i<60;i++){fail_after=i;pearl_library next;int rc=pearl_library_scan(&next,root);if(rc)failures++;else {assert(next.media_catalog&&next.collection_count>=1&&!next.truncated);pearl_library_free(&next);}assert(old.track_count==1&&!strcmp(old.tracks[0].path,file));}fail_after=-1;pearl_library_free(&old);assert(failures>10);remove(file);remove(hidden);snprintf(hidden,sizeof(hidden),"%s/.pearl",root);rmdir(hidden);rmdir(root);puts("Allocation failure at each catalog construction stage cleans up and leaves the previous snapshot intact.");}
