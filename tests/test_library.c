#include "player.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static void file(const char *path){FILE *f=fopen(path,"wb");assert(f);fclose(f);}
int main(void){
 char root[]="/tmp/pearl-tests-XXXXXX";assert(mkdtemp(root));char path[1024];
 snprintf(path,sizeof(path),"%s/Artist",root);mkdir(path,0700);
 snprintf(path,sizeof(path),"%s/Artist/First",root);mkdir(path,0700);
 snprintf(path,sizeof(path),"%s/Artist/Second",root);mkdir(path,0700);
 snprintf(path,sizeof(path),"%s/Artist/First/10 Finale.mp3",root);file(path);
 snprintf(path,sizeof(path),"%s/Artist/First/2 Opening.FLAC",root);file(path);
 snprintf(path,sizeof(path),"%s/Artist/Second/1 Solo.wav",root);file(path);
 snprintf(path,sizeof(path),"%s/Artist/First/notmusic.txt",root);file(path);
 snprintf(path,sizeof(path),"%s/Artist/First/cover.rgb",root);file(path); // malformed art must not be accepted
 pearl_library l;assert(pearl_library_scan(&l,root)==0);assert(l.album_count==2&&l.track_count==3);
 unsigned a=0;while(strcmp(l.albums[a].title,"First"))a++;
 assert(l.albums[a].count==2&&l.albums[a].art[0]==0);
 int first=l.albums[a].first;assert(!strcmp(l.tracks[first].title,"2 Opening"));
 assert(pearl_next(&l,first,-1)==first+1);assert(pearl_next(&l,first+1,1)==first);
 assert(pearl_volume(64,2,65)==65&&pearl_volume(1,-2,65)==0&&pearl_volume(0,2,65)==2);
 pearl_button b={0};assert(pearl_button_update(&b,true,100)==BUTTON_NONE);
 assert(pearl_button_update(&b,false,110)==BUTTON_NONE);assert(pearl_button_update(&b,false,150)==BUTTON_NONE); // contact bounce
 assert(pearl_button_update(&b,true,200)==BUTTON_NONE);assert(pearl_button_update(&b,true,240)==BUTTON_NONE);
 assert(pearl_button_update(&b,false,500)==BUTTON_NONE);assert(pearl_button_update(&b,false,540)==BUTTON_SHORT);
 assert(pearl_button_update(&b,true,600)==BUTTON_NONE);assert(pearl_button_update(&b,true,640)==BUTTON_NONE);
 assert(pearl_button_update(&b,true,2440)==BUTTON_LONG);assert(pearl_button_update(&b,true,3000)==BUTTON_NONE);
 assert(pearl_button_update(&b,false,3010)==BUTTON_NONE);assert(pearl_button_update(&b,false,3050)==BUTTON_NONE); // long hold never adjusts volume on release
 pearl_library_free(&l);assert(pearl_library_scan(&l,"/does-not-exist")==-2);
 printf("Nested albums, natural order, navigation, malformed artwork, volume and button semantics pass.\n");
}
