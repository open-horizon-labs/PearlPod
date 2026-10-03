#include "network.h"
#include "cJSON.h"
#include "driver/usb_serial_jtag.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "player.h"
#include "sync.h"
#include "power.h"
#include "power_policy.h"
#include "wifi_policy.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Scan lifecycle follows roon-knob common/wifi_manager.c at a4b5a38:
 * async completion, heap records, bounded deduplicated cache, clear driver
 * records on every path. Independently implemented here; no copied source.
 * Reconnect pause/backoff follows T-Dongle gateway_main.c at ce0e172.
 */
typedef pearl_wifi_profile profile;
typedef enum { SETUP, CONNECT, SCAN, OFF, SAVE } action;
typedef struct {
  action kind;
  profile value;
} command;
static QueueHandle_t commands;
static TaskHandle_t worker_handle;
static void worker(void *arg);
static void stop(void);
static bool base_ready;
static unsigned retries;
static atomic_uint disconnect_reason;
static int64_t lease_deadline;
static SemaphoreHandle_t lock, off_done;
static atomic_bool shutting_down;
static pearl_network_state state = {.message = "WiFi is off"};
static profile profiles[PEARL_WIFI_PROFILE_LIMIT];
static unsigned profile_count;
static int selected = -1;
static bool initialized, initialization_attempted, started, want_connect,
    select_after_scan;
