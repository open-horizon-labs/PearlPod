#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "replace.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static unsigned calls,fail;
int pearl_test_rename(const char *a,const char *b) {
 calls++;if(calls==fail){errno=ENOSPC;return -1;}
 if(access(b,F_OK)==0){errno=EEXIST;return -1;}
 return renameat(AT_FDCWD,a,AT_FDCWD,b);
}
static void write_file(const char *p,const char *s){FILE *f=fopen(p,"wb");assert(f);assert(fputs(s,f)>=0);assert(!fclose(f));}
static void check(const char *p,const char *s){char buf[20]={0};FILE *f=fopen(p,"rb");assert(f);assert(fread(buf,1,sizeof(buf)-1,f)==strlen(s));fclose(f);assert(!strcmp(buf,s));}
int main(void) {
 char dir[]="/tmp/pearl-replace-XXXXXX";assert(mkdtemp(dir));
 char old[256],source[256],backup[256],journal[256],meta[256];
 snprintf(old,sizeof(old),"%s/playlist.m3u8",dir);snprintf(source,sizeof(source),"%s/.in.playlist",dir);
 snprintf(backup,sizeof(backup),"%s/.in.playlist.old",dir);snprintf(meta,sizeof(meta),"%s/.pearl",dir);assert(!mkdir(meta,0700));
 snprintf(journal,sizeof(journal),"%s/.pearl/rename.pending",dir);
 write_file(old,"old");write_file(source,"new");assert(!pearl_replace_file(dir,source,old));check(old,"new");assert(access(journal,F_OK));
 write_file(source,"next");calls=0;fail=4;assert(pearl_replace_file(dir,source,old));check(old,"new");assert(access(journal,F_OK));fail=0;
 // A reset after the old name moved, before the new name moved.
 assert(!renameat(AT_FDCWD,old,AT_FDCWD,backup));
 FILE *f=fopen(journal,"wb");assert(f);fprintf(f,"%s\n%s\n%s\n",old,backup,source);fclose(f);
 assert(!pearl_replace_recover(dir));check(old,"next");assert(access(backup,F_OK));
 // Reset after new publication but before removing the old backup.
 write_file(backup,"previous");write_file(journal,"");f=fopen(journal,"wb");assert(f);fprintf(f,"%s\n%s\n%s\n",old,backup,source);fclose(f);
 assert(!pearl_replace_recover(dir));check(old,"next");assert(access(backup,F_OK));
 assert(!pearl_replace_recover(dir));unlink(old);rmdir(meta);rmdir(dir);puts("FAT replacement and reset recovery passed");
}
