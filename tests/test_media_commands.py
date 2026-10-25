"""Compile production selection/seek/command routines with silent hardware seams."""
from pathlib import Path
import subprocess,tempfile,os
root=Path(__file__).resolve().parents[1]
s=(root/'main/audio.c').read_text()
a=s[s.index('static void select_position('):s.index('static void preference_task(')]
b=s[s.index('static void command_apply('):s.index('static bool service(')]
code=r'''
#include "media.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#define portMAX_DELAY 0
#define ESP_OK 0
static pearl_library *library,*pending_library;
static pearl_state state;
static int active_collection=-1,collection_position,playback_epoch,prefs_lock,prefs;
static bool detached,detach_signaled,stopping,prefs_open,save_ok=true,missing_part;
typedef struct{int kind,value,position;}command;
enum{PLAY,TOGGLE,STEP,VOLUME,STOP,COLLECTION,DETACH,ATTACH,SEEK,SEEK_TO,CHAPTER};
static pearl_state pearl_audio_state(void){return state;}
static void publish(pearl_state s){state=s;}
static bool save(void){return save_ok;}
static void xSemaphoreTake(int a,int b){(void)a;(void)b;}
static void xSemaphoreGive(int a){(void)a;}
static int nvs_get_str(int a,const char*b,char*c,size_t*d){(void)a;(void)b;(void)c;(void)d;return -1;}
static media_item items[2];static media_segment parts[2];static media_representation rep;
static media_progress saved[2]={{.position_ms=75000},{.position_ms=23000}};
const media_item *pearl_media_item(const pearl_library*l,int t){return t>=0&&(unsigned)t<l->track_count?l->tracks[t].media:NULL;}
bool pearl_media_progress_load(const pearl_library*l,int t,media_progress*out){const media_item*i=pearl_media_item(l,t);if(!i)return false;*out=saved[i==&items[1]];return true;}
uint32_t pearl_media_start(const pearl_library*l,int t){return t==1&&pearl_media_item(l,t)==&items[0]?60000:0;}
int pearl_media_locate(const pearl_library*l,int t,uint32_t p,uint32_t*out){(void)l;const media_item*i=pearl_media_item(l,t);if(i==&items[0]){if(missing_part&&p>=60000)return -1;*out=p>=60000?p-60000:p;return p>=60000?1:0;}*out=p;return t;}
'''
# player.h declares pearl_audio_state externally; retain matching external linkage.
code=code.replace('static pearl_state pearl_audio_state','pearl_state pearl_audio_state')
code+=a+b+r'''
int main(void){pearl_track tracks[5]={0};pearl_library l={.tracks=tracks,.track_count=5};library=&l;items[0].kind=MEDIA_AUDIOBOOK;items[0].duration_ms=120000;items[1].kind=MEDIA_PODCAST;items[1].duration_ms=90000;media_chapter chapter={.start_ms=70000};items[0].chapters=&chapter;items[0].chapter_count=1;tracks[0].media=tracks[1].media=&items[0];tracks[1].media_segment=1;tracks[2].media=&items[1];tracks[3].media_owned=true;
 state=(pearl_state){.track=-1,.paused=true};select_position(&state,0,true,false);assert(state.track==1&&state.milliseconds==75000&&state.paused&&state.spoken);
 missing_part=true;select_position(&state,0,true,true);assert(state.track<0&&state.paused&&strstr(state.error,"not on the card"));command_apply((command){.kind=TOGGLE});assert(state.track<0&&state.paused);missing_part=false;command_apply((command){.kind=PLAY,.value=2});assert(state.track==2&&state.milliseconds==23000&&!state.paused);
 command_apply((command){.kind=PLAY,.value=0});assert(state.track==1&&state.milliseconds==75000);
 state.paused=true;command_apply((command){.kind=SEEK,.value=-30});assert(state.track==0&&state.milliseconds==45000&&state.paused);
 command_apply((command){.kind=CHAPTER,.value=0});assert(state.track==1&&state.milliseconds==70000&&state.paused);
 command_apply((command){.kind=SEEK_TO,.value=120000});assert(state.completed&&state.paused);
 command_apply((command){.kind=TOGGLE});assert(state.track==0&&!state.completed&&!state.paused&&!state.milliseconds);
 command_apply((command){.kind=PLAY,.value=4});assert(!state.spoken&&!state.milliseconds);
 command_apply((command){.kind=PLAY,.value=3});assert(state.track==4);
 state.track=-1;state.paused=true;command_apply((command){.kind=TOGGLE});assert(state.track>=0&&state.track!=3);
 state.volume=65;command_apply((command){.kind=VOLUME,.value=35});assert(state.volume==100);
 command_apply((command){.kind=VOLUME,.value=2});assert(state.volume==100);
 command_apply((command){.kind=VOLUME,.value=-102});assert(state.volume==0);
 save_ok=false;int track=state.track;command_apply((command){.kind=PLAY,.value=2});assert(state.track==track);
 puts("Production commands: boot paused, per-item resume, music fresh, split seek, chapters, completion restart, orphan rejection, failed-save guard pass");}
'''
# Library functions are linked as actual code; unrelated overlay dependency uses real shared objects.
with tempfile.TemporaryDirectory(prefix='pearl-media-command.') as d:
 p=Path(d)/'commands.c';p.write_text(code)
 shared=root/'shared/embedded-services/components'
 env=os.environ.copy();env['C_INCLUDE_PATH']=str(shared/'media_library/include')+':'+str(shared/'device_input/include')+':'+str(Path(os.environ['IDF_PATH'])/'components/json/cJSON')
 subprocess.run(['cc','-Wall','-Wextra','-Werror','-Wno-unused-variable','-fsanitize=address,undefined','-ffunction-sections','-fdata-sections','-I',str(root/'main'),str(p),str(root/'main/library.c'),str(shared/'device_input/device_input.c'),str(shared/'media_library/media_library.c'),'-Wl,--gc-sections','-o',d+'/test'],env=env,check=True)
 subprocess.run([d+'/test'],check=True)
