"""Compile the production selector against stale, mixed and missing mDNS results."""
from pathlib import Path
import subprocess
import tempfile
s=Path('main/sync.c').read_text();a=s.index('static void choose_library(');b=s.index('\nstatic void task(',a)
code=r'''
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <assert.h>
#define ESP_OK 0
#define ESP_IPADDR_TYPE_V4 4
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(p) 192u,168u,1u,(p)->addr
struct ip4 {unsigned addr;};
typedef struct ip {struct {int type; union {struct ip4 ip4;}u_addr;}addr;struct ip *next;} mdns_ip_addr_t;
typedef struct result {mdns_ip_addr_t *addr;struct result *next;unsigned port;}mdns_result_t;
static atomic_bool cancel;
static mdns_result_t *answer;
static unsigned healthy,queried,freed,probes;
static int mdns_query_ptr(const char *name,const char *protocol,unsigned timeout,unsigned max,mdns_result_t **out){assert(!strcmp(name,"_pearlpod-sync")&&!strcmp(protocol,"_tcp")&&timeout==3000&&max>=2);queried++;*out=answer;return 0;}
static void mdns_query_results_free(mdns_result_t *r){assert(r==answer);freed++;}
static bool library_alive(const char *url){unsigned a,b,c,d,p;probes++;assert(sscanf(url,"http://%u.%u.%u.%u:%u",&a,&b,&c,&d,&p)==5);return d==healthy;}
'''+s[a:b]+r'''
int main(void){
 mdns_ip_addr_t ipv6={.addr.type=6},dead={.addr.type=4,.addr.u_addr.ip4.addr=2},live={.addr.type=4,.addr.u_addr.ip4.addr=254};
 ipv6.next=&dead;mdns_result_t first={.addr=&ipv6,.port=8787},second={.addr=&live,.port=8787};first.next=&second;answer=&first;
 char url[200]={0};healthy=254;
 choose_library(url,sizeof(url),"http://192.0.2.2:8787",true);
 assert(!strcmp(url,"http://192.0.2.254:8787")&&queried==1&&freed==1&&probes==2);
 // Empty discovery still permits a healthy explicit fallback.
 answer=0;url[0]=0;healthy=3;choose_library(url,sizeof(url),"http://192.0.2.3:8787",true);
 assert(!strcmp(url,"http://192.0.2.3:8787"));
 // An obsolete saved endpoint must never become selected without a probe.
 url[0]=0;healthy=254;choose_library(url,sizeof(url),"http://192.0.2.2:8787",true);assert(!url[0]);
 // Failed mDNS initialization must not disable the explicit fallback.
 choose_library(url,sizeof(url),"http://192.0.2.254:8787",false);assert(url[0]);
 url[0]=0;cancel=true;unsigned before=probes;choose_library(url,sizeof(url),"http://192.0.2.254:8787",false);assert(!url[0]&&probes==before);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'discovery.c').write_text(code)
 subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'discovery.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Production library discovery checks passed')
