#include "media_pull.h"
#include "player.h"
#include "media_library.h"
#include "media_transfer.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#define PULL_PATH 512u
#define PULL_URL 1200u
/* File names remain ordinary readable paths; all bookkeeping is hidden. */
typedef struct {
 esp_http_client_handle_t client;
 char url[PULL_URL],range[96],etag[128],expected[65];
 const char *root;
 bool owned_path;
 bool (*cancelled)(void);
 void (*report)(const pearl_transfer_info *);
 pearl_transfer_info info;
} transfer;
static bool canceled(void *arg){transfer *t=arg;return t->cancelled&&t->cancelled();}
static bool parents(const char *path){char copy[PULL_PATH];if(strlen(path)>=sizeof(copy))return false;strcpy(copy,path);for(char *p=copy+1;*p;p++)if(*p=='/'){*p=0;if(mkdir(copy,0700)&&errno!=EEXIST)return false;*p='/';}return true;}
static bool join(const char *a,const char *b,char *out,size_t size){return snprintf(out,size,"%s/%s",a,b)<(int)size;}
static bool encode(const char *path,char *out,size_t size){static const char hex[]="0123456789ABCDEF";size_t used=0;for(const unsigned char *p=(const unsigned char *)path;*p;p++){bool plain=isalnum(*p)||strchr("-._~/",*p);if(used+(plain?1:3)>=size)return false;if(plain)out[used++]=*p;else{out[used++]='%';out[used++]=hex[*p>>4];out[used++]=hex[*p&15];}}out[used]=0;return true;}
static esp_err_t event(esp_http_client_event_t *e){transfer *t=e->user_data;if(e->event_id==HTTP_EVENT_ON_HEADER&&e->header_key&&e->header_value){if(!strcasecmp(e->header_key,"Content-Range"))snprintf(t->range,sizeof(t->range),"%s",e->header_value);if(!strcasecmp(e->header_key,"ETag"))snprintf(t->etag,sizeof(t->etag),"%s",e->header_value);}return ESP_OK;}
static bool open_http(void *arg,uint32_t offset,const char *if_range,media_transfer_response *out){
 transfer *t=arg;t->range[0]=t->etag[0]=0;
 esp_http_client_config_t cfg={.url=t->url,.timeout_ms=5000,.buffer_size=4096,.buffer_size_tx=1024,.disable_auto_redirect=true,.event_handler=event,.user_data=t};
 t->client=esp_http_client_init(&cfg);if(!t->client)return false;
 esp_http_client_set_header(t->client,"Accept-Encoding","identity");
 if(offset){char range[48];snprintf(range,sizeof(range),"bytes=%lu-",(unsigned long)offset);esp_http_client_set_header(t->client,"Range",range);esp_http_client_set_header(t->client,"If-Range",if_range);}
 if(esp_http_client_open(t->client,0)!=ESP_OK){esp_http_client_cleanup(t->client);t->client=NULL;return false;}
 int64_t length=esp_http_client_fetch_headers(t->client);
 *out=(media_transfer_response){.status=esp_http_client_get_status_code(t->client),.length=length,.content_range=t->range,.etag=t->etag};return true;
}
static int read_http(void *arg,void *buffer,unsigned size){transfer *t=arg;return esp_http_client_read(t->client,buffer,size);}
static bool complete_http(void *arg){transfer *t=arg;return esp_http_client_is_complete_data_received(t->client);}
static void close_http(void *arg){transfer *t=arg;if(t->client){esp_http_client_close(t->client);esp_http_client_cleanup(t->client);t->client=NULL;}}
static void yield_http(void *arg){(void)arg;vTaskDelay(1);}
static bool verify(void *arg,const char *path){
 transfer *t=arg;FILE *f=fopen(path,"rb");if(!f)return false;
 unsigned char *buffer=malloc(8192),digest[32];mbedtls_sha256_context hash;mbedtls_sha256_init(&hash);
 bool ok=buffer&&mbedtls_sha256_starts(&hash,0)==0;size_t n;
 while(ok&&(n=fread(buffer,1,8192,f))){if(canceled(t)||mbedtls_sha256_update(&hash,buffer,n))ok=false;else vTaskDelay(1);}
 if(ferror(f))ok=false;
 if(ok&&mbedtls_sha256_finish(&hash,digest))ok=false;
 fclose(f);free(buffer);mbedtls_sha256_free(&hash);
 if(!ok)return false;
 char actual[65];for(unsigned i=0;i<32;i++)snprintf(actual+i*2,3,"%02x",digest[i]);
 return !strcmp(actual,t->expected);
}
/* Sync releases the decoder before entry. Only recorded HTTP objects may be
 * repaired; arbitrary card files keep their original bytes. */
