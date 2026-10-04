"""Exercise the actual SDK ISR function with a concurrent owner change."""
import os
from pathlib import Path
import subprocess
import tempfile

sdk=Path(os.environ['IDF_PATH'])
s=(sdk/'components/esp_hw_support/spi_bus_lock.c').read_text()
a=s.index('SPI_BUS_LOCK_ISR_ATTR static inline bool bg_exit_core(')
b=s.index('\nIRAM_ATTR static inline void dev_wait_prepare',a)
function=s[a:b]
harness=r'''
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>
typedef int BaseType_t;
typedef struct dev { int id; } spi_bus_lock_dev_t;
typedef struct { spi_bus_lock_dev_t *acquiring_dev; bool acq_dev_bg_active,in_isr; uint32_t status; } spi_bus_lock_t;
#define SPI_BUS_LOCK_ISR_ATTR
#define BG_MASK 1
#define LOCK_MASK 2
#define BUS_LOCK_DEBUG_EXECUTE_CHECK(x) ((void)0)
static spi_bus_lock_dev_t owner={7};
static int resumed;
static void bg_enable(spi_bus_lock_t *l) {(void)l;}
static uint32_t lock_status_fetch(spi_bus_lock_t *l) {return l->status;}
static uint32_t dev_mask(spi_bus_lock_t *l,spi_bus_lock_dev_t *d) {assert(d);l->acquiring_dev=0;return 1;}
#define DEV_BG_MASK(d) dev_mask(lock,(d))
static void resume_dev_in_isr(spi_bus_lock_dev_t *d,BaseType_t *yield) {assert(d==&owner);resumed++;*yield=1;}
static bool schedule_core(spi_bus_lock_t *l,uint32_t status,spi_bus_lock_dev_t **out) {(void)status;*out=&owner;l->acquiring_dev=0;return true;}
'''
harness+=function+r'''
int main(void) {
 BaseType_t yield=0;
 spi_bus_lock_t l={.acquiring_dev=&owner,.in_isr=true,.status=0};
 assert(bg_exit_core(&l,false,&yield));assert(resumed==1&&yield==1&&!l.in_isr);
 l=(spi_bus_lock_t){.in_isr=true,.status=LOCK_MASK};
 assert(bg_exit_core(&l,false,&yield));assert(resumed==2&&!l.in_isr);
 l=(spi_bus_lock_t){.in_isr=true,.status=BG_MASK};
 assert(!bg_exit_core(&l,false,&yield));assert(resumed==2&&l.in_isr);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'race.c').write_text(harness)
 subprocess.run(['cc','-fsanitize=address,undefined',str(p/'race.c'),'-o',str(p/'race')],check=True)
 subprocess.run([str(p/'race')],check=True)
print('Actual SDK ISR race regression checks passed')
