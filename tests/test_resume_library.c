#include "player.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
  assert(argc==2);pearl_library library;
  assert(pearl_library_scan(&library,argv[1])==0);
  bool found=false;
  for(unsigned i=0;i<library.collection_count;i++) {
    pearl_collection *c=&library.collections[i];
    if(c->kind==PEARL_PLAYLISTS&&!strcmp(c->title,"After school")) { assert(c->count==2);found=true; }
    assert(strcmp(c->title,"Weekend"));
  }
  assert(found);assert(library.track_count==1);
  pearl_library_free(&library);puts("Completed playlist discovered and playable without a full-sync marker.");
}
