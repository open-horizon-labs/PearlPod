"""Exercise the production standby orchestration with deterministic hardware stubs."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'main/main.c').read_text()
start = source.index('static void standby_cancel(')
# These are the final two definitions in main.c. Compile the actual routines,
# not a parallel model of their ordering and early-return branches.
routines = source[start:]
stubs = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdio.h>
#define atomic_load(p) (*(p))
#define ESP_LOGW(...) ((void)0)
#define pdMS_TO_TICKS(n) (n)
#define CONFIG_PEARL_BUTTON_UP 0
#define ESP_OK 0
#define GPIO_NUM_41 41
#define GPIO_INTR_LOW_LEVEL 0
static bool library_ready,rescan_running,screen_locked;
static bool trace_busy,sync_ok,network_ok,wake_ok,scan_on_sync,flush_stuck;
static int64_t clock_us;
static int events[32],count,activities;
enum {WAKE=1,BEGIN,REFRESH,TRACE,SYNC,NETWORK,AUDIO,OFF,SLEEP,CANCEL};
static jmp_buf sleeping;
static struct {int flushing;} draw;
static struct {typeof(draw) *draw_buf;} driver={&draw};
typedef struct {typeof(driver) *driver;} lv_disp_t;
static lv_disp_t disp={&driver};
static void record(int event){assert(count<32);events[count++]=event;}
static void vTaskDelay(int ms){clock_us+=(int64_t)ms*1000;if(!flush_stuck)draw.flushing=0;}
static int64_t esp_timer_get_time(void){return clock_us;}
static bool example_lvgl_lock(int timeout){(void)timeout;return true;}
static void example_lvgl_unlock(void){}
static void pearl_ui_shutdown_begin(void){record(BEGIN);}
static void pearl_ui_shutdown_cancel(const char *reason){assert(reason&&*reason);record(CANCEL);}
static void pearl_power_activity(void){activities++;}
static void lv_refr_now(void *display){(void)display;record(REFRESH);draw.flushing=1;}
static lv_disp_t *lv_disp_get_default(void){return &disp;}
static void display_sleep(bool asleep,bool manual){(void)manual;screen_locked=asleep;record(asleep?OFF:WAKE);}
static void pearl_trace_cancel(void){record(TRACE);}
static bool pearl_trace_sd_busy(void){return trace_busy;}
static bool pearl_sync_shutdown(void){record(SYNC);if(scan_on_sync)rescan_running=true;return sync_ok;}
static bool pearl_network_shutdown(void){record(NETWORK);return network_ok;}
static bool pearl_power_deep_supported(void){return true;}
static int esp_sleep_enable_ext0_wakeup(int pin,int level){(void)pin;(void)level;return wake_ok?ESP_OK:-1;}
static void pearl_audio_shutdown(void){record(AUDIO);}
static int gpio_get_level(int pin){(void)pin;return 1;}
static void rtc_gpio_pullup_en(int pin){(void)pin;}
static void rtc_gpio_pulldown_dis(int pin){(void)pin;}
static void gpio_hold_en(int pin){(void)pin;}
static void gpio_deep_sleep_hold_en(void){}
static void esp_deep_sleep_start(void){record(SLEEP);longjmp(sleeping,1);}
static void gpio_wakeup_enable(int pin,int level){(void)pin;(void)level;}
static void esp_sleep_enable_gpio_wakeup(void){}
static void esp_light_sleep_start(void){assert(!"unexpected fallback");}
static void esp_restart(void){assert(!"unexpected restart");}
'''
tests = r'''
static void reset(void){library_ready=true;rescan_running=screen_locked=false;trace_busy=scan_on_sync=flush_stuck=false;sync_ok=network_ok=wake_ok=true;clock_us=0;count=activities=0;draw.flushing=0;}
static bool seen(int event){for(int i=0;i<count;i++)if(events[i]==event)return true;return false;}
static void success(bool manual){if(!setjmp(sleeping)){enter_standby(manual);assert(!"sleep did not start");}}
int main(void){
 reset();success(true);assert(events[0]==BEGIN&&events[1]==REFRESH&&events[2]==TRACE);assert(events[count-2]==OFF&&events[count-1]==SLEEP);assert(clock_us>=600000&&clock_us<610000);
 reset();screen_locked=true;success(true);assert(events[0]==WAKE&&events[1]==BEGIN);
 reset();screen_locked=true;success(false);assert(!seen(WAKE)&&!seen(BEGIN)&&!seen(REFRESH));assert(clock_us==0&&seen(SLEEP));
 reset();success(false);assert(seen(BEGIN)&&clock_us>=600000);
 reset();trace_busy=true;enter_standby(true);assert(seen(CANCEL)&&!seen(SYNC)&&!seen(OFF));assert(activities==1&&clock_us>=5000000);
 reset();sync_ok=false;enter_standby(true);assert(seen(CANCEL)&&!seen(NETWORK)&&!seen(AUDIO)&&!seen(OFF));
 reset();network_ok=false;enter_standby(true);assert(seen(CANCEL)&&!seen(AUDIO)&&!seen(OFF));
 reset();wake_ok=false;enter_standby(true);assert(seen(CANCEL)&&!seen(AUDIO)&&!seen(OFF));
 reset();scan_on_sync=true;enter_standby(true);assert(seen(CANCEL)&&!seen(AUDIO)&&!seen(OFF));
 reset();library_ready=false;enter_standby(true);assert(count==0&&activities==1);
 reset();rescan_running=true;enter_standby(true);assert(count==0&&activities==1);
 reset();screen_locked=true;sync_ok=false;enter_standby(false);assert(!seen(BEGIN)&&!seen(CANCEL)&&!seen(OFF)&&screen_locked);
 reset();flush_stuck=true;success(true);assert(clock_us>=700000&&clock_us<710000); // bounded DMA wait plus farewell
 puts("Production standby: render-before-cleanup, dark idle, manual wake, bounded flush, all cancellation branches and rescan exclusion pass");
}
'''
with tempfile.TemporaryDirectory(prefix='pearl-shutdown-') as temp:
    path = Path(temp)
    (path / 'test.c').write_text(stubs + routines + tests)
    subprocess.run(['cc', '-std=gnu17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
