#include "player.h"
#include "lvgl.h"
#include "artwork.h"
#ifdef PEARL_UI_HOST
#include "platform.h"
#else
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern const lv_img_dsc_t pearl_welcome;
#define ALBUM_PAGE 8
#define TRACK_PAGE 32
#define THUMB_SIZE 64
static const uint32_t bg=0x101827,cream=0xfff6dc,yellow=0xffd75e,teal=0x56ddc5,surface=0x1d2c40;
static pearl_library *lib;
static lv_obj_t *body,*heading,*status,*transport,*title,*album_label,*art,*time_label,*play_label,*volume_label,*nav,*back,*page_prev,*page_next,*hint;
static lv_obj_t *thumbs[ALBUM_PAGE],*track_rows[TRACK_PAGE];
static lv_img_dsc_t thumb_desc[ALBUM_PAGE],art_desc;
static uint8_t *thumb_pixels[ALBUM_PAGE],*art_pixels;
static bool thumb_requested[ALBUM_PAGE];
static unsigned album_offset,track_offset,art_generation;
static int page=-2,last_track=-2,last_mark=-2;
static unsigned track_offsets[PEARL_MAX_ALBUMS];
static int album_scroll[PEARL_MAX_ALBUMS/ALBUM_PAGE],track_scroll[PEARL_MAX_ALBUMS];
static QueueHandle_t art_requests,art_results;
static char startup_error[120];
typedef struct {char path[PEARL_PATH];unsigned generation;int slot;} art_request;
typedef struct {uint8_t *pixels;unsigned generation;int slot;} art_result;
static void render(void);
static lv_obj_t *label(lv_obj_t *parent,const char *text,int x,int y,int w,const lv_font_t *font){
    lv_obj_t *o=lv_label_create(parent);lv_label_set_text(o,text);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,font->line_height+2);lv_obj_set_style_text_font(o,font,0);lv_obj_set_style_text_color(o,lv_color_hex(cream),0);lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);return o;
}
static lv_obj_t *button(lv_obj_t *parent,const char *text,int x,int y,int w,int h,lv_event_cb_t cb,void *user){
    lv_obj_t *o=lv_btn_create(parent);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_set_style_bg_color(o,lv_color_hex(yellow),0);lv_obj_set_style_bg_color(o,lv_color_hex(teal),LV_STATE_PRESSED);lv_obj_set_style_radius(o,14,0);lv_obj_set_style_shadow_width(o,0,0);lv_obj_set_style_pad_all(o,0,0);lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *l=label(o,text,0,0,w,&lv_font_montserrat_20);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_color(l,lv_color_hex(bg),0);lv_obj_center(l);lv_obj_add_event_cb(o,cb,LV_EVENT_CLICKED,user);return o;
}
static void save_place(void){if(page==-1)album_scroll[album_offset/ALBUM_PAGE]=lv_obj_get_scroll_y(body);else if(page>=0&&page<PEARL_MAX_ALBUMS){track_scroll[page]=lv_obj_get_scroll_y(body);track_offsets[page]=track_offset;}}
static void navigate(int target){save_place();page=target;render();}
static void go_albums(lv_event_t *e){navigate(-1);}
static void go_now(lv_event_t *e){navigate(-3);}
static void go_back(lv_event_t *e){navigate(-1);}
static void open_album(lv_event_t *e){int target=(intptr_t)lv_event_get_user_data(e);if(target!=page)track_offset=track_offsets[target];navigate(target);}
static void browse_page(int direction){
    save_place();if(page==-1){if(direction<0&&album_offset>=ALBUM_PAGE)album_offset-=ALBUM_PAGE;else if(direction>0&&album_offset+ALBUM_PAGE<lib->album_count)album_offset+=ALBUM_PAGE;else return;}
    else if(page>=0){if(direction<0&&track_offset>=TRACK_PAGE)track_offset-=TRACK_PAGE;else if(direction>0&&track_offset+TRACK_PAGE<lib->albums[page].count)track_offset+=TRACK_PAGE;else return;track_scroll[page]=0;}
    render();
}
static void track_page(lv_event_t *e){browse_page((intptr_t)lv_event_get_user_data(e));}
static void select_track(lv_event_t *e){save_place();pearl_audio_play((intptr_t)lv_event_get_user_data(e));page=-3;render();}
static void play(lv_event_t *e){pearl_audio_toggle();}
static void prev(lv_event_t *e){pearl_audio_step(-1);}
static void next(lv_event_t *e){pearl_audio_step(1);}
static void play_album(lv_event_t *e){if(lib&&page>=0){save_place();pearl_audio_play(lib->albums[page].first);page=-3;render();}}
static void gesture(lv_event_t *e){
    lv_indev_t *input=lv_indev_get_act();if(!input||!lib)return;lv_dir_t dir=lv_indev_get_gesture_dir(input);
    if(dir!=LV_DIR_LEFT&&dir!=LV_DIR_RIGHT)return;
    lv_indev_wait_release(input); // Consume the release so a swipe cannot select a track.
    if(page>=0&&dir==LV_DIR_RIGHT)navigate(-1);
    else if(page==-1)browse_page(dir==LV_DIR_LEFT?1:-1);
    else if(page>=0&&dir==LV_DIR_LEFT)browse_page(1);
    else if(page==-3&&dir==LV_DIR_RIGHT)navigate(-1);
}
static uint8_t *load_art(const art_request *r){
    uint8_t *p=NULL;
        if(r->path[0]&&strstr(r->path,".rgb")){FILE *f=fopen(r->path,"rb");if(f){p=heap_caps_malloc(PEARL_ART_SIZE*PEARL_ART_SIZE*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(p&&(fread(p,1,PEARL_ART_SIZE*PEARL_ART_SIZE*2,f)!=PEARL_ART_SIZE*PEARL_ART_SIZE*2||fgetc(f)!=EOF)){free(p);p=NULL;}fclose(f);}}
        if(r->path[0]&&!p)p=pearl_art_pixels(r->path);
        if(p&&r->slot>=0){uint8_t *small=malloc(THUMB_SIZE*THUMB_SIZE*2);if(small)for(int y=0;y<THUMB_SIZE;y++)for(int x=0;x<THUMB_SIZE;x++){int from=((y*PEARL_ART_SIZE/THUMB_SIZE)*PEARL_ART_SIZE+x*PEARL_ART_SIZE/THUMB_SIZE)*2;memcpy(small+(y*THUMB_SIZE+x)*2,p+from,2);}free(p);p=small;}
        return p;
}
static void art_task(void *arg){
    art_request r;while(1){xQueueReceive(art_requests,&r,portMAX_DELAY);uint8_t *p=load_art(&r);
        art_result result={p,r.generation,r.slot};if(xQueueSend(art_results,&result,pdMS_TO_TICKS(50))!=pdTRUE)free(p);
    }
}
static bool request_art(const char *path,int slot){art_request r={.generation=art_generation,.slot=slot};snprintf(r.path,sizeof(r.path),"%s",path?path:"");return xQueueSend(art_requests,&r,0)==pdTRUE;}
static void reset_body(void){
    art_generation++;art_request pending;while(xQueueReceive(art_requests,&pending,0)==pdTRUE){}lv_obj_clean(body);title=album_label=art=NULL;last_track=last_mark=-2;
    if(art_pixels){lv_img_cache_invalidate_src(&art_desc);free(art_pixels);art_pixels=NULL;}
    for(unsigned i=0;i<ALBUM_PAGE;i++){lv_img_cache_invalidate_src(&thumb_desc[i]);free(thumb_pixels[i]);thumb_pixels[i]=NULL;thumbs[i]=NULL;thumb_requested[i]=false;}
    memset(track_rows,0,sizeof(track_rows));lv_obj_scroll_to_y(body,0,LV_ANIM_OFF);
}
static void row(const char *name,unsigned count,int index,int y,bool is_album){
    int height=is_album?92:72;lv_obj_t *o=lv_btn_create(body);lv_obj_set_pos(o,0,y);lv_obj_set_size(o,424,height);lv_obj_set_style_bg_color(o,lv_color_hex(surface),0);lv_obj_set_style_bg_color(o,lv_color_hex(0x294555),LV_STATE_PRESSED);lv_obj_set_style_radius(o,12,0);lv_obj_set_style_shadow_width(o,0,0);lv_obj_set_style_pad_all(o,0,0);lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    int left=52;if(is_album){left=92;unsigned slot=index-album_offset;thumbs[slot]=lv_img_create(o);lv_img_set_src(thumbs[slot],&pearl_welcome);lv_img_set_zoom(thumbs[slot],THUMB_SIZE*256/PEARL_ART_SIZE);lv_img_set_pivot(thumbs[slot],0,0);lv_obj_set_pos(thumbs[slot],14,14);}
    else {char number[12];snprintf(number,sizeof(number),"%u",(unsigned)(index-lib->albums[page].first+1));label(o,number,12,24,36,&lv_font_montserrat_16);track_rows[index-lib->albums[page].first-track_offset]=o;}
    lv_obj_t *t=label(o,name,left,is_album?18:10,424-left-12,&lv_font_montserrat_20);
    char line[64];if(is_album){pearl_state current=pearl_audio_state();bool active=current.track>=0&&(unsigned)current.track<lib->track_count&&lib->tracks[current.track].album==(unsigned)index;snprintf(line,sizeof(line),"%u track%s%s",count,count==1?"":"s",active?"  /  Selected":"");}else snprintf(line,sizeof(line),"%s",index==pearl_audio_state().track?"Playing now":"");
    lv_obj_t *sub=label(o,line,left,is_album?52:40,424-left-12,&lv_font_montserrat_16);lv_obj_set_style_text_color(sub,lv_color_hex(teal),0);
    (void)t;lv_obj_add_event_cb(o,is_album?open_album:select_track,LV_EVENT_CLICKED,(void *)(intptr_t)index);
}
static void render(void){
    reset_body();lv_obj_add_flag(transport,LV_OBJ_FLAG_HIDDEN);lv_obj_clear_flag(nav,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(back,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(page_prev,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(page_next,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(volume_label,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(heading,18,12);lv_obj_set_width(heading,268);lv_obj_set_pos(body,18,60);lv_obj_set_size(body,424,304);lv_obj_clear_flag(body,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(body,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(status,18,367);lv_obj_set_size(status,424,20);lv_obj_clear_flag(hint,LV_OBJ_FLAG_HIDDEN);
    unsigned count=0;bool has_pages=false;
    if(page==-1){
        char text[64];snprintf(text,sizeof(text),"Listener's albums");lv_label_set_text(heading,text);lv_label_set_text(hint,lib&&lib->album_count>3?"Swipe up to explore":"Choose your next adventure");
        if(!lib||!lib->album_count){label(body,"Your music goes here.",0,25,420,&lv_font_montserrat_24);lv_obj_t *help=label(body,"Add albums to the card's music folder, then restart.",0,80,420,&lv_font_montserrat_20);lv_obj_set_height(help,100);lv_label_set_long_mode(help,LV_LABEL_LONG_WRAP);return;}
        count=lib->album_count-album_offset;if(count>ALBUM_PAGE)count=ALBUM_PAGE;for(unsigned i=0;i<count;i++)row(lib->albums[album_offset+i].title,lib->albums[album_offset+i].count,album_offset+i,i*100,true);
        has_pages=lib->album_count>ALBUM_PAGE;lv_obj_scroll_to_y(body,album_scroll[album_offset/ALBUM_PAGE],LV_ANIM_OFF);
        if(has_pages)lv_label_set_text(hint,"Swipe left / right for more albums");
    }else if(page>=0&&lib&&(unsigned)page<lib->album_count){
        pearl_album *a=&lib->albums[page];lv_obj_clear_flag(back,LV_OBJ_FLAG_HIDDEN);lv_obj_set_pos(heading,96,12);lv_obj_set_width(heading,194);lv_label_set_text(heading,a->title);button(body,LV_SYMBOL_PLAY "  Play album",0,0,424,64,play_album,NULL);
        count=a->count-track_offset;if(count>TRACK_PAGE)count=TRACK_PAGE;for(unsigned i=0;i<count;i++)row(lib->tracks[a->first+track_offset+i].title,0,a->first+track_offset+i,76+i*80,false);
        has_pages=a->count>TRACK_PAGE;lv_label_set_text(hint,"Swipe right to return to albums");lv_obj_scroll_to_y(body,track_scroll[page],LV_ANIM_OFF);
    }else if(page==-3){
        lv_obj_add_flag(nav,LV_OBJ_FLAG_HIDDEN);lv_obj_clear_flag(back,LV_OBJ_FLAG_HIDDEN);lv_obj_clear_flag(transport,LV_OBJ_FLAG_HIDDEN);lv_obj_clear_flag(volume_label,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(hint,LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(heading,96,14);lv_obj_set_width(heading,244);lv_label_set_text(heading,"Now playing");lv_obj_set_size(body,424,302);lv_obj_clear_flag(body,LV_OBJ_FLAG_SCROLLABLE);
        art=lv_img_create(body);lv_img_set_src(art,&pearl_welcome);lv_obj_set_pos(art,92,0);
        title=label(body,"Choose an album",0,248,424,&lv_font_montserrat_24);lv_label_set_long_mode(title,LV_LABEL_LONG_SCROLL_CIRCULAR);lv_obj_set_style_anim_speed(title,28,0);lv_obj_set_style_text_align(title,LV_TEXT_ALIGN_CENTER,0);
        album_label=label(body,"Your music. Your world.",0,278,328,&lv_font_montserrat_16);lv_label_set_long_mode(album_label,LV_LABEL_LONG_SCROLL_CIRCULAR);lv_obj_set_style_anim_speed(album_label,24,0);
        lv_obj_set_pos(status,18,358);lv_obj_set_size(status,424,18);
    }
    if(has_pages){lv_obj_clear_flag(page_prev,LV_OBJ_FLAG_HIDDEN);lv_obj_clear_flag(page_next,LV_OBJ_FLAG_HIDDEN);bool before=page==-1?album_offset>0:track_offset>0;bool after=page==-1?album_offset+count<lib->album_count:track_offset+count<lib->albums[page].count;
        if(before)lv_obj_clear_state(page_prev,LV_STATE_DISABLED);else lv_obj_add_state(page_prev,LV_STATE_DISABLED);if(after)lv_obj_clear_state(page_next,LV_STATE_DISABLED);else lv_obj_add_state(page_next,LV_STATE_DISABLED);
    }else if(page!=-3)lv_obj_set_width(heading,page>=0?348:424);
    // The album route is visibly selected only while browsing the album collection.
    lv_obj_set_style_bg_color(lv_obj_get_child(nav,0),lv_color_hex(page==-1?teal:yellow),0);
}
static void tick(lv_timer_t *timer){
    if(!lib)return;
    pearl_state s=pearl_audio_state();char text[160];snprintf(text,sizeof(text),"Vol %d",s.volume);lv_label_set_text(volume_label,text);
    lv_label_set_text(status,s.error[0]?s.error:startup_error);bool error=s.error[0]||startup_error[0];if(error)lv_obj_add_flag(hint,LV_OBJ_FLAG_HIDDEN);else if(page!=-3)lv_obj_clear_flag(hint,LV_OBJ_FLAG_HIDDEN);
    if(page==-3&&title){
        if(s.track!=last_track){last_track=s.track;art_generation++;art_request pending;while(xQueueReceive(art_requests,&pending,0)==pdTRUE){}lv_img_cache_invalidate_src(&art_desc);free(art_pixels);art_pixels=NULL;if(s.track>=0&&(unsigned)s.track<lib->track_count){pearl_track *t=&lib->tracks[s.track];lv_label_set_text(title,t->title);lv_label_set_text(album_label,lib->albums[t->album].title);lv_img_set_src(art,&pearl_welcome);request_art(lib->albums[t->album].art[0]?lib->albums[t->album].art:t->path,-1);}}
        lv_label_set_text(play_label,s.paused?LV_SYMBOL_PLAY:LV_SYMBOL_PAUSE);lv_obj_set_style_bg_color(lv_obj_get_parent(play_label),lv_color_hex(s.paused?yellow:teal),0);lv_label_set_text(heading,s.track<0?"Your music":s.paused?"Paused":"Now playing");
        snprintf(text,sizeof(text),"%u:%02u",(unsigned)(s.seconds/60),(unsigned)(s.seconds%60));lv_label_set_text(time_label,text);lv_obj_clear_flag(time_label,LV_OBJ_FLAG_HIDDEN);
    }else lv_obj_add_flag(time_label,LV_OBJ_FLAG_HIDDEN);
    if(page>=0&&s.track!=last_mark){last_mark=s.track;pearl_album *a=&lib->albums[page];for(unsigned i=0;i<TRACK_PAGE&&track_rows[i];i++){bool active=a->first+track_offset+i==(unsigned)s.track;lv_obj_set_style_bg_color(track_rows[i],lv_color_hex(active?0x284b50:surface),0);lv_label_set_text(lv_obj_get_child(track_rows[i],2),active?"Selected track":"");}}
    if(page==-1){lv_area_t area;lv_obj_get_coords(body,&area);for(unsigned i=0;i<ALBUM_PAGE&&thumbs[i];i++){lv_area_t bounds;lv_obj_get_coords(lv_obj_get_parent(thumbs[i]),&bounds);if(!thumb_requested[i]&&bounds.y2>=area.y1&&bounds.y1<=area.y2){pearl_album *a=&lib->albums[album_offset+i];thumb_requested[i]=request_art(a->art[0]?a->art:lib->tracks[a->first].path,i);}}}
    art_result r;while(xQueueReceive(art_results,&r,0)==pdTRUE){
        if(r.generation==art_generation&&r.pixels&&r.slot<0&&art){lv_img_cache_invalidate_src(&art_desc);uint8_t *old=art_pixels;art_pixels=r.pixels;art_desc=(lv_img_dsc_t){.header={.cf=LV_IMG_CF_TRUE_COLOR,.w=PEARL_ART_SIZE,.h=PEARL_ART_SIZE},.data_size=PEARL_ART_SIZE*PEARL_ART_SIZE*2,.data=art_pixels};lv_img_set_src(art,&art_desc);free(old);}
        else if(r.generation==art_generation&&r.pixels&&r.slot>=0&&r.slot<ALBUM_PAGE&&thumbs[r.slot]){int i=r.slot;thumb_pixels[i]=r.pixels;thumb_desc[i]=(lv_img_dsc_t){.header={.cf=LV_IMG_CF_TRUE_COLOR,.w=THUMB_SIZE,.h=THUMB_SIZE},.data_size=THUMB_SIZE*THUMB_SIZE*2,.data=r.pixels};lv_img_set_zoom(thumbs[i],256);lv_img_set_src(thumbs[i],&thumb_desc[i]);}
        else free(r.pixels);
    }
}
void pearl_ui_start(void){
    lv_obj_t *screen=lv_scr_act();lv_obj_set_style_bg_color(screen,lv_color_hex(bg),0);lv_obj_set_style_pad_all(screen,0,0);lv_obj_clear_flag(screen,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_event_cb(screen,gesture,LV_EVENT_GESTURE,NULL);
    heading=label(screen,"Hi, Listener!",18,12,424,&lv_font_montserrat_28);
    body=lv_obj_create(screen);lv_obj_set_pos(body,18,60);lv_obj_set_size(body,424,304);lv_obj_set_style_bg_opa(body,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(body,0,0);lv_obj_set_style_pad_all(body,0,0);lv_obj_set_scroll_dir(body,LV_DIR_VER);
    lv_obj_t *welcome=lv_img_create(body);lv_img_set_src(welcome,&pearl_welcome);lv_obj_set_pos(welcome,92,0);label(body,"Finding your music...",0,256,424,&lv_font_montserrat_20);
    back=button(screen,LV_SYMBOL_LEFT,18,4,64,52,go_back,NULL);
    page_prev=button(screen,LV_SYMBOL_LEFT,298,4,64,52,track_page,(void *)(intptr_t)-1);page_next=button(screen,LV_SYMBOL_RIGHT,378,4,64,52,track_page,(void *)(intptr_t)1);
    transport=lv_obj_create(screen);lv_obj_set_pos(transport,18,378);lv_obj_set_size(transport,424,76);lv_obj_set_style_bg_opa(transport,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(transport,0,0);lv_obj_set_style_pad_all(transport,0,0);lv_obj_clear_flag(transport,LV_OBJ_FLAG_SCROLLABLE);
    button(transport,LV_SYMBOL_PREV,0,4,112,68,prev,NULL);lv_obj_t *p=button(transport,LV_SYMBOL_PLAY,128,0,168,76,play,NULL);play_label=lv_obj_get_child(p,0);lv_obj_set_style_text_font(play_label,&lv_font_montserrat_28,0);button(transport,LV_SYMBOL_NEXT,312,4,112,68,next,NULL);
    time_label=label(screen,"0:00",356,338,86,&lv_font_montserrat_16);lv_obj_set_style_text_align(time_label,LV_TEXT_ALIGN_RIGHT,0);volume_label=label(screen,"Vol 20",356,18,86,&lv_font_montserrat_16);lv_obj_set_style_text_align(volume_label,LV_TEXT_ALIGN_RIGHT,0);
    nav=lv_obj_create(screen);lv_obj_set_pos(nav,18,390);lv_obj_set_size(nav,424,64);lv_obj_set_style_bg_opa(nav,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(nav,0,0);lv_obj_set_style_pad_all(nav,0,0);lv_obj_clear_flag(nav,LV_OBJ_FLAG_SCROLLABLE);button(nav,LV_SYMBOL_LIST "  Albums",0,0,204,64,go_albums,NULL);button(nav,LV_SYMBOL_AUDIO "  Playing",220,0,204,64,go_now,NULL);
    hint=label(screen,"",18,367,424,&lv_font_montserrat_16);lv_obj_set_style_text_color(hint,lv_color_hex(teal),0);status=label(screen,"",18,367,424,&lv_font_montserrat_16);lv_obj_set_style_text_color(status,lv_color_hex(yellow),0);
    lv_obj_add_flag(back,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(page_prev,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(page_next,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(transport,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(time_label,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(volume_label,LV_OBJ_FLAG_HIDDEN);
    art_requests=xQueueCreate(ALBUM_PAGE+1,sizeof(art_request));art_results=xQueueCreate(ALBUM_PAGE+1,sizeof(art_result));xTaskCreate(art_task,"artwork",16384,NULL,1,NULL);lv_timer_create(tick,150,NULL);
}
void pearl_ui_ready(pearl_library *l,const char *err){lib=l;snprintf(startup_error,sizeof(startup_error),"%s",err?err:"");page=-1;render();}
