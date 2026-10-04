/* FAT cannot rename over an existing file. Keep a recoverable old copy until
 * the fully closed upload has its final name. One FTP writer owns this journal. */
#include "replace.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int journal_path(const char *root,char *path,unsigned size) {
 return snprintf(path,size,"%s/.pearl/rename.pending",root)>=(int)size?-1:0;
}
static int exists(const char *p) {struct stat s;return stat(p,&s)==0;}
static int safe(const char *root,const char *path) {
 size_t n=strlen(root);
 return !strncmp(root,path,n)&&path[n]=='/'&&!strstr(path,"/../")&&!strchr(path,'\n')&&!strchr(path,'\r');
}
int pearl_replace_recover(const char *root) {
 char journal[256],destination[256],backup[256],source[256];
 if(journal_path(root,journal,sizeof(journal)))return -1;
 FILE *f=fopen(journal,"rb");
 if(!f)return errno==ENOENT?0:-1;
 int ok=fgets(destination,sizeof(destination),f)&&fgets(backup,sizeof(backup),f)&&fgets(source,sizeof(source),f);
 fclose(f);
 if(!ok){errno=EINVAL;return -1;}
 destination[strcspn(destination,"\r\n")]=0;backup[strcspn(backup,"\r\n")]=0;source[strcspn(source,"\r\n")]=0;
 if(!safe(root,destination)||!safe(root,backup)||!safe(root,source)){errno=EINVAL;return -1;}
 if(!exists(destination)) {
  if(exists(source)) {if(rename(source,destination))return -1;}
  else if(exists(backup)) {if(rename(backup,destination))return -1;}
  else {errno=ENOENT;return -1;}
 }
 if(exists(backup)&&unlink(backup))return -1;
 return unlink(journal);
}
int pearl_replace_file(const char *root,const char *source,const char *destination) {
 if(pearl_replace_recover(root))return -1;
 if(!rename(source,destination))return 0;
 if(errno!=EEXIST)return -1;
 struct stat s;
 if(stat(destination,&s)||!S_ISREG(s.st_mode))return -1;
 char journal[256],temporary[256],backup[256];
 if(journal_path(root,journal,sizeof(journal))||snprintf(temporary,sizeof(temporary),"%s/.pearl/rename.tmp",root)>=(int)sizeof(temporary)||snprintf(backup,sizeof(backup),"%s.old",source)>=(int)sizeof(backup)) {errno=ENAMETOOLONG;return -1;}
 if(!safe(root,source)||!safe(root,destination)){errno=EINVAL;return -1;}
 if(exists(backup)){errno=EEXIST;return -1;}
 FILE *f=fopen(temporary,"wb");if(!f)return -1;
 int ok=fprintf(f,"%s\n%s\n%s\n",destination,backup,source)>0;
 if(fflush(f)||fsync(fileno(f)))ok=0;
 if(fclose(f))ok=0;
 if(!ok){unlink(temporary);return -1;}
 if(rename(temporary,journal))return -1;
 if(rename(destination,backup)) {unlink(journal);return -1;}
 if(rename(source,destination)) {
  int error=errno;
  if(!rename(backup,destination))unlink(journal);
  errno=error;return -1;
 }
 if(unlink(backup))return -1;
 return unlink(journal);
}
