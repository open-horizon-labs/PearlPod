#include "player.h"
#include "network.h"
#include "captive_dns.h"
#include "sync.h"
#include "tracer.h"
#include "power.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "driver/usb_serial_jtag.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern void pearl_sd_bench(void);
extern void pearl_sd_trace(char *out,unsigned size);
static pearl_library *lib;
static void run(const char *line){
 if(!strcmp(line,"status")){pearl_state s=pearl_audio_state();printf("PEARL status ready=%d track=%d paused=%d volume=%d seconds=%lu error=%s\n",s.ready,s.track,s.paused,s.volume,(unsigned long)s.seconds,s.error);}
 else if(!strcmp(line,"list")){if(!pearl_library_lock())return;printf("PEARL library albums=%u tracks=%u\n",lib->album_count,lib->track_count);for(unsigned i=0;i<lib->track_count;i++)printf("PEARL track %u %s\n",i,lib->tracks[i].path);pearl_library_unlock();}
 else if(!strcmp(line,"groups")){if(!pearl_library_lock())return;for(unsigned i=0;i<lib->collection_count;i++)printf("PEARL group %u kind=%u tracks=%u %s\n",i,lib->collections[i].kind,lib->collections[i].count,lib->collections[i].title);pearl_library_unlock();}
 else if(!strncmp(line,"playgroup ",10)){int group,position;if(sscanf(line+10,"%d %d",&group,&position)==2)pearl_audio_play_collection(group,position);}
 else if(!strncmp(line,"play ",5))pearl_audio_play(atoi(line+5));
 else if(!strcmp(line,"rescan"))pearl_library_rescan();
 else if(!strcmp(line,"pause"))pearl_audio_toggle();
 else if(!strcmp(line,"next"))pearl_audio_step(1);
 else if(!strcmp(line,"prev"))pearl_audio_step(-1);
 else if(!strncmp(line,"volume ",7)){pearl_state s=pearl_audio_state();pearl_audio_volume(atoi(line+7)-s.volume);}
 else if(!strcmp(line,"power")){uint32_t now=(uint32_t)(esp_timer_get_time()/1000);bool host=usb_serial_jtag_is_connected();printf("PEARL power screen_asleep=%d deep_supported=%d usb_connected=%d usb_host_live=%d idle_ms=%lu\n",pearl_power_screen_asleep(),pearl_power_deep_supported(),pearl_power_usb_guard(now,host),host,(unsigned long)(now-pearl_power_last_activity()));}
 else if(!strcmp(line,"memory"))printf("PEARL memory internal=%u minimum=%u largest=%u psram=%u\n",(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),(unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
 else if(!strcmp(line,"sd bench")||!strcmp(line,"trace sd"))pearl_sd_bench();
 else if(!strcmp(line,"trace start"))printf("PEARL tracer start_ok=%d\n",pearl_trace_start());
 else if(!strcmp(line,"trace stop")){pearl_trace_stop();pearl_trace_snapshot();}
 else if(!strcmp(line,"trace status"))pearl_trace_snapshot();
 else if(!strcmp(line,"trace cancel"))pearl_trace_cancel();
 else if(!strncmp(line,"trace events",12))pearl_trace_events(line[12]==' '?strtoul(line+13,NULL,10):128);
 else if(!strncmp(line,"trace net ",10))printf("PEARL tracer probe_start_ok=%d\n",pearl_sync_probe_start(line+10));
 else if(!strcmp(line,"sd trace")){char trace[160];pearl_sd_trace(trace,sizeof(trace));printf("PEARL sd %s\n",trace);}
 else if(!strcmp(line,"sync cancel"))pearl_sync_cancel();
 else if(!strcmp(line,"sync"))pearl_sync_start();
 else if(!strncmp(line,"sync source ",12))printf("PEARL sync source saved=%d\n",pearl_sync_source(line+12));
 else if(!strcmp(line,"sync trace")){static EXT_RAM_BSS_ATTR char trace[1536];pearl_sync_trace(trace,sizeof(trace));printf("PEARL sync trace %s\n",trace);}
 else if(!strcmp(line,"sync status")){char msg[120];pearl_sync_status(msg,sizeof(msg));printf("PEARL sync %s\n",msg);}
 else if(!strcmp(line,"wifi portal"))printf("PEARL captive active=%d dns_replies=%u\n",pearl_captive_dns_active(),pearl_captive_dns_replies());
 else if(!strcmp(line,"wifi setup"))pearl_network_setup();
 else if(!strcmp(line,"wifi scan"))pearl_network_scan();
 else if(!strcmp(line,"wifi connect"))pearl_network_connect();
 else if(!strcmp(line,"wifi off"))pearl_network_off();
 else if(!strcmp(line,"wifi status")){pearl_network_state s=pearl_network_snapshot();printf("PEARL wifi enabled=%d setup=%d connected=%d scanning=%d count=%u ip=%s message=%s\n",s.enabled,s.setup,s.connected,s.scanning,s.count,s.ip,s.message);for(unsigned i=0;i<s.count;i++)printf("PEARL AP %s rssi=%d secure=%d\n",s.aps[i].ssid,s.aps[i].rssi,s.aps[i].secure);}
 else if(!strcmp(line,"buttons"))printf("PEARL buttons gpio0=%d gpio47=%d gpio48=%d\n",gpio_get_level(0),gpio_get_level(47),gpio_get_level(48));
 else printf("PEARL commands: status, list, groups, play N, playgroup N P, rescan, pause, next, prev, volume N, buttons, memory, power, wifi setup/scan/connect/off/status, sync, sync status, sync source URL, trace start/status/stop/events/sd/net URL/cancel\n");
 fflush(stdout);
}
static void task(void *arg){char line[256];unsigned n=0;while(1){int c=getchar();if(c==EOF){clearerr(stdin);vTaskDelay(pdMS_TO_TICKS(20));continue;}pearl_power_usb_activity();if(c=='\r'||c=='\n'){if(n){line[n]=0;run(line);n=0;}}else if(n<sizeof(line)-1)line[n++]=c;}}
void pearl_console_start(pearl_library *l){lib=l;gpio_set_direction(47,GPIO_MODE_INPUT);gpio_pullup_en(47);xTaskCreate(task,"console",6144,NULL,1,NULL);}
