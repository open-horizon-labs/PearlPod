#include "player.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
 else if(!strcmp(line,"buttons"))printf("PEARL buttons gpio0=%d gpio47=%d gpio48=%d\n",gpio_get_level(0),gpio_get_level(47),gpio_get_level(48));
 else printf("PEARL commands: status, list, play N, pause, next, prev, volume N, buttons\n");
 fflush(stdout);
}
static void task(void *arg){char line[128];unsigned n=0;while(1){int c=getchar();if(c==EOF){clearerr(stdin);vTaskDelay(pdMS_TO_TICKS(20));continue;}if(c=='\r'||c=='\n'){if(n){line[n]=0;run(line);n=0;}}else if(n<sizeof(line)-1)line[n++]=c;}}
void pearl_console_start(pearl_library *l){lib=l;gpio_set_direction(47,GPIO_MODE_INPUT);gpio_pullup_en(47);xTaskCreate(task,"console",4096,NULL,1,NULL);}
