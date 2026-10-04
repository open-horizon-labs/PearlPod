"""Exercise the production TCP send helper with deterministic short writes."""
from pathlib import Path
import subprocess
import tempfile
s=Path('vendor/ftp/ftp.c').read_text();a=s.index('static bool ftp_send_all(');b=s.index('\nstatic void ftp_send_reply(',a)
code=r'''
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
static unsigned calls,mode,delays;
static uint64_t clock_value;
static unsigned char output[100];static unsigned used;
static uint64_t trace_clock(void){clock_value+=1000000;return clock_value;}
static void vTaskDelay(unsigned n){delays+=n;}
static int send(int socket,const void *data,size_t size,int flags){
 (void)socket;(void)flags;calls++;
 if(mode==1){errno=EAGAIN;return -1;}
 if(mode==2){errno=EPIPE;return -1;}
 if(mode==3)return 0;
 if(calls==2){errno=EAGAIN;return -1;}
 if(calls==1){errno=EINTR;return -1;}
 size_t n=size>5?5:size;memcpy(output+used,data,n);used+=n;return (int)n;
}
'''+s[a:b]+r'''
int main(void){
 const char payload[]="hello world";
 assert(ftp_send_all(1,payload,sizeof(payload)));assert(used==sizeof(payload));assert(!memcmp(output,payload,sizeof(payload)));assert(delays==1);
 calls=used=delays=0;clock_value=0;mode=1;assert(!ftp_send_all(1,payload,sizeof(payload)));assert(errno==ETIMEDOUT&&calls<=5&&used==0);
 mode=2;assert(!ftp_send_all(1,payload,sizeof(payload)));assert(errno==EPIPE);
 mode=3;assert(!ftp_send_all(1,payload,sizeof(payload)));
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'send.c').write_text(code)
 subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'send.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Production short TCP sends, backpressure, timeout and disconnect checks passed')
