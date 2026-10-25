#include "player.h"
#include "media.h"
#include <limits.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/i2s_std.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_attr.h"
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
#include "minimp3_ex.h"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_SIMD
#include "dr_flac.h"
#include "codec_flac.h"
#include "audio_volume.h"
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

static pearl_library *library;
static QueueHandle_t commands;
static SemaphoreHandle_t state_lock;
static pearl_state state={.track=-1,.volume=20,.paused=true};
static i2s_chan_handle_t tx;
static int cs_addr=-1;
static atomic_bool stopping,detached,worker_running,preferences_running;
static bool detach_signaled;
static SemaphoreHandle_t reload_done;
static pearl_library *pending_library;
static int active_collection=-1,collection_position;
static unsigned playback_epoch,decode_epoch;
static nvs_handle_t prefs;
static bool prefs_open;
static int persisted_track=-2,persisted_volume=-2;
static SemaphoreHandle_t prefs_lock;
typedef struct {int kind,value,position;} command;
enum {PLAY,TOGGLE,STEP,VOLUME,STOP,COLLECTION,DETACH,ATTACH,SEEK,SEEK_TO,CHAPTER};
static void send(int kind,int value){if(commands){command c={.kind=kind,.value=value};if(xQueueSend(commands,&c,0)!=pdTRUE)ESP_LOGW("player","command queue full");}}
void pearl_audio_play(int i){send(PLAY,i);}
void pearl_audio_seek(int seconds){send(SEEK,seconds);}
void pearl_audio_seek_to(uint32_t milliseconds){send(SEEK_TO,(int)milliseconds);}
void pearl_audio_chapter(unsigned chapter){send(CHAPTER,(int)chapter);}
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
        {0x90001,0},{0x90002,0},{0x90003,0xec},{0x90004,0}, // unity PCM volume
#if CONFIG_PEARL_HIGH_IMPEDANCE_OUTPUT
        {0xb0000,0x1e},{0x80000,0x31}, // 2 Vrms, >=600 ohm loads and VP >=3.3 V only
#else
        {0xb0000,0x1c},{0x80000,0x20}, // 1.41 Vrms, full headphone load range
