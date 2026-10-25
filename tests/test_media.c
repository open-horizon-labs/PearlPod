#define _POSIX_C_SOURCE 200809L
#include "media.h"
#include "playlist.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static void write_text(const char *path,const char *text){FILE*f=fopen(path,"wb");assert(f);assert(fputs(text,f)>=0);assert(!fclose(f));}
int main(void){char root[]="/tmp/pearl-media-test.XXXXXX",path[512];assert(mkdtemp(root));snprintf(path,sizeof(path),"%s/.pearl",root);assert(!mkdir(path,0700));
 const char *id="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
 const char *rev="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
 char json[8192];snprintf(json,sizeof(json),"{\"version\":1,\"revision\":\"%s\",\"items\":[{\"id\":\"%s\",\"kind\":\"audiobook\",\"source\":{\"type\":\"abs\",\"scope\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"id\":\"book\"},\"title\":\"A journey\",\"artist\":\"Reader\",\"duration_ms\":120000,\"representations\":[{\"id\":\"%s\",\"codec\":\"mp3\",\"segments\":[{\"path\":\"part1.mp3\",\"title\":\"First\",\"start_ms\":0,\"duration_ms\":60000,\"bytes\":1,\"sha256\":\"%s\"},{\"path\":\"part2.mp3\",\"title\":\"Second\",\"start_ms\":60000,\"duration_ms\":60000,\"bytes\":1,\"sha256\":\"%s\"}]}],\"chapters\":[{\"title\":\"Arrival\",\"start_ms\":0,\"end_ms\":70000},{\"title\":\"Home\",\"start_ms\":70000,\"end_ms\":120000}]}],\"collections\":[]}",rev,id,rev,rev,rev);
 snprintf(path,sizeof(path),"%s/.pearl/media-library.json",root);write_text(path,json);
 const char *files[]={"song.mp3","part1.mp3","part2.mp3","orphan.mp3"};for(unsigned n=0;n<4;n++){snprintf(path,sizeof(path),"%s/%s",root,files[n]);write_text(path,"x");}
 snprintf(path,sizeof(path),"%s/.pearl/media-owned.txt",root);write_text(path,"orphan.mp3\n");
 pearl_library lib={0};assert(!pearl_library_scan(&lib,root));assert(lib.track_count==4&&lib.media_catalog&&lib.media_catalog->item_count==1);int first=-1,second=-1,song=-1;unsigned books=0;
 for(unsigned n=0;n<lib.track_count;n++){if(strstr(lib.tracks[n].path,"part1"))first=n;if(strstr(lib.tracks[n].path,"part2"))second=n;if(strstr(lib.tracks[n].path,"song"))song=n;}
 assert(first>=0&&second>=0&&song>=0);assert(pearl_media_item(&lib,first)->kind==MEDIA_AUDIOBOOK);assert(!pearl_media_item(&lib,song));
 for(unsigned n=0;n<lib.collection_count;n++){pearl_collection*c=&lib.collections[n];if(c->kind==PEARL_AUDIOBOOKS){assert(c->count==1);books++;}else for(unsigned j=0;j<c->count;j++)assert(c->tracks[j]==(unsigned)song);}
 assert(books==1);for(unsigned n=0;n<lib.track_count;n++)if(strstr(lib.tracks[n].path,"orphan"))assert(!pearl_track_playable(&lib,n));assert(pearl_track_playable(&lib,pearl_next(&lib,-1,1)));assert(pearl_next(&lib,song,1)==song);assert(pearl_next(&lib,song,1)!=(int)(lib.track_count-1)||pearl_track_playable(&lib,lib.track_count-1));uint32_t offset;assert(pearl_media_locate(&lib,first,75000,&offset)==second&&offset==15000);assert(pearl_media_progress_save(&lib,second,75000,false));media_progress p;assert(pearl_media_progress_load(&lib,first,&p)&&p.position_ms==75000&&!p.completed);assert(pearl_media_progress_save(&lib,second,120000,true));assert(pearl_media_progress_load(&lib,first,&p)&&p.completed);
 pearl_library_free(&lib);assert(!pearl_library_scan(&lib,root));for(unsigned n=0;n<lib.track_count;n++)if(lib.tracks[n].media){assert(pearl_media_progress_load(&lib,n,&p)&&p.completed&&p.position_ms==120000);break;}
 pearl_library_free(&lib);snprintf(path,sizeof(path),"%s/.pearl/media-library.json.prev",root);write_text(path,json);snprintf(path,sizeof(path),"%s/.pearl/media-library.json",root);write_text(path,"broken");assert(!pearl_library_scan(&lib,root)&&lib.media_recovered&&lib.media_catalog);pearl_library_free(&lib);snprintf(path,sizeof(path),"%s/.pearl/media-library.json.prev",root);write_text(path,"broken");assert(pearl_library_scan(&lib,root)==-1&&!lib.tracks);snprintf(path,sizeof(path),"rm -rf %s",root);assert(!system(path));puts("Typed overlay: ordinary music, split-book timeline, chapter metadata, hidden orphan, durable per-item completion pass");return 0;}
