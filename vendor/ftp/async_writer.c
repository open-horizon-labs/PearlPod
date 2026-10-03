#include "async_writer.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#ifdef PEARL_FTP_HOST
#include <pthread.h>
#include <time.h>
static uint64_t clock_us(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
static void pause_worker(void){struct timespec t={0,1000000};nanosleep(&t,NULL);}
#else
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static uint64_t clock_us(void){return esp_timer_get_time();}
static void pause_worker(void){vTaskDelay(1);}
#endif
struct pearl_writer {
 uint8_t *ring;unsigned capacity;
 FILE *file;
 atomic_uint head,tail,high_water,full,bytes,calls,write_us,write_max_us,flush_us;
 atomic_bool active,finish,abort,failed,stop,stopped;
#ifdef PEARL_FTP_HOST
 pthread_t thread;
#endif
};
static void writer_loop(pearl_writer *w){
 while(!atomic_load(&w->stop)){
  if(!atomic_load_explicit(&w->active,memory_order_acquire)){pause_worker();continue;}
  unsigned tail=atomic_load_explicit(&w->tail,memory_order_relaxed);
  unsigned head=atomic_load_explicit(&w->head,memory_order_acquire);
  unsigned size=head-tail;
  if(atomic_load(&w->abort)||atomic_load(&w->failed))size=0;
  if(size){
   unsigned offset=tail%w->capacity;
   if(size>32768)size=32768;
   if(size>w->capacity-offset)size=w->capacity-offset;
   uint64_t before=clock_us();
   unsigned written=fwrite(w->ring+offset,1,size,w->file);
   unsigned duration=clock_us()-before;
   atomic_fetch_add(&w->calls,1);atomic_fetch_add(&w->write_us,duration);
   if(duration>atomic_load(&w->write_max_us))w->write_max_us=duration;
   atomic_fetch_add(&w->bytes,written);
   if(written!=size)w->failed=true;
   atomic_store_explicit(&w->tail,tail+size,memory_order_release);
  }else if(atomic_load(&w->finish)||atomic_load(&w->abort)||atomic_load(&w->failed)){
   uint64_t before=clock_us();
   if(fclose(w->file)!=0)w->failed=true;
   w->file=NULL;atomic_store(&w->tail,atomic_load(&w->head));atomic_fetch_add(&w->flush_us,(unsigned)(clock_us()-before));
   atomic_store_explicit(&w->active,false,memory_order_release);
  }else pause_worker();
 }
 atomic_store(&w->stopped,true);
}
#ifdef PEARL_FTP_HOST
static void *worker(void *arg){writer_loop(arg);return NULL;}
#else
static void worker(void *arg){writer_loop(arg);vTaskDelete(NULL);}
#endif
pearl_writer *pearl_writer_create(unsigned capacity){
 if(capacity<32768||capacity%32768)return NULL;
 pearl_writer *w=calloc(1,sizeof(*w));if(!w)return NULL;
#ifdef PEARL_FTP_HOST
 w->ring=malloc(capacity);
#else
 w->ring=heap_caps_malloc(capacity,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#endif
 if(!w->ring){free(w);return NULL;}w->capacity=capacity;
#ifdef PEARL_FTP_HOST
 if(pthread_create(&w->thread,NULL,worker,w)){
#else
 if(xTaskCreate(worker,"sd_writer",4096,w,2,NULL)!=pdPASS){
#endif
  free(w->ring);free(w);return NULL;
 }
 return w;
}
bool pearl_writer_begin(pearl_writer *w,FILE *file){
 if(!w||!file||atomic_load(&w->active)||atomic_load(&w->stop))return false;
 w->head=w->tail=0;w->finish=w->abort=w->failed=false;w->file=file;
 atomic_store_explicit(&w->active,true,memory_order_release);return true;
}
void pearl_writer_backpressure(pearl_writer *w){if(w)atomic_fetch_add(&w->full,1);}
bool pearl_writer_failed(pearl_writer *w){return !w||atomic_load(&w->failed);}
unsigned pearl_writer_space(pearl_writer *w){
 if(!w||!atomic_load(&w->active)||atomic_load(&w->failed))return 0;
 unsigned head=atomic_load_explicit(&w->head,memory_order_relaxed);
 unsigned tail=atomic_load_explicit(&w->tail,memory_order_acquire);
 return w->capacity-(head-tail);
}
bool pearl_writer_append(pearl_writer *w,const void *data,unsigned size){
 if(!w||!data||size>pearl_writer_space(w)||atomic_load(&w->finish)||atomic_load(&w->abort)){
  if(w){atomic_fetch_add(&w->full,1);}
  return false;
 }
 unsigned head=atomic_load_explicit(&w->head,memory_order_relaxed),offset=head%w->capacity;
 unsigned first=size;if(first>w->capacity-offset)first=w->capacity-offset;
 memcpy(w->ring+offset,data,first);memcpy(w->ring,(const uint8_t*)data+first,size-first);
 unsigned tail=atomic_load_explicit(&w->tail,memory_order_acquire),used=head+size-tail;
 if(used>atomic_load(&w->high_water))w->high_water=used;
 atomic_store_explicit(&w->head,head+size,memory_order_release);return true;
}
bool pearl_writer_finish(pearl_writer *w,bool abort){
 if(!w)return false;
 if(abort)w->abort=true;
 w->finish=true;
 uint64_t deadline=clock_us()+30000000;
 while(atomic_load_explicit(&w->active,memory_order_acquire)&&clock_us()<deadline)pause_worker();
 return !atomic_load(&w->active)&&!atomic_load(&w->failed);
}
bool pearl_writer_destroy(pearl_writer *w){
 if(!w)return true;
 pearl_writer_finish(w,true);
 if(atomic_load(&w->active))return false; /* Never free buffers still used by SD. */
 w->stop=true;
 while(!atomic_load(&w->stopped))pause_worker();
#ifdef PEARL_FTP_HOST
 pthread_join(w->thread,NULL);
#endif
 free(w->ring);free(w);return true;
}
void pearl_writer_trace(pearl_writer *w,char *out,unsigned size){
 if(!w){snprintf(out,size,"writer=off");return;}
 unsigned tail=atomic_load(&w->tail),head=atomic_load(&w->head);
 unsigned queued=head-tail;if(queued>w->capacity)queued=w->capacity;
 snprintf(out,size,"queued=%u queue_max=%u queue_full=%u sd_bytes=%u sd_calls=%u sd_us=%u sd_max_us=%u sd_flush_us=%u sd_failed=%d",queued,atomic_load(&w->high_water),atomic_load(&w->full),atomic_load(&w->bytes),atomic_load(&w->calls),atomic_load(&w->write_us),atomic_load(&w->write_max_us),atomic_load(&w->flush_us),atomic_load(&w->failed));
}
