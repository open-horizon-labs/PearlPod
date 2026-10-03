#include "player.h"
#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

int pearl_volume(int current,int delta,int ceiling) { int n=current+delta;return n<0?0:n>ceiling?ceiling:n; }
pearl_button_event pearl_button_update(pearl_button *b,bool pressed,uint32_t now) {
    if(pressed!=b->raw){b->raw=pressed;b->changed=now;}
    if(now-b->changed>=35 && b->stable!=b->raw){
        b->stable=b->raw;
        if(b->stable){b->pressed=now;b->long_sent=false;}
        else if(!b->long_sent)return BUTTON_SHORT;
    }
    if(b->stable && !b->long_sent && now-b->pressed>=1800){b->long_sent=true;return BUTTON_LONG;}
    return BUTTON_NONE;
}
#include "metadata.h"
#include "managed.h"
#include "playlist.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
static void *catalog_malloc(size_t n){if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)<n+2*1024*1024)return NULL;return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
static void *catalog_realloc(void *p,size_t n){size_t old=p?heap_caps_get_allocated_size(p):0;if(n>old&&heap_caps_get_free_size(MALLOC_CAP_SPIRAM)<n-old+2*1024*1024)return NULL;return heap_caps_realloc(p,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
#define malloc(n) catalog_malloc(n)
#define realloc(p,n) catalog_realloc(p,n)
#endif
static bool grow(void **p,unsigned *capacity,unsigned needed,size_t size){if(needed<=*capacity)return true;unsigned n=*capacity?*capacity*2:16;if(n<needed)n=needed;if(n>SIZE_MAX/size)return false;void *next=realloc(*p,n*size);if(!next)return false;*p=next;*capacity=n;return true;}
static char *copy(pearl_library *l,const char *s){if(!s)s="";char *p=malloc(strlen(s)+1);if(p)memcpy(p,s,strlen(s)+1);if(!p)l->truncated=true;return p;}
static bool audio(const char *s){const char *e=strrchr(s,'.');return e&&(!strcasecmp(e,".mp3")||!strcasecmp(e,".flac")||!strcasecmp(e,".wav"));}
static int natural(const char *a,const char *b){while(*a&&*b){if(isdigit((unsigned char)*a)&&isdigit((unsigned char)*b)){const char *ae=a,*be=b;while(*ae=='0')ae++;while(*be=='0')be++;const char *ax=ae,*bx=be;while(isdigit((unsigned char)*ax))ax++;while(isdigit((unsigned char)*bx))bx++;size_t an=ax-ae,bn=bx-be;if(an!=bn)return an<bn?-1:1;int d=strncmp(ae,be,an);if(d)return d;a=ax;b=bx;}else{int d=tolower((unsigned char)*a)-tolower((unsigned char)*b);if(d)return d;a++;b++;}}return (unsigned char)*a-(unsigned char)*b;}
static void title_file(const char *path,char *out,size_t capacity){FILE *f=fopen(path,"rb");if(!f)return;char name[PEARL_NAME];if(fgets(name,sizeof(name),f)){name[strcspn(name,"\r\n")]=0;if(name[0])snprintf(out,capacity,"%s",name);}fclose(f);}
static char *cover(pearl_library *l,const char *dir){const char *names[]={"pearl-cover.rgb","cover.rgb","folder.rgb","cover.jpg","folder.jpg","cover.png","folder.png","Cover.jpg","Folder.jpg"};for(unsigned i=0;i<sizeof(names)/sizeof(*names);i++){char path[PEARL_PATH];if(snprintf(path,sizeof(path),"%s/%s",dir,names[i])>=(int)sizeof(path))continue;struct stat st;if(!stat(path,&st)&&((i<3&&st.st_size==PEARL_ART_SIZE*PEARL_ART_SIZE*2)||(i>=3&&st.st_size>0&&st.st_size<=400*1024)))return copy(l,path);}return copy(l,"");}
static void free_track(pearl_track *t){free(t->path);free(t->title);free(t->artist);free(t->lyrics);free(t->genre);}
static void add_track(pearl_library *l,const char *path,const char *dir,const char *filename){
 pearl_tags tags;pearl_metadata(path,&tags);char title[PEARL_NAME];snprintf(title,sizeof(title),"%.159s",filename);char *dot=strrchr(title,'.');if(dot)*dot=0;if(tags.title[0])snprintf(title,sizeof(title),"%s",tags.title);else{char side[PEARL_PATH];if(snprintf(side,sizeof(side),"%s.pearl-title",path)<(int)sizeof(side))title_file(side,title,sizeof(title));}
 pearl_track t={.path=copy(l,path),.title=copy(l,title),.artist=copy(l,tags.artist[0]?tags.artist:"Unknown artist"),.genre=copy(l,tags.genre),.track_number=tags.track,.disc_number=tags.disc};if(!t.path||!t.title||!t.artist||!t.genre){free_track(&t);return;}
 const char *group_artist=tags.album_artist[0]?tags.album_artist:t.artist;unsigned a=0;for(;a<l->album_count;a++){pearl_album *album=&l->albums[a];if(strstr(dir,"/.pearl/album_")?!strcmp(album->path,dir):tags.album[0]?(!strcasecmp(album->title,tags.album)&&!strcasecmp(album->artist,group_artist)):(!strcmp(album->path,dir)&&!album->artist[0]))break;}
 if(a==l->album_count){if(!grow((void**)&l->albums,&l->album_capacity,a+1,sizeof(*l->albums))){l->truncated=true;free_track(&t);return;}const char *base=strrchr(dir,'/');char album_title[PEARL_NAME];snprintf(album_title,sizeof(album_title),"%s",tags.album[0]?tags.album:base?base+1:dir);if(!tags.album[0]){char side[PEARL_PATH];if(snprintf(side,sizeof(side),"%s/.pearl-title",dir)<(int)sizeof(side))title_file(side,album_title,sizeof(album_title));}pearl_album album={.path=copy(l,dir),.title=copy(l,album_title),.artist=copy(l,tags.album[0]?group_artist:""),.art=cover(l,dir)};if(!album.path||!album.title||!album.artist||!album.art){free(album.path);free(album.title);free(album.artist);free(album.art);free_track(&t);return;}l->albums[l->album_count++]=album;}
 char side[PEARL_PATH];snprintf(side,sizeof(side),"%s",path);char *suffix=strrchr(side,'.');if(suffix){for(unsigned kind=0;kind<2;kind++){snprintf(suffix,sizeof(side)-(suffix-side),"%s",kind?".txt":".lrc");struct stat st;if(!stat(side,&st)&&st.st_size<=256*1024){t.lyrics=copy(l,side);break;}}}
 t.album=a;if(!grow((void**)&l->tracks,&l->track_capacity,l->track_count+1,sizeof(*l->tracks))){l->truncated=true;free_track(&t);return;}l->tracks[l->track_count++]=t;
}
int pearl_collection_add(pearl_library *l,pearl_view kind,const char *title,const char *path){for(unsigned i=0;i<l->collection_count;i++){pearl_collection *c=&l->collections[i];if(kind!=PEARL_ALBUMS&&c->kind==kind&&!strcmp(c->path,path)&&!strcasecmp(c->title,title))return i;}if(!grow((void**)&l->collections,&l->collection_capacity,l->collection_count+1,sizeof(*l->collections))){l->truncated=true;return -1;}pearl_collection c={.title=copy(l,title),.path=copy(l,path),.kind=kind};if(!c.title||!c.path){free(c.title);free(c.path);return -1;}l->collections[l->collection_count]=c;return l->collection_count++;}
bool pearl_collection_append(pearl_library *l,int index,unsigned track){if(index<0)return false;pearl_collection *c=&l->collections[index];if(!grow((void**)&c->tracks,&c->capacity,c->count+1,sizeof(*c->tracks))){l->truncated=true;return false;}c->tracks[c->count++]=track;return true;}
static void scan(pearl_library *l,const char *root){char **pending=NULL;unsigned capacity=0,count=0;if(!grow((void**)&pending,&capacity,1,sizeof(*pending))) {l->truncated=true;return;}pending[count++]=copy(l,root);
 while(count&&!l->truncated){char *dir=pending[--count];DIR *d=opendir(dir);if(!d){l->skipped++;free(dir);continue;}struct dirent *e;while(!l->truncated&&(e=readdir(d))){if(e->d_name[0]=='.')continue;char path[PEARL_PATH];if(snprintf(path,sizeof(path),"%s/%s",dir,e->d_name)>=(int)sizeof(path)){l->skipped++;continue;}struct stat st;
#ifdef ESP_PLATFORM
 if(stat(path,&st))continue;
#else
 if(lstat(path,&st)||S_ISLNK(st.st_mode))continue;
#endif
 if(S_ISDIR(st.st_mode)){if(!grow((void**)&pending,&capacity,count+1,sizeof(*pending))){l->truncated=true;break;}char *next=copy(l,path);if(next)pending[count++]=next;}
 else if(S_ISREG(st.st_mode)&&audio(path))add_track(l,path,dir,e->d_name);
 else if(S_ISREG(st.st_mode)){const char *ext=strrchr(path,'.');if(ext&&(!strcasecmp(ext,".m3u")||!strcasecmp(ext,".m3u8")||!strcasecmp(ext,".xspf"))){char title[PEARL_NAME];snprintf(title,sizeof(title),"%.159s",e->d_name);char *dot=strrchr(title,'.');if(dot)*dot=0;pearl_collection_add(l,PEARL_PLAYLISTS,title,path);}}}closedir(d);free(dir);}
 while(count)free(pending[--count]);
 free(pending);}
static int album_cmp(const void *av,const void *bv){const pearl_album *a=av,*b=bv;int n=natural(a->title,b->title);if(n)return n;n=natural(a->artist,b->artist);return n?n:natural(a->path,b->path);}
static int track_cmp(const void *av,const void *bv){const pearl_track *a=av,*b=bv;if(a->album!=b->album)return a->album<b->album?-1:1;unsigned ad=a->disc_number?a->disc_number:1,bd=b->disc_number?b->disc_number:1;if(ad!=bd)return ad<bd?-1:1;if(a->track_number!=b->track_number){if(!a->track_number)return 1;if(!b->track_number)return -1;return a->track_number<b->track_number?-1:1;}return natural(a->path,b->path);}
static int collection_cmp(const void *av,const void *bv){const pearl_collection *a=av,*b=bv;if(a->kind!=b->kind)return a->kind<b->kind?-1:1;int n=natural(a->title,b->title);return n?n:strcmp(a->path,b->path);}
static int library_scan(pearl_library *l,const char *root,const char *candidate){memset(l,0,sizeof(*l));DIR *d=opendir(root);if(!d)return -2;closedir(d);scan(l,root);
#if defined(ESP_PLATFORM) || defined(PEARL_MANAGED_HOST)
 if(candidate){if(!pearl_managed_candidate(l,candidate,add_track))l->truncated=true;}else
#else
 (void)candidate;
#endif
 if(!pearl_managed_load(l,root,add_track))l->truncated=true;
if(l->truncated){pearl_library_free(l);return -1;}unsigned *map=malloc((l->album_count?l->album_count:1)*sizeof(*map));if(!map){pearl_library_free(l);return -1;}for(unsigned i=0;i<l->album_count;i++)l->albums[i].first=i;if(l->album_count>1)qsort(l->albums,l->album_count,sizeof(*l->albums),album_cmp);for(unsigned i=0;i<l->album_count;i++){map[l->albums[i].first]=i;l->albums[i].first=0;}for(unsigned i=0;i<l->track_count;i++)l->tracks[i].album=map[l->tracks[i].album];free(map);if(l->track_count>1)qsort(l->tracks,l->track_count,sizeof(*l->tracks),track_cmp);
 for(unsigned i=0;i<l->track_count;i++){pearl_track *t=&l->tracks[i];pearl_album *a=&l->albums[t->album];if(!a->count)a->first=i;a->count++;char folder[PEARL_PATH];snprintf(folder,sizeof(folder),"%s",t->path);char *slash=strrchr(folder,'/');if(slash)*slash=0;const char *name=!strncmp(folder,root,strlen(root))?folder+strlen(root):folder;while(*name=='/')name++;if(!*name)name="Music root";if(strstr(t->path,"/.pearl/objects/")){snprintf(folder,sizeof(folder),"%s",a->path);name=a->title;}pearl_collection_append(l,pearl_collection_add(l,PEARL_FOLDERS,name,folder),i);pearl_collection_append(l,pearl_collection_add(l,PEARL_ARTISTS,t->artist,""),i);}
 for(unsigned a=0;a<l->album_count;a++){pearl_album *album=&l->albums[a];int c=pearl_collection_add(l,PEARL_ALBUMS,album->title,album->path);for(unsigned i=0;i<album->count;i++)pearl_collection_append(l,c,album->first+i);}
 for(unsigned i=0;i<l->collection_count;i++)if(l->collections[i].kind==PEARL_PLAYLISTS)pearl_playlist_read(l,i,root);
 if(l->truncated){pearl_library_free(l);return -1;}if(l->collection_count>1)qsort(l->collections,l->collection_count,sizeof(*l->collections),collection_cmp);return 0;}
void pearl_library_free(pearl_library *l){for(unsigned i=0;i<l->track_count;i++)free_track(&l->tracks[i]);for(unsigned i=0;i<l->album_count;i++){free(l->albums[i].path);free(l->albums[i].title);free(l->albums[i].artist);free(l->albums[i].art);}for(unsigned i=0;i<l->collection_count;i++){free(l->collections[i].title);free(l->collections[i].path);free(l->collections[i].tracks);}free(l->tracks);free(l->albums);free(l->collections);memset(l,0,sizeof(*l));}
int pearl_next(const pearl_library *l,int current,int direction){if(current<0||(unsigned)current>=l->track_count)return l->track_count?0:-1;const pearl_album *a=&l->albums[l->tracks[current].album];int relative=(current-(int)a->first+direction)%(int)a->count;if(relative<0)relative+=a->count;return a->first+relative;}
int pearl_collection_step(const pearl_library *l,int collection,int position,int direction){if(collection<0||(unsigned)collection>=l->collection_count)return -1;const pearl_collection *c=&l->collections[collection];if(!c->count)return -1;int relative=(position+direction)%(int)c->count;if(relative<0)relative+=c->count;return relative;}

int pearl_library_scan(pearl_library *l,const char *root){return library_scan(l,root,NULL);}
#if defined(ESP_PLATFORM) || defined(PEARL_MANAGED_HOST)
bool pearl_library_validate_sync(const char *sha){pearl_library l={0};
#ifdef PEARL_MANAGED_HOST
 const char *root="/tmp/pearl-managed-tests/music";
#else
 const char *root="/sdcard/music";
#endif
 int result=library_scan(&l,root,sha);pearl_library_free(&l);return result==0;}
#endif
