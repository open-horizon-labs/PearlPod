#include "player.h"
#include "metadata.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static void empty(const char *p){FILE *f=fopen(p,"wb");assert(f);fclose(f);}
static void frame(FILE *f,const char *key,const char *text){size_t n=strlen(text)+1;unsigned char h[10]={0};memcpy(h,key,4);h[4]=n>>24;h[5]=n>>16;h[6]=n>>8;h[7]=n;fwrite(h,1,10,f);fputc(3,f);fwrite(text,1,n-1,f);}
static void tag(const char *path,const char *title,const char *artist,const char *album,const char *track,const char *disc,const char *album_artist){FILE *f=fopen(path,"wb+");assert(f);unsigned char h[10]={'I','D','3',3};fwrite(h,1,10,f);frame(f,"TIT2",title);frame(f,"TPE1",artist);frame(f,"TALB",album);frame(f,"TRCK",track);frame(f,"TPOS",disc);frame(f,"TPE2",album_artist);long n=ftell(f)-10;h[6]=(n>>21)&127;h[7]=(n>>14)&127;h[8]=(n>>7)&127;h[9]=n&127;rewind(f);fwrite(h,1,10,f);fclose(f);}
static unsigned find(const pearl_library *l,const char *name,pearl_view view){for(unsigned i=0;i<l->collection_count;i++)if(l->collections[i].kind==view&&!strcmp(l->collections[i].title,name))return i;assert(!"collection missing");return 0;}
int main(void){char root[]="/tmp/pearl-catalog-XXXXXX";assert(mkdtemp(root));char path[1024],folder[1024];
 for(unsigned i=0;i<300;i++){snprintf(folder,sizeof(folder),"%s/folder%03u",root,i);assert(!mkdir(folder,0700));snprintf(path,sizeof(path),"%s/song.mp3",folder);empty(path);}
 snprintf(folder,sizeof(folder),"%s/Bulk",root);assert(!mkdir(folder,0700));for(unsigned i=0;i<300;i++){snprintf(path,sizeof(path),"%s/%u.wav",folder,i);empty(path);}
 snprintf(folder,sizeof(folder),"%s",root);for(unsigned i=0;i<24;i++){strcat(folder,"/d");assert(!mkdir(folder,0700));}snprintf(path,sizeof(path),"%s/deep.flac",folder);empty(path);
 snprintf(path,sizeof(path),"%s/00 finale.mp3",root);tag(path,"Finale","Listener","Hero mix","10/12","1","Listener");snprintf(path,sizeof(path),"%s/z opening.mp3",root);tag(path,"Opening","Listener","Hero mix","1","1","Listener");snprintf(path,sizeof(path),"%s/a disc two.mp3",root);tag(path,"Second disc","Listener","Hero mix","1","2","Listener");
 snprintf(path,sizeof(path),"%s/other.mp3",root);tag(path,"Another artist","Other","Hero mix","1","1","Other");snprintf(path,sizeof(path),"%s/mixed-a.mp3",root);tag(path,"A","Artist A","Compilation","1","1","Various Artists");snprintf(path,sizeof(path),"%s/mixed-b.mp3",root);tag(path,"B","Artist B","Compilation","2","1","Various Artists");
 snprintf(path,sizeof(path),"%s/favorites.m3u8",root);FILE *f=fopen(path,"w");assert(f);fputs("\xef\xbb\xbf#EXTM3U\n00 finale.mp3\nz opening.mp3\n00 finale.mp3\n../../escape.mp3\nmissing.mp3\n",f);fclose(f);
 snprintf(path,sizeof(path),"%s/ordered.xspf",root);f=fopen(path,"w");assert(f);fputs("<playlist version=\"1\" xmlns=\"http://xspf.org/ns/0/\"><trackList><x:track><x:location>z&#x20;opening.mp3</x:location></x:track><track><location>not-there.mp3</location><location>00%20finale.mp3</location></track></trackList></playlist>",f);fclose(f);
 snprintf(path,sizeof(path),"%s/cycle",root);assert(!symlink(root,path));pearl_library l;assert(!pearl_library_scan(&l,root));assert(l.track_count==607&&l.album_count==305&&!l.truncated);
 unsigned hero=find(&l,"Hero mix",PEARL_ALBUMS);pearl_collection *c=&l.collections[hero];if(c->count!=3){hero++;c=&l.collections[hero];}assert(c->count==3);assert(!strcmp(l.tracks[c->tracks[0]].title,"Opening"));assert(!strcmp(l.tracks[c->tracks[1]].title,"Finale"));assert(!strcmp(l.tracks[c->tracks[2]].title,"Second disc"));
 assert(l.collections[find(&l,"Bulk",PEARL_FOLDERS)].count==300);assert(l.collections[find(&l,"Compilation",PEARL_ALBUMS)].count==2);assert(l.collections[find(&l,"Listener",PEARL_ARTISTS)].count==3);
 c=&l.collections[find(&l,"favorites",PEARL_PLAYLISTS)];assert(c->count==3);assert(!strcmp(l.tracks[c->tracks[0]].title,"Finale"));assert(!strcmp(l.tracks[c->tracks[1]].title,"Opening"));assert(c->tracks[0]==c->tracks[2]);assert(pearl_collection_step(&l,find(&l,"favorites",PEARL_PLAYLISTS),0,-1)==2);
 c=&l.collections[find(&l,"ordered",PEARL_PLAYLISTS)];assert(c->count==2);assert(!strcmp(l.tracks[c->tracks[0]].title,"Opening"));assert(l.skipped>=3);
 pearl_library next;snprintf(path,sizeof(path),"%s/zz newest.mp3",root);tag(path,"New track","Listener","AAA new album","1","1","Listener");assert(!pearl_library_scan(&next,root));assert(next.track_count==608);assert(!strcmp(next.albums[0].title,"AAA new album"));assert(l.track_count==607);pearl_library_free(&next);pearl_library_free(&l);
 puts("607 tracks, 305 albums, 300-track folder, 24 levels, tags/disc order, artist grouping, ordered playlists, unsafe paths, symlink cycle and sorted fresh snapshot pass.");}
