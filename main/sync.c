#include "sync.h"
#include "esp_http_client.h"
#include "esp_mac.h"
#include "esp_timer.h"
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

static atomic_int stage;

static const char *messages[] = {"Sync is off",
                                 "Connecting to WiFi...",
                                 "Finding library...",
                                 "Syncing music...",
                                 "Checking update...",
                                 "Synced!",
                                 "Sync failed. Existing music kept.",
                                 "Pause music, then sync again."};

void pearl_sync_status(char *out, unsigned size) {
  snprintf(out, size, "%s", messages[atomic_load(&stage)]);
}
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
  pearl_network_state s = pearl_network_snapshot();
  return s.connected;
}
static void task(void *arg) {
  (void)arg;
  bool ftp = false, dns = false, success = false;
  stage = 1;
  pearl_network_connect();

  int64_t start = esp_timer_get_time();

  while (!atomic_load(&cancel) && !connected() &&
         esp_timer_get_time() - start < 40000000) {
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  if (atomic_load(&cancel) || !connected())
    goto done;

  stage = 2;
  char url[200] = {0};
  nvs_handle_t h;

  if (nvs_open("pearl_sync", NVS_READONLY, &h) == ESP_OK) {
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
  if (!url[0])
    goto done;

  mkdir("/sdcard/music/.pearl", 0755);
  mkdir("/sdcard/music/.pearl/objects", 0755);
  mkdir("/sdcard/music/.pearl/catalogs", 0755);

  unlink("/sdcard/music/.pearl/ready");
  unlink("/sdcard/music/.pearl/ready.tmp");

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

  const char *body = "{\"ftp_port\":2121}";
  esp_http_client_set_method(client, HTTP_METHOD_POST);
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_post_field(client, body, strlen(body));

  esp_err_t result = esp_http_client_perform(client);
  int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (result != ESP_OK || status != 202)
    goto done;

  stage = 3;
  start = esp_timer_get_time();
  int64_t last = start;

  while (!atomic_load(&cancel) && esp_timer_get_time() - start < 840000000 &&
         connected()) {
    int64_t now = esp_timer_get_time();

    if (ftp_run((now - last) / 1000) < 0)
      break;
    last = now;

    FILE *ready = ftp_getstate() == E_FTP_STE_READY
                      ? fopen("/sdcard/music/.pearl/ready", "rb")
                      : NULL;

    if (ready) {
      char sha[67] = {0};
      bool read = fgets(sha, sizeof(sha), ready) != NULL;
      fclose(ready);
      sha[strcspn(sha, "\r\n")] = 0;

      pearl_ftp_close();
      ftp = false;
      stage = 4;

      if (read && pearl_audio_state().paused && pearl_managed_activate(sha)) {
        success = true;
        pearl_library_rescan();
      }
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
done:
  if (ftp)
    pearl_ftp_close();

  if (dns)
    mdns_free();

  pearl_network_off();
  stage = success ? 5 : 6;
  busy = false;
  vTaskDelete(NULL);
}
void pearl_sync_start(void) {
  if (!pearl_audio_state().paused) {
    stage = 7;
    return;
  }
  if (atomic_exchange(&busy, true))
    return;
  cancel = false;

  if (xTaskCreate(task, "sync", 8192, NULL, 2, NULL) != pdPASS) {
    busy = false;
    stage = 6;
  }
}

bool pearl_sync_shutdown(void) {
  cancel = true;
  for (unsigned i = 0; i < 250 && busy; i++)
    vTaskDelay(pdMS_TO_TICKS(20));
  return !busy;
}
