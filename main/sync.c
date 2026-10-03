#include "sync.h"
#include "tracer.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ftp.h"
#include "managed.h"
#include "mdns.h"
#include "network.h"
#include "nvs.h"
#include "player.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static atomic_bool busy, cancel;
static char probe_url[161];

static atomic_int stage;

extern unsigned pearl_ftp_received_bytes(void);
extern void pearl_ftp_reset_progress(void);
extern void pearl_ftp_trace(char *out,unsigned size);
static atomic_int tx_before=-1,tx_after=-1,tx_result,ps_result;
static atomic_uint loop_us,loop_max_us,loop_calls,yield_us,connect_us,discovery_us,trigger_us,marker_us,network_us;
void pearl_sync_trace(char *out,unsigned size) {
 wifi_ap_record_t ap={0};
 int radio=esp_wifi_sta_get_ap_info(&ap);
 char ftp[640];pearl_ftp_trace(ftp,sizeof(ftp));
 snprintf(out,size,"bssid=%02x:%02x:%02x:%02x:%02x:%02x rssi=%d channel=%u phy_11n=%u radio_result=%d tx_before_qdbm=%d tx_after_qdbm=%d tx_result=%d ps_result=%d connect_us=%u discovery_us=%u trigger_us=%u marker_us=%u network_us=%u loops=%u loop_us=%u loop_max_us=%u yield_us=%u %s",ap.bssid[0],ap.bssid[1],ap.bssid[2],ap.bssid[3],ap.bssid[4],ap.bssid[5],ap.rssi,ap.primary,ap.phy_11n,radio,atomic_load(&tx_before),atomic_load(&tx_after),atomic_load(&tx_result),atomic_load(&ps_result),atomic_load(&connect_us),atomic_load(&discovery_us),atomic_load(&trigger_us),atomic_load(&marker_us),atomic_load(&network_us),atomic_load(&loop_calls),atomic_load(&loop_us),atomic_load(&loop_max_us),atomic_load(&yield_us),ftp);
}
static atomic_uint elapsed, quiet, transferred;
static const char *messages[] = {
 "Ready to sync\n\nAdd PP: playlists in Plex. Keep the library computer running. Pause music before starting.",
 "Connecting to WiFi\nUsing your saved networks.",
 "Finding your library\nLooking for the library computer on WiFi.",
 "Waiting for your library\nThe computer is checking which files are needed.",
 "Checking your music\nVerifying the update before adding it.",
 "Your music is ready!\nOpen Playlists to listen. WiFi is turning off.",
 "Sync stopped\nYour existing music is safe. Check the library computer, then try again.",
 "Pause your music first\nThen tap Start sync again.",
 "The card is full\nFree some space on the card, then try again.",
 "Could not connect\nOpen WiFi setup, check your network, then try again.",
 "Library not found\nKeep the sync service running on the same WiFi, then try again.",
 "Library is preparing\nThe computer is not ready yet. Try again shortly.",
 "Library is busy\nAnother sync is running. Try again shortly.",
 "Sync cancelled\nYour existing music is safe.",
 "Connection lost\nCheck WiFi, then try again. Your existing music is safe.",
 "Sync timed out\nCheck the library computer, then try again. Your existing music is safe."
};
void pearl_sync_status(char *out, unsigned size) {
 unsigned current=atomic_load(&stage), bytes=atomic_load(&transferred);
 if(current==3 && bytes) {
  unsigned kb=bytes/1024;
  snprintf(out,size,"Receiving your music\n%u.%u MB received\n\nLast data: %u sec ago\nElapsed: %u:%02u%s",kb/1024,(kb%1024)*10/1024,atomic_load(&quiet),atomic_load(&elapsed)/60,atomic_load(&elapsed)%60,atomic_load(&quiet)>=30?"\nWaiting for the computer...":"");
 } else if(atomic_load(&busy))
  snprintf(out,size,"%s\n\nElapsed: %u:%02u",messages[current],atomic_load(&elapsed)/60,atomic_load(&elapsed)%60);
 else snprintf(out,size,"%s",messages[current]);
}
void pearl_sync_cancel(void) { atomic_store(&cancel,true); }
bool pearl_sync_busy(void) { return atomic_load(&busy); }
bool pearl_sync_source(const char *url) {
  if (strncmp(url, "http://", 7) || strlen(url) > 160 || strchr(url, '\n') ||
      strchr(url, '@'))
    return false;

  nvs_handle_t h;

  if (nvs_open("pearl_sync", NVS_READWRITE, &h) != ESP_OK)
    return false;
  esp_err_t e = nvs_set_str(h, "source", url);

  if (e == ESP_OK)
    e = nvs_commit(h);
  nvs_close(h);
  return e == ESP_OK;
}
extern void pearl_ftp_close(void);

