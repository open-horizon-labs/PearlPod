#include "ftp.h"
#include "ftp_platform.h"
#include "sync_progress.h"
#include <signal.h>
#include <string.h>
#include <sys/resource.h>
extern const char *MOUNT_POINT;
extern void pearl_ftp_close(void);
extern unsigned pearl_ftp_received_bytes(void);
extern void pearl_ftp_reset_progress(void);
bool pearl_trace_ram_sink(void){return getenv("PEARL_FTP_RAM_PROBE")!=NULL;}
static volatile sig_atomic_t stop;
static bool progress_info(const char *json) {
  pearl_transfer_info info;
  if(!pearl_progress_parse(json,&info))return false;
  const char *path=getenv("PEARL_PROGRESS_LOG");
  if(path){FILE *log=fopen(path,"ab");if(!log)return false;fprintf(log,"%s\n",json);fclose(log);}
  if(getenv("PEARL_FTP_CRASH_AFTER_PLAYLIST")&&!strcmp(info.kind,"saved"))stop=1;
  return true;
}
static void stopping(int signal) {
  (void)signal;
  stop = 1;
}
int main(int argc, char **argv) {
  if (argc != 2)
    return 1;
  MOUNT_POINT = argv[1];
  if(getenv("PEARL_FTP_LIMIT")){struct rlimit limit={32768,32768};setrlimit(RLIMIT_FSIZE,&limit);signal(SIGXFSZ,SIG_IGN);}
  unsigned delay=getenv("PEARL_FTP_DELAY_US")?strtoul(getenv("PEARL_FTP_DELAY_US"),NULL,10):1000;
  signal(SIGTERM, stopping);
  signal(SIGINT, stopping);
  if (!ftp_init())
    return 2;
  pearl_ftp_set_progress_handler(progress_info);
  pearl_ftp_reset_progress();
  if(pearl_ftp_received_bytes()!=0)return 3;
  ftp_enable();
  unsigned long last = xTaskGetTickCount();
  while (!stop) {
    unsigned long now = xTaskGetTickCount();
    if (ftp_run(now - last) < 0)
      break;
    last = now;
    usleep(delay);
  }
  printf("RECEIVED %u\n",pearl_ftp_received_bytes());
  pearl_ftp_close();
  return 0;
}
