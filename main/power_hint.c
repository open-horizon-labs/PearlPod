#include "power_hint.h"
#include "lvgl.h"
static lv_obj_t *panel,*bar,*label;
static lv_timer_t *expiry;
static void expire(lv_timer_t *timer){(void)timer;pearl_power_hint_hide();}
void pearl_power_hint_hide(void){
    if(panel)lv_obj_add_flag(panel,LV_OBJ_FLAG_HIDDEN);
    if(expiry)lv_timer_pause(expiry);
}
static void dismiss(lv_event_t *event){(void)event;pearl_power_hint_hide();}
static void show(bool held,unsigned percent,bool waking){
    if(!panel){
        panel=lv_obj_create(lv_layer_top());
        lv_obj_set_size(panel,300,104);lv_obj_align(panel,LV_ALIGN_CENTER,0,100);
        lv_obj_set_style_bg_color(panel,lv_color_hex(0x101827),0);
        lv_obj_set_style_bg_opa(panel,LV_OPA_COVER,0);
        lv_obj_set_style_border_color(panel,lv_color_hex(0xffd75e),0);
        lv_obj_set_style_border_width(panel,2,0);
        lv_obj_set_style_pad_all(panel,12,0);
        lv_obj_clear_flag(panel,LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(panel,dismiss,LV_EVENT_CLICKED,NULL);
        label=lv_label_create(panel);
        lv_label_set_text(label,"Hold - to turn off");
        lv_obj_set_style_text_font(label,&lv_font_montserrat_20,0);
        lv_obj_set_style_text_color(label,lv_color_hex(0xfff6dc),0);
        lv_obj_align(label,LV_ALIGN_TOP_MID,0,0);
        bar=lv_bar_create(panel);lv_obj_set_size(bar,260,16);
        lv_obj_align(bar,LV_ALIGN_BOTTOM_MID,0,-4);
        lv_bar_set_range(bar,0,100);
        lv_obj_set_style_bg_color(bar,lv_color_hex(0xffd75e),LV_PART_INDICATOR);
        expiry=lv_timer_create(expire,3000,NULL);
    }
    lv_label_set_text(label,waking?"Keep holding\nto turn on":"Hold - to turn off");
    lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_align(panel,LV_ALIGN_CENTER,0,waking?0:100);
    lv_obj_clear_flag(panel,LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(panel);
    lv_bar_set_value(bar,percent>100?100:percent,LV_ANIM_OFF);
    if(held)lv_timer_pause(expiry);
    else{lv_timer_reset(expiry);lv_timer_resume(expiry);}
}

void pearl_power_hint_update(bool held,unsigned percent){show(held,percent,false);}
void pearl_power_hint_wake(unsigned percent){show(true,percent,true);}
