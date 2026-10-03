#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_SIMD
#include "dr_flac.h"
#include "codec_flac.h"
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
int main(int argc,char **argv){assert(argc==6);unsigned rate=strtoul(argv[3],NULL,10),bits=strtoul(argv[4],NULL,10);bool damaged=atoi(argv[5]);if(!damaged){pearl_tags tags;pearl_metadata(argv[1],&tags);assert(!strcmp(tags.title,"Matrix song")&&!strcmp(tags.artist,"Listener")&&!strcmp(tags.album,"Compatibility")&&tags.track==7);}
 pearl_flac_stream stream;drflac *f=pearl_flac_open(argv[1],&stream);if(damaged&&!f)return 0;if(!f||f->sampleRate!=rate||f->channels!=2||f->bitsPerSample!=bits){fprintf(stderr,"Decoder: %p rate=%u bits=%u channels=%u\n",(void*)f,f?f->sampleRate:0,f?f->bitsPerSample:0,f?f->channels:0);abort();}drwav w;assert(drwav_init_file(&w,argv[2],NULL));int16_t actual[2048],expected[2048];uint64_t count=0;size_t n;while((n=drflac_read_pcm_frames_s16(f,1024,actual))){size_t wanted=drwav_read_pcm_frames_s16(&w,n,expected);assert(wanted==n);if(!damaged)for(size_t i=0;i<n*2;i++)assert(actual[i]==expected[i]);count+=n;}if(damaged)assert(count<w.totalPCMFrameCount);else assert(count==w.totalPCMFrameCount&&count==rate);pearl_flac_close(f,&stream);drwav_uninit(&w);printf("%s: %llu frames %u Hz / %u bits, %s\n",argv[1],(unsigned long long)count,rate,bits,damaged?"damage detected":"PCM matches WAV");}
