#include "sync_progress.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  const char *json="{\"p\":\"After school\",\"t\":\"You Say Run\",\"a\":\"Soundtrack\",\"r\":\"Artist\",\"k\":\"song\",\"b\":10000000,\"d\":0,\"f\":6000000,\"n\":2,\"s\":0,\"i\":1,\"c\":1,\"q\":0}";
  pearl_transfer_info info;
  assert(pearl_progress_parse(json,&info));
  pearl_transfer_progress p;pearl_progress_reset(&p);
  pearl_sync_view view;pearl_progress_view(&p,0,&view);
  assert(!view.determinate&&!strstr(view.timing,"min"));
  assert(pearl_progress_update(&p,&info,0));
  pearl_progress_sample(&p,1000000,1000);pearl_progress_view(&p,1000,&view);
  assert(!strcmp(view.title,"After school")&&!strcmp(view.detail,"You Say Run"));
  assert(view.percent==10&&strstr(view.timing,"Estimating"));
  for(unsigned i=2;i<=6;i++)pearl_progress_sample(&p,(uint64_t)i*1000000,i*1000);
  pearl_progress_view(&p,6000,&view);assert(view.percent==60&&strstr(view.timing,"under a minute"));
  pearl_progress_sample(&p,6000000,22000);pearl_progress_view(&p,22000,&view);
  assert(strstr(view.timing,"Waiting")&&!strstr(view.timing,"min"));
  info.completed_bytes=6000000;info.file_bytes=4000000;info.songs_done=1;
  assert(pearl_progress_update(&p,&info,23000));
  pearl_progress_sample(&p,1000000,24000);pearl_progress_view(&p,24000,&view);
  assert(view.percent==70&&!strcmp(view.count,"Overall: 1 of 2 songs"));
  /* Reconnect retries start the current file again, without counting bytes
   * from its failed attempt as completed work or producing a huge ETA. */
  assert(pearl_progress_update(&p,&info,25000));pearl_progress_sample(&p,1000,26000);
  pearl_progress_view(&p,26000,&view);assert(view.percent==60);
  info.completed_bytes=0;assert(!pearl_progress_update(&p,&info,27000));
  info.completed_bytes=10000000;info.file_bytes=0;info.songs_done=2;info.playlists_ready=1;strcpy(info.kind,"finishing");
  assert(pearl_progress_update(&p,&info,28000));pearl_progress_view(&p,28000,&view);
  assert(view.percent==100&&view.playlists_ready==1&&strstr(view.detail,"Updating"));
  assert(!pearl_progress_parse("[]",&info));assert(!pearl_progress_parse("{}",&info));
  char bad[600];snprintf(bad,sizeof(bad),"%s trailing",json);assert(!pearl_progress_parse(bad,&info));
  assert(!pearl_progress_parse("{\"b\":-1}",&info));
  assert(!pearl_progress_parse("{\"x\":[[[[[[[[[[[[[[[[[0]]]]]]]]]]]]]]]]]}",&info));
  /* uint32 time rollover must not suppress the warmup or stall state. */
  assert(pearl_progress_parse(json,&info));pearl_progress_reset(&p);
  uint32_t start=UINT32_MAX-3000;assert(pearl_progress_update(&p,&info,start));
  pearl_progress_sample(&p,1000000,start+6000);pearl_progress_view(&p,start+22000,&view);assert(strstr(view.timing,"Waiting"));
  /* Totals above 4 GiB must not wrap the percentage or remaining bytes. */
  pearl_progress_reset(&p);info.total_bytes=UINT64_C(10000000000);info.completed_bytes=UINT64_C(5000000000);info.file_bytes=1000000;
  assert(pearl_progress_update(&p,&info,0));pearl_progress_view(&p,0,&view);assert(view.percent==50);
  puts("Content progress, measured ETA, stalls, retries, invalid input and large totals pass.");
}
