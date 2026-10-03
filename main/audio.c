#include "player.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/i2s_std.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <stdatomic.h>
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "minimp3.h"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_SIMD
#include "dr_flac.h"
#include "codec_flac.h"
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

static pearl_library *library;
static QueueHandle_t commands;
static SemaphoreHandle_t state_lock;
static pearl_state state={.track=-1,.volume=20,.paused=true};
static i2s_chan_handle_t tx;
static int cs_addr=-1;
static atomic_bool stopping,detached;
static bool detach_signaled;
static SemaphoreHandle_t reload_done;
static pearl_library *pending_library;
static int active_collection=-1,collection_position;
static unsigned playback_epoch,decode_epoch;
static nvs_handle_t prefs;
static bool prefs_open;
static SemaphoreHandle_t prefs_lock;
typedef struct {int kind,value,position;} command;
enum {PLAY,TOGGLE,STEP,VOLUME,STOP,COLLECTION,DETACH,ATTACH};
static void send(int kind,int value){if(commands){command c={.kind=kind,.value=value};if(xQueueSend(commands,&c,0)!=pdTRUE)ESP_LOGW("player","command queue full");}}
void pearl_audio_play(int i){send(PLAY,i);}
void pearl_audio_play_collection(int collection,int position){if(!commands||collection<0||position<0)return;command c={.kind=COLLECTION,.value=collection,.position=position};if(xQueueSend(commands,&c,0)!=pdTRUE)ESP_LOGW("player","command queue full");} void pearl_audio_toggle(void){send(TOGGLE,0);}
void pearl_audio_step(int d){send(STEP,d);} void pearl_audio_volume(int d){send(VOLUME,d);}
pearl_state pearl_audio_state(void){pearl_state s;if(!state_lock)return state;xSemaphoreTake(state_lock,portMAX_DELAY);s=state;xSemaphoreGive(state_lock);return s;}
static void publish(pearl_state s){if(!state_lock){state=s;return;}xSemaphoreTake(state_lock,portMAX_DELAY);state=s;xSemaphoreGive(state_lock);}
static void error(const char *msg){pearl_state s=pearl_audio_state();snprintf(s.error,sizeof(s.error),"%s",msg);s.paused=true;publish(s);ESP_LOGW("player","%s",msg);}
static esp_err_t cs_write(uint32_t r,uint8_t v){uint8_t b[]={r>>16,r>>8,r,0,v};return i2c_master_write_to_device(I2C_NUM_0,cs_addr,b,sizeof(b),pdMS_TO_TICKS(60));}
static esp_err_t cs_read(uint32_t r,uint8_t *v){uint8_t b[]={r>>16,r>>8,r,0};return i2c_master_write_read_device(I2C_NUM_0,cs_addr,b,sizeof(b),v,1,pdMS_TO_TICKS(60));}
static esp_err_t dac_init(void){
    gpio_set_direction(41,GPIO_MODE_OUTPUT);gpio_set_level(41,0);vTaskDelay(pdMS_TO_TICKS(5));gpio_set_level(41,1);vTaskDelay(pdMS_TO_TICKS(5));
    for(int a=0x30;a<=0x33;a++){cs_addr=a;uint8_t id[3]={0};if(cs_read(0x10000,&id[0])==ESP_OK && cs_read(0x10001,&id[1])==ESP_OK && cs_read(0x10002,&id[2])==ESP_OK){
        ESP_LOGW("player","DAC address=%02x ID=%02x%02x%02x",a,id[0],id[1],id[2]);if(id[0]==0x43 && id[1]==0x13 && (id[2]&0xf0)==0x10)break;
    }cs_addr=-1;}
    if(cs_addr<0){
        // PCM5102 variant has no I2C control port. Its active-low mute is GPIO48.
        if(CONFIG_PEARL_BUTTON_DOWN==48){return ESP_ERR_NOT_FOUND;} // Don't drive a possible button as output.
        gpio_set_direction(48,GPIO_MODE_OUTPUT);gpio_set_level(48,0);
        ESP_LOGW("player","PCM5102 path selected; mute remains asserted until playback");return ESP_OK;
    }
    // 24.576 MHz crystal and ASP slave configuration recovered from upstream v1.3.2.
    // Clock selection and headphone power sequence checked against Cirrus DS1155F2 §5.7/§5.13.
    static const struct {uint32_t r;uint8_t v;} regs[]={
        {0x20052,4},{0xf0010,0xe7},{0x20000,0xf6},
        {0x1000b,2},{0x1000c,4}, // 48 kHz, 32-bit input slots
        {0x40010,1},{0x40011,0},{0x40012,8},{0x40013,0},
        {0x40014,31},{0x40015,0},{0x40016,63},{0x40017,0},
        {0x40018,0x0c},{0x40019,0x0a},{0x50000,0},{0x50001,0},
        {0x5000a,7},{0x5000b,15},{0x90000,2},
        {0x90001,32},{0x90002,32},{0x90003,0xec},{0x90004,0},
        {0xb0000,0x16},{0x80000,0x10}, // lower full-scale output; software ceiling remains independent
    };
    for(unsigned i=0;i<sizeof(regs)/sizeof(regs[0]);i++){esp_err_t e=cs_write(regs[i].r,regs[i].v);if(e)return e;}
    uint8_t status=0;int tries=30;
    do {if(cs_read(0xf0000,&status))return ESP_FAIL;if(status&0x10)break;vTaskDelay(pdMS_TO_TICKS(2));}while(--tries);
    if(!tries){ESP_LOGE("player","DAC crystal not ready");return ESP_ERR_TIMEOUT;}
    if(cs_write(0x10006,0))return ESP_FAIL; // direct XTAL, 24.576 MHz
    vTaskDelay(pdMS_TO_TICKS(1));
    if(cs_write(0x10010,0x99)||cs_write(0x80032,0x20)||cs_write(0x20000,0xb6)||cs_write(0x20000,0xa6))return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(12));
    if(cs_write(0x80032,0)||cs_write(0x10010,0))return ESP_FAIL;
    return ESP_OK;
}
static void save(void){if(!prefs_open)return;xSemaphoreTake(prefs_lock,portMAX_DELAY);if(detached||!library){xSemaphoreGive(prefs_lock);return;}pearl_state s=pearl_audio_state();nvs_set_i32(prefs,"volume",s.volume);if(s.track>=0&&(unsigned)s.track<library->track_count)nvs_set_str(prefs,"track",library->tracks[s.track].path);nvs_commit(prefs);xSemaphoreGive(prefs_lock);}
static void preference_task(void *arg){
    pearl_state last={.track=-99,.volume=-99};
    while(!stopping){vTaskDelay(pdMS_TO_TICKS(3000));pearl_state s=pearl_audio_state();if(s.track!=last.track||s.volume!=last.volume){save();last=s;}}
    vTaskDelete(NULL);
}
static void command_apply(command c){
    pearl_state s=pearl_audio_state();
    if(detached&&c.kind!=ATTACH&&c.kind!=STOP&&c.kind!=VOLUME)return;
    if(c.kind==PLAY && c.value>=0 && (unsigned)c.value<library->track_count){active_collection=-1;s.track=c.value;playback_epoch++;s.seconds=0;s.paused=false;s.error[0]=0;}
    else if(c.kind==COLLECTION){unsigned index=c.value,position=c.position;if(index<library->collection_count&&position<library->collections[index].count){active_collection=index;collection_position=position;s.track=library->collections[index].tracks[position];playback_epoch++;s.seconds=0;s.paused=false;s.error[0]=0;}}
    else if(c.kind==DETACH){save();detached=true;detach_signaled=false;playback_epoch++;s.paused=true;}
    else if(c.kind==ATTACH){xSemaphoreTake(prefs_lock,portMAX_DELAY);library=pending_library;active_collection=-1;s.track=-1;s.seconds=0;s.paused=true;s.error[0]=0;char path[PEARL_PATH];size_t size=sizeof(path);if(prefs_open&&nvs_get_str(prefs,"track",path,&size)==ESP_OK)for(unsigned i=0;i<library->track_count;i++)if(!strcmp(path,library->tracks[i].path)){s.track=i;break;}publish(s);detached=false;xSemaphoreGive(prefs_lock);}
    else if(c.kind==TOGGLE){if(s.track<0 && library->track_count)s.track=0;s.paused=!s.paused;}
    else if(c.kind==STEP){if(active_collection>=0){collection_position=pearl_collection_step(library,active_collection,collection_position,c.value);s.track=collection_position>=0?(int)library->collections[active_collection].tracks[collection_position]:-1;}else s.track=pearl_next(library,s.track,c.value);playback_epoch++;s.seconds=0;s.error[0]=0;}
    else if(c.kind==VOLUME)s.volume=pearl_volume(s.volume,c.value,CONFIG_PEARL_MAX_VOLUME);
    else if(c.kind==STOP){s.paused=true;stopping=true;}
    publish(s);
}
static bool service(int current){command c;while(xQueueReceive(commands,&c,0)==pdTRUE)command_apply(c);pearl_state s=pearl_audio_state();return !stopping && !detached && s.track==current && decode_epoch==playback_epoch;}
static int32_t output[4096];
static int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
static uint8_t input[16384];
static double phase;
static int16_t previous[2];
static bool have_previous;
static uint64_t frames_written;
static bool emit(const int16_t *samples,size_t frames,unsigned channels,unsigned rate,int current){
    if(!rate||rate>192000||!channels||channels>2){error("Unsupported audio format");return false;}
    pearl_state initial=pearl_audio_state();
    float gain=initial.volume?powf(10.f,(-60.f+0.75f*initial.volume)/20.f):0.f;
    size_t used=0;double step=(double)rate/48000.0;
    for(size_t i=0;i<frames;i++){
        int16_t l=samples[i*channels],r=channels==2?samples[i*channels+1]:l;
        if(!have_previous){previous[0]=l;previous[1]=r;have_previous=true;}
        while(phase<1.0){
            output[used++]=(int32_t)((previous[0]+(l-previous[0])*phase)*gain)*65536;
            output[used++]=(int32_t)((previous[1]+(r-previous[1])*phase)*gain)*65536;
            phase+=step;frames_written++;
            if(used==4096){size_t bytes=0;if(i2s_channel_write(tx,output,used*sizeof(*output),&bytes,1000)!=ESP_OK||bytes!=used*sizeof(*output)){error("Audio output interrupted");return false;}used=0;if(!service(current))return false;}
        }
        phase-=1.0;previous[0]=l;previous[1]=r;
    }
    if(used){size_t bytes=0;if(i2s_channel_write(tx,output,used*sizeof(*output),&bytes,1000)!=ESP_OK||bytes!=used*sizeof(*output)){error("Audio output interrupted");return false;}}
    pearl_state s=pearl_audio_state();s.seconds=frames_written/48000;publish(s);return true;
}
static bool wait_playing(int current){
    while(service(current)){
        if(!pearl_audio_state().paused)return true;
        // DMA silence prevents replay of the last buffer during pause.
        int32_t silence[256]={0};size_t done;i2s_channel_write(tx,silence,sizeof(silence),&done,100);
        vTaskDelay(pdMS_TO_TICKS(15));
    }return false;
}
static bool decode(const char *path,int current){
    decode_epoch=playback_epoch;
    const char *ext=strrchr(path,'.');phase=0;have_previous=false;frames_written=0;
    if(ext&&!strcasecmp(ext,".flac")){
        pearl_flac_stream stream;drflac *f=pearl_flac_open(path,&stream);if(!f){error("Can't read this FLAC. Choose another track.");return false;}
        if(f->channels>2||f->sampleRate>192000){pearl_flac_close(f,&stream);error("Only mono or stereo FLAC is supported");return false;}
        uint64_t decoded=0;bool eof=false;while(wait_playing(current)){size_t n=drflac_read_pcm_frames_s16(f,1024,pcm);if(!n){if(!decoded||(f->totalPCMFrameCount&&decoded<f->totalPCMFrameCount))error("Damaged FLAC. Choose another track.");else eof=true;break;}decoded+=n;if(!emit(pcm,n,f->channels,f->sampleRate,current))break;}
        pearl_flac_close(f,&stream);return eof;
    }
    if(ext&&!strcasecmp(ext,".wav")){
        drwav f;if(!drwav_init_file(&f,path,NULL)){error("Can't read this WAV. Choose another track.");return false;}
        if(f.channels>2||f.sampleRate>192000){drwav_uninit(&f);error("Only mono or stereo WAV is supported");return false;}
        bool eof=false;while(wait_playing(current)){size_t n=drwav_read_pcm_frames_s16(&f,1024,pcm);if(!n){eof=true;break;}if(!emit(pcm,n,f.channels,f.sampleRate,current))break;}
        drwav_uninit(&f);return eof;
    }
    FILE *f=fopen(path,"rb");if(!f){error("Track missing. Reinsert the card and restart.");return false;}
    setvbuf(f,NULL,_IOFBF,16384);
    pearl_audio_offset(f);
    mp3dec_t decoder;mp3dec_init(&decoder);size_t count=0;bool eof=false,played=false;
    while(wait_playing(current)){
        if(count<sizeof(input)){size_t n=fread(input+count,1,sizeof(input)-count,f);count+=n;if(!n&&ferror(f)){error("Card read failed. Restart with the card inserted.");break;}}
        if(!count){eof=true;break;}
        mp3dec_frame_info_t info;int n=mp3dec_decode_frame(&decoder,input,count,pcm,&info);
        if(info.frame_bytes>0){count-=info.frame_bytes;memmove(input,input+info.frame_bytes,count);}
        else if(feof(f)){eof=true;break;}
        else if(count==sizeof(input)){memmove(input,input+1,--count);}
        if(n>0){played=true;if(!emit(pcm,n,info.channels,info.hz,current))break;}
        else vTaskDelay(1);
    }
    fclose(f);if(eof&&!played){error("Can't read this MP3. Choose another track.");return false;}return eof;
}
static void task(void *arg){
    if(nvs_open("pearl",NVS_READWRITE,&prefs)==ESP_OK){prefs_open=true;int32_t v;pearl_state s=pearl_audio_state();if(nvs_get_i32(prefs,"volume",&v)==ESP_OK)s.volume=pearl_volume(v,0,CONFIG_PEARL_MAX_VOLUME);char p[PEARL_PATH];size_t n=sizeof(p);if(nvs_get_str(prefs,"track",p,&n)==ESP_OK)for(unsigned i=0;i<library->track_count;i++)if(!strcmp(p,library->tracks[i].path)){s.track=i;break;}publish(s);}
    xTaskCreate(preference_task,"preferences",3072,NULL,1,NULL);
    esp_err_t e=dac_init();if(e){error("Audio hardware not ready. Check the board variant.");goto stopped;}
    i2s_chan_config_t cfg=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);cfg.dma_desc_num=8;cfg.dma_frame_num=511;cfg.auto_clear=true;
    e=i2s_new_channel(&cfg,&tx,NULL);if(e){error("Can't initialize audio output");goto stopped;}
    i2s_std_config_t std={.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(48000),.slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,I2S_SLOT_MODE_STEREO),.gpio_cfg={.mclk=I2S_GPIO_UNUSED,.bclk=40,.ws=38,.dout=39,.din=I2S_GPIO_UNUSED}};
    e=i2s_channel_init_std_mode(tx,&std);if(e||i2s_channel_enable(tx)){error("Can't start audio output");goto stopped;}
    if(cs_addr<0)gpio_set_level(48,1);
    {pearl_state s=pearl_audio_state();s.ready=true;publish(s);}
    while(!stopping){
        command c;if(xQueueReceive(commands,&c,pdMS_TO_TICKS(20))==pdTRUE)command_apply(c);
        pearl_state s=pearl_audio_state();
        if(detached){if(!detach_signaled){detach_signaled=true;xSemaphoreGive(reload_done);}continue;}
        if(s.track<0||s.paused)continue;
        bool eof=decode(library->tracks[s.track].path,s.track);
        if(eof && service(s.track)){pearl_state now=pearl_audio_state();int next=pearl_next(library,s.track,1);const pearl_album *a=&library->albums[library->tracks[s.track].album];
            now.seconds=0;if(active_collection>=0){const pearl_collection *collection=&library->collections[active_collection];if((unsigned)(collection_position+1)>=collection->count)now.paused=true;else{collection_position++;now.track=collection->tracks[collection_position];playback_epoch++;}}else if(next==(int)a->first){now.paused=true;}else{now.track=next;playback_epoch++;}publish(now);save();}
    }
