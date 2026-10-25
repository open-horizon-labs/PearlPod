#define _POSIX_C_SOURCE 200809L
#include "media_pull.h"
#include "player.h"
static bool audio_ready;
pearl_state pearl_audio_state(void){return (pearl_state){.paused=true,.ready=audio_ready};}
#include "esp_http_client.h"
#include "media_library.h"
#include <openssl/sha.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define BYTES 200000
static unsigned char data[BYTES];static char hash[65],catalog[4096],etag[68];static unsigned object_requests,resumed;static int catalog_status=200,fail_after=-1;static bool wrong_hash,wrong_etag,cancel_now;
struct http{esp_http_client_config_t cfg;bool object;unsigned offset,position;int status;char range[96],if_range[128];};
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t*c){struct http*h=calloc(1,sizeof(*h));assert(h);h->cfg=*c;h->object=strstr(c->url,"/objects/")!=NULL;return h;}
int esp_http_client_set_header(struct http*h,const char*k,const char*v){if(!strcmp(k,"Range")){assert(sscanf(v,"bytes=%u-",&h->offset)==1);}if(!strcmp(k,"If-Range"))snprintf(h->if_range,sizeof(h->if_range),"%s",v);return 0;}
int esp_http_client_open(struct http*h,int n){(void)n;h->status=h->object?(h->offset?206:200):catalog_status;if(h->object){object_requests++;assert(strstr(h->cfg.url,"Folder/A%20book.mp3"));if(h->offset){resumed++;assert(!strcmp(h->if_range,etag));}h->position=h->offset;}return 0;}
int64_t esp_http_client_fetch_headers(struct http*h){if(h->object){esp_http_client_event_t e={.event_id=HTTP_EVENT_ON_HEADER,.user_data=h->cfg.user_data,.header_key="ETag",.header_value=wrong_etag?"\"wrong\"":etag};h->cfg.event_handler(&e);if(h->offset){snprintf(h->range,sizeof(h->range),"bytes %u-%u/%u",h->offset,BYTES-1,BYTES);e.header_key="Content-Range";e.header_value=h->range;h->cfg.event_handler(&e);}return BYTES-h->offset;}return strlen(catalog);}
int esp_http_client_get_status_code(struct http*h){return h->status;}
int esp_http_client_read(struct http*h,void*b,unsigned n){if(h->object&&fail_after>=0&&h->position>=(unsigned)fail_after)return -1;unsigned total=h->object?BYTES:strlen(catalog);if(n>total-h->position)n=total-h->position;if(h->object)memcpy(b,data+h->position,n);else memcpy(b,catalog+h->position,n);if(wrong_hash&&h->object&&n)((unsigned char*)b)[0]^=1;h->position+=n;return n;}
bool esp_http_client_is_complete_data_received(struct http*h){return h->position==(h->object?BYTES:strlen(catalog));}
void esp_http_client_close(struct http*h){(void)h;}
void esp_http_client_cleanup(struct http*h){free(h);}
void vTaskDelay(int ticks){(void)ticks;struct timespec t={0,1000000};nanosleep(&t,NULL);}
static bool cancelled(void){return cancel_now;}
static bool exists(const char*p){struct stat st;return !stat(p,&st);}
int main(void){for(unsigned n=0;n<BYTES;n++)data[n]=n%251;unsigned char d[32];SHA256(data,BYTES,d);for(unsigned n=0;n<32;n++)sprintf(hash+n*2,"%02x",d[n]);snprintf(etag,sizeof(etag),"\"%s\"",hash);char id[65];memset(id,'a',64);id[64]=0;
 snprintf(catalog,sizeof(catalog),"{\"version\":1,\"revision\":\"%s\",\"items\":[{\"id\":\"%s\",\"kind\":\"podcast\",\"source\":{\"type\":\"rss\",\"scope\":\"%s\",\"id\":\"episode\"},\"title\":\"An episode\",\"artist\":\"Show\",\"duration_ms\":60000,\"representations\":[{\"id\":\"%s\",\"codec\":\"mp3\",\"segments\":[{\"path\":\"Folder/A book.mp3\",\"title\":\"Episode\",\"start_ms\":0,\"duration_ms\":60000,\"bytes\":%u,\"sha256\":\"%s\"}]}],\"chapters\":[]}],\"collections\":[]}",id,id,id,id,BYTES,hash);
 char root[]="/tmp/pearl-pull.XXXXXX",error[160],active[512],final[512];assert(mkdtemp(root));snprintf(active,sizeof(active),"%s/.pearl/media-library.json",root);snprintf(final,sizeof(final),"%s/Folder/A book.mp3",root);
 catalog_status=503;assert(pearl_media_pull("http://local",root,cancelled,NULL,error,sizeof(error))==PEARL_PULL_FAILED&&strstr(error,"pending")&&!object_requests&&!exists(active));catalog_status=404;assert(pearl_media_pull("http://local",root,cancelled,NULL,error,sizeof(error))==PEARL_PULL_UNSUPPORTED);catalog_status=200;
 char folder[512];snprintf(folder,sizeof(folder),"%s/Folder",root);assert(!mkdir(folder,0700));FILE *foreign=fopen(final,"wb");assert(foreign);fputs("user media",foreign);fclose(foreign);assert(pearl_media_pull("http://local",root,cancelled,NULL,error,sizeof(error))==PEARL_PULL_FAILED&&!exists(active));assert(pearl_media_pull("http://local",root,cancelled,NULL,error,sizeof(error))==PEARL_PULL_FAILED);assert(!unlink(final));char cleanup[600];snprintf(cleanup,sizeof(cleanup),"rm -f %s/.pearl/downloads/*",root);assert(!system(cleanup));
 fail_after=70000;assert(pearl_media_pull("http://local",root,cancelled,NULL,error,sizeof(error))==PEARL_PULL_FAILED&&!exists(active)&&!exists(final));fail_after=-1;
 assert(pearl_media_pull("http://local",root,cancelled,NULL,error,sizeof(error))==PEARL_PULL_OK&&exists(active)&&exists(final)&&resumed);unsigned requests=object_requests;assert(pearl_media_pull("http://local",root,cancelled,NULL,error,sizeof(error))==PEARL_PULL_OK&&object_requests==requests);
 FILE*f=fopen(final,"wb");assert(f);fputs("corrupt",f);fclose(f);assert(pearl_media_pull("http://local",root,cancelled,NULL,error,sizeof(error))==PEARL_PULL_OK&&exists(active));assert(!unlink(final));char clean[600];snprintf(clean,sizeof(clean),"rm -f %s/.pearl/downloads/*",root);assert(!system(clean));wrong_hash=true;assert(pearl_media_pull("http://local",root,cancelled,NULL,error,sizeof(error))==PEARL_PULL_FAILED&&!exists(final)&&exists(active));wrong_hash=false;wrong_etag=true;assert(pearl_media_pull("http://local",root,cancelled,NULL,error,sizeof(error))==PEARL_PULL_FAILED&&!exists(final));wrong_etag=false;audio_ready=true;assert(pearl_media_pull("http://local",root,cancelled,NULL,error,sizeof(error))==PEARL_PULL_FAILED&&!exists(final));audio_ready=false;assert(pearl_media_pull("http://local",root,cancelled,NULL,error,sizeof(error))==PEARL_PULL_OK&&exists(final));
 cancel_now=true;assert(pearl_media_pull("http://local",root,cancelled,NULL,error,sizeof(error))==PEARL_PULL_CANCELED&&exists(active));char cmd[600];snprintf(cmd,sizeof(cmd),"rm -rf %s",root);assert(!system(cmd));puts("Actual Pearl pull + durable writer: 503/404, interrupted Range/If-Range resume, SHA, ETag, cache verify, owned repair, immutable user-file guard, cancellation, catalog activation pass");}
