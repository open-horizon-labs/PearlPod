#include "library_index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
static void *allocate(size_t n){
 if(n>SIZE_MAX-2*1024*1024||heap_caps_get_free_size(MALLOC_CAP_SPIRAM)<n+2*1024*1024)return NULL;
 return heap_caps_calloc(1,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
}
#else
#ifdef PEARL_INDEX_TEST
extern void *pearl_index_alloc(size_t n);
#define allocate pearl_index_alloc
#else
static void *allocate(size_t n){return calloc(1,n);}
#endif
#endif
#define INDEX_LIMIT (16u*1024*1024)
#define INDEX_VERSION 1u
static const unsigned char magic[8]={'P','P','I','N','D','E','X',0};
typedef struct {FILE *f;uint32_t hash;size_t bytes,limit;bool ok;} stream;
static void hash_bytes(stream *s,const void *data,size_t n){const unsigned char *b=data;for(size_t i=0;i<n;i++){s->hash^=b[i];s->hash*=16777619u;}s->bytes+=n;}
static void put(stream *s,const void *data,size_t n){if(!s->ok)return;if(n>s->limit-s->bytes||fwrite(data,1,n,s->f)!=n){s->ok=false;return;}hash_bytes(s,data,n);}
static void get(stream *s,void *data,size_t n){if(!s->ok)return;if(n>s->limit-s->bytes||fread(data,1,n,s->f)!=n){s->ok=false;return;}hash_bytes(s,data,n);}
static void put_u32(stream *s,uint32_t n){unsigned char b[4]={n,n>>8,n>>16,n>>24};put(s,b,4);}
static uint32_t get_u32(stream *s){unsigned char b[4]={0};get(s,b,4);return (uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);}
static void put_string(stream *s,const char *p){if(!p){put_u32(s,UINT32_MAX);return;}size_t n=strlen(p);if(n>=PEARL_PATH){s->ok=false;return;}put_u32(s,n);put(s,p,n);}
static char *get_string(stream *s){uint32_t n=get_u32(s);if(!s->ok||n==UINT32_MAX)return NULL;if(n>=PEARL_PATH||n>s->limit-s->bytes){s->ok=false;return NULL;}char *p=allocate(n+1);if(!p){s->ok=false;return NULL;}get(s,p,n);if(!s->ok||memchr(p,0,n)){free(p);s->ok=false;return NULL;}p[n]=0;return p;}
static bool path(char *out,const char *root,const char *name){return snprintf(out,PEARL_PATH,"%s/.pearl/%s",root,name)<PEARL_PATH;}
bool pearl_index_invalidate(const char *root){char p[PEARL_PATH];return path(p,root,"library.idx")&&(!unlink(p)||errno==ENOENT);}
static bool valid(const pearl_library *l){
 if(l->truncated)return false;
 for(unsigned i=0;i<l->track_count;i++){const pearl_track *t=&l->tracks[i];if(!t->path||!t->title||!t->artist||t->album>=l->album_count)return false;const pearl_album *a=&l->albums[t->album];if(i<a->first||i-a->first>=a->count)return false;}
 for(unsigned i=0;i<l->album_count;i++){const pearl_album *a=&l->albums[i];if(!a->path||!a->title||!a->artist||!a->art||a->first>l->track_count||a->count>l->track_count-a->first)return false;for(unsigned j=0;j<a->count;j++)if(l->tracks[a->first+j].album!=i)return false;}
 for(unsigned i=0;i<l->collection_count;i++){const pearl_collection *c=&l->collections[i];if(!c->title||!c->path||(unsigned)c->kind>PEARL_PLAYLISTS||(c->count&&!c->tracks))return false;for(unsigned j=0;j<c->count;j++)if(c->tracks[j]>=l->track_count)return false;}
 return true;
}
bool pearl_index_save(const pearl_library *l,const char *root,const char *revision){
 if(!valid(l))return false;
 char tmp[PEARL_PATH],final[PEARL_PATH],dir[PEARL_PATH];
 if(!path(tmp,root,"library.tmp")||!path(final,root,"library.idx")||snprintf(dir,sizeof(dir),"%s/.pearl",root)>=(int)sizeof(dir))return false;
 if(mkdir(dir,0700)&&errno!=EEXIST)return false;
 FILE *f=fopen(tmp,"wb");if(!f)return false;
 stream s={.f=f,.hash=2166136261u,.limit=INDEX_LIMIT,.ok=true};
 put(&s,magic,sizeof(magic));put_u32(&s,INDEX_VERSION);put_string(&s,root);put_string(&s,revision?revision:"");
 put_u32(&s,l->track_count);put_u32(&s,l->album_count);put_u32(&s,l->collection_count);put_u32(&s,l->skipped);
 for(unsigned i=0;i<l->track_count&&s.ok;i++){const pearl_track *t=&l->tracks[i];put_string(&s,t->path);put_string(&s,t->title);put_string(&s,t->artist);put_string(&s,t->lyrics);put_string(&s,t->genre);put_u32(&s,t->album);put_u32(&s,t->track_number);put_u32(&s,t->disc_number);}
 for(unsigned i=0;i<l->album_count&&s.ok;i++){const pearl_album *a=&l->albums[i];put_string(&s,a->path);put_string(&s,a->title);put_string(&s,a->artist);put_string(&s,a->art);put_u32(&s,a->first);put_u32(&s,a->count);}
 for(unsigned i=0;i<l->collection_count&&s.ok;i++){const pearl_collection *c=&l->collections[i];put_string(&s,c->title);put_string(&s,c->path);put_u32(&s,c->kind);put_u32(&s,c->count);for(unsigned j=0;j<c->count&&s.ok;j++)put_u32(&s,c->tracks[j]);}
 uint32_t checksum=s.hash;put_u32(&s,checksum);bool ok=s.ok;
 if(fflush(f)||fsync(fileno(f)))ok=false;
 if(fclose(f))ok=false;
 /* FAT cannot replace an existing name. An interruption here leaves no index,
  * causing a rebuild, never a partial snapshot. Music is untouched. */
 if(ok&&unlink(final)&&errno!=ENOENT)ok=false;
 if(ok&&rename(tmp,final))ok=false;
 if(!ok)unlink(tmp);
 return ok;
}
bool pearl_index_load(pearl_library *out,const char *root,const char *revision){
 char p[PEARL_PATH];struct stat st;if(!path(p,root,"library.idx")||stat(p,&st)||!S_ISREG(st.st_mode)||st.st_size<36||st.st_size>INDEX_LIMIT)return false;
 FILE *f=fopen(p,"rb");if(!f)return false;stream s={.f=f,.hash=2166136261u,.limit=(size_t)st.st_size-4,.ok=true};pearl_library l={0};
 unsigned char got[8];get(&s,got,8);if(!s.ok||memcmp(got,magic,8)||get_u32(&s)!=INDEX_VERSION)goto fail;
 char *saved_root=get_string(&s),*saved_revision=get_string(&s);bool identity=s.ok&&saved_root&&saved_revision&&!strcmp(saved_root,root)&&!strcmp(saved_revision,revision?revision:"");free(saved_root);free(saved_revision);if(!identity)goto fail;
 unsigned tracks=get_u32(&s),albums=get_u32(&s),collections=get_u32(&s);l.skipped=get_u32(&s);
 if(!s.ok||tracks>s.limit/32||albums>s.limit/24||collections>s.limit/16)goto fail;
 if(tracks){l.tracks=allocate((size_t)tracks*sizeof(*l.tracks));if(!l.tracks)goto fail;}l.track_capacity=tracks;
 if(albums){l.albums=allocate((size_t)albums*sizeof(*l.albums));if(!l.albums)goto fail;}l.album_capacity=albums;
 if(collections){l.collections=allocate((size_t)collections*sizeof(*l.collections));if(!l.collections)goto fail;}l.collection_capacity=collections;
 for(unsigned i=0;i<tracks&&s.ok;i++){pearl_track *t=&l.tracks[i];l.track_count=i+1;t->path=get_string(&s);t->title=get_string(&s);t->artist=get_string(&s);t->lyrics=get_string(&s);t->genre=get_string(&s);t->album=get_u32(&s);t->track_number=get_u32(&s);t->disc_number=get_u32(&s);}
 for(unsigned i=0;i<albums&&s.ok;i++){pearl_album *a=&l.albums[i];l.album_count=i+1;a->path=get_string(&s);a->title=get_string(&s);a->artist=get_string(&s);a->art=get_string(&s);a->first=get_u32(&s);a->count=get_u32(&s);}
 for(unsigned i=0;i<collections&&s.ok;i++){pearl_collection *c=&l.collections[i];l.collection_count=i+1;c->title=get_string(&s);c->path=get_string(&s);c->kind=get_u32(&s);unsigned n=get_u32(&s);if(n>(s.limit-s.bytes)/4){s.ok=false;break;}if(n){c->tracks=allocate((size_t)n*sizeof(*c->tracks));if(!c->tracks){s.ok=false;break;}}c->count=c->capacity=n;for(unsigned j=0;j<n&&s.ok;j++)c->tracks[j]=get_u32(&s);}
 if(!s.ok||s.bytes!=s.limit)goto fail;
 unsigned char end[4];if(fread(end,1,4,f)!=4)goto fail;uint32_t checksum=(uint32_t)end[0]|((uint32_t)end[1]<<8)|((uint32_t)end[2]<<16)|((uint32_t)end[3]<<24);
 if(checksum!=s.hash||!valid(&l))goto fail;
 fclose(f);*out=l;return true;
fail:fclose(f);pearl_library_free(&l);return false;
}