static atomic_uint events;
static int64_t retry_at, connect_deadline;
static httpd_handle_t server;
static char token[33];
static esp_netif_t *ap_netif;
extern const unsigned char portal_start[] asm("_binary_portal_html_start");
extern const unsigned char portal_end[] asm("_binary_portal_html_end");
#define E_SCAN 1u
#define E_IP 2u
#define E_DOWN 4u
static void message(const char *s) {
  xSemaphoreTake(lock, portMAX_DELAY);
  snprintf(state.message, sizeof(state.message), "%s", s);
  xSemaphoreGive(lock);
}
pearl_network_state pearl_network_snapshot(void) {
  pearl_network_state copy = {.message = "WiFi is off"};
  if (lock) {
    xSemaphoreTake(lock, portMAX_DELAY);
    copy = state;
    xSemaphoreGive(lock);
  }
  return copy;
}
static bool flag(unsigned which) {
  xSemaphoreTake(lock, portMAX_DELAY);
  bool value = which == 0   ? state.setup
               : which == 1 ? state.connected
               : which == 2 ? state.scanning
                            : state.enabled;
  xSemaphoreGive(lock);
  return value;
}
bool pearl_network_enabled(void) {
  if (!lock)
    return false;
  return flag(3);
}
static bool worker_start(void) {
  if (worker_handle)
    return true;
  return xTaskCreate(worker, "pearl_wifi", 8192, NULL, 1, &worker_handle) ==
         pdPASS;
}
static bool post(command c) {
  if (!lock || !commands)
    return false;
  xSemaphoreTake(lock, portMAX_DELAY);
  bool running = worker_start();
  xSemaphoreGive(lock);
  return running && commands && !atomic_load(&shutting_down) &&
         xQueueSend(commands, &c, 0) == pdTRUE;
}
void pearl_network_setup(void) { post((command){.kind = SETUP}); }
void pearl_network_connect(void) { post((command){.kind = CONNECT}); }
void pearl_network_scan(void) { post((command){.kind = SCAN}); }
void pearl_network_off(void) { post((command){.kind = OFF}); }
bool pearl_network_shutdown(void) {
  if (!commands || !worker_handle)
    return true;
  atomic_store(&shutting_down, true);
  xQueueReset(commands);
  while (xSemaphoreTake(off_done, 0) == pdTRUE) {
  }
  command c = {.kind = OFF};
  if (xQueueSend(commands, &c, pdMS_TO_TICKS(100)) != pdTRUE) {
    atomic_store(&shutting_down, false);
    return false;
  }
  return xSemaphoreTake(off_done, pdMS_TO_TICKS(5000)) == pdTRUE;
}
static void event(void *arg, esp_event_base_t base, int32_t id, void *data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE)
    atomic_fetch_or(&events, E_SCAN);
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    wifi_event_sta_disconnected_t *disconnected = data;
    atomic_store(&disconnect_reason, disconnected->reason);
    atomic_fetch_or(&events, E_DOWN);
  }
  if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *ip = data;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (!state.enabled) {
      xSemaphoreGive(lock);
      return;
    }
    snprintf(state.ip, sizeof(state.ip), IPSTR, IP2STR(&ip->ip_info.ip));
    snprintf(state.message, sizeof(state.message),
             "Connected. Your music stays offline.");
    xSemaphoreGive(lock);
    atomic_fetch_or(&events, E_IP);
  }
}
static __attribute__((noinline)) void load(void) {
  nvs_handle_t n;
  if (nvs_open("pearl_wifi", NVS_READONLY, &n) != ESP_OK)
    return;
  size_t bytes = sizeof(profiles);
  if (nvs_get_blob(n, "profiles", profiles, &bytes) == ESP_OK &&
      bytes % sizeof(profile) == 0) {
    profile_count = bytes / sizeof(profile);
    for (unsigned i = 0; i < profile_count; i++)
      if (!memchr(profiles[i].ssid, 0, 33) ||
          !memchr(profiles[i].password, 0, 65) || !profiles[i].ssid[0]) {
        profile_count = 0;
        memset(profiles, 0, sizeof(profiles));
        break;
      }
  }
  nvs_close(n);
}
static bool persist_profiles(const pearl_wifi_profile *next, unsigned count,
                             void *context) {
  if (count == profile_count &&
      !memcmp(profiles, next, count * sizeof(profile)))
    return true;
  nvs_handle_t n;
  if (nvs_open("pearl_wifi", NVS_READWRITE, &n) != ESP_OK)
    return false;
  esp_err_t e = nvs_set_blob(n, "profiles", next, count * sizeof(profile));
  if (e == ESP_OK)
    e = nvs_commit(n);
  nvs_close(n);
  return e == ESP_OK;
}
static __attribute__((noinline)) void import_card(void) {
  profile next[PEARL_WIFI_PROFILE_LIMIT] = {0};
  unsigned count = 0;
  pearl_wifi_import_result result = pearl_wifi_config_import(
      "/sdcard/wifi.toml", persist_profiles, NULL, next, &count);
  if (result == WIFI_IMPORT_DONE || result == WIFI_IMPORT_RETAINED) {
    memcpy(profiles, next, sizeof(profiles));
    profile_count = count;
    selected = -1;
    message(result == WIFI_IMPORT_DONE
                ? "Card WiFi imported. WiFi is off."
                : "WiFi imported; remove wifi.toml from the card.");
  } else if (result == WIFI_IMPORT_INVALID)
    message("Invalid wifi.toml. Saved networks unchanged.");
  else if (result == WIFI_IMPORT_STORAGE)
    message("Cannot save wifi.toml. File and saved networks kept.");
  else if (result == WIFI_IMPORT_IO)
    message("Cannot read wifi.toml. Saved networks unchanged.");
  memset(next, 0, sizeof(next));
}
static bool save(profile p) {
  profile next[PEARL_WIFI_PROFILE_LIMIT];
  memcpy(next, profiles, sizeof(next));
  unsigned count = profile_count, index = count;
  for (unsigned i = 0; i < count; i++)
    if (!strcmp(next[i].ssid, p.ssid)) {
      index = i;
      break;
    }
  if (index == PEARL_WIFI_PROFILE_LIMIT)
    return false;
  next[index] = p;
  if (index == count)
    count++;
  nvs_handle_t n;
  if (nvs_open("pearl_wifi", NVS_READWRITE, &n) != ESP_OK)
    return false;
  esp_err_t e = nvs_set_blob(n, "profiles", next, count * sizeof(profile));
  if (e == ESP_OK)
    e = nvs_commit(n);
  nvs_close(n);
  if (e != ESP_OK)
    return false;
  memcpy(profiles, next, sizeof(next));
  profile_count = count;
  selected = index;
  return true;
}
/* Restrict the provisioning server to requests addressed to the setup AP,
 * even while AP+STA is connected to a household network. */
