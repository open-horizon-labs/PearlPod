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
 f=tmpfile();assert(f);assert(pearl_writer_begin(w,f));
 assert(pearl_writer_append(w,data,sizeof(data)));assert(pearl_writer_finish(w,true));
 f=fopen("/dev/null","rb");assert(f);assert(pearl_writer_begin(w,f));
 assert(pearl_writer_append(w,data,sizeof(data)));assert(!pearl_writer_finish(w,false));assert(pearl_writer_failed(w));
 assert(pearl_writer_destroy(w));
 puts("Async writer wraparound, backpressure, ordered drain, cancellation, restart and write failure pass.");
}