stopped:
    if(tx)i2s_channel_disable(tx);
    if(cs_addr>=0){cs_write(0x90003,0xef);vTaskDelay(pdMS_TO_TICKS(20));cs_write(0x20000,0xfe);gpio_set_level(41,0);}else if(CONFIG_PEARL_BUTTON_DOWN!=48)gpio_set_level(48,0);
    if(!detached){save();}
    pearl_state s=pearl_audio_state();s.ready=false;s.paused=true;publish(s);vTaskDelete(NULL);
}
void pearl_audio_start(pearl_library *l){library=l;state_lock=xSemaphoreCreateMutex();prefs_lock=xSemaphoreCreateMutex();reload_done=xSemaphoreCreateBinary();commands=xQueueCreate(16,sizeof(command));if(!state_lock||!prefs_lock||!reload_done||!commands){error("Not enough memory for playback");return;}xTaskCreatePinnedToCore(task,"audio",32768,NULL,5,NULL,1);}
void pearl_audio_shutdown(void){send(STOP,0);for(int i=0;i<150&&pearl_audio_state().ready;i++)vTaskDelay(pdMS_TO_TICKS(10));}

bool pearl_audio_detach(void){if(!pearl_audio_state().ready)return false;while(xSemaphoreTake(reload_done,0)==pdTRUE){}send(DETACH,0);if(xSemaphoreTake(reload_done,pdMS_TO_TICKS(5000))!=pdTRUE){pending_library=library;send(ATTACH,0);return false;}xSemaphoreTake(prefs_lock,portMAX_DELAY);return true;}
void pearl_audio_attach(pearl_library *l){pending_library=l;xSemaphoreGive(prefs_lock);send(ATTACH,0);}
