#include "tracer.h"
#include <assert.h>
#include <stdio.h>
int64_t host_time=1000;
bool fail_allocation;
static bool sync_busy;
pearl_state pearl_audio_state(void){return (pearl_state){.paused=true};}
bool pearl_sync_busy(void){return sync_busy;}
void pearl_sync_cancel(void){}
bool pearl_network_enabled(void){return false;}
void pearl_trace_audio_restore(void){}
static unsigned duration=2000,response=0x900;
static esp_err_t outcome=ESP_OK;
static esp_err_t original(int slot,sdmmc_command_t *cmd){assert(slot==0);host_time+=duration;cmd->response[0]=response;return outcome;}
static void send(sdmmc_card_t *card,unsigned opcode,unsigned bytes,bool read){
 sdmmc_command_t cmd={.opcode=opcode,.datalen=bytes,.flags=read?SCF_CMD_READ:0};
 assert(card->host.do_transaction(0,&cmd)==outcome);assert(cmd.response[0]==response);
}
int main(void){
 assert(!pearl_trace_start());
 sdmmc_card_t card={.host={.do_transaction=original,.unaligned_multi_block_rw_max_chunk_size=16,.dma_aligned_buffer=(void*)1},.real_freq_khz=40000,.log_bus_width=2,.csd={.sector_size=512}};
 pearl_trace_attach(&card);pearl_trace_attach(&card); /* Idempotent, never recurse. */
 send(&card,25,8192,false);
 fail_allocation=true;assert(!pearl_trace_start());fail_allocation=false;
 assert(pearl_trace_start());
 send(&card,25,8192,false);host_time+=100;
 duration=100;send(&card,13,0,false);
 duration=50;send(&card,18,16384,true);
 outcome=ESP_ERR_TIMEOUT;duration=300;send(&card,24,512,false);
 outcome=ESP_OK;response=0xe00;send(&card,13,0,false);
 puts("CASE classified");pearl_trace_snapshot();
 response=0x900;
 for(unsigned i=0;i<2100;i++)send(&card,13,0,false);
 puts("CASE wrapped");pearl_trace_snapshot();pearl_trace_events(16);
 pearl_trace_stop();send(&card,25,8192,false);
 puts("CASE stopped");pearl_trace_snapshot();
 sync_busy=true;assert(!pearl_trace_start());sync_busy=false;
 assert(!pearl_trace_ram_sink());pearl_trace_probe(true);assert(pearl_trace_ram_sink());pearl_trace_probe(false);assert(!pearl_trace_ram_sink());
 assert(pearl_trace_start());pearl_trace_sd("/nonexistent");assert(!pearl_trace_sd_busy());
 puts("CASE reset");pearl_trace_snapshot();
 duration=4000000;for(unsigned i=0;i<1300;i++)send(&card,13,0,false);
 puts("CASE long");pearl_trace_snapshot();
 return 0;
}
