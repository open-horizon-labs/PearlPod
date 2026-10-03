#include "player.h"
#include "lvgl.h"
#include "artwork.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern const lv_img_dsc_t pearl_welcome;
static pearl_library *lib;
static lv_obj_t *body,*heading,*status,*transport,*title,*album_label,*art,*progress,*time_label,*play_label,*volume_label;
static unsigned track_offset;
static int page=-2,last_track=-2; // -2 loading, -1 albums, -3 now playing, >=0 album index
static uint8_t *art_pixels;
static lv_img_dsc_t art_desc;
static QueueHandle_t art_requests,art_results;
static unsigned art_generation;
static char startup_error[120];
typedef struct {char path[PEARL_PATH];unsigned generation;} art_request;
typedef struct {uint8_t *pixels;unsigned generation;} art_result;
static const uint32_t bg=0x101827,cream=0xfff6dc,yellow=0xffd75e,teal=0x56ddc5;
static void render(void);
static lv_obj_t *label(lv_obj_t *parent,const char *text,int x,int y,int w,const lv_font_t *font){lv_obj_t *o=lv_label_create(parent);lv_label_set_text(o,text);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);lv_obj_set_style_text_font(o,font,0);lv_obj_set_style_text_color(o,lv_color_hex(cream),0);return o;}
static lv_obj_t *button(lv_obj_t *parent,const char *text,int x,int y,int w,int h,lv_event_cb_t cb,void *user){
    lv_obj_t *o=lv_btn_create(parent);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_set_style_bg_color(o,lv_color_hex(yellow),0);lv_obj_set_style_radius(o,14,0);lv_obj_set_style_shadow_width(o,0,0);
    lv_obj_t *l=lv_label_create(o);lv_label_set_text(l,text);lv_obj_set_style_text_color(l,lv_color_hex(bg),0);lv_obj_set_style_text_font(l,&lv_font_montserrat_20,0);lv_obj_center(l);
    lv_obj_add_event_cb(o,cb,LV_EVENT_CLICKED,user);return o;
}
static void go_albums(lv_event_t *e){page=-1;render();}
static void go_now(lv_event_t *e){page=-3;render();}
static void open_album(lv_event_t *e){page=(intptr_t)lv_event_get_user_data(e);track_offset=0;render();}
static void track_page(lv_event_t *e){int direction=(intptr_t)lv_event_get_user_data(e);if(direction<0){track_offset=track_offset>=32?track_offset-32:0;}else if(lib&&page>=0&&track_offset+32<lib->albums[page].count){track_offset+=32;}render();}
static void select_track(lv_event_t *e){pearl_audio_play((intptr_t)lv_event_get_user_data(e));page=-3;last_track=-2;render();}
static void play(lv_event_t *e){pearl_audio_toggle();}
static void prev(lv_event_t *e){pearl_audio_step(-1);}static void next(lv_event_t *e){pearl_audio_step(1);}
static void play_album(lv_event_t *e){if(lib&&page>=0){pearl_audio_play(lib->albums[page].first);page=-3;last_track=-2;render();}}
static void art_task(void *arg){
    art_request r;while(1){xQueueReceive(art_requests,&r,portMAX_DELAY);uint8_t *p=NULL;
        if(r.path[0]&&strstr(r.path,".rgb")){FILE *f=fopen(r.path,"rb");if(f){p=heap_caps_malloc(PEARL_ART_SIZE*PEARL_ART_SIZE*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(p&&(fread(p,1,PEARL_ART_SIZE*PEARL_ART_SIZE*2,f)!=PEARL_ART_SIZE*PEARL_ART_SIZE*2||fgetc(f)!=EOF)){free(p);p=NULL;}fclose(f);}}
        if(r.path[0]&&!p)p=pearl_art_pixels(r.path);
        art_result result={p,r.generation};if(xQueueSend(art_results,&result,pdMS_TO_TICKS(50))!=pdTRUE)free(p);
    }
}
static void request_art(const char *path){art_request r={.generation=++art_generation};snprintf(r.path,sizeof(r.path),"%s",path?path:"");xQueueOverwrite(art_requests,&r);}
static void reset_body(void){
    art_generation++;lv_obj_clean(body);title=album_label=art=progress=NULL;
    if(art_pixels){lv_img_cache_invalidate_src(&art_desc);free(art_pixels);art_pixels=NULL;}
    lv_obj_scroll_to_y(body,0,LV_ANIM_OFF);
}
static void row(const char *name,unsigned count,int index,int y,bool is_album){
    lv_obj_t *o=lv_btn_create(body);lv_obj_set_pos(o,0,y);lv_obj_set_size(o,424,64);lv_obj_set_style_bg_color(o,lv_color_hex(0x1d2c40),0);lv_obj_set_style_radius(o,12,0);lv_obj_set_style_shadow_width(o,0,0);lv_obj_set_style_pad_all(o,0,0);
    lv_obj_t *t=label(o,name,14,8,356,&lv_font_montserrat_20);lv_label_set_long_mode(t,LV_LABEL_LONG_DOT);
    char line[64];if(is_album)snprintf(line,sizeof(line),"%u track%s",count,count==1?"":"s");else snprintf(line,sizeof(line),"Tap to play");
    lv_obj_t *sub=label(o,line,14,35,350,&lv_font_montserrat_16);lv_obj_set_style_text_color(sub,lv_color_hex(teal),0);
    lv_obj_add_event_cb(o,is_album?open_album:select_track,LV_EVENT_CLICKED,(void *)(intptr_t)index);
}
static void render(void){
    reset_body();lv_obj_add_flag(transport,LV_OBJ_FLAG_HIDDEN);
    if(page==-1){
        lv_label_set_text(heading,"Listener's albums");
        if(!lib||!lib->album_count){label(body,"Your music goes here.",0,25,420,&lv_font_montserrat_24);label(body,"Add MP3, FLAC or WAV albums to the card's music folder, then restart.",0,80,420,&lv_font_montserrat_20);return;}
        for(unsigned i=0;i<lib->album_count;i++)row(lib->albums[i].title,lib->albums[i].count,i,i*72,true);
    }else if(page>=0&&lib&&(unsigned)page<lib->album_count){
        pearl_album *a=&lib->albums[page];lv_label_set_text(heading,a->title);button(body,"Play album",0,0,424,54,play_album,NULL);
        unsigned count=a->count-track_offset;if(count>32)count=32;int top=64;
        if(a->count>32){button(body,"Previous",0,64,128,54,track_page,(void *)(intptr_t)-1);button(body,"Next",296,64,128,54,track_page,(void *)(intptr_t)1);char range[48];snprintf(range,sizeof(range),"%u-%u",track_offset+1,track_offset+count);label(body,range,148,82,140,&lv_font_montserrat_16);top=128;}
        for(unsigned i=0;i<count;i++)row(lib->tracks[a->first+track_offset+i].title,0,a->first+track_offset+i,top+i*72,false);
    }else if(page==-3){
        lv_label_set_text(heading,"Now playing");lv_obj_clear_flag(transport,LV_OBJ_FLAG_HIDDEN);
        art=lv_img_create(body);lv_img_set_src(art,&pearl_welcome);lv_obj_set_pos(art,92,0);
        title=label(body,"Choose an album",0,248,424,&lv_font_montserrat_24);lv_label_set_long_mode(title,LV_LABEL_LONG_DOT);
        album_label=label(body,"Your music. Your world.",0,280,424,&lv_font_montserrat_16);lv_label_set_long_mode(album_label,LV_LABEL_LONG_DOT);
        last_track=-2;
    }
}
static void tick(lv_timer_t *timer){
    if(!lib)return;
    pearl_state s=pearl_audio_state();char text[160];
    snprintf(text,sizeof(text),"Volume %d%s",s.volume,lib->truncated?"  |  Library limit reached":"");lv_label_set_text(volume_label,text);
    lv_label_set_text(status,s.error[0]?s.error:startup_error);
    if(page==-3&&title){
        if(s.track!=last_track){last_track=s.track;
            if(s.track>=0&&(unsigned)s.track<lib->track_count){pearl_track *t=&lib->tracks[s.track];lv_label_set_text(title,t->title);lv_label_set_text(album_label,lib->albums[t->album].title);lv_img_set_src(art,&pearl_welcome);request_art(lib->albums[t->album].art[0]?lib->albums[t->album].art:t->path);}
        }
        lv_label_set_text(play_label,s.paused?LV_SYMBOL_PLAY:LV_SYMBOL_PAUSE);
        snprintf(text,sizeof(text),"%u:%02u",(unsigned)(s.seconds/60),(unsigned)(s.seconds%60));lv_label_set_text(time_label,text);
    }
    art_result r;while(xQueueReceive(art_results,&r,0)==pdTRUE){
        if(r.generation==art_generation&&art&&r.pixels){
            lv_img_cache_invalidate_src(&art_desc);uint8_t *old=art_pixels;art_pixels=r.pixels;
            art_desc=(lv_img_dsc_t){.header={.cf=LV_IMG_CF_TRUE_COLOR,.w=PEARL_ART_SIZE,.h=PEARL_ART_SIZE},.data_size=PEARL_ART_SIZE*PEARL_ART_SIZE*2,.data=art_pixels};
            lv_img_set_src(art,&art_desc);free(old);
        }else free(r.pixels);
    }
}
void pearl_ui_start(void){
    lv_obj_t *screen=lv_scr_act();lv_obj_set_style_bg_color(screen,lv_color_hex(bg),0);lv_obj_set_style_pad_all(screen,0,0);lv_obj_clear_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
    heading=label(screen,"Hi, Listener!",18,12,424,&lv_font_montserrat_28);lv_label_set_long_mode(heading,LV_LABEL_LONG_DOT);
    body=lv_obj_create(screen);lv_obj_set_pos(body,18,54);lv_obj_set_size(body,424,302);lv_obj_set_style_bg_opa(body,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(body,0,0);lv_obj_set_style_pad_all(body,0,0);lv_obj_set_scroll_dir(body,LV_DIR_VER);
    lv_obj_t *welcome=lv_img_create(body);lv_img_set_src(welcome,&pearl_welcome);lv_obj_set_pos(welcome,92,0);label(body,"Finding your music...",0,256,424,&lv_font_montserrat_20);
    transport=lv_obj_create(screen);lv_obj_set_pos(transport,18,360);lv_obj_set_size(transport,424,54);lv_obj_set_style_bg_opa(transport,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(transport,0,0);lv_obj_set_style_pad_all(transport,0,0);lv_obj_clear_flag(transport,LV_OBJ_FLAG_SCROLLABLE);
    button(transport,LV_SYMBOL_PREV,0,0,74,54,prev,NULL);lv_obj_t *p=button(transport,LV_SYMBOL_PLAY,84,0,74,54,play,NULL);play_label=lv_obj_get_child(p,0);button(transport,LV_SYMBOL_NEXT,168,0,74,54,next,NULL);time_label=label(transport,"0:00",278,15,130,&lv_font_montserrat_20);lv_obj_add_flag(transport,LV_OBJ_FLAG_HIDDEN);
    volume_label=label(screen,"Side buttons: volume",18,420,270,&lv_font_montserrat_16);
    button(screen,"Albums",282,420,78,36,go_albums,NULL);button(screen,"Playing",366,420,78,36,go_now,NULL);
    status=label(screen,"",18,362,424,&lv_font_montserrat_16);lv_obj_set_style_text_color(status,lv_color_hex(yellow),0);
    art_requests=xQueueCreate(1,sizeof(art_request));art_results=xQueueCreate(2,sizeof(art_result));xTaskCreate(art_task,"artwork",16384,NULL,1,NULL);lv_timer_create(tick,150,NULL);
}
void pearl_ui_ready(pearl_library *l,const char *err){lib=l;snprintf(startup_error,sizeof(startup_error),"%s",err?err:"");page=-1;render();if(err&&*err)lv_label_set_text(status,err);
    // Transport labels survive body rebuilds.
    play_label=lv_obj_get_child(lv_obj_get_child(transport,1),0);time_label=lv_obj_get_child(transport,3);
}