#endif
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
static bool save(void){
    if(!prefs_lock)return true;
    xSemaphoreTake(prefs_lock,portMAX_DELAY);
    if(detached||!library){xSemaphoreGive(prefs_lock);return true;}
    pearl_state s=pearl_audio_state();
    bool ok=pearl_media_progress_save(library,s.track,s.milliseconds,s.completed);
    if(prefs_open&&(persisted_track!=s.track||persisted_volume!=s.volume)){nvs_set_i32(prefs,"volume",s.volume);if(s.track>=0&&(unsigned)s.track<library->track_count)nvs_set_str(prefs,"track",library->tracks[s.track].path);if(nvs_commit(prefs)!=ESP_OK)ok=false;else{persisted_track=s.track;persisted_volume=s.volume;}}
    xSemaphoreGive(prefs_lock);
    if(!ok)error("Cannot save listening place. Check card space.");
    return ok;
}
/* Select/resume is explicit. Boot and library replacement stay paused. */
static void select_position(pearl_state *s,int track,bool resume,bool play) {
    if(!pearl_track_playable(library,track))track=-1;
    s->track=track;s->seconds=s->milliseconds=0;s->duration_ms=0;s->completed=false;s->seeking=false;s->spoken=false;s->paused=track<0||!play;s->error[0]=0;
    const media_item *item=pearl_media_item(library,track);
    if(item){s->spoken=media_kind_resumes(item->kind);s->duration_ms=item->duration_ms;uint32_t position=pearl_media_start(library,track);media_progress saved;
        if(resume&&media_kind_resumes(item->kind)&&pearl_media_progress_load(library,track,&saved)) {
            position=saved.position_ms>item->duration_ms?item->duration_ms:saved.position_ms;s->completed=saved.completed;
            if(play&&s->completed){position=0;s->completed=false;}
        }
        uint32_t offset=0;int located=pearl_media_locate(library,track,position,&offset);
        if(located>=0)s->track=located;else{s->track=-1;s->paused=true;snprintf(s->error,sizeof(s->error),"This part is not on the card. Sync again.");}
        s->milliseconds=position;s->seconds=position/1000;
    }
    playback_epoch++;
}
static void seek_position(pearl_state *s,uint32_t position) {
    if(!pearl_track_playable(library,s->track))return;
    if(s->duration_ms&&position>=s->duration_ms){position=s->duration_ms;s->completed=true;s->paused=true;}else s->completed=false;
    uint32_t offset=0;int track=pearl_media_locate(library,s->track,position,&offset);
    if(track<0){snprintf(s->error,sizeof(s->error),"This part is not on the card");s->paused=true;return;}
    s->track=track;s->milliseconds=position;s->seconds=position/1000;s->seeking=!s->paused;playback_epoch++;
}
static void preference_task(void *arg){
    pearl_state last={.track=-99,.volume=-99};unsigned ticks=0;
    while(!stopping){
        vTaskDelay(pdMS_TO_TICKS(100));
        if(++ticks<30)continue;
        ticks=0;pearl_state s=pearl_audio_state();
        if(s.track!=last.track||s.volume!=last.volume||s.paused!=last.paused||s.completed!=last.completed||(s.spoken&&s.milliseconds/5000!=last.milliseconds/5000)){save();last=s;}
    }
    preferences_running=false;vTaskDelete(NULL);
}
static void command_apply(command c){
    pearl_state s=pearl_audio_state();
    if(detached&&c.kind!=ATTACH&&c.kind!=STOP&&c.kind!=VOLUME)return;
    if((c.kind==PLAY||c.kind==COLLECTION||c.kind==STEP)&&!save())return;
    if(c.kind==PLAY && pearl_track_playable(library,c.value)){active_collection=-1;select_position(&s,c.value,true,true);}
    else if(c.kind==COLLECTION){unsigned index=c.value,position=c.position;if(index<library->collection_count&&position<library->collections[index].count){active_collection=index;collection_position=position;select_position(&s,library->collections[index].tracks[position],true,true);for(unsigned n=0;n<library->collections[index].count;n++)if(library->collections[index].tracks[n]==(unsigned)s.track)collection_position=n;}}
    else if(c.kind==DETACH){if(!save())return;detached=true;detach_signaled=false;playback_epoch++;s.paused=true;}
    else if(c.kind==ATTACH){xSemaphoreTake(prefs_lock,portMAX_DELAY);library=pending_library;active_collection=-1;select_position(&s,-1,false,false);char path[PEARL_PATH];size_t size=sizeof(path);if(prefs_open&&nvs_get_str(prefs,"track",path,&size)==ESP_OK)for(unsigned i=0;i<library->track_count;i++)if(pearl_track_playable(library,i)&&!strcmp(path,library->tracks[i].path)){select_position(&s,i,true,false);break;}publish(s);detached=false;xSemaphoreGive(prefs_lock);}
    else if(c.kind==TOGGLE){if(s.track<0&&library->track_count)select_position(&s,pearl_next(library,-1,1),true,false);if(s.track<0){publish(s);return;}if(s.completed)seek_position(&s,0);s.paused=!s.paused;}
    else if(c.kind==STEP){int track;if(active_collection>=0){collection_position=pearl_collection_step(library,active_collection,collection_position,c.value);track=collection_position>=0?(int)library->collections[active_collection].tracks[collection_position]:-1;}else track=pearl_next(library,s.track,c.value);select_position(&s,track,true,!s.paused);}
    else if(c.kind==SEEK){int64_t target=(int64_t)s.milliseconds+(int64_t)c.value*1000;seek_position(&s,target<0?0:target>UINT32_MAX?UINT32_MAX:(uint32_t)target);}
    else if(c.kind==SEEK_TO)seek_position(&s,(uint32_t)c.value);
    else if(c.kind==CHAPTER){const media_item *item=pearl_media_item(library,s.track);if(item&&c.value>=0&&(unsigned)c.value<item->chapter_count)seek_position(&s,item->chapters[c.value].start_ms);}
    else if(c.kind==VOLUME)s.volume=pearl_volume(s.volume,c.value,PEARL_VOLUME_MAX);
    else if(c.kind==STOP){s.paused=true;stopping=true;}
    publish(s);
    if(c.kind==TOGGLE||c.kind==SEEK||c.kind==SEEK_TO||c.kind==CHAPTER||c.kind==STOP)save();
}
static bool service(int current){command c;while(xQueueReceive(commands,&c,0)==pdTRUE)command_apply(c);pearl_state s=pearl_audio_state();return !stopping && !detached && s.track==current && decode_epoch==playback_epoch;}
/* CPU sample buffers are not I2S DMA buffers: i2s_channel_write copies them.
 * Keep scarce internal RAM available for WiFi and direct SD DMA batches. */
