"""Render and exercise the production minus-button instruction and progress UI."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
lvgl=root/'managed_components/lvgl__lvgl';build=root/'.state/lvgl-power-hint'
flags='-DLV_CONF_SKIP=1 -DLV_MEM_SIZE=262144 -DLV_COLOR_DEPTH=32 -DLV_FONT_MONTSERRAT_20=1 -fsanitize=address,undefined -fno-omit-frame-pointer'
subprocess.run(['cmake','-S',str(lvgl),'-B',str(build),'-DCMAKE_C_FLAGS='+flags],check=True,stdout=subprocess.DEVNULL)
subprocess.run(['cmake','--build',str(build),'--target','lvgl','-j','8'],check=True,stdout=subprocess.DEVNULL)
code=r'''
#include <assert.h>
#include <stdio.h>
#include "power_hint.c"
static unsigned char pixels[460*460*4];
static void flush(lv_disp_drv_t *d,const lv_area_t *a,lv_color_t *c){
 for(int y=a->y1;y<=a->y2;y++)for(int x=a->x1;x<=a->x2;x++){
  lv_color32_t color;color.full=lv_color_to32(*c++);unsigned i=(y*460+x)*4;
  pixels[i]=color.ch.red;pixels[i+1]=color.ch.green;pixels[i+2]=color.ch.blue;pixels[i+3]=255;
 }lv_disp_flush_ready(d);
}
static void step(unsigned ms){for(unsigned t=0;t<ms;t+=10){lv_tick_inc(10);lv_timer_handler();}}
int main(void){
 lv_init();static lv_color_t buffer[460*10];lv_disp_draw_buf_t draw;
 lv_disp_draw_buf_init(&draw,buffer,NULL,460*10);lv_disp_drv_t d;lv_disp_drv_init(&d);
 d.hor_res=460;d.ver_res=460;d.draw_buf=&draw;d.flush_cb=flush;lv_disp_drv_register(&d);
 pearl_power_hint_hide();pearl_power_hint_update(false,0);
 assert(!lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN)&&lv_bar_get_value(bar)==0);
 step(3100);assert(lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN));
 pearl_power_hint_update(true,50);step(8100);
 assert(!lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN)&&lv_bar_get_value(bar)==50);
 lv_obj_update_layout(panel);int x=lv_obj_get_x(panel)-230,y=lv_obj_get_y(panel)+lv_obj_get_height(panel)-230;
 assert(x*x+y*y<230*230); // card corners stay inside the round screen
 lv_refr_now(NULL);FILE *f=fopen("hold.rgba","wb");assert(f);
 assert(fwrite(pixels,1,sizeof(pixels),f)==sizeof(pixels));fclose(f);
 pearl_power_hint_update(true,999);assert(lv_bar_get_value(bar)==100);
 pearl_power_hint_update(false,0);assert(lv_bar_get_value(bar)==0);
 step(3100);assert(lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN));
 pearl_power_hint_update(false,0);lv_event_send(panel,LV_EVENT_CLICKED,NULL);
 assert(lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN));
 pearl_power_hint_update(true,30);pearl_power_hint_hide();
 assert(lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN));
 puts("Minus hint: expiry, held progress, release reset, bounds, clamp and dismissal pass");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 tmp=Path(tmp);src=tmp/'test.c';binary=tmp/'test';src.write_text(code)
 subprocess.run(['cc','-std=c17','-Wall','-Wextra','-Werror',*flags.split(),'-I',str(root/'main'),'-I',str(lvgl),str(src),str(build/'lib/liblvgl.a'),'-lm','-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True,cwd=tmp)
 (root/'dist').mkdir(exist_ok=True)
 subprocess.run(['ffmpeg','-v','error','-y','-f','rawvideo','-pixel_format','rgba','-video_size','460x460','-i',str(tmp/'hold.rgba'),'-frames:v','1',str(root/'dist/power-hold.png')],check=True)
