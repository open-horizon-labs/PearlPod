#include "ftp.h"
#include "ftp_platform.h"
#include <signal.h>
#include <sys/resource.h>
extern const char *MOUNT_POINT;
extern void pearl_ftp_close(void);
static volatile sig_atomic_t stop;
static void stopping(int signal) {
  (void)signal;
  stop = 1;
}
int main(int argc, char **argv) {
  if (argc != 2)
    return 1;
  MOUNT_POINT = argv[1];
  if(getenv("PEARL_FTP_LIMIT")){struct rlimit limit={32768,32768};setrlimit(RLIMIT_FSIZE,&limit);signal(SIGXFSZ,SIG_IGN);}
  unsigned delay=getenv("PEARL_FTP_DELAY_US")?5000:1000;
  signal(SIGTERM, stopping);
  signal(SIGINT, stopping);
  if (!ftp_init())
    return 2;
  ftp_enable();
  unsigned long last = xTaskGetTickCount();
  while (!stop) {
    unsigned long now = xTaskGetTickCount();
    if (ftp_run(now - last) < 0)
      break;
    last = now;
    usleep(delay);
  }
  pearl_ftp_close();
  return 0;
}
