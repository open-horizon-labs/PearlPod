#include "artwork.h"
#include "player.h"
#include "metadata.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_SIMD
#define STBI_MAX_DIMENSIONS 1024
#include "stb_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#define ART_LIMIT (400*1024)
static uint32_t be32(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static uint32_t sync32(const uint8_t *p){return ((p[0]&127)<<21)|((p[1]&127)<<14)|((p[2]&127)<<7)|(p[3]&127);}
static uint8_t *read_blob(FILE *f,size_t n,size_t *size){if(!n||n>ART_LIMIT)return NULL;uint8_t *b=malloc(n);if(!b)return NULL;if(fread(b,1,n,f)!=n){free(b);return NULL;}*size=n;return b;}
uint8_t *pearl_art_payload(const char *path,size_t *size){
 *size=0;FILE *f=fopen(path,"rb");if(!f)return NULL;uint8_t *result=NULL;uint8_t h[10];
 const char *ext=strrchr(path,'.');
 if(ext&&(!strcasecmp(ext,".jpg")||!strcasecmp(ext,".jpeg")||!strcasecmp(ext,".png"))){fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);if(n>0)result=read_blob(f,n,size);goto end;}
 if(ext&&!strcasecmp(ext,".flac"))pearl_audio_offset(f);
 if(fread(h,1,10,f)!=10)goto end;
 if(!memcmp(h,"ID3",3)&&(h[3]==3||h[3]==4)&&!(h[5]&0xc0)){
  uint8_t version=h[3];uint32_t remaining=sync32(h+6);
  while(remaining>=10){if(fread(h,1,10,f)!=10)break;remaining-=10;if(!h[0])break;uint32_t n=version==4?sync32(h+4):be32(h+4);if(n>remaining)break;remaining-=n;
   if(!memcmp(h,"APIC",4)&&n<=ART_LIMIT&&h[8]==0&&h[9]==0){size_t got;uint8_t *blob=read_blob(f,n,&got);if(!blob)break;
    size_t p=1;while(p<got&&blob[p])p++;p++;if(p>=got){free(blob);break;}p++; // picture type
    if(blob[0]==0||blob[0]==3){while(p<got&&blob[p])p++;p++;}
    else {while(p+1<got&&(blob[p]||blob[p+1]))p+=2;p+=2;}
    if(p<got){memmove(blob,blob+p,got-p);*size=got-p;result=blob;break;}free(blob);
   }else if(fseek(f,n,SEEK_CUR))break;
  }
 }else if(!memcmp(h,"fLaC",4)){
  long base=pearl_audio_offset(f);fseek(f,base+4,SEEK_SET);bool_last:;
  uint8_t block[4];if(fread(block,1,4,f)!=4)goto end;unsigned n=((unsigned)block[1]<<16)|(block[2]<<8)|block[3];
  if((block[0]&127)==6&&n<=ART_LIMIT){size_t got;uint8_t *blob=read_blob(f,n,&got);if(!blob)goto end;size_t p=4;
   if(p+4>got){free(blob);goto end;}uint32_t mime=be32(blob+p);p+=4;if(mime>got-p){free(blob);goto end;}p+=mime;
   if(p+4>got){free(blob);goto end;}uint32_t desc=be32(blob+p);p+=4;if(desc>got-p){free(blob);goto end;}p+=desc;
   if(p+20>got){free(blob);goto end;}p+=16;uint32_t bytes=be32(blob+p);p+=4;if(bytes&&bytes<=got-p){memmove(blob,blob+p,bytes);*size=bytes;result=blob;goto end;}free(blob);
  }else if(fseek(f,n,SEEK_CUR))goto end;
  if(!(block[0]&128))goto bool_last;
 }
end:fclose(f);return result;
}

uint8_t *pearl_art_pixels(const char *path){
    size_t n;uint8_t *blob=pearl_art_payload(path,&n);if(!blob)return NULL;
    int w,h,c;uint8_t *rgb=stbi_load_from_memory(blob,n,&w,&h,&c,3);free(blob);
    if(!rgb||w<=0||h<=0){stbi_image_free(rgb);return NULL;}
    uint8_t *p=malloc(PEARL_ART_SIZE*PEARL_ART_SIZE*2);
    if(p){int crop=w<h?w:h;int ox=(w-crop)/2,oy=(h-crop)/2;
        for(int y=0;y<PEARL_ART_SIZE;y++)for(int x=0;x<PEARL_ART_SIZE;x++){
            int at=((oy+y*crop/PEARL_ART_SIZE)*w+ox+x*crop/PEARL_ART_SIZE)*3;
            uint16_t color=((rgb[at]>>3)<<11)|((rgb[at+1]>>2)<<5)|(rgb[at+2]>>3);unsigned out=(y*PEARL_ART_SIZE+x)*2;
            p[out]=color>>8;p[out+1]=color;
        }
    }
    stbi_image_free(rgb);return p;
}
