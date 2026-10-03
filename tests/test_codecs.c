#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "minimp3.h"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_SIMD
#include "dr_flac.h"
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
int main(int argc,char **argv){
 assert(argc==4);short pcm[2304];
 drwav w;assert(drwav_init_file(&w,argv[1],NULL));assert(w.channels==2&&w.sampleRate==44100);unsigned long frames=0,n;
 while((n=drwav_read_pcm_frames_s16(&w,1024,pcm)))frames+=n;assert(frames==88200);drwav_uninit(&w);
 drflac *f=drflac_open_file(argv[2],NULL);assert(f&&f->channels==2&&f->sampleRate==44100);frames=0;
 while((n=drflac_read_pcm_frames_s16(f,1024,pcm)))frames+=n;assert(frames==88200);drflac_close(f);
 FILE *m=fopen(argv[3],"rb");assert(m);fseek(m,0,SEEK_END);long size=ftell(m);rewind(m);unsigned char *bytes=malloc(size);assert(fread(bytes,1,size,m)==(unsigned long)size);fclose(m);
 mp3dec_t dec;mp3dec_init(&dec);size_t pos=0;frames=0;
 while(pos<(size_t)size){mp3dec_frame_info_t info;int samples=mp3dec_decode_frame(&dec,bytes+pos,size-pos,pcm,&info);if(info.frame_bytes<=0)break;pos+=info.frame_bytes;if(samples){assert(info.channels==2&&info.hz==44100);frames+=samples;}}
 assert(frames>=88200&&frames<92000);free(bytes);puts("Real MP3, FLAC and WAV fixtures decode to expected stereo frame counts.");
}
