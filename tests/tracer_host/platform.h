#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_TIMEOUT 0x107
#define SCF_CMD_READ 0x40
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
#define MALLOC_CAP_DMA 8
#define CONFIG_FREERTOS_HZ 1000
#define MMC_R1_READY_FOR_DATA (1<<8)
#define MMC_R1_CURRENT_STATE(resp) (((resp)[0]>>9)&15)
#define MMC_R1_CURRENT_STATE_TRAN 4
typedef struct {unsigned opcode,arg,response[4];void *data;size_t datalen;int flags;} sdmmc_command_t;
typedef struct {esp_err_t (*do_transaction)(int,sdmmc_command_t*);void *dma_aligned_buffer;size_t unaligned_multi_block_rw_max_chunk_size;} sdmmc_host_t;
typedef struct {sdmmc_host_t host;int real_freq_khz,log_bus_width;struct{int sector_size;}csd;} sdmmc_card_t;
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(lock) pthread_mutex_lock(lock)
#define portEXIT_CRITICAL(lock) pthread_mutex_unlock(lock)
extern int64_t host_time;
extern bool fail_allocation;
static inline int64_t esp_timer_get_time(void){return host_time;}
static inline void *heap_caps_calloc(size_t n,size_t size,int caps){(void)caps;return fail_allocation?NULL:calloc(n,size);}
static inline void *heap_caps_malloc(size_t n,int caps){(void)caps;return malloc(n);}
static inline void *heap_caps_aligned_alloc(size_t alignment,size_t n,int caps){(void)caps;void *p=NULL;return posix_memalign(&p,alignment,n)?NULL:p;}
static inline size_t heap_caps_get_allocated_size(void *p){(void)p;return 8192;}
static inline size_t heap_caps_get_free_size(int caps){(void)caps;return 65536;}
static inline size_t heap_caps_get_minimum_free_size(int caps){(void)caps;return 8192;}
static inline size_t heap_caps_get_largest_free_block(int caps){(void)caps;return 32768;}
static inline void vTaskDelay(unsigned n){host_time+=n*1000;}
static inline void vTaskDelete(void *task){(void)task;}
#define pdPASS 1
static inline int xTaskCreate(void (*task)(void*),const char *name,unsigned stack,void *arg,int priority,void *handle){(void)task;(void)name;(void)stack;(void)arg;(void)priority;(void)handle;return 0;}
typedef struct {bool paused;} pearl_state;
pearl_state pearl_audio_state(void);
bool pearl_sync_busy(void);
void pearl_sync_cancel(void);
bool pearl_network_enabled(void);
