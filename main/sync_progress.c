#include "sync_progress.h"
#include "cJSON.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void pearl_progress_reset(pearl_transfer_progress *p) { memset(p, 0, sizeof(*p)); }

static bool number(cJSON *root, const char *key, uint64_t limit, uint64_t *out) {
  cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
  if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble < 0 ||
      v->valuedouble > (double)limit || floor(v->valuedouble) != v->valuedouble) return false;
  *out = (uint64_t)v->valuedouble;
  return true;
}
static bool text(cJSON *root, const char *key, char *out, unsigned size) {
  cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
  if (!cJSON_IsString(v) || strlen(v->valuestring) >= size) return false;
  for (const unsigned char *s = (unsigned char *)v->valuestring; *s; s++) if (*s < 32) return false;
  snprintf(out, size, "%s", v->valuestring);
  return true;
}
bool pearl_progress_parse(const char *json, pearl_transfer_info *out) {
  if (!json || strlen(json) > 480) return false;
  /* Keep untrusted recursion bounded before cJSON allocates or descends. */
  bool quoted=false,escaped=false;unsigned depth=0;
  for(const char *s=json;*s;s++) {
    if(quoted) {if(escaped)escaped=false;else if(*s=='\\')escaped=true;else if(*s=='"')quoted=false;}
    else if(*s=='"')quoted=true;
    else if(*s=='{'||*s=='[') {if(++depth>8)return false;}
    else if(*s=='}'||*s==']') {if(!depth)return false;depth--;}
  }
  if(depth||quoted)return false;
  const char *end;
  cJSON *root = cJSON_ParseWithOpts(json, &end, true);
  if (!cJSON_IsObject(root)) { cJSON_Delete(root); return false; }
  pearl_transfer_info value = {0};
  uint64_t d, n, i, c, q;
  bool ok = text(root,"p",value.playlist,sizeof(value.playlist)) &&
    text(root,"t",value.song,sizeof(value.song)) && text(root,"a",value.album,sizeof(value.album)) &&
    text(root,"r",value.artist,sizeof(value.artist)) && text(root,"k",value.kind,sizeof(value.kind)) &&
    number(root,"b",UINT64_C(1099511627776),&value.total_bytes) &&
    number(root,"d",value.total_bytes,&value.completed_bytes) &&
    number(root,"f",value.total_bytes-value.completed_bytes,&value.file_bytes) &&
    number(root,"n",16384,&n) && number(root,"s",n,&d) &&
    number(root,"c",16384,&c) && number(root,"i",c,&i) && number(root,"q",c,&q);
  if (ok) {
    value.songs_done=d; value.songs_total=n; value.playlist_index=i; value.playlist_count=c;value.playlists_ready=q;
    ok = !strcmp(value.kind,"song") || !strcmp(value.kind,"artwork") ||
      !strcmp(value.kind,"lyrics") || !strcmp(value.kind,"playlist") || !strcmp(value.kind,"saving") || !strcmp(value.kind,"saved") || !strcmp(value.kind,"finishing");
  }
  cJSON_Delete(root);
  if (ok) *out=value;
  return ok;
}
bool pearl_progress_update(pearl_transfer_progress *p, const pearl_transfer_info *info, uint32_t now) {
  if (info->completed_bytes > info->total_bytes || info->file_bytes > info->total_bytes-info->completed_bytes ||
      info->songs_done > info->songs_total || info->playlist_index > info->playlist_count) return false;
  /* A reconnect may restart the current file; published completed work never goes backwards. */
  if (p->known && (info->total_bytes != p->info.total_bytes || info->completed_bytes < p->info.completed_bytes)) return false;
  if (!p->known) { p->started_ms=p->sample_ms=p->last_data_ms=now; p->sample_bytes=p->started_bytes=info->completed_bytes; }
  p->known=true; p->info=*info; p->received=info->completed_bytes;
  return true;
}
void pearl_progress_sample(pearl_transfer_progress *p, uint64_t file_bytes, uint32_t now) {
  if (!p->known) return;
  uint64_t next=p->info.completed_bytes+(file_bytes>p->info.file_bytes?p->info.file_bytes:file_bytes);
  if (next>p->received) p->last_data_ms=now;
  p->received=next;
  uint32_t dt=now-p->sample_ms;
  if (dt>=1000) {
    double rate=next>=p->sample_bytes?(double)(next-p->sample_bytes)*1000/dt:0;
    /* A one-second WiFi/SD stall should not turn a ten-minute estimate into
     * an hour. Smooth over roughly twenty samples, while stalls remain explicit. */
    p->rate=p->sampled?p->rate*0.95+rate*0.05:rate;
    p->sampled=true; p->sample_ms=now; p->sample_bytes=next;
  }
}
void pearl_progress_view(const pearl_transfer_progress *p, uint32_t now, pearl_sync_view *out) {
  memset(out,0,sizeof(*out)); out->busy=true;
  if (!p->known) {
    snprintf(out->title,sizeof(out->title),"Getting your playlists");
    snprintf(out->detail,sizeof(out->detail),"Checking what has changed");
    snprintf(out->context,sizeof(out->context),"The computer is preparing your music.");
    snprintf(out->timing,sizeof(out->timing),"Waiting for the library computer"); return;
  }
  const pearl_transfer_info *i=&p->info;
  out->playlists_ready=i->playlists_ready;
  snprintf(out->title,sizeof(out->title),"%s",i->playlist[0]?i->playlist:"Your playlists");
  bool finishing=!strcmp(i->kind,"finishing");
  const char *detail=finishing?"Updating your library":!strcmp(i->kind,"song")?i->song:
    !strcmp(i->kind,"artwork")?"Adding album artwork":!strcmp(i->kind,"lyrics")?"Adding lyrics":!strcmp(i->kind,"saving")?"Saving the playlist":!strcmp(i->kind,"saved")?"Playlist saved":"Updating the playlist";
  snprintf(out->detail,sizeof(out->detail),"%s",detail);
  snprintf(out->context,sizeof(out->context),"%s%s%s",i->artist,i->artist[0]&&i->album[0]?" / ":"",i->album);
  snprintf(out->count,sizeof(out->count),"Overall: %u of %u songs",i->songs_done,i->songs_total);
  if (!i->songs_total) snprintf(out->count,sizeof(out->count),"Songs already on your Pod");
  out->determinate=i->total_bytes>0;
  out->percent=i->total_bytes?(unsigned)(p->received*100/i->total_bytes):0;
  if (out->percent>=100 && !finishing) out->percent=99;
  if (finishing) snprintf(out->timing,sizeof(out->timing),"Almost ready to listen");
  else if ((uint32_t)(now-p->last_data_ms)>=15000) snprintf(out->timing,sizeof(out->timing),"Waiting for the computer...");
  else if (!p->sampled || (uint32_t)(now-p->started_ms)<5000 || p->rate<1)
    snprintf(out->timing,sizeof(out->timing),"Estimating time left...");
  else {
    uint32_t duration=now-p->started_ms;
    double average=duration&&p->received>p->started_bytes?(double)(p->received-p->started_bytes)*1000/duration:0;
    /* Include all file/control/SD pauses actually observed this session, while
     * retaining some sensitivity to a sustained change in radio conditions. */
    double rate=average>0?average*0.75+p->rate*0.25:p->rate;
    double seconds=(i->total_bytes-p->received)/rate;
    if (seconds<60) snprintf(out->timing,sizeof(out->timing),"Sync: under a minute left");
    else if (seconds<3600) snprintf(out->timing,sizeof(out->timing),"Whole sync: about %.0f min left",ceil(seconds/60));
    else snprintf(out->timing,sizeof(out->timing),"Whole sync: about %.0f hr left",ceil(seconds/3600));
  }
}
