#include "theme.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
static bool fail_pixels;
void *pearl_theme_pixels_alloc(size_t bytes){return fail_pixels?NULL:malloc(bytes);}
static void write_file(const char *p,const char *s){FILE *f=fopen(p,"wb");assert(f);fputs(s,f);fclose(f);}
int main(void){
 pearl_theme_load("/does-not-exist",0);assert(!strcmp(pearl_theme_current()->welcome.heading,"Hello!"));assert(!pearl_theme_current()->welcome.pixels);
 pearl_theme_load("theme-packs/Default",0);assert(!strcmp(pearl_theme_current()->id,"midnight"));assert(strstr(pearl_theme_current()->welcome.heading,"Listener"));assert(pearl_theme_current()->welcome.pixels&&pearl_theme_current()->farewell.pixels);
 fail_pixels=true;pearl_theme_load("theme-packs/Default",0);assert(!pearl_theme_current()->welcome.pixels&&!pearl_theme_current()->farewell.pixels);assert(strstr(pearl_theme_current()->welcome.heading,"Listener"));fail_pixels=false;pearl_theme_load("theme-packs/Default",0);
 char first[96];strcpy(first,pearl_theme_current()->welcome.heading);
 pearl_theme_load("theme-packs/Default",1);assert(!strcmp(pearl_theme_current()->id,"sakura"));
 pearl_theme_load("theme-packs/Default",2);assert(!strcmp(pearl_theme_current()->id,"midnight"));assert(strcmp(first,pearl_theme_current()->welcome.heading));
 char root[]="/tmp/pearl-theme-XXXXXX";assert(mkdtemp(root));char path[512],themes[512],pack[512];snprintf(themes,sizeof(themes),"%s/Themes",root);mkdir(themes,0700);snprintf(pack,sizeof(pack),"%s/Themes/test",root);mkdir(pack,0700);
 snprintf(path,sizeof(path),"%s/Person.toml",root);write_file(path,"name='Jonah'\nthemes=['test']\n");
 snprintf(path,sizeof(path),"%s/Themes/test/theme.toml",root);write_file(path,"version=1\n[[welcome]]\nheading='Hi, {name}!'\nimage='../escape.rgb565'\n[[farewell]]\nimage='missing.rgb565'\n");
 pearl_theme_load(root,0);assert(!strcmp(pearl_theme_current()->welcome.heading,"Hi, Jonah!"));assert(!pearl_theme_current()->welcome.pixels&&!pearl_theme_current()->farewell.pixels);
 write_file(path,"version=1\n[[welcome]]\nimage='short.rgb565'\n");char image[512];snprintf(image,sizeof(image),"%s/Themes/test/short.rgb565",root);write_file(image,"short");pearl_theme_load(root,0);assert(!pearl_theme_current()->welcome.pixels);
 FILE *large=fopen(image,"wb");assert(large);for(int i=0;i<PEARL_THEME_PIXELS+1;i++)fputc(0,large);fclose(large);pearl_theme_load(root,0);assert(!pearl_theme_current()->welcome.pixels);
 write_file(path,"version=2\n");pearl_theme_load(root,0);assert(!pearl_theme_current()->id[0]);
 write_file(path,"version=[broken");pearl_theme_load(root,0);assert(!pearl_theme_current()->id[0]);assert(strstr(pearl_theme_current()->welcome.heading,"Jonah"));
 FILE *f=fopen(path,"wb");assert(f);for(int i=0;i<9000;i++)fputc('x',f);fclose(f);pearl_theme_load(root,0);assert(!pearl_theme_current()->id[0]);
 unlink(image);unlink(path);snprintf(path,sizeof(path),"%s/Person.toml",root);write_file(path,"name='Alex'\nthemes=['../escape']\n");pearl_theme_load(root,0);assert(!pearl_theme_current()->id[0]);assert(strstr(pearl_theme_current()->welcome.heading,"Alex"));
 unlink(path);
 char link[512];snprintf(link,sizeof(link),"%s/Themes/midnight",root);char *target=realpath("theme-packs/Default/Themes/midnight",NULL);assert(target);assert(!symlink(target,link));free(target);
 write_file(path,"name='Alex'\nthemes=['midnight']\n");
 for(unsigned i=0;i<2;i++){pearl_theme_load(root,i);assert(pearl_theme_current()->welcome.pixels&&pearl_theme_current()->farewell.pixels);assert(strstr(pearl_theme_current()->welcome.heading,"Alex")&&!strstr(pearl_theme_current()->welcome.heading,"Listener"));assert(strstr(pearl_theme_current()->farewell.heading,"Alex"));}
 unlink(link);unlink(path);rmdir(pack);rmdir(themes);rmdir(root);pearl_theme_clear();puts("Theme rotation, name independence, missing card, malformed/oversized packs and invalid assets pass");
}
