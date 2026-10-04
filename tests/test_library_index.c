#include "library_index.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static unsigned allocations,fail_after=UINT32_MAX;
void *pearl_index_alloc(size_t n){return allocations++>=fail_after?NULL:calloc(1,n);}
static void corrupt(const char *p,long offset,unsigned char byte){FILE *f=fopen(p,"r+b");assert(f);assert(!fseek(f,offset,SEEK_SET));assert(fputc(byte,f)!=EOF);fclose(f);}
int main(void){
 char root[]="/tmp/pearl-index-XXXXXX";assert(mkdtemp(root));char path[1024];
 snprintf(path,sizeof(path),"%s/song.mp3",root);FILE *f=fopen(path,"wb");assert(f);fclose(f);
 char lyric[1024];snprintf(lyric,sizeof(lyric),"%s/song.lrc",root);f=fopen(lyric,"wb");assert(f);fputs("[00:01]Hi",f);fclose(f);
 char playlist[1024];snprintf(playlist,sizeof(playlist),"%s/order.m3u8",root);f=fopen(playlist,"wb");assert(f);fputs("song.mp3\nsong.mp3\n",f);fclose(f);
 pearl_library a={0},b={0};assert(!pearl_library_scan(&a,root));assert(a.track_count==1);assert(a.tracks[0].lyrics);
 free(a.tracks[0].genre);a.tracks[0].genre=strdup("Pop / Anime");a.tracks[0].track_number=7;a.tracks[0].disc_number=2;
 assert(pearl_index_save(&a,root,"catalog-A"));assert(pearl_index_load(&b,root,"catalog-A"));
 assert(b.track_count==a.track_count&&b.album_count==a.album_count&&b.collection_count==a.collection_count);
 assert(!strcmp(b.tracks[0].title,a.tracks[0].title)&&!strcmp(b.tracks[0].artist,a.tracks[0].artist)&&!strcmp(b.tracks[0].genre,"Pop / Anime"));assert(b.tracks[0].track_number==7&&b.tracks[0].disc_number==2);assert(!strcmp(b.albums[0].art,a.albums[0].art));
 assert(!strcmp(b.tracks[0].path,a.tracks[0].path));assert(!strcmp(b.tracks[0].lyrics,a.tracks[0].lyrics));
 for(unsigned i=0;i<a.collection_count;i++){assert(b.collections[i].count==a.collections[i].count);assert(!strcmp(b.collections[i].title,a.collections[i].title));for(unsigned j=0;j<a.collections[i].count;j++)assert(b.collections[i].tracks[j]==a.collections[i].tracks[j]);if(b.collections[i].kind==PEARL_PLAYLISTS)assert(b.collections[i].count==2&&b.collections[i].tracks[0]==b.collections[i].tracks[1]);}
 pearl_library_free(&b);
 assert(!pearl_index_load(&b,root,"catalog-B"));assert(!b.tracks);
 /* No individual media reads/stats: removing media still loads the snapshot. */
 unlink(path);assert(pearl_index_load(&b,root,"catalog-A"));pearl_library_free(&b);
 snprintf(path,sizeof(path),"%s/.pearl/library.idx",root);
 corrupt(path,8,99);assert(!pearl_index_load(&b,root,"catalog-A"));assert(pearl_index_save(&a,root,"catalog-A"));
 corrupt(path,40,255);assert(!pearl_index_load(&b,root,"catalog-A"));assert(pearl_index_save(&a,root,"catalog-A"));
 FILE *raw=fopen(path,"rb");assert(raw);fseek(raw,0,SEEK_END);long length=ftell(raw);fclose(raw);
 /* Even a syntactically intact, correctly checksummed invalid reference fails. */
 raw=fopen(path,"r+b");assert(raw);unsigned char *blob=malloc(length);assert(blob);assert(fread(blob,1,length,raw)==(size_t)length);
 memset(blob+length-8,255,4);uint32_t hash=2166136261u;for(long i=0;i<length-4;i++){hash^=blob[i];hash*=16777619u;}for(int i=0;i<4;i++)blob[length-4+i]=hash>>(i*8);
 rewind(raw);assert(fwrite(blob,1,length,raw)==(size_t)length);fclose(raw);free(blob);assert(!pearl_index_load(&b,root,"catalog-A"));assert(pearl_index_save(&a,root,"catalog-A"));
 assert(!truncate(path,length-1));assert(!pearl_index_load(&b,root,"catalog-A"));assert(pearl_index_save(&a,root,"catalog-A"));
 bool recovered=false;for(unsigned n=0;n<128;n++){allocations=0;fail_after=n;if(pearl_index_load(&b,root,"catalog-A")){pearl_library_free(&b);recovered=true;break;}assert(!b.tracks&&!b.albums&&!b.collections);}assert(recovered);fail_after=UINT32_MAX;
 /* A half-written temporary never replaces the valid snapshot. */
 char tmp[1024];snprintf(tmp,sizeof(tmp),"%s/.pearl/library.tmp",root);f=fopen(tmp,"wb");assert(f);fputs("partial",f);fclose(f);assert(pearl_index_load(&b,root,"catalog-A"));pearl_library_free(&b);
 assert(pearl_index_invalidate(root));assert(!pearl_index_load(&b,root,"catalog-A"));assert(pearl_index_invalidate(root));
 pearl_library_free(&a);unlink(tmp);snprintf(tmp,sizeof(tmp),"%s/.pearl",root);rmdir(tmp);unlink(lyric);unlink(playlist);rmdir(root);
 puts("Index round-trip, playlist duplicates, lyrics, no media reads, revisions, corruption, truncation, OOM and interrupted temporary pass");
}