static bool local(httpd_req_t *r) {
  struct sockaddr_in address;
  socklen_t length = sizeof(address);
  esp_netif_ip_info_t ip;
  return ap_netif &&
         getsockname(httpd_req_to_sockfd(r), (struct sockaddr *)&address,
                     &length) == 0 &&
         address.sin_family == AF_INET &&
         esp_netif_get_ip_info(ap_netif, &ip) == ESP_OK &&
         address.sin_addr.s_addr == ip.ip.addr;
}
static esp_err_t deny(httpd_req_t *r) {
  return httpd_resp_send_err(r, HTTPD_403_FORBIDDEN, "Setup AP only");
}
static esp_err_t page(httpd_req_t *r) {
  if (!local(r))
    return deny(r);
  httpd_resp_set_type(r, "text/html; charset=utf-8");
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  httpd_resp_set_hdr(r, "Content-Security-Policy",
                     "default-src 'self'; script-src 'unsafe-inline'; "
                     "style-src 'unsafe-inline'; frame-ancestors 'none'");
  return httpd_resp_send(r, (const char *)portal_start,
                         portal_end - portal_start);
}
static esp_err_t snapshot(httpd_req_t *r) {
  if (!local(r))
    return deny(r);
  pearl_network_state s = pearl_network_snapshot();
  cJSON *o = cJSON_CreateObject(), *a = cJSON_CreateArray(),
        *saved = cJSON_CreateArray();
  if (!o || !a || !saved) {
    cJSON_Delete(o);
    cJSON_Delete(a);
    cJSON_Delete(saved);
    return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR,
                               "Out of memory");
  }
  cJSON_AddStringToObject(o, "token", token);
  cJSON_AddStringToObject(o, "message", s.message);
  cJSON_AddStringToObject(o, "ip", s.ip);
  cJSON_AddBoolToObject(o, "scanning", s.scanning);
  for (unsigned i = 0; i < s.count; i++) {
    cJSON *n = cJSON_CreateObject();
    if (!n)
      continue;
    cJSON_AddStringToObject(n, "ssid", s.aps[i].ssid);
    cJSON_AddNumberToObject(n, "rssi", s.aps[i].rssi);
    cJSON_AddBoolToObject(n, "secure", s.aps[i].secure);
    cJSON_AddItemToArray(a, n);
  }
  xSemaphoreTake(lock, portMAX_DELAY);
  for (unsigned i = 0; i < profile_count; i++)
    cJSON_AddItemToArray(saved, cJSON_CreateString(profiles[i].ssid));
  xSemaphoreGive(lock);
  cJSON_AddItemToObject(o, "networks", a);
  cJSON_AddItemToObject(o, "saved", saved);
  char *json = cJSON_PrintUnformatted(o);
  cJSON_Delete(o);
  if (!json)
    return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR,
                               "Out of memory");
  httpd_resp_set_type(r, "application/json");
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  esp_err_t e = httpd_resp_sendstr(r, json);
  free(json);
  return e;
}
static esp_err_t mutate(httpd_req_t *r) {
  char supplied[40];
  if (!local(r))
    return deny(r);
  if (httpd_req_get_hdr_value_str(r, "X-Listener-Token", supplied,
                                  sizeof(supplied)) != ESP_OK ||
      strcmp(supplied, token))
    return httpd_resp_send_err(r, HTTPD_403_FORBIDDEN, "Reopen setup page");
  command c = {.kind = SCAN};
  char body[256] = {0};
  if (r->content_len <= 0 || r->content_len >= (int)sizeof(body))
    return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Invalid request");
  int used = 0;
  while (used < r->content_len) {
    int n = httpd_req_recv(r, body + used, r->content_len - used);
    if (n <= 0)
      return ESP_FAIL;
    used += n;
  }
  cJSON *o = cJSON_Parse(body);
  memset(body, 0, sizeof(body));
  if (!o)
    return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Invalid JSON");
  if (!strcmp(r->uri, "/save")) {
    c.kind = SAVE;
    cJSON *ssid = cJSON_GetObjectItemCaseSensitive(o, "ssid"),
          *pass = cJSON_GetObjectItemCaseSensitive(o, "password");
    if (!cJSON_IsString(ssid) || !cJSON_IsString(pass) ||
        !ssid->valuestring[0] || strlen(ssid->valuestring) > 32 ||
        strlen(pass->valuestring) > 63 ||
        (pass->valuestring[0] && strlen(pass->valuestring) < 8)) {
      cJSON_Delete(o);
      return httpd_resp_send_err(
          r, HTTPD_400_BAD_REQUEST,
          "Use a 1–32 byte name and an empty or 8–63 byte password");
    }
    snprintf(c.value.ssid, sizeof(c.value.ssid), "%s", ssid->valuestring);
    snprintf(c.value.password, sizeof(c.value.password), "%s",
             pass->valuestring);
  }
  cJSON_Delete(o);
  bool queued = post(c);
  memset(&c, 0, sizeof(c));
  if (!queued) {
    httpd_resp_set_status(r, "503 Service Unavailable");
    return httpd_resp_sendstr(r, "Busy; try again");
  }
  httpd_resp_set_status(r, "202 Accepted");
  return httpd_resp_sendstr(r, "Queued");
}
/* Read-only bounded diagnostics. Provisioning routes remain AP-only. */
static esp_err_t diagnostics(httpd_req_t *r) {
  if (!flag(0) && !flag(1)) {
    httpd_resp_set_status(r, "503 Service Unavailable");
    return httpd_resp_sendstr(r, "Not connected");
  }
  unsigned retry_count, lease_seconds;
  xSemaphoreTake(lock, portMAX_DELAY);
  retry_count = state.retries;
  lease_seconds = state.lease_seconds;
  xSemaphoreGive(lock);
  pearl_state audio = pearl_audio_state();
  unsigned albums, tracks;
  pearl_library_counts(&albums, &tracks);
  char *json = malloc(1200);
  if (!json)
    return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR,
                               "Out of memory");
  int length = snprintf(
      json, 1200,
      "{\"uptime_ms\":%llu,\"reset_reason\":%d,\"wake_cause\":%d,\"battery_"
      "percent\":null,\"audio\":{\"ready\":%s,\"playing\":%s,\"track\":%d,"
      "\"volume\":%d,\"seconds\":%lu},\"library\":{\"albums\":%u,\"tracks\":%u}"
      ",\"power\":{\"screen_asleep\":%s,\"deep_sleep_supported\":%s,\"usb_"
      "connected\":%s,\"idle_ms\":%lu},\"memory\":{\"free_internal\":%u,"
      "\"minimum_internal\":%u,\"largest_internal\":%u,\"free_psram\":%u,"
      "\"worker_stack_free\":%u},\"wifi\":{\"setup\":%s,\"connected\":%s,"
      "\"scanning\":%s,\"retries\":%u,\"disconnect_reason\":%u,\"lease_"
      "seconds\":%llu},\"sync\":{\"busy\":%s}}",
      (unsigned long long)(esp_timer_get_time() / 1000), esp_reset_reason(),
      esp_sleep_get_wakeup_cause(), audio.ready ? "true" : "false",
      audio.ready && !audio.paused ? "true" : "false", audio.track,
      audio.volume, (unsigned long)audio.seconds, albums, tracks,
      pearl_power_screen_asleep() ? "true" : "false",
      pearl_power_deep_supported() ? "true" : "false",
      usb_serial_jtag_is_connected() ? "true" : "false",
      (unsigned long)((uint32_t)(esp_timer_get_time() / 1000) -
                      pearl_power_last_activity()),
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
      (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL |
                                                MALLOC_CAP_8BIT),
      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                 MALLOC_CAP_8BIT),
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
      (unsigned)uxTaskGetStackHighWaterMark(worker_handle),
      flag(0) ? "true" : "false", flag(1) ? "true" : "false",
      flag(2) ? "true" : "false", retry_count, atomic_load(&disconnect_reason),
      (unsigned long long)lease_seconds,pearl_sync_busy()?"true":"false");
  esp_err_t result;
  if (length < 0 || length >= 1200)
    result = httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR,
                                 "Status too large");
  else {
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    result = httpd_resp_send(r, json, length);
  }
  free(json);
  return result;
}
static void schedule_retry(void) {
  if (retries >= 5) {
    bool setup = flag(0);
    want_connect = false;
    connect_deadline = retry_at = 0;
    if (!setup)
      stop();
    message("Connection attempts stopped. Check saved networks in setup.");
    return;
  }
  retry_at = esp_timer_get_time() + pearl_wifi_retry_delay(retries ? retries - 1 : 0) * 1000000LL;
}
static bool initialize(void) {
  if (initialized)
    return true;
  if (initialization_attempted)
    return false;
  initialization_attempted = true;
  if (!base_ready) {
    esp_err_t e = esp_netif_init();
    if (e != ESP_OK)
      return false;
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE)
      return false;
    if (!esp_netif_create_default_wifi_sta())
      return false;
    ap_netif = esp_netif_create_default_wifi_ap();
    if (!ap_netif)
      return false;
    if (esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event, NULL) !=
            ESP_OK ||
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event,
                                   NULL) != ESP_OK)
      return false;
    base_ready = true;
  }
  if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 48000 ||
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) <
          16384)
    return false;
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&cfg) != ESP_OK)
    return false;
  initialized = true;
  if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK ||
      esp_wifi_set_ps(WIFI_PS_MIN_MODEM) != ESP_OK) {
    esp_wifi_deinit();
    initialized = false;
    return false;
  }
  return true;
}
static void stop(void) {
  want_connect = false;
  select_after_scan = false;
  connect_deadline = retry_at = 0;
  if (server) {
    httpd_stop(server);
    server = NULL;
  }
  if (started) {
    esp_wifi_scan_stop();
    esp_wifi_clear_ap_list();
    esp_wifi_stop();
    started = false;
  }
  if (initialized) {
    esp_wifi_deinit();
    initialized = false;
    initialization_attempted = false;
  }
  initialization_attempted = false;
  lease_deadline = 0;
  atomic_store(&events, 0);
  memset(token, 0, sizeof(token));
  xSemaphoreTake(lock, portMAX_DELAY);
  state = (pearl_network_state){.message = "WiFi is off"};
  xSemaphoreGive(lock);
}
static __attribute__((noinline)) bool start(bool setup) {
  if (started && flag(0) == setup) {
    lease_deadline =
        esp_timer_get_time() + (setup ? CONFIG_PEARL_WIFI_SETUP_SEC
                                      : CONFIG_PEARL_WIFI_DIAGNOSTIC_SEC) *
                                   1000000LL;
    return true;
  }
  stop();
  if (!initialize()) {
    message("WiFi unavailable or memory low. Music remains available.");
    return false;
  }
  if (esp_wifi_set_mode(setup ? WIFI_MODE_APSTA : WIFI_MODE_STA) != ESP_OK) {
    stop();
    message("WiFi mode unavailable. Try again.");
    return false;
  }
  if (setup) {
    wifi_config_t ap = {0};
    unsigned char random[8];
    esp_fill_random(random, sizeof(random));
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "PearlPod-%02X%02X",
             random[0], random[1]);
    for (unsigned i = 0; i < 8; i++)
      snprintf((char *)ap.ap.password + i * 2, 3, "%02x", random[i]);
    ap.ap.ssid_len = strlen((char *)ap.ap.ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 2;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    if (esp_wifi_set_config(WIFI_IF_AP, &ap) != ESP_OK) {
      stop();
      message("Setup configuration failed. Try again.");
      return false;
    }
    esp_fill_random(random, sizeof(random));
    for (unsigned i = 0; i < 8; i++)
      snprintf(token + i * 2, 3, "%02x", random[i]);
    esp_fill_random(random, sizeof(random));
    for (unsigned i = 0; i < 8; i++)
      snprintf(token + 16 + i * 2, 3, "%02x", random[i]);
    xSemaphoreTake(lock, portMAX_DELAY);
    snprintf(state.ap_ssid, sizeof(state.ap_ssid), "%.32s", ap.ap.ssid);
    snprintf(state.ap_password, sizeof(state.ap_password), "%.16s",
             ap.ap.password);
    xSemaphoreGive(lock);
  }
  if (esp_wifi_start() != ESP_OK) {
    stop();
    message("WiFi could not start.");
    return false;
  }
  started = true;
  xSemaphoreTake(lock, portMAX_DELAY);
  state.enabled = true;
  state.setup = setup;
  xSemaphoreGive(lock);
  lease_deadline =
      esp_timer_get_time() +
      (setup ? CONFIG_PEARL_WIFI_SETUP_SEC : CONFIG_PEARL_WIFI_DIAGNOSTIC_SEC) *
          1000000LL;
  {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 5;
    if (httpd_start(&server, &cfg) != ESP_OK) {
      stop();
      message("Setup could not start.");
      return false;
    }
    httpd_uri_t routes[] = {
        {.uri = "/status", .method = HTTP_GET, .handler = diagnostics},
        {.uri = "/", .method = HTTP_GET, .handler = page},
        {.uri = "/state", .method = HTTP_GET, .handler = snapshot},
        {.uri = "/scan", .method = HTTP_POST, .handler = mutate},
        {.uri = "/save", .method = HTTP_POST, .handler = mutate}};
    for (unsigned i = 0; i < 5; i++)
      if (httpd_register_uri_handler(server, &routes[i]) != ESP_OK) {
        stop();
        message("Diagnostic server unavailable.");
        return false;
      }
    if (setup)
      message("Setup ready. Join the network shown here.");
  }
  return true;
}
static void connect_selected(void) {
  if (selected < 0 || (unsigned)selected >= profile_count)
    return;
  wifi_config_t cfg = {0};
  memcpy(cfg.sta.ssid, profiles[selected].ssid,
         strlen(profiles[selected].ssid));
  memcpy(cfg.sta.password, profiles[selected].password,
         strlen(profiles[selected].password));
  cfg.sta.pmf_cfg.capable = true;
  retries++;
  xSemaphoreTake(lock, portMAX_DELAY);
  state.retries = retries;
  xSemaphoreGive(lock);
  esp_wifi_disconnect();
  if (esp_wifi_set_config(WIFI_IF_STA, &cfg) != ESP_OK) {
    schedule_retry();
    message("Network configuration failed.");
    return;
  }
  if (esp_wifi_connect() != ESP_OK) {
    schedule_retry();
    message("Connection failed. Check your saved network in setup.");
  } else {
    connect_deadline = esp_timer_get_time() + 20000000;
    message("Connecting to your saved network...");
  }
}
static __attribute__((noinline)) void scan(bool pick) {
  if (!started || flag(2))
    return;
  select_after_scan = pick;
  if (!flag(1)) {
    connect_deadline = 0;
    retry_at = 0;
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(150));
  }
  wifi_scan_config_t cfg = {.show_hidden = false,
                            .scan_type = WIFI_SCAN_TYPE_ACTIVE,
                            .scan_time.active = {.min = 100, .max = 250}};
  xSemaphoreTake(lock, portMAX_DELAY);
  state.scanning = true;
  state.count = 0;
  xSemaphoreGive(lock);
  if (esp_wifi_scan_start(&cfg, false) != ESP_OK) {
    xSemaphoreTake(lock, portMAX_DELAY);
    state.scanning = false;
    xSemaphoreGive(lock);
    message("Scan could not start. Try again.");
    if (want_connect) {
      retries++;
      schedule_retry();
    }
  }
}
static __attribute__((noinline)) void scan_done(void) {
  uint16_t found = 0;
  esp_wifi_scan_get_ap_num(&found);
  uint16_t count =
      found > PEARL_WIFI_SCAN_LIMIT ? PEARL_WIFI_SCAN_LIMIT : found;
  wifi_ap_record_t *records = count ? calloc(count, sizeof(*records)) : NULL;
  bool good =
      !count ||
      (records && esp_wifi_scan_get_ap_records(&count, records) == ESP_OK);
  esp_wifi_clear_ap_list();
  xSemaphoreTake(lock, portMAX_DELAY);
  state.count = 0;
  state.scanning = false;
  if (good)
    for (unsigned i = 0; i < count; i++) {
      records[i].ssid[32] = 0;
      if (!records[i].ssid[0] || records[i].primary > 14)
        continue;
      bool duplicate = false;
      for (unsigned j = 0; j < state.count; j++)
        if (!strcmp(state.aps[j].ssid, (char *)records[i].ssid))
          duplicate = true;
      if (duplicate)
        continue;
      pearl_wifi_ap *a = &state.aps[state.count++];
      snprintf(a->ssid, sizeof(a->ssid), "%s", records[i].ssid);
      a->rssi = records[i].rssi;
      a->secure = records[i].authmode != WIFI_AUTH_OPEN;
    }
  xSemaphoreGive(lock);
  free(records);
  if (!good) {
    message("Scan failed. Try again.");
    if (want_connect) {
      retries++;
      schedule_retry();
    }
    return;
  }
  if (select_after_scan) {
    pearl_network_state s = pearl_network_snapshot();
    int signal[PEARL_WIFI_PROFILE_LIMIT];
    for (unsigned i = 0; i < profile_count; i++) {
      signal[i] = -127;
      for (unsigned j = 0; j < s.count; j++)
        if (!strcmp(profiles[i].ssid, s.aps[j].ssid))
          signal[i] = s.aps[j].rssi;
    }
    int previous = selected;
    selected = pearl_wifi_pick(signal, profile_count, selected, s.connected);
    select_after_scan = false;
    if (selected >= 0) {
      if (s.connected && selected == previous)
        message("Connected. Scan complete.");
      else
        connect_selected();
    } else {
      selected = previous >= 0 ? previous : 0;
      connect_selected();
    }
  } else {
    message(flag(1) ? "Connected. Scan complete."
                    : "Scan complete. Choose a network in setup.");
    if (want_connect && !flag(1))
      retry_at = esp_timer_get_time() + 1000000;
  }
}
static void worker(void *arg) {
  load();
  import_card();
  command c;
  for (;;) {
    if (xQueueReceive(commands, &c, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (c.kind == OFF) {
        stop();
        atomic_store(&shutting_down, false);
        xSemaphoreGive(off_done);
      } else if (c.kind == SETUP) {
        if (start(true))
          scan(false);
      } else if (c.kind == CONNECT) {
        if (!profile_count)
          message("No saved networks. Open WiFi setup first.");
        else if (start(false)) {
          want_connect = true;
          retries = 0;
          scan(true);
        }
      } else if (c.kind == SCAN) {
        if (started)
          scan(false);
      } else if (c.kind == SAVE) {
        xSemaphoreTake(lock, portMAX_DELAY);
        bool saved = save(c.value);
        xSemaphoreGive(lock);
        if (saved) {
          want_connect = true;
          retries = 0;
          if (flag(2)) {
            esp_wifi_scan_stop();
            esp_wifi_clear_ap_list();
            xSemaphoreTake(lock, portMAX_DELAY);
            state.scanning = false;
            xSemaphoreGive(lock);
            atomic_fetch_and(&events, ~E_SCAN);
          }
          select_after_scan = false;
          connect_selected();
        } else
          message("Could not save. Storage unavailable or four networks "
                  "already saved.");
      }
      memset(&c, 0, sizeof(c));
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    state.retries = retries;
    int64_t remaining = lease_deadline - esp_timer_get_time();
    state.lease_seconds = remaining > 0 ? remaining / 1000000 : 0;
    xSemaphoreGive(lock);
    unsigned e = atomic_exchange(&events, 0);
    if (!started)
      continue;
    if (e & E_DOWN) {
      xSemaphoreTake(lock, portMAX_DELAY);
      state.connected = false;
      if (!(e & E_IP))
        state.ip[0] = 0;
      xSemaphoreGive(lock);
      unsigned reason = atomic_load(&disconnect_reason);
      xSemaphoreTake(lock, portMAX_DELAY);
      state.disconnect_reason = reason;
      xSemaphoreGive(lock);
      if (want_connect && (reason == WIFI_REASON_AUTH_FAIL ||
                           reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT)) {
        want_connect = false;
        connect_deadline = retry_at = 0;
        if (!flag(0))
          stop();
        message("Authentication failed. Check your password in setup.");
      }
      if (want_connect && !flag(2) && !connect_deadline) {
        schedule_retry();
        message("Disconnected. Retrying with backoff.");
      }
    }
    if (e & E_IP) {
      xSemaphoreTake(lock, portMAX_DELAY);
      state.connected = true;
      xSemaphoreGive(lock);
      connect_deadline = retry_at = 0;
      retries = 0;
    }
    if ((e & E_SCAN) && flag(2))
      scan_done();
    int64_t now = esp_timer_get_time();
    if (connect_deadline && now >= connect_deadline) {
      connect_deadline = 0;
      esp_wifi_disconnect();
      schedule_retry();
      message("Connection timed out. Check password and network in setup.");
    }
    if (lease_deadline && now >= lease_deadline) {
      stop();
      message("WiFi session finished. Radio is off.");
      continue;
    }
    if (want_connect && retry_at && now >= retry_at && !flag(2)) {
      retry_at = 0;
      scan(true);
    }
  }
}
void pearl_network_init(void) {
  lock = xSemaphoreCreateMutex();
  if (!lock)
    return;
  off_done = xSemaphoreCreateBinary();
  commands = xQueueCreate(4, sizeof(command));
  if (!off_done || !commands ||
      ((esp_reset_reason() == ESP_RST_PANIC ||
        esp_reset_reason() == ESP_RST_TASK_WDT ||
        esp_reset_reason() == ESP_RST_INT_WDT)
           ? false
           : !worker_start())) {
    if (commands) {
      vQueueDelete(commands);
      commands = NULL;
    }
    message("WiFi unavailable: out of memory.");
  } else if (!worker_handle)
    message("WiFi delayed after crash. Open setup to retry.");
}
