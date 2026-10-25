#include "media.h"
#include "playlist.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static bool location(const pearl_library *l,const char *name,char out[PEARL_PATH]) {
 return l&&l->root&&snprintf(out,PEARL_PATH,"%s/.pearl/%s",l->root,name)<PEARL_PATH;
}
const media_item *pearl_media_item(const pearl_library *l,int track) {
 return l&&track>=0&&(unsigned)track<l->track_count?l->tracks[track].media:NULL;
}
uint32_t pearl_media_start(const pearl_library *l,int track) {
 const media_item *i=pearl_media_item(l,track);const media_representation *r=i?media_item_representation(i,"mp3"):NULL;
 return r&&l->tracks[track].media_segment<r->count?r->segments[l->tracks[track].media_segment].start_ms:0;
}
int pearl_media_locate(const pearl_library *l,int track,uint32_t position,uint32_t *offset) {
 const media_item *i=pearl_media_item(l,track);const media_representation *r=i?media_item_representation(i,"mp3"):NULL;
 if(!r){if(offset)*offset=position;return track;}
 unsigned part=media_timeline_locate(r->segments,r->count,i->duration_ms,position,offset);
 for(unsigned n=0;n<l->track_count;n++)if(l->tracks[n].media==i&&l->tracks[n].media_segment==part)return (int)n;
 return -1;
}
static bool progress_path(const pearl_library *l,const media_item *i,char out[PEARL_PATH]) {
 return l&&l->root&&i&&media_id_valid(i->id)&&snprintf(out,PEARL_PATH,"%s/.pearl/progress/%s.json",l->root,i->id)<PEARL_PATH;
}
bool pearl_media_progress_load(const pearl_library *l,int track,media_progress *out) {
 const media_item *i=pearl_media_item(l,track);char path[PEARL_PATH];
 return progress_path(l,i,path)&&media_progress_load(path,i->id,out);
}
bool pearl_media_progress_save(const pearl_library *l,int track,uint32_t position,bool completed) {
 const media_item *i=pearl_media_item(l,track);if(!i||!media_kind_resumes(i->kind))return true;
 char path[PEARL_PATH],dir[PEARL_PATH];if(!location(l,"progress",dir)||!progress_path(l,i,path))return false;
 if(mkdir(dir,0700)&&errno!=EEXIST)return false;
 media_progress p={.position_ms=position>i->duration_ms?i->duration_ms:position,.duration_ms=i->duration_ms,.completed=completed};
 return media_progress_save(path,i->id,&p);
}
static bool append_group(pearl_library *l,pearl_view kind,const char *title,const char *key,const media_item *item) {
 char tagged[80];snprintf(tagged,sizeof(tagged),"media:%s",key);
 int c=pearl_collection_add(l,kind,title,tagged);if(c<0)return false;
 for(unsigned t=0;t<l->track_count;t++)if(l->tracks[t].media==item&&(!media_kind_resumes(item->kind)||l->tracks[t].media_segment==0)&&!pearl_collection_append(l,c,t))return false;
 return true;
}
bool pearl_media_apply(pearl_library *l,const char *root) {
 if(!l||!root)return false;
 free(l->root);l->root=strdup(root);if(!l->root)return false;
 unsigned keep=0;
 for(unsigned c=0;c<l->collection_count;c++) {
  pearl_collection *v=&l->collections[c];
  if(v->kind>PEARL_PLAYLISTS||!strncmp(v->path,"media:",6)){free(v->title);free(v->path);free(v->tracks);continue;}
  if(c!=keep)l->collections[keep]=*v;
  keep++;
 }
 l->collection_count=keep;
 for(unsigned t=0;t<l->track_count;t++){l->tracks[t].media=NULL;l->tracks[t].media_owned=false;}
 media_library_free(l->media_catalog);l->media_catalog=NULL;
 char path[PEARL_PATH];if(!location(l,"media-library.json",path))return false;
 bool had_catalog=access(path,F_OK)==0;
 char *json=media_text_read(path,MEDIA_LIBRARY_JSON_BYTES);
 media_catalog *catalog=json?media_library_parse(json,strlen(json)):NULL;free(json);
 l->media_recovered=false;
 if(!catalog){l->media_recovered=had_catalog;if(!location(l,"media-library.json.prev",path))return false;had_catalog=had_catalog||access(path,F_OK)==0;json=media_text_read(path,MEDIA_LIBRARY_JSON_BYTES);catalog=json?media_library_parse(json,strlen(json)):NULL;free(json);}
 if(had_catalog&&!catalog)return false;
 l->media_catalog=catalog;
 /* Ownership is recorded durably before new objects appear. Incomplete/newer
  * objects and retained prior generations never leak into ordinary music. */
 if(location(l,"media-owned.txt",path)) {
  FILE *owned=fopen(path,"rb");char relative[256];
  if(owned){while(fgets(relative,sizeof(relative),owned)) {if(!strchr(relative,'\n'))continue;relative[strcspn(relative,"\r\n")]=0;char absolute[PEARL_PATH];if(media_path_valid(relative)&&media_path_join(root,relative,absolute,sizeof(absolute)))for(unsigned t=0;t<l->track_count;t++)if(!strcmp(l->tracks[t].path,absolute))l->tracks[t].media_owned=true;}fclose(owned);}
 }
 if(catalog) {
 for(unsigned n=0;n<catalog->item_count;n++) {
  const media_item *item=&catalog->items[n];const media_representation *r=media_item_representation(item,"mp3");if(!r)continue;
  for(unsigned part=0;part<r->count;part++) {
   char absolute[PEARL_PATH];if(!media_path_join(root,r->segments[part].path,absolute,sizeof(absolute)))return false;
   for(unsigned t=0;t<l->track_count;t++)if(!strcmp(l->tracks[t].path,absolute)) {l->tracks[t].media=item;l->tracks[t].media_segment=part;l->tracks[t].media_owned=true;if(part==0&&item->has_artwork){char artwork[PEARL_PATH];if(!media_path_join(root,item->artwork.path,artwork,sizeof(artwork)))return false;char *copy=strdup(artwork);if(!copy)return false;free(l->albums[l->tracks[t].album].art);l->albums[l->tracks[t].album].art=copy;}break;}
  }
  bool grouped=false;
  if(item->kind==MEDIA_PODCAST)for(unsigned c=0;c<catalog->collection_count;c++) {
   const media_collection *group=&catalog->collections[c];if(strcmp(group->kind,"show"))continue;
   for(unsigned n=0;n<group->count;n++)if(!strcmp(group->items[n],item->id)) {
    if(!append_group(l,PEARL_PODCASTS,group->title,group->id,item))return false;
    grouped=true;break;
   }
  }
  if(item->kind!=MEDIA_MUSIC&&!grouped&&!append_group(l,item->kind==MEDIA_PODCAST?PEARL_PODCASTS:PEARL_AUDIOBOOKS,item->title,item->id,item))return false;
 }
 for(unsigned c=0;c<catalog->collection_count;c++) {
  const media_collection *group=&catalog->collections[c];if(strcmp(group->kind,"playlist"))continue;
  for(unsigned n=0;n<group->count;n++){const media_item *item=media_library_find(catalog,group->items[n]);if(item&&item->kind==MEDIA_MUSIC&&!append_group(l,PEARL_PLAYLISTS,group->title,group->id,item))return false;}
 }
 }
 /* Music views retain their existing order, excluding speech-only items. */
 for(unsigned c=0;c<keep;c++) {pearl_collection *v=&l->collections[c];unsigned count=0;
  for(unsigned t=0;t<v->count;t++){const media_item *item=l->tracks[v->tracks[t]].media;if((!l->tracks[v->tracks[t]].media_owned&&!item)||(item&&item->kind==MEDIA_MUSIC))v->tracks[count++]=v->tracks[t];}v->count=count;
 }
 return !l->truncated;
}
