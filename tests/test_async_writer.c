#include "async_writer.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static void pause_test(void){struct timespec t={0,1000000};nanosleep(&t,NULL);}
int main(void){
 assert(!pearl_writer_create(1));
 pearl_writer *w=pearl_writer_create(32768);assert(w);
 FILE *f=tmpfile();assert(f); /* Worker owns this handle; retain a duplicate for verification. */
 int copy=dup(fileno(f));assert(copy>=0);
 assert(pearl_writer_begin(w,f));
 unsigned char data[8192];
 for(unsigned i=0;i<128;i++){
  memset(data,i,sizeof(data));
  while(!pearl_writer_append(w,data,sizeof(data)))pause_test();
 }
 assert(pearl_writer_finish(w,false));
 FILE *read=fdopen(copy,"rb");assert(read);rewind(read);
 for(unsigned i=0;i<128;i++){assert(fread(data,1,sizeof(data),read)==sizeof(data));for(unsigned j=0;j<sizeof(data);j++)assert(data[j]==(unsigned char)i);}
 assert(fgetc(read)==EOF);fclose(read);
 /* Irregular TCP segmentation and a non-sector-aligned final tail must
  * survive batching and ring wrap exactly, without waiting for another packet. */
 f=tmpfile();assert(f);copy=dup(fileno(f));assert(copy>=0);
 assert(pearl_writer_begin(w,f));
 unsigned total=0;
 for(unsigned i=0;i<400;i++){
  unsigned n=(i*97)%sizeof(data)+1;
  for(unsigned j=0;j<n;j++)data[j]=(unsigned char)((total+j)%251);
  while(!pearl_writer_append(w,data,n))pause_test();
  total+=n;
 }
 assert(pearl_writer_finish(w,false));read=fdopen(copy,"rb");assert(read);rewind(read);
 for(unsigned i=0;i<total;i++)assert(fgetc(read)==(int)(i%251));
 assert(fgetc(read)==EOF);fclose(read);
 /* Packet-sized writes would pass the byte checks but lose the direct SD path. */
 char trace[512];unsigned calls=0;
 pearl_writer_trace(w,trace,sizeof(trace));
 assert(sscanf(strstr(trace,"sd_calls="),"sd_calls=%u",&calls)==1);
 assert(calls==32+(total+32767)/32768);
 f=tmpfile();assert(f);assert(pearl_writer_begin(w,f));
 assert(pearl_writer_append(w,data,sizeof(data)));assert(pearl_writer_finish(w,true));
 f=fopen("/dev/null","rb");assert(f);assert(pearl_writer_begin(w,f));
 assert(pearl_writer_append(w,data,sizeof(data)));assert(!pearl_writer_finish(w,false));assert(pearl_writer_failed(w));
 assert(pearl_writer_destroy(w));
 puts("Async writer wraparound, backpressure, ordered drain, cancellation, restart and write failure pass.");
}
