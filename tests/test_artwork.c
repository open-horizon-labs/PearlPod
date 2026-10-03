#include "artwork.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
static void be(FILE *f,uint32_t n){for(int i=3;i>=0;i--)fputc((n>>(i*8))&255,f);}
int main(void){
 char p[]="/tmp/pearl-art-XXXXXX";int fd=mkstemp(p);close(fd);FILE *f=fopen(p,"wb");
 uint8_t jpeg[]={0xff,0xd8,0xff,0xd9};uint8_t apic[]={0,'i','m','a','g','e','/','j','p','e','g',0,3,0,0xff,0xd8,0xff,0xd9};
 uint8_t tag[]={'I','D','3',3,0,0,0,0,0,28};fwrite(tag,1,10,f);fwrite("APIC",1,4,f);be(f,sizeof(apic));fputc(0,f);fputc(0,f);fwrite(apic,1,sizeof(apic),f);fclose(f);
 size_t n;uint8_t *b=pearl_art_payload(p,&n);assert(b&&n==4&&!memcmp(b,jpeg,4));free(b);
 f=fopen(p,"wb");tag[9]=20;fwrite(tag,1,10,f);fwrite("APIC",1,4,f);be(f,99999999);fputc(0,f);fputc(0,f);fclose(f);assert(!pearl_art_payload(p,&n));
 f=fopen(p,"wb");fwrite("fLaC",1,4,f);fputc(0x86,f);fputc(0,f);fputc(0,f);fputc(36,f);be(f,3);be(f,0);be(f,0);be(f,240);be(f,240);be(f,24);be(f,0);be(f,4);fwrite(jpeg,1,4,f);fclose(f);
 b=pearl_art_payload(p,&n);assert(b&&n==4&&!memcmp(b,jpeg,4));free(b);
 f=fopen(p,"wb");fwrite("fLaC",1,4,f);fputc(0x86,f);fputc(0,f);fputc(0,f);fputc(8,f);be(f,3);be(f,99999);fclose(f);assert(!pearl_art_payload(p,&n));unlink(p);
 puts("Embedded MP3/FLAC art extraction and adversarial length checks pass.");
}
