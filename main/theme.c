#include "theme.h"
#include "tomlc17.h"
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef PEARL_THEME_TEST
extern void *pearl_theme_pixels_alloc(size_t bytes);
#define pixel_alloc pearl_theme_pixels_alloc
#elif defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#define pixel_alloc(n) heap_caps_malloc((n),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)
#else
#define pixel_alloc malloc
#endif
static pearl_theme current;
static char farewell_path[512];
static void defaults(void){
 current.background=0x101827;current.surface=0x1d2c40;current.text=0xfff6dc;current.accent=0xffd75e;current.secondary=0x56ddc5;
 snprintf(current.title,sizeof(current.title),"Simple");
 snprintf(current.welcome.heading,sizeof(current.welcome.heading),"Hello!");
 snprintf(current.welcome.phrase,sizeof(current.welcome.phrase),"Your music is here.");
 snprintf(current.farewell.heading,sizeof(current.farewell.heading),"See you soon!");
 snprintf(current.farewell.phrase,sizeof(current.farewell.phrase),"Thanks for listening.");
}
const pearl_theme *pearl_theme_current(void){if(!current.title[0])defaults();return &current;}
void pearl_theme_clear(void){free(current.welcome.pixels);free(current.farewell.pixels);memset(&current,0,sizeof(current));farewell_path[0]=0;defaults();}
static bool text(toml_datum_t d,char *out,size_t cap){
 if(d.type!=TOML_STRING||d.u.str.len<1||(size_t)d.u.str.len>=cap||strlen(d.u.s)!=(size_t)d.u.str.len)return false;
 for(int i=0;i<d.u.str.len;i++)if((unsigned char)d.u.s[i]<32)return false;
 memcpy(out,d.u.s,d.u.str.len+1);return true;
}
static bool basename_ok(const char *s){if(!s[0]||s[0]=='.')return false;for(;*s;s++)if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||*s=='-'||*s=='_'||*s=='.'))return false;return true;}
static toml_result_t document(const char *path){
 struct stat st;toml_result_t bad={0};if(stat(path,&st)||!S_ISREG(st.st_mode)||st.st_size<1||st.st_size>8192)return bad;
 FILE *f=fopen(path,"rb");if(!f)return bad;char *b=malloc(st.st_size+1);if(!b){fclose(f);return bad;}
 size_t n=fread(b,1,st.st_size,f);fclose(f);b[n]=0;
 toml_result_t r={0};if(n==(size_t)st.st_size&&!memchr(b,0,n))r=toml_parse(b,n);free(b);return r;
}
static void phrase(char *out,size_t cap,const char *tmpl){
 size_t used=0;while(*tmpl&&used+1<cap){const char *part=NULL;size_t n=0;
 if(!strncmp(tmpl,"{name}",6)){part=current.name[0]?current.name:"friend";n=strlen(part);tmpl+=6;}
 else{part=tmpl++;n=1;}if(used+n>=cap)break;memcpy(out+used,part,n);used+=n;}out[used]=0;
}
static uint32_t color(toml_datum_t table,const char *key,uint32_t fallback){char s[9];if(!text(toml_get(table,key),s,sizeof(s))||strlen(s)!=7||s[0]!='#')return fallback;for(int i=1;i<7;i++)if(!isxdigit((unsigned char)s[i]))return fallback;char *end;unsigned long v=strtoul(s+1,&end,16);return *end?fallback:(uint32_t)v;}
static uint8_t *read_pixels(const char *path){
 struct stat st;if(!path[0]||stat(path,&st)||!S_ISREG(st.st_mode)||st.st_size!=PEARL_THEME_PIXELS)return NULL;
 FILE *f=fopen(path,"rb");if(!f)return NULL;
 uint8_t *p=pixel_alloc(PEARL_THEME_PIXELS);
 if(p&&fread(p,1,PEARL_THEME_PIXELS,f)!=PEARL_THEME_PIXELS){free(p);p=NULL;}
 fclose(f);return p;
}
uint8_t *pearl_theme_read_farewell(void){return read_pixels(farewell_path);}
void pearl_theme_publish_farewell(uint8_t *pixels){if(current.farewell.pixels)free(pixels);else current.farewell.pixels=pixels;}
static void scene(toml_datum_t table,const char *key,const char *dir,uint32_t seq,pearl_theme_scene *out,bool deferred){
 toml_datum_t list=toml_get(table,key);if(list.type!=TOML_ARRAY||list.u.arr.size<1||list.u.arr.size>8)return;
 toml_datum_t item=list.u.arr.elem[seq%(unsigned)list.u.arr.size];if(item.type!=TOML_TABLE)return;
 char heading[96],line[128],image[65],path[512];
 if(text(toml_get(item,"heading"),heading,sizeof(heading)))phrase(out->heading,sizeof(out->heading),heading);
 if(text(toml_get(item,"phrase"),line,sizeof(line)))phrase(out->phrase,sizeof(out->phrase),line);
 if(!text(toml_get(item,"image"),image,sizeof(image))||!basename_ok(image))return;
 if(snprintf(path,sizeof(path),"%s/%s",dir,image)>=(int)sizeof(path))return;
 if(deferred)snprintf(farewell_path,sizeof(farewell_path),"%s",path);
 else out->pixels=read_pixels(path);
}
void pearl_theme_load(const char *root,uint32_t seq){
 pearl_theme_clear();char path[512],id[49]={0};
 if(snprintf(path,sizeof(path),"%s/Person.toml",root)>=(int)sizeof(path))return;
 toml_result_t person=document(path);if(!person.ok){toml_free(person);return;}
 text(toml_get(person.toptab,"name"),current.name,sizeof(current.name));
 if(current.name[0])snprintf(current.welcome.heading,sizeof(current.welcome.heading),"Hi, %s!",current.name);
 if(current.name[0])snprintf(current.farewell.heading,sizeof(current.farewell.heading),"See you soon, %s!",current.name);
 toml_datum_t favorites=toml_get(person.toptab,"themes"),rotation=toml_get(person.toptab,"rotate");
 bool rotate=rotation.type==TOML_BOOLEAN&&rotation.u.boolean;
 if(favorites.type==TOML_ARRAY&&favorites.u.arr.size>0&&favorites.u.arr.size<=8)text(favorites.u.arr.elem[rotate?seq%(unsigned)favorites.u.arr.size:0],id,sizeof(id));
 unsigned scene_seq=rotate&&favorites.type==TOML_ARRAY&&favorites.u.arr.size>0?seq/(unsigned)favorites.u.arr.size:seq;
 toml_free(person);if(!basename_ok(id))return;
 if(snprintf(path,sizeof(path),"%s/Themes/%s/theme.toml",root,id)>=(int)sizeof(path))return;
 toml_result_t pack=document(path);if(!pack.ok){toml_free(pack);return;}
 toml_datum_t version=toml_get(pack.toptab,"version");if(version.type!=TOML_INT64||version.u.int64!=1){toml_free(pack);return;}
 snprintf(current.id,sizeof(current.id),"%s",id);text(toml_get(pack.toptab,"title"),current.title,sizeof(current.title));
 toml_datum_t palette=toml_get(pack.toptab,"palette");
 current.background=color(palette,"background",current.background);current.surface=color(palette,"surface",current.surface);current.text=color(palette,"text",current.text);current.accent=color(palette,"accent",current.accent);current.secondary=color(palette,"secondary",current.secondary);
 char dir[512];snprintf(dir,sizeof(dir),"%s/Themes/%s",root,id);
 scene(pack.toptab,"welcome",dir,scene_seq,&current.welcome,false);scene(pack.toptab,"farewell",dir,scene_seq,&current.farewell,true);toml_free(pack);
}
