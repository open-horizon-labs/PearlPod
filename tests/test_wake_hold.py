"""Exercise the actual boot wake gate, including release during display init."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'main/main.c').read_text();code=s[s.index('static atomic_bool wake_released;'):s.index('void app_main(void)')]
stubs=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <setjmp.h>
#include <stdio.h>
#define PEARL_BUTTON_HOLD_MS 1800
#define CONFIG_PEARL_BUTTON_UP 0
#define ESP_ERROR_CHECK(x) assert((x)==0)
#define pdMS_TO_TICKS(n) (n)
typedef void *esp_timer_handle_t;
static int player_panel;
static bool released,display_off,panel_sleep,wake_armed,hidden;
static int64_t now,release_at=-1;
static unsigned draws,percent,stopped,deleted;
static jmp_buf sleeping;
static int64_t esp_timer_get_time(void){return now;}
static int gpio_get_level(int p){(void)p;return released;}
static void *lv_scr_act(void){return 0;}
static unsigned lv_color_hex(unsigned n){return n;}
static void lv_obj_set_style_bg_color(void *p,unsigned c,int part){(void)p;(void)c;(void)part;}
static void pearl_power_hint_wake(unsigned p){assert(p<=100);assert(!draws||p>=percent);percent=p;draws++;}
static void lv_timer_handler(void){}
static void lv_refr_now(void *p){(void)p;}
static void vTaskDelay(unsigned ms){now+=ms*1000;if(release_at>=0&&now>=release_at)released=true;}
static int esp_timer_stop(void *m){assert(m);stopped++;return 0;}
static int esp_timer_delete(void *m){assert(m);deleted++;return 0;}
static void pearl_power_hint_hide(void){hidden=true;}
static void esp_lcd_panel_disp_on_off(int p,bool on){(void)p;display_off=!on;}
static void panel_qspi_amoled_sleep(int p,bool sleep){(void)p;panel_sleep=sleep;}
static int esp_sleep_enable_ext0_wakeup(int pin,int level){assert(pin==0&&level==0);wake_armed=true;return 0;}
static void rtc_gpio_pullup_en(int pin){(void)pin;}
static void rtc_gpio_pulldown_dis(int pin){(void)pin;}
static void esp_deep_sleep_start(void){longjmp(sleeping,1);}
'''
tests=r'''
static void reset(void){now=0;wake_started=0;release_at=-1;released=false;atomic_store(&wake_released,false);display_off=panel_sleep=wake_armed=hidden=false;draws=percent=stopped=deleted=0;}
static void cancelled(void){if(!setjmp(sleeping)){wake_hold_finish((void*)1);assert(!"early release booted");}assert(display_off&&panel_sleep&&wake_armed&&hidden&&stopped==1&&deleted==1);}
int main(void){
 reset();now=500000;release_at=900000;cancelled();assert(draws>0&&percent<100);
 reset();now=400000;released=true;wake_button_sample(0);released=false;now=800000;cancelled();assert(!draws); // release/repress during display init stays cancelled
 reset();now=700000;wake_hold_finish((void*)1);assert(percent==100&&draws>1&&hidden&&!display_off&&stopped==1&&deleted==1&&now==1800000);
 reset();now=2000000;released=true;wake_button_sample(0);wake_hold_finish((void*)1);assert(percent==100&&!display_off); // release after completed hold is accepted
 puts("Wake gate: early release, init-time release latch, monotonic progress, accepted hold and timer cleanup pass");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);src=p/'test.c';binary=p/'test';src.write_text(stubs+code+tests)
 subprocess.run(['cc','-std=c17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(src),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
# The gate must run before touch/GUI workers, card scan, audio or network startup.
app=s[s.index('void app_main(void)'):]
gate=app.index('if(button_wake)wake_hold_finish(wake_monitor);')
assert gate<app.index('xTaskCreate(example_lvgl_port_task')
assert gate<app.index('gpio_deep_sleep_hold_dis()')

# A held button must route through the hint even after a power-on reset.
assert '!gpio_get_level(CONFIG_PEARL_BUTTON_UP);' in app[:app.index('esp_timer_handle_t wake_monitor')]
# Paint before the touch controller's two 200ms reset delays.
assert gate<app.index('esp_lcd_touch_new_i2c(')
