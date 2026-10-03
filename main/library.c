#include "player.h"
#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

int pearl_volume(int current,int delta,int ceiling) { int n=current+delta;return n<0?0:n>ceiling?ceiling:n; }
pearl_button_event pearl_button_update(pearl_button *b,bool pressed,uint32_t now) {
    if(pressed!=b->raw){b->raw=pressed;b->changed=now;}
    if(now-b->changed>=35 && b->stable!=b->raw){
        b->stable=b->raw;
        if(b->stable){b->pressed=now;b->long_sent=false;}
        else if(!b->long_sent)return BUTTON_SHORT;
    }
    if(b->stable && !b->long_sent && now-b->pressed>=1800){b->long_sent=true;return BUTTON_LONG;}
    return BUTTON_NONE;
}
static void title_file(const char *path,char *out,size_t capacity){FILE *f=fopen(path,"rb");if(!f)return;char name[PEARL_NAME];if(fgets(name,sizeof(name),f)){name[strcspn(name,"\r\n")]=0;if(name[0])snprintf(out,capacity,"%s",name);}fclose(f);}
static bool audio(const char *s) {const char *e=strrchr(s,'.');return e && (!strcasecmp(e,".mp3")||!strcasecmp(e,".flac")||!strcasecmp(e,".wav"));}
static int natural(const char *a,const char *b) {
    while(*a && *b){
        if(isdigit((unsigned char)*a)&&isdigit((unsigned char)*b)){
            char *ae,*be; unsigned long av=strtoul(a,&ae,10),bv=strtoul(b,&be,10);
            if(av!=bv)return av<bv?-1:1;
            a=ae;b=be;
        }else{int d=tolower((unsigned char)*a)-tolower((unsigned char)*b);if(d)return d;a++;b++;}
    }
    return (unsigned char)*a-(unsigned char)*b;
}
static int track_cmp(const void *av,const void *bv){const pearl_track *a=av,*b=bv;return a->album!=b->album?(a->album<b->album?-1:1):natural(a->path,b->path);}
static void scan(pearl_library *l,const char *dir,unsigned depth) {
    if(depth>12){l->truncated=true;return;}
    DIR *d=opendir(dir);if(!d)return;
    struct dirent *e;int album=-1;
    while((e=readdir(d))){
        if(e->d_name[0]=='.')continue;
        char path[PEARL_PATH];if(snprintf(path,sizeof(path),"%s/%s",dir,e->d_name)>=(int)sizeof(path)){l->truncated=true;continue;}
        struct stat st;if(stat(path,&st))continue;
        if(S_ISDIR(st.st_mode))scan(l,path,depth+1);
        else if(S_ISREG(st.st_mode)&&audio(e->d_name)){
            if(l->track_count==PEARL_MAX_TRACKS){l->truncated=true;continue;}
            if(album<0){
                if(l->album_count==PEARL_MAX_ALBUMS){l->truncated=true;continue;}
                album=l->album_count++;pearl_album *a=&l->albums[album];
                snprintf(a->path,sizeof(a->path),"%s",dir);
                const char *name=strrchr(dir,'/');snprintf(a->title,sizeof(a->title),"%s",name?name+1:dir);
                char title_path[PEARL_PATH];
                if(snprintf(title_path,sizeof(title_path),"%s/.pearl-title",dir)<(int)sizeof(title_path))title_file(title_path,a->title,sizeof(a->title));
                // Raw artwork is prepared by tools/prepare_card.py; no decoding on audio/UI tasks.
                const char *arts[]={"pearl-cover.rgb","cover.rgb","folder.rgb","cover.jpg","folder.jpg","cover.png","folder.png","Cover.jpg","Folder.jpg"};
                for(unsigned i=0;i<sizeof(arts)/sizeof(arts[0]);i++){
                    char p[PEARL_PATH];if(snprintf(p,sizeof(p),"%s/%s",dir,arts[i])>=(int)sizeof(p))continue;
                    struct stat ast;if(!stat(p,&ast)&&((i<3&&ast.st_size==PEARL_ART_SIZE*PEARL_ART_SIZE*2)||(i>=3&&ast.st_size>0&&ast.st_size<=400*1024))){strcpy(a->art,p);break;}
                }
            }
            pearl_track *t=&l->tracks[l->track_count++];strcpy(t->path,path);t->album=album;
            snprintf(t->title,sizeof(t->title),"%.159s",e->d_name);char *dot=strrchr(t->title,'.');if(dot)*dot=0;
            char title_path[PEARL_PATH];if(snprintf(title_path,sizeof(title_path),"%s.pearl-title",path)<(int)sizeof(title_path))title_file(title_path,t->title,sizeof(t->title));
        }
    }
    closedir(d);
}
int pearl_library_scan(pearl_library *l,const char *root){
    memset(l,0,sizeof(*l));l->tracks=calloc(PEARL_MAX_TRACKS,sizeof(*l->tracks));l->albums=calloc(PEARL_MAX_ALBUMS,sizeof(*l->albums));
    if(!l->tracks||!l->albums){pearl_library_free(l);return -1;}
    DIR *d=opendir(root);if(!d){pearl_library_free(l);return -2;}closedir(d);
    scan(l,root,0);qsort(l->tracks,l->track_count,sizeof(*l->tracks),track_cmp);
    for(unsigned i=0;i<l->track_count;i++){pearl_album *a=&l->albums[l->tracks[i].album];if(a->count==0)a->first=i;a->count++;}
    return 0;
}
void pearl_library_free(pearl_library *l){free(l->tracks);free(l->albums);memset(l,0,sizeof(*l));}
int pearl_next(const pearl_library *l,int current,int direction){
    if(current<0||(unsigned)current>=l->track_count)return l->track_count?0:-1;
    const pearl_album *a=&l->albums[l->tracks[current].album];
    int relative=(current-(int)a->first+direction)%(int)a->count;if(relative<0)relative+=a->count;
    return a->first+relative;
}
