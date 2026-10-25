#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "minimp3_ex.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static size_t read_file(void*b,size_t n,void*f){return fread(b,1,n,f);}
static int seek_file(uint64_t n,void*f){return fseek(f,(long)n,SEEK_SET);}
int main(int argc,char**argv){assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);mp3dec_ex_t*d=calloc(1,sizeof(*d));assert(d);mp3dec_io_t io={.read=read_file,.read_data=f,.seek=seek_file,.seek_data=f};assert(!mp3dec_ex_open_cb(d,&io,MP3D_SEEK_TO_SAMPLE));assert(d->samples&&d->info.hz&&d->info.channels);size_t total=d->samples;mp3d_sample_t*all=malloc(total*sizeof(*all));assert(all);size_t n=mp3dec_ex_read(d,all,total);assert(n==total);uint64_t point=(uint64_t)d->info.hz*d->info.channels*1500/1000;assert(point+4096<total);assert(!mp3dec_ex_seek(d,point));mp3d_sample_t actual[4096];assert(mp3dec_ex_read(d,actual,4096)==4096);assert(!memcmp(actual,all+point,sizeof(actual)));assert(!mp3dec_ex_seek(d,0));assert(mp3dec_ex_read(d,actual,4096)==4096&&!memcmp(actual,all,sizeof(actual)));mp3dec_ex_close(d);free(d);free(all);fclose(f);puts("Real MP3 callback decoder: exact 1.5s seek and restart match silent full decode");}