static __attribute__((noinline)) bool connected(void) {
  int64_t before=esp_timer_get_time();
  pearl_network_state s = pearl_network_snapshot();
  atomic_fetch_add(&network_us,(unsigned)(esp_timer_get_time()-before));
  return s.connected;
}
static void task(void *arg) {
  (void)arg;
  bool ftp = false, dns = false, success = false, card_full = false;
  int failure=6;
  int64_t session_start=esp_timer_get_time();
  stage = 1;
  pearl_network_connect();

  int64_t start = esp_timer_get_time();

  while (!atomic_load(&cancel) && !connected() &&
         esp_timer_get_time() - start < 40000000) {
    elapsed=(esp_timer_get_time()-session_start)/1000000;
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  if (atomic_load(&cancel) || !connected()) { failure=9; goto done; }

  connect_us=esp_timer_get_time()-session_start;
  int8_t power;
  if(esp_wifi_get_max_tx_power(&power)==ESP_OK)tx_before=power;
  tx_result=esp_wifi_set_max_tx_power(84); /* Highest accepted SDK request; PHY limits still apply. */
  if(esp_wifi_get_max_tx_power(&power)==ESP_OK)tx_after=power;
  ps_result=esp_wifi_set_ps(WIFI_PS_NONE); /* Radio is off at completion. */
  stage = 2;
  int64_t discovery_start=esp_timer_get_time();
  char url[200] = {0};
  nvs_handle_t h;

  if(probe_url[0])snprintf(url,sizeof(url),"%s",probe_url);
  else if (nvs_open("pearl_sync", NVS_READONLY, &h) == ESP_OK) {
    size_t n = sizeof(url);
    nvs_get_str(h, "source", url, &n);
    nvs_close(h);
  }
  if (mdns_init() == ESP_OK) {
    dns = true;
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char name[32];
    snprintf(name, sizeof(name), "pearlpod-%02x%02x%02x", mac[3], mac[4],
             mac[5]);
    mdns_hostname_set(name);

    mdns_txt_item_t txt[] = {{"version", "1"}, {"ftp_port", "2121"}};
    mdns_service_add("PearlPod", "_pearlpod", "_tcp", 80, txt, 2);

    if (!url[0]) {
      mdns_result_t *results = NULL;

      if (mdns_query_ptr("_pearlpod-sync", "_tcp", 3000, 1, &results) ==
              ESP_OK &&
          results) {
        for (mdns_ip_addr_t *ip = results->addr; ip; ip = ip->next)
          if (ip->addr.type == ESP_IPADDR_TYPE_V4) {
            snprintf(url, sizeof(url), "http://" IPSTR ":%u",
                     IP2STR(&ip->addr.u_addr.ip4), results->port);
            break;
          }
        mdns_query_results_free(results);
      }
    }
  }
  discovery_us=esp_timer_get_time()-discovery_start;
  if (!url[0]) { failure=10; goto done; }

  mkdir("/sdcard/music/.pearl", 0755);
  mkdir("/sdcard/music/.pearl/objects", 0755);
  mkdir("/sdcard/music/.pearl/catalogs", 0755);

  /* Keep staged objects for retry. Collect only after successful activation. */
  unlink("/sdcard/music/.pearl/ready");
  unlink("/sdcard/music/.pearl/ready.tmp");
  unlink("/sdcard/music/.pearl/error.txt");

  if (!ftp_init())
    goto done;

  ftp = true;
  ftp_enable();
  ftp_run(0);
  ftp_run(0);

  size_t used = strlen(url);

  if (used && url[used - 1] == '/')
    url[--used] = 0;

  if (used + 5 >= sizeof(url))
    goto done;
  strcat(url, "/sync");

  esp_http_client_config_t cfg = {.url = url,
                                  .timeout_ms = 5000,
                                  .buffer_size = 1024,
                                  .buffer_size_tx = 512,
                                  .disable_auto_redirect = true};

  esp_http_client_handle_t client = esp_http_client_init(&cfg);

  if (!client)
    goto done;

  uint64_t total = 0, available = 0;
  if (esp_vfs_fat_info("/sdcard", &total, &available) != ESP_OK) {
    esp_http_client_cleanup(client);
    goto done;
  }
  char body[96];
  snprintf(body, sizeof(body), "{\"ftp_port\":2121,\"free_bytes\":%llu}",
           (unsigned long long)available);
  esp_http_client_set_method(client, HTTP_METHOD_POST);
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_post_field(client, body, strlen(body));

  int64_t trigger_start=esp_timer_get_time();
  esp_err_t result = esp_http_client_perform(client);
  trigger_us=esp_timer_get_time()-trigger_start;
  int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (result != ESP_OK || status != 202) {
    failure=status==503?11:status==409?12:6; goto done;
  }

  stage = 3;
  start = esp_timer_get_time();
  int64_t last = start, last_data=start;
  unsigned prior_bytes=0;
  int64_t last_yield=start, next_marker_check=start;
  int64_t limit=probe_url[0]?300000000LL:7200000000LL;

  while (!atomic_load(&cancel) && esp_timer_get_time() - start < limit &&
         connected()) {
    int64_t now = esp_timer_get_time();

    int run_result=ftp_run((now-last)/1000);
    unsigned run_duration=esp_timer_get_time()-now;
    atomic_fetch_add(&loop_calls,1);atomic_fetch_add(&loop_us,run_duration);
    if(run_duration>atomic_load(&loop_max_us))loop_max_us=run_duration;
    if(run_result<0)break;
    last = now;
    unsigned bytes=pearl_ftp_received_bytes();
    bool made_progress=bytes!=prior_bytes;
    if(made_progress) { last_data=now;prior_bytes=bytes; }
    transferred=bytes;quiet=(now-last_data)/1000000;
    if(now-last_data>=180000000LL) { failure=15;break; }
    elapsed=(now-session_start)/1000000;

    bool check_markers=!probe_url[0] && now>=next_marker_check && ftp_getstate()==E_FTP_STE_READY;
    if(check_markers)next_marker_check=now+1000000;
    int64_t marker_start=esp_timer_get_time();
    FILE *failed = check_markers
                       ? fopen("/sdcard/music/.pearl/error.txt", "rb") : NULL;
    if (failed) {
      char reason[32] = {0};
      card_full = fgets(reason, sizeof(reason), failed) && !strncmp(reason, "card_full", 9);
      fclose(failed);
      break;
    }
    FILE *ready = check_markers
                      ? fopen("/sdcard/music/.pearl/ready", "rb")
                      : NULL;

    atomic_fetch_add(&marker_us,(unsigned)(esp_timer_get_time()-marker_start));
    if (ready) {
      char sha[67] = {0};
      bool read = fgets(sha, sizeof(sha), ready) != NULL;
      fclose(ready);
      sha[strcspn(sha, "\r\n")] = 0;

      pearl_ftp_close();
      ftp = false;
      stage = 4;
      pearl_network_off(); /* Upload is complete; release radio RAM before parsing. */

      if (read && pearl_audio_state().paused && pearl_managed_activate(sha)) {
        success = true;
        pearl_managed_collect();
        pearl_library_rescan();
      }
      ESP_LOGW("sync","Activation complete success=%d stack_remaining=%u",success,(unsigned)uxTaskGetStackHighWaterMark(NULL));
      break;
    }
    /* Drain ready data without a sleep after every chunk. Yield on idle,
     * or every 20 ms while busy so UI/control tasks keep running. */
    if(!made_progress || esp_timer_get_time()-last_yield>=20000) {
      int64_t before=esp_timer_get_time();vTaskDelay(1);
      atomic_fetch_add(&yield_us,(unsigned)(esp_timer_get_time()-before));
      last_yield=esp_timer_get_time();
    }
  }
  if (!success && !card_full && !connected()) failure=14;
  else if (!success && esp_timer_get_time()-start>=limit) failure=15;
done:
  if (ftp)
    pearl_ftp_close();

  if (dns)
    mdns_free();

  pearl_network_off();
  stage = success ? 5 : cancel ? 13 : card_full ? 8 : failure;
  pearl_trace_probe(false);probe_url[0]=0;
  busy = false;
  vTaskDelete(NULL);
}
static bool start_sync(const char *override) {
  if (atomic_load(&busy)||pearl_trace_sd_busy()) return false;
  if (!pearl_audio_state().paused) {stage=7;return false;}
  if (atomic_exchange(&busy,true))return false;
  snprintf(probe_url,sizeof(probe_url),"%s",override?override:"");
  pearl_trace_probe(override!=NULL);
  cancel=false;
  elapsed=quiet=transferred=0;
  tx_before=tx_after=-1;tx_result=ps_result=0;
  loop_us=loop_max_us=loop_calls=yield_us=connect_us=discovery_us=trigger_us=marker_us=network_us=0;
  pearl_ftp_reset_progress();stage=1;
  if (xTaskCreate(task,"sync",12288,NULL,2,NULL)!=pdPASS){
    pearl_trace_probe(false);probe_url[0]=0;busy=false;stage=6;return false;
  }
  return true;
}
void pearl_sync_start(void){start_sync(NULL);}

bool pearl_sync_shutdown(void) {
  cancel = true;
  for (unsigned i = 0; i < 250 && busy; i++)
    vTaskDelay(pdMS_TO_TICKS(20));
  return !busy;
}

bool pearl_sync_probe_start(const char *url){
 if(strncmp(url,"http://",7)||strlen(url)>160||strchr(url,'\n')||strchr(url,'\r')||strchr(url,'@'))return false;
 return start_sync(url);
}
