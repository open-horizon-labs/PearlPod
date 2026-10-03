#include "metadata.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
static void frame(FILE *f,const char *id,const unsigned char *data,size_t n){unsigned char h[10]={0};memcpy(h,id,4);h[4]=n>>24;h[5]=n>>16;h[6]=n>>8;h[7]=n;fwrite(h,1,10,f);fwrite(data,1,n,f);}
static void finish(FILE *f){long n=ftell(f)-10;unsigned char h[10]={'I','D','3',3};h[6]=(n>>21)&127;h[7]=(n>>14)&127;h[8]=(n>>7)&127;h[9]=n&127;rewind(f);fwrite(h,1,10,f);fclose(f);}
int main(void){char path[]="/tmp/pearl-tag-XXXXXX";int fd=mkstemp(path);assert(fd>=0);close(fd);FILE *f=fopen(path,"wb+");assert(f);unsigned char h[10]={'I','D','3',3};fwrite(h,1,10,f);const unsigned char utf16[]={1,255,254,'A',0,'l',0,'i',0,'c',0,'e',0,' ',0,0x3d,0xd8,0x0a,0xde};frame(f,"TIT2",utf16,sizeof(utf16));const unsigned char track[]={3,'1','2','/','2','0'};frame(f,"TRCK",track,sizeof(track));finish(f);pearl_tags t;pearl_metadata(path,&t);assert(!strcmp(t.title,"Alice \xf0\x9f\x98\x8a")&&t.track==12);
 f=fopen(path,"wb+");assert(f);fwrite(h,1,10,f);unsigned char *cover=calloc(300*1024,1);assert(cover);frame(f,"APIC",cover,300*1024);free(cover);const unsigned char artist[]={3,'A','l','i','c','e'};frame(f,"TPE1",artist,sizeof(artist));finish(f);pearl_metadata(path,&t);assert(!strcmp(t.artist,"Alice"));
 f=fopen(path,"wb");assert(f);fwrite("fLaC",1,4,f);unsigned char bad[]={0x84,0,0,8,255,255,255,255,1,0,0,0};fwrite(bad,1,sizeof(bad),f);fclose(f);pearl_metadata(path,&t);assert(!t.title[0]&&!t.artist[0]);
 f=fopen(path,"wb");assert(f);h[6]=127;h[7]=127;h[8]=127;h[9]=127;fwrite(h,1,10,f);fclose(f);pearl_metadata(path,&t);f=fopen(path,"rb");assert(pearl_audio_offset(f)==0);fclose(f);
 f=fopen(path,"wb");assert(f);unsigned char legacy[128]={0};memcpy(legacy,"TAG",3);memcpy(legacy+3,"Legacy title",12);memcpy(legacy+33,"Alice",5);memcpy(legacy+63,"Legacy album",12);legacy[126]=9;fwrite(legacy,1,sizeof(legacy),f);fclose(f);pearl_metadata(path,&t);assert(!strcmp(t.title,"Legacy title")&&!strcmp(t.artist,"Alice")&&t.track==9);
 f=fopen(path,"wb");assert(f);unsigned char old[]={ 'I','D','3',2,0,0,0,0,0,13,'T','T','2',0,0,7,0,'L','e','g','a','c','y'};fwrite(old,1,sizeof(old),f);fclose(f);pearl_metadata(path,&t);assert(!strcmp(t.title,"Legacy"));
 f=fopen(path,"wb");assert(f);unsigned char wave[]={ 'R','I','F','F',34,0,0,0,'W','A','V','E','L','I','S','T',22,0,0,0,'I','N','F','O','I','N','A','M',10,0,0,0,'W','a','v','e',' ','s','o','n','g',0};fwrite(wave,1,sizeof(wave),f);fclose(f);pearl_metadata(path,&t);assert(!strcmp(t.title,"Wave song"));remove(path);puts("UTF-16 surrogate text, track fractions, metadata after large artwork, hostile lengths and bounded ID3 audio offsets pass.");}
