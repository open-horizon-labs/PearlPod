#include "captive_dns.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char query[]={0x12,0x34,1,0,0,1,0,0,0,0,0,0,7,'c','a','p','t','i','v','e',5,'a','p','p','l','e',3,'c','o','m',0,0,1,0,1};
int main(void){
 unsigned char reply[512],q[512];size_t n=sizeof(query);
 int length=pearl_captive_dns_reply(query,n,reply,sizeof(reply));
 assert(length==(int)n+16&&reply[0]==0x12&&reply[1]==0x34&&reply[7]==1);
 assert(!memcmp(reply+length-4,"\xc0\xa8\x04\x01",4));
 memcpy(q,query,n);q[n-3]=28;
 assert(pearl_captive_dns_reply(q,n,reply,sizeof(reply))==(int)n&&reply[7]==0); /* AAAA */
 q[n-3]=65;assert(pearl_captive_dns_reply(q,n,reply,sizeof(reply))==(int)n&&reply[7]==0); /* HTTPS */
 for(size_t size=0;size<n;size++)assert(pearl_captive_dns_reply(query,size,reply,sizeof(reply))<0);
 memcpy(q,query,n);q[12]=0xc0;assert(pearl_captive_dns_reply(q,n,reply,sizeof(reply))<0);
 memcpy(q,query,n);q[5]=2;assert(pearl_captive_dns_reply(q,n,reply,sizeof(reply))<0);
 assert(pearl_captive_dns_reply(query,n,reply,n)<0);
 unsigned state=7;
 for(unsigned i=0;i<10000;i++){
  size_t size=i%sizeof(q);
  for(size_t j=0;j<size;j++){state=state*1664525u+1013904223u;q[j]=state>>24;}
  int result=pearl_captive_dns_reply(q,size,reply,sizeof(reply));assert(result<0||result<=512);
 }
 puts("Captive A/AAAA/HTTPS replies, truncated/compressed/multiple questions and 10000 malformed packets pass");
}
