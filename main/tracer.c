/* Explicitly invoked diagnostics. Never writes card sectors directly. */
#include "tracer.h"
#ifndef PEARL_TRACER_HOST
#include "sync.h"
#include "network.h"
#include "player.h"
#include "sd_protocol_defs.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include <stdatomic.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#define EVENTS 2048u
#define SAMPLE (1024u*1024u)
#define VOLUME (16u*1024u*1024u)
static sdmmc_card_t *card;
static esp_err_t (*original_transaction)(int,sdmmc_command_t *);
static atomic_bool enabled, sd_busy, cancelled, probe;
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
typedef struct {uint64_t start;unsigned us,opcode,bytes,status;int result;} event;
typedef struct {
 unsigned commands,reads,writes,statuses,busy_statuses,max_status_gap_us,errors,max_us,latency[8],sizes[8];
 uint64_t status_gap_us,record_us,read_bytes,write_bytes,command_us,read_us,write_us,status_us;
} counters;
static counters totals;
static event *events;
static event slowest[16];
static unsigned slow_count;
static unsigned sequence;
static int64_t started,last_end;
static unsigned last_opcode;
static unsigned bucket(unsigned value,const unsigned *limits){unsigned i=0;while(i<7&&value>limits[i])i++;return i;}
static esp_err_t transaction(int slot,sdmmc_command_t *cmd){
 if(!atomic_load(&enabled))return original_transaction(slot,cmd);
 int64_t start=esp_timer_get_time();esp_err_t result=original_transaction(slot,cmd);
 unsigned us=esp_timer_get_time()-start;
 static const unsigned latency[]={100,500,1000,5000,20000,100000,500000};
 static const unsigned sizes[]={512,1024,4096,8192,16384,32768,65536};
 portENTER_CRITICAL(&mux);
 totals.commands++;totals.command_us+=us;totals.errors+=result!=ESP_OK;
 if(us>totals.max_us)totals.max_us=us;
 totals.latency[bucket(us,latency)]++;
 if(cmd->datalen){totals.sizes[bucket(cmd->datalen,sizes)]++;
  if(cmd->flags&SCF_CMD_READ){totals.reads++;totals.read_bytes+=cmd->datalen;totals.read_us+=us;}
  else{totals.writes++;totals.write_bytes+=cmd->datalen;totals.write_us+=us;}
 }
 if(cmd->opcode==13){
  totals.statuses++;totals.status_us+=us;
  if(result==ESP_OK&&(!(cmd->response[0]&MMC_R1_READY_FOR_DATA)||MMC_R1_CURRENT_STATE(cmd->response)!=MMC_R1_CURRENT_STATE_TRAN))totals.busy_statuses++;
  if(last_end&&(last_opcode==13||last_opcode==24||last_opcode==25)&&start>=last_end){
   unsigned gap=start-last_end;totals.status_gap_us+=gap;
   if(gap>totals.max_status_gap_us)totals.max_status_gap_us=gap;
  }
 }
 last_end=start+us;last_opcode=cmd->opcode;
 event current={.start=start,.us=us,.opcode=cmd->opcode,.bytes=cmd->datalen,.status=cmd->response[0],.result=result};
 events[sequence%EVENTS]=current;sequence++;
 if(slow_count<16)slowest[slow_count++]=current;
 else{
  unsigned least=0;for(unsigned i=1;i<16;i++)if(slowest[i].us<slowest[least].us)least=i;
  if(us>slowest[least].us)slowest[least]=current;
 }
 totals.record_us+=esp_timer_get_time()-start-us;
 portEXIT_CRITICAL(&mux);
 return result;
}
void pearl_trace_attach(sdmmc_card_t *mounted){if(mounted->host.do_transaction==transaction)return;card=mounted;original_transaction=card->host.do_transaction;card->host.do_transaction=transaction;}
bool pearl_trace_start(void){
 if(!card||atomic_load(&sd_busy)||pearl_sync_busy())return false;
 if(!events)events=heap_caps_calloc(EVENTS,sizeof(event),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!events)return false;
 portENTER_CRITICAL(&mux);memset(&totals,0,sizeof(totals));sequence=slow_count=0;last_end=0;last_opcode=0;started=esp_timer_get_time();enabled=true;portEXIT_CRITICAL(&mux);
 printf("PEARL tracer started version=1 event_capacity=%u event_bytes=%u\n",EVENTS,(unsigned)(EVENTS*sizeof(event)));return true;
}
void pearl_trace_stop(void){enabled=false;}
void pearl_trace_probe(bool value){probe=value;}
bool pearl_trace_ram_sink(void){return atomic_load(&probe);}
bool pearl_trace_sd_busy(void){return atomic_load(&sd_busy);}
void pearl_trace_cancel(void){cancelled=true;pearl_sync_cancel();}
void pearl_trace_snapshot(void){
 counters c;unsigned seq;portENTER_CRITICAL(&mux);c=totals;seq=sequence;portEXIT_CRITICAL(&mux);
 printf("PEARL tracer {\"version\":1,\"enabled\":%d,\"sd_busy\":%d,\"probe\":%d,\"elapsed_us\":%"PRIi64",\"clock_khz\":%d,\"bus_width\":%d,\"sector_bytes\":%d,\"staging_bytes\":%u,\"chunk_sectors\":%u,\"tick_hz\":%d,\"internal_free\":%u,\"internal_min\":%u,\"internal_largest\":%u,\"psram_free\":%u,\"commands\":%u,\"reads\":%u,\"writes\":%u,\"status_commands\":%u,\"busy_statuses\":%u,\"status_gap_us\":%"PRIu64",\"max_status_gap_us\":%u,\"record_us\":%"PRIu64",\"errors\":%u,\"read_bytes\":%"PRIu64",\"write_bytes\":%"PRIu64",\"command_us\":%"PRIu64",\"read_us\":%"PRIu64",\"write_us\":%"PRIu64",\"status_us\":%"PRIu64",\"max_command_us\":%u,\"events_overwritten\":%u,\"latency_bins\":[%u,%u,%u,%u,%u,%u,%u,%u],\"size_bins\":[%u,%u,%u,%u,%u,%u,%u,%u]}\n",
 atomic_load(&enabled),atomic_load(&sd_busy),atomic_load(&probe),started?esp_timer_get_time()-started:0,
 card?card->real_freq_khz:0,card?(1<<card->log_bus_width):0,card?card->csd.sector_size:0,
 card&&card->host.dma_aligned_buffer?(unsigned)heap_caps_get_allocated_size(card->host.dma_aligned_buffer):0,
 card?(unsigned)card->host.unaligned_multi_block_rw_max_chunk_size:0,CONFIG_FREERTOS_HZ,
 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),(unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
 c.commands,c.reads,c.writes,c.statuses,c.busy_statuses,c.status_gap_us,c.max_status_gap_us,c.record_us,c.errors,c.read_bytes,c.write_bytes,c.command_us,c.read_us,c.write_us,c.status_us,c.max_us,seq>EVENTS?seq-EVENTS:0,
 c.latency[0],c.latency[1],c.latency[2],c.latency[3],c.latency[4],c.latency[5],c.latency[6],c.latency[7],
 c.sizes[0],c.sizes[1],c.sizes[2],c.sizes[3],c.sizes[4],c.sizes[5],c.sizes[6],c.sizes[7]);
}
void pearl_trace_events(unsigned count){
 unsigned last;portENTER_CRITICAL(&mux);last=sequence;portEXIT_CRITICAL(&mux);
 if(count>EVENTS)count=EVENTS;
 if(count>last)count=last;
 for(unsigned i=last-count;i<last;i++){
  event e;bool present;portENTER_CRITICAL(&mux);present=sequence-i<=EVENTS;e=events[i%EVENTS];portEXIT_CRITICAL(&mux);
  if(present)printf("PEARL tracer_event seq=%u start_us=%"PRIu64" opcode=%u bytes=%u duration_us=%u response=%u result=%d\n",i,e.start,e.opcode,e.bytes,e.us,e.status,e.result);
 }
 for(unsigned i=0;i<16;i++){
  event e;bool present;portENTER_CRITICAL(&mux);present=i<slow_count;e=slowest[i];portEXIT_CRITICAL(&mux);
  if(present)printf("PEARL tracer_slowest start_us=%"PRIu64" opcode=%u bytes=%u duration_us=%u response=%u result=%d\n",e.start,e.opcode,e.bytes,e.us,e.status,e.result);
 }
 printf("PEARL tracer_events_end count=%u\n",count);
}
extern void pearl_trace_audio_restore(void);
static void sd_task(void *arg){
 char *source=arg;
 unsigned char *sample=heap_caps_malloc(SAMPLE,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 char *stdio_buffer=heap_caps_malloc(32768,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 FILE *input=sample?fopen(source,"rb"):NULL;
 size_t available=input?fread(sample,1,SAMPLE,input):0;
 if(input)fclose(input);
 free(source);
 const char *target="/sdcard/music/.pearl/.trace-sd.tmp";
 /* Compare normal buffered PSRAM writes, unbuffered PSRAM writes and
  * aligned internal writes. Never mutate the mounted driver's shared buffer. */
 const unsigned chunks[]={32768,32768,4096,8192,16384,32768};
 mkdir("/sdcard/music/.pearl",0755);
 if(available&&stdio_buffer){
  available-=available%512;
  for(unsigned mode=0;available&&mode<6&&!cancelled;mode++){
   unsigned char *dma=mode>=2?heap_caps_aligned_alloc(16,chunks[mode],MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL):NULL;
   if(mode>=2&&!dma){printf("PEARL tracer_sd skipped mode=%u chunk_bytes=%u reason=internal_memory\n",mode,chunks[mode]);continue;}
   int fd=open(target,O_WRONLY|O_CREAT|O_EXCL,0600);
   FILE *out=fd>=0?fdopen(fd,"wb"):NULL;
   if(fd>=0&&!out)close(fd);
   bool ok=out&&setvbuf(out,mode==0?stdio_buffer:NULL,mode==0?_IOFBF:_IONBF,mode==0?32768:0)==0;
   size_t total=0;
   unsigned longest=0,yield_us=0,copy_us=0;
   int64_t start=esp_timer_get_time(),last_yield=start;
   counters before;
   portENTER_CRITICAL(&mux);before=totals;portEXIT_CRITICAL(&mux);
   while(ok&&!cancelled&&total<VOLUME){
    unsigned offset=total%available,n=chunks[mode];
    if(n>available-offset)n=available-offset;
    if(n>VOLUME-total)n=VOLUME-total;
    const unsigned char *data=sample+offset;
    int64_t before_write=esp_timer_get_time();
    if(dma){memcpy(dma,data,n);data=dma;copy_us+=esp_timer_get_time()-before_write;}
    before_write=esp_timer_get_time();
    size_t written=0;
    if(mode==0)written=fwrite(data,1,n,out);
    else{
     while(written<n){
      ssize_t amount=write(fd,data+written,n-written);
      if(amount<0&&errno==EINTR)continue;
      if(amount<=0)break;
      written+=(size_t)amount;
     }
    }
    unsigned duration=esp_timer_get_time()-before_write;
    if(duration>longest)longest=duration;
    total+=written;ok=written==n;
    if(esp_timer_get_time()-last_yield>=20000){
     before_write=esp_timer_get_time();vTaskDelay(1);yield_us+=esp_timer_get_time()-before_write;last_yield=esp_timer_get_time();
    }
   }
   unsigned writes=esp_timer_get_time()-start;
   int64_t close_start=esp_timer_get_time();
   if(out&&fclose(out)!=0)ok=false;
   unsigned close_us=esp_timer_get_time()-close_start;
   counters after;
   portENTER_CRITICAL(&mux);after=totals;portEXIT_CRITICAL(&mux);
   printf("PEARL tracer_sd mode=%u internal_buffer=%d buffered_stdio=%d chunk_bytes=%u bytes=%u write_us=%u close_us=%u copy_us=%u yield_us=%u max_write_us=%u commands=%u command_write_us=%"PRIu64" command_status_us=%"PRIu64" cancelled=%d ok=%d\n",
    mode,mode>=2,mode==0,chunks[mode],(unsigned)total,writes,close_us,copy_us,yield_us,longest,after.commands-before.commands,after.write_us-before.write_us,after.status_us-before.status_us,atomic_load(&cancelled),ok);
   printf("PEARL tracer_sd_bins mode=%u latency=%u,%u,%u,%u,%u,%u,%u,%u sizes=%u,%u,%u,%u,%u,%u,%u,%u errors=%u\n",mode,
    after.latency[0]-before.latency[0],after.latency[1]-before.latency[1],after.latency[2]-before.latency[2],after.latency[3]-before.latency[3],
    after.latency[4]-before.latency[4],after.latency[5]-before.latency[5],after.latency[6]-before.latency[6],after.latency[7]-before.latency[7],
    after.sizes[0]-before.sizes[0],after.sizes[1]-before.sizes[1],after.sizes[2]-before.sizes[2],after.sizes[3]-before.sizes[3],
    after.sizes[4]-before.sizes[4],after.sizes[5]-before.sizes[5],after.sizes[6]-before.sizes[6],after.sizes[7]-before.sizes[7],after.errors-before.errors);
   if(fd>=0&&unlink(target)!=0)printf("PEARL tracer_sd cleanup_failed=1\n");
   free(dma);
   if(!ok)break;
  }
 }else printf("PEARL tracer_sd unavailable=sample_or_memory\n");
 free(sample);free(stdio_buffer);
 pearl_trace_audio_restore();sd_busy=false;
 printf("PEARL tracer_sd_done\n");fflush(stdout);vTaskDelete(NULL);
}
void pearl_trace_sd(const char *source){
 if(!card||!enabled||pearl_sync_busy()||pearl_network_enabled()||!pearl_audio_state().paused||atomic_exchange(&sd_busy,true)){
  printf("PEARL tracer_sd refused=requires_trace_paused_offline_idle\n");return;
 }
 char *copy=strdup(source);cancelled=false;
 if(!copy||xTaskCreate(sd_task,"trace_sd",6144,copy,2,NULL)!=pdPASS){free(copy);sd_busy=false;printf("PEARL tracer_sd failed=task_or_memory\n");}
}