static bool replaceable(void *arg,const char *path){transfer *t=arg;if(pearl_audio_state().ready)return false;struct stat st;return (stat(path,&st)!=0&&errno==ENOENT)||t->owned_path;}
static void transferred(void *arg,const download_writer_stats *stats){transfer *t=arg;t->info.file_bytes=stats->received;if(t->report)t->report(&t->info);}
static bool ownership(const char *root,const char *path,bool record){
 char registry[PULL_PATH];if(!join(root,".pearl/media-owned.txt",registry,sizeof(registry))||!parents(registry))return false;
 FILE *f=fopen(registry,"rb");char line[256];bool exists=false;
 if(f){while(fgets(line,sizeof(line),f)){if(!strchr(line,'\n'))continue;line[strcspn(line,"\r\n")]=0;if(!strcmp(line,path)){exists=true;break;}}if(ferror(f)){fclose(f);return false;}fclose(f);}
 if(exists)return true;
 if(!record)return false;
 f=fopen(registry,"ab");if(!f)return false;
 bool ok=fprintf(f,"\n%s\n",path)>0;if(fflush(f)||fsync(fileno(f)))ok=false;if(fclose(f))ok=false;return ok;
}
static bool object(transfer *t,const char *base,const char *revision,const char *path,const char *sha,uint32_t bytes,void *buffer){
 char encoded[800],final[PULL_PATH],partial[PULL_PATH],marker[PULL_PATH],previous[PULL_PATH],etag[68];
 if(!media_path_valid(path)||!encode(path,encoded,sizeof(encoded))||snprintf(t->url,sizeof(t->url),"%s/media-library/v1/objects/%s/%s",base,revision,encoded)>=(int)sizeof(t->url)||!media_path_join(t->root,path,final,sizeof(final))||snprintf(partial,sizeof(partial),"%s/.pearl/downloads/%s.part",t->root,sha)>=(int)sizeof(partial)||snprintf(marker,sizeof(marker),"%s.done",partial)>=(int)sizeof(marker)||snprintf(previous,sizeof(previous),"%s.prev",partial)>=(int)sizeof(previous)||!parents(final)||!parents(partial))return false;
 t->owned_path=ownership(t->root,path,false);struct stat existing;bool present=!stat(final,&existing);
 if(!present&&!ownership(t->root,path,true))return false;
 snprintf(etag,sizeof(etag),"\"%s\"",sha);snprintf(t->expected,sizeof(t->expected),"%s",sha);
 media_transfer_request request={.final_path=final,.partial_path=partial,.checkpoint_path=marker,.previous_path=previous,.etag=etag,.bytes=bytes,.queue_capacity=65536,.buffer=buffer,.buffer_bytes=16384};
 media_transfer_io io={.context=t,.open=open_http,.read=read_http,.complete=complete_http,.close=close_http,.cancelled=canceled,.yield=yield_http,.progress=transferred,.verify=verify,.can_replace=replaceable};
 media_transfer_result result=media_transfer_run(&request,&io);
 if(result!=MEDIA_TRANSFER_OK)return false;
 if(!ownership(t->root,path,true))return false;
 t->info.completed_bytes+=bytes;t->info.file_bytes=0;return true;
}
pearl_pull_result pearl_media_pull(const char *base,const char *root,bool (*cancelled)(void),void (*progress)(const pearl_transfer_info *),char *error,unsigned error_bytes){
 transfer t={.root=root,.cancelled=cancelled,.report=progress};media_transfer_response response;media_catalog *catalog=NULL;char *json=NULL;void *buffer=NULL;pearl_pull_result result=PEARL_PULL_FAILED;
 if(error_bytes)error[0]=0;
 if(snprintf(t.url,sizeof(t.url),"%s/media-library/v1/catalog?device=pearlpod",base)>=(int)sizeof(t.url))goto done;
 if(!open_http(&t,0,NULL,&response))goto done;
 if(response.status==404){result=PEARL_PULL_UNSUPPORTED;goto done;}
 if(response.status!=200){if(error_bytes)snprintf(error,error_bytes,response.status==503?"Library preparation pending. Try sync again shortly.":"Library server returned HTTP %d",response.status);goto done;}
 if(response.length>MEDIA_LIBRARY_JSON_BYTES||response.length==0)goto done;
 json=malloc(MEDIA_LIBRARY_JSON_BYTES+1);if(!json)goto done;
 size_t used=0;while(used<MEDIA_LIBRARY_JSON_BYTES&&!canceled(&t)){int n=read_http(&t,json+used,MEDIA_LIBRARY_JSON_BYTES-used);if(n<0)goto done;if(!n)break;used+=(unsigned)n;vTaskDelay(1);}
 if(canceled(&t)){result=PEARL_PULL_CANCELED;goto done;}
 if(!complete_http(&t)||!used)goto done;
 json[used]=0;close_http(&t);
 catalog=media_library_parse(json,used);if(!catalog){if(error_bytes)snprintf(error,error_bytes,"Invalid library catalog. Existing music is safe.");goto done;}
 buffer=malloc(16384);if(!buffer)goto done;
 for(unsigned n=0;n<catalog->item_count;n++){const media_item *item=&catalog->items[n];const media_representation *r=media_item_representation(item,"mp3");if(!r){if(error_bytes)snprintf(error,error_bytes,"This item needs MP3 preparation for PearlPod");goto done;}for(unsigned p=0;p<r->count;p++)t.info.total_bytes+=r->segments[p].bytes;if(item->has_artwork)t.info.total_bytes+=item->artwork.bytes;}
 t.info.songs_total=catalog->item_count;t.info.playlist_count=1;t.info.playlist_index=1;snprintf(t.info.playlist,sizeof(t.info.playlist),"Your library");
 for(unsigned n=0;n<catalog->item_count&&!canceled(&t);n++){
  const media_item *item=&catalog->items[n];const media_representation *r=media_item_representation(item,"mp3");snprintf(t.info.song,sizeof(t.info.song),"%.64s",item->title);snprintf(t.info.kind,sizeof(t.info.kind),"song");
  if(progress)progress(&t.info);
  for(unsigned p=0;p<r->count;p++)if(!object(&t,base,catalog->revision,r->segments[p].path,r->segments[p].sha256,r->segments[p].bytes,buffer))goto done;
  if(item->has_artwork&&!object(&t,base,catalog->revision,item->artwork.path,item->artwork.sha256,item->artwork.bytes,buffer))goto done;
  t.info.songs_done=n+1;if(progress)progress(&t.info);
 }
 if(canceled(&t)){result=PEARL_PULL_CANCELED;goto done;}
 char active[PULL_PATH];if(!join(root,".pearl/media-library.json",active,sizeof(active))||!parents(active)||!media_text_replace(active,json))goto done;
 t.info.playlists_ready=1;snprintf(t.info.kind,sizeof(t.info.kind),"saved");if(progress)progress(&t.info);result=PEARL_PULL_OK;
done:
 close_http(&t);media_library_free(catalog);free(json);free(buffer);
 if(canceled(&t))result=PEARL_PULL_CANCELED;
 if(result==PEARL_PULL_FAILED&&error_bytes&&!error[0])snprintf(error,error_bytes,"Sync interrupted or card full. Existing music is safe; retry resumes.");
 return result;
}
