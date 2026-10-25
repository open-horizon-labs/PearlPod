"""Run actual DAC initialization and PCM emission silently, including full-scale peaks."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
audio = (root / 'main/audio.c').read_text()
init = audio[audio.index('static esp_err_t dac_init('):audio.index('static bool save(')]
emit = audio[audio.index('static bool emit('):audio.index('static bool wait_playing(')]
seams = r'''
#include "audio_volume.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NOT_FOUND -2
#define ESP_ERR_TIMEOUT -3
#define GPIO_MODE_OUTPUT 1
#define CONFIG_PEARL_BUTTON_DOWN 48
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define pdMS_TO_TICKS(x) (x)
typedef int esp_err_t;
static int cs_addr,tx;
static unsigned writes;
static uint32_t addresses[64];static uint8_t values[64];
static void gpio_set_direction(int a,int b){(void)a;(void)b;}
static void gpio_set_level(int a,int b){(void)a;(void)b;}
static void vTaskDelay(int a){(void)a;}
static int cs_read(uint32_t r,uint8_t *v){
 *v=r==0x10000?0x43:r==0x10001?0x13:0x10;return ESP_OK;
}
static int cs_write(uint32_t r,uint8_t v){addresses[writes]=r;values[writes++]=v;return ESP_OK;}
typedef struct {int volume;unsigned milliseconds,seconds;} pearl_state;
static pearl_state state;
static pearl_state pearl_audio_state(void){return state;}
static void publish(pearl_state s){state=s;}
static void error(const char *text){(void)text;assert(0);}
static bool service(int current){(void)current;return true;}
static int32_t output[4096],captured[8192];static size_t count;
static double phase;static int16_t previous[2];static bool have_previous;
static uint64_t frames_written;static uint32_t timeline_start;
static int i2s_channel_write(int handle,const void *data,size_t bytes,size_t *done,int timeout){
 (void)handle;(void)timeout;assert(count+bytes/4<=8192);
 memcpy(captured+count,data,bytes);count+=bytes/4;*done=bytes;return ESP_OK;
}
static unsigned reg(uint32_t address){for(unsigned i=0;i<writes;i++)if(addresses[i]==address)return values[i];assert(0);return 0;}
'''
checks = r'''
int main(void){
 assert(dac_init()==ESP_OK);assert(reg(0x90001)==0&&reg(0x90002)==0);
 assert(reg(0x20000)&0x10); /* Set output range while HP remains powered down. */
#if CONFIG_PEARL_HIGH_IMPEDANCE_OUTPUT
 assert(reg(0x80000)==0x31&&reg(0xb0000)==0x1e);
#else
 assert(reg(0x80000)==0x20&&reg(0xb0000)==0x1c);
#endif
 unsigned range=0,power=0;
 for(unsigned i=0;i<writes;i++){if(addresses[i]==0x80000)range=i;if(addresses[i]==0x20000&&!(values[i]&0x10))power=i;}
 assert(range&&power>range);
 assert(pearl_volume_gain(-1)==0&&pearl_volume_gain(0)==0);
 assert(pearl_volume_gain(100)==1&&pearl_volume_gain(101)==1);
 assert(pearl_volume_slot(40000.f,1)==32767*65536);
 assert(pearl_volume_slot(-40000.f,1)==INT32_MIN);
 float prior=0;
 const unsigned rates[]={44100,48000,96000,192000};
 const int16_t samples[]={32767,-32768,-32768,32767,32767,-32768,-32768,32767};
 for(int volume=0;volume<=PEARL_VOLUME_MAX;volume++){
  float gain=pearl_volume_gain(volume);assert(gain>=prior&&gain<=1);prior=gain;
  for(unsigned r=0;r<4;r++){
   state.volume=volume;count=0;phase=0;have_previous=false;frames_written=0;
   assert(emit(samples,4,2,rates[r],0));assert(count>=2);
   assert(captured[0]>=0&&captured[1]<=0);
   if(!volume)for(size_t i=0;i<count;i++)assert(captured[i]==0);
   if(volume==100){assert(captured[0]==32767*65536);assert(captured[1]==INT32_MIN);}
  }
 }
 puts("Actual DAC modes, unity full scale, mute, monotonic gain and peak PCM emission: PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='pearl-volume-') as directory:
    source = Path(directory) / 'volume.c'
    source.write_text(seams + init + emit + checks)
    for high_impedance in (0, 1):
        binary = Path(directory) / f'volume-{high_impedance}'
        subprocess.run(['cc', '-std=c17', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', f'-DCONFIG_PEARL_HIGH_IMPEDANCE_OUTPUT={high_impedance}',
                        '-I', str(root / 'main'), str(source), '-lm', '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