static EXT_RAM_BSS_ATTR int32_t output[4096];
static EXT_RAM_BSS_ATTR int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
static double phase;
static int16_t previous[2];
static bool have_previous;
static uint64_t frames_written;
static uint32_t timeline_start;
static bool emit(const int16_t *samples,size_t frames,unsigned channels,unsigned rate,int current){
    if(!rate||rate>192000||!channels||channels>2){error("Unsupported audio format");return false;}
    pearl_state initial=pearl_audio_state();
    float gain=pearl_volume_gain(initial.volume);
    size_t used=0;double step=(double)rate/48000.0;
    for(size_t i=0;i<frames;i++){
        int16_t l=samples[i*channels],r=channels==2?samples[i*channels+1]:l;
        if(!have_previous){previous[0]=l;previous[1]=r;have_previous=true;}
        while(phase<1.0){
            output[used++]=pearl_volume_slot(previous[0]+(l-previous[0])*phase,gain);
            output[used++]=pearl_volume_slot(previous[1]+(r-previous[1])*phase,gain);
            phase+=step;frames_written++;
            if(used==4096){size_t bytes=0;if(i2s_channel_write(tx,output,used*sizeof(*output),&bytes,1000)!=ESP_OK||bytes!=used*sizeof(*output)){error("Audio output interrupted");return false;}used=0;if(!service(current))return false;}
        }
        phase-=1.0;previous[0]=l;previous[1]=r;
    }
    if(used){size_t bytes=0;if(i2s_channel_write(tx,output,used*sizeof(*output),&bytes,1000)!=ESP_OK||bytes!=used*sizeof(*output)){error("Audio output interrupted");return false;}}
    pearl_state s=pearl_audio_state();s.milliseconds=timeline_start+frames_written/48;s.seconds=s.milliseconds/1000;publish(s);return true;
}
static bool wait_playing(int current){
    while(service(current)){
        if(!pearl_audio_state().paused)return true;
        // DMA silence prevents replay of the last buffer during pause.
        int32_t silence[256]={0};size_t done;i2s_channel_write(tx,silence,sizeof(silence),&done,100);
        vTaskDelay(pdMS_TO_TICKS(15));
    }return false;
}
typedef struct {FILE *file;int current;bool canceled;} mp3_source;
static size_t read_mp3(void *buffer,size_t size,void *context){mp3_source *source=context;size_t total=0;while(total<size){if(!service(source->current)){source->canceled=true;break;}size_t chunk=size-total;if(chunk>16384)chunk=16384;size_t n=fread((char *)buffer+total,1,chunk,source->file);total+=n;if(n<chunk)break;}return total;}
static int seek_mp3(uint64_t offset,void *context){mp3_source *source=context;return offset>LONG_MAX?-1:fseek(source->file,(long)offset,SEEK_SET);}
static bool decode(const char *path,int current){
    decode_epoch=playback_epoch;
    const char *ext=strrchr(path,'.');phase=0;have_previous=false;
    pearl_state initial=pearl_audio_state();timeline_start=pearl_media_start(library,current);
    uint32_t offset=initial.milliseconds>timeline_start?initial.milliseconds-timeline_start:0;
    frames_written=(uint64_t)offset*48;
    initial.seeking=offset!=0;publish(initial);
    if(ext&&!strcasecmp(ext,".flac")){
        pearl_flac_stream stream;drflac *f=pearl_flac_open(path,&stream);if(!f){error("Can't read this FLAC. Choose another track.");return false;}
        if(f->channels>2||f->sampleRate>192000){pearl_flac_close(f,&stream);error("Only mono or stereo FLAC is supported");return false;}
        uint64_t decoded=(uint64_t)offset*f->sampleRate/1000;
        if(offset&&!drflac_seek_to_pcm_frame(f,decoded)){pearl_flac_close(f,&stream);error("Cannot seek this FLAC");return false;}
        initial.seeking=false;publish(initial);bool eof=false;while(wait_playing(current)){size_t n=drflac_read_pcm_frames_s16(f,1024,pcm);if(!n){if(!decoded||(f->totalPCMFrameCount&&decoded<f->totalPCMFrameCount))error("Damaged FLAC. Choose another track.");else eof=true;break;}decoded+=n;if(!emit(pcm,n,f->channels,f->sampleRate,current))break;}
        pearl_flac_close(f,&stream);return eof;
    }
    if(ext&&!strcasecmp(ext,".wav")){
        drwav f;if(!drwav_init_file(&f,path,NULL)){error("Can't read this WAV. Choose another track.");return false;}
        if(f.channels>2||f.sampleRate>192000){drwav_uninit(&f);error("Only mono or stereo WAV is supported");return false;}
        if(offset&&!drwav_seek_to_pcm_frame(&f,(uint64_t)offset*f.sampleRate/1000)){drwav_uninit(&f);error("Cannot seek this WAV");return false;}
        initial.seeking=false;publish(initial);
        bool eof=false;while(wait_playing(current)){size_t n=drwav_read_pcm_frames_s16(&f,1024,pcm);if(!n){eof=true;break;}if(!emit(pcm,n,f.channels,f.sampleRate,current))break;}
        drwav_uninit(&f);return eof;
    }
    FILE *f=fopen(path,"rb");if(!f){error("Track missing. Check the card.");return false;}
    mp3dec_ex_t *decoder=calloc(1,sizeof(*decoder));if(!decoder){fclose(f);error("Not enough memory to seek MP3");return false;}
    mp3_source source={.file=f,.current=current};mp3dec_io_t io={.read=read_mp3,.read_data=&source,.seek=seek_mp3,.seek_data=&source};
    bool opened=mp3dec_ex_open_cb(decoder,&io,MP3D_SEEK_TO_SAMPLE)==0;
    if(!opened||decoder->info.hz<8000||decoder->info.hz>192000||decoder->info.channels<1||decoder->info.channels>2){mp3dec_ex_close(decoder);free(decoder);fclose(f);if(!source.canceled)error("Can't read this MP3");return false;}
    uint64_t sample=(uint64_t)offset*decoder->info.hz*decoder->info.channels/1000;
    if(mp3dec_ex_seek(decoder,sample)){mp3dec_ex_close(decoder);free(decoder);fclose(f);if(!source.canceled)error("Cannot resume this MP3");return false;}
    initial=pearl_audio_state();initial.seeking=false;
    if(!initial.duration_ms&&decoder->samples)initial.duration_ms=decoder->samples*1000/decoder->info.hz/decoder->info.channels;
    publish(initial);
    bool eof=false;
    while(wait_playing(current)) {
        size_t samples=mp3dec_ex_read(decoder,pcm,MINIMP3_MAX_SAMPLES_PER_FRAME);
        if(source.canceled)break;
        if(!samples){if(decoder->last_error||ferror(f))error("Card read failed. Try Play again.");else eof=true;break;}
        if(!emit(pcm,samples/decoder->info.channels,decoder->info.channels,decoder->info.hz,current))break;
    }
    mp3dec_ex_close(decoder);free(decoder);fclose(f);return eof;
}
static void task(void *arg){
    if(nvs_open("pearl",NVS_READWRITE,&prefs)==ESP_OK){prefs_open=true;int32_t v;pearl_state s=pearl_audio_state();if(nvs_get_i32(prefs,"volume",&v)==ESP_OK)s.volume=pearl_volume(v,0,PEARL_VOLUME_MAX);char p[PEARL_PATH];size_t n=sizeof(p);if(nvs_get_str(prefs,"track",p,&n)==ESP_OK)for(unsigned i=0;i<library->track_count;i++)if(pearl_track_playable(library,i)&&!strcmp(p,library->tracks[i].path)){select_position(&s,i,true,false);break;}publish(s);}
    preferences_running=true;
    if(xTaskCreate(preference_task,"preferences",6144,NULL,1,NULL)!=pdPASS)preferences_running=false;
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
        if(eof && service(s.track)){
            pearl_state now=pearl_audio_state();const media_item *item=pearl_media_item(library,s.track);
            if(item&&media_kind_resumes(item->kind)) {
                const media_representation *r=media_item_representation(item,"mp3");unsigned part=library->tracks[s.track].media_segment;
                if(r&&part+1<r->count){seek_position(&now,r->segments[part+1].start_ms);now.paused=false;}
                else{now.milliseconds=item->duration_ms;now.seconds=now.milliseconds/1000;now.completed=true;now.paused=true;}
            } else {
                int next=pearl_next(library,s.track,1);const pearl_album *album=&library->albums[library->tracks[s.track].album];
                if(active_collection>=0){const pearl_collection *collection=&library->collections[active_collection];if((unsigned)(collection_position+1)>=collection->count)now.paused=true;else{collection_position++;select_position(&now,collection->tracks[collection_position],false,true);}}
                else if(next<0||next<=s.track||next==(int)album->first)now.paused=true;
                else select_position(&now,next,false,true);
            }
            publish(now);save();
        }
    }
stopped:
    stopping=true;
    if(tx){i2s_channel_disable(tx);i2s_del_channel(tx);tx=NULL;}
    if(cs_addr>=0){cs_write(0x90003,0xef);vTaskDelay(pdMS_TO_TICKS(20));cs_write(0x20000,0xfe);gpio_set_level(41,0);}else if(CONFIG_PEARL_BUTTON_DOWN!=48)gpio_set_level(48,0);
    if(!detached){save();}
    while(atomic_load(&preferences_running))vTaskDelay(pdMS_TO_TICKS(10));
    if(prefs_open){nvs_close(prefs);prefs_open=false;}
    pearl_state s=pearl_audio_state();s.ready=false;s.paused=true;publish(s);
    worker_running=false;vTaskDelete(NULL);
}
void pearl_audio_start(pearl_library *l){
    if(atomic_load(&worker_running))return;
    library=l;
    if(!state_lock)state_lock=xSemaphoreCreateMutex();
    if(!prefs_lock)prefs_lock=xSemaphoreCreateMutex();
    if(!reload_done)reload_done=xSemaphoreCreateBinary();
    if(!commands)commands=xQueueCreate(16,sizeof(command));
    if(!state_lock||!prefs_lock||!reload_done||!commands){error("Not enough memory for playback");return;}
    xQueueReset(commands);stopping=detached=false;detach_signaled=false;active_collection=-1;
    pearl_state s=pearl_audio_state();s.ready=false;select_position(&s,-1,false,false);persisted_track=persisted_volume=-2;publish(s);
    worker_running=true;
    if(xTaskCreatePinnedToCore(task,"audio",32768,NULL,5,NULL,1)!=pdPASS){worker_running=false;error("Not enough memory for playback task");}
}
void pearl_audio_shutdown(void){
    if(!atomic_load(&worker_running))return;
    command c={.kind=STOP};
    if(xQueueSendToFront(commands,&c,pdMS_TO_TICKS(100))!=pdTRUE){ESP_LOGW("player","Could not stop playback: command queue full");return;}
    for(unsigned i=0;i<500&&atomic_load(&worker_running);i++)vTaskDelay(pdMS_TO_TICKS(10));
}
bool pearl_audio_release_for_sync(void){
    if(!pearl_audio_state().paused)return false;
    pearl_audio_shutdown();
    if(atomic_load(&worker_running)||atomic_load(&preferences_running))return false;
    /* Deleted-task stacks are reclaimed by the idle tasks. */
    vTaskDelay(pdMS_TO_TICKS(20));
    return true;
}
bool pearl_audio_restore_from_sync(void){
    pearl_audio_start(library);
    for(unsigned i=0;i<400&&atomic_load(&worker_running)&&!pearl_audio_state().ready;i++)vTaskDelay(pdMS_TO_TICKS(10));
    return pearl_audio_state().ready;
}

static bool detach_command(void){if(!pearl_audio_state().ready)return false;while(xSemaphoreTake(reload_done,0)==pdTRUE){}send(DETACH,0);if(xSemaphoreTake(reload_done,pdMS_TO_TICKS(5000))!=pdTRUE){pending_library=library;send(ATTACH,0);return false;}return true;}
bool pearl_audio_detach(void){if(!detach_command())return false;xSemaphoreTake(prefs_lock,portMAX_DELAY);return true;}
void pearl_audio_attach(pearl_library *l){pending_library=l;xSemaphoreGive(prefs_lock);send(ATTACH,0);}
