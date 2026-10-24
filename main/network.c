#include "network.h"
#include "wifi_provisioning_idf.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "player.h"
#include "sync.h"
#include "power.h"
#include <stdio.h>
#include <stdlib.h>
extern const unsigned char portal_start[] asm("_binary_portal_html_start");
extern const unsigned char portal_end[] asm("_binary_portal_html_end");
/* Bounded diagnostics and setup are available while WiFi is running. */
static esp_err_t diagnostics(httpd_req_t *r) {
  pearl_network_state s=pearl_network_snapshot();
  unsigned retry_count=s.retries,lease_seconds=s.lease_seconds;
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
      (unsigned)device_wifi_worker_stack_free(),
      s.setup ? "true" : "false", s.connected ? "true" : "false",
      s.scanning ? "true" : "false", retry_count, s.disconnect_reason,
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
void pearl_network_init(void) {
 const device_wifi_config config={
 .ap_prefix="PearlPod", .nvs_namespace="pearl_wifi", .import_path="/sdcard/wifi.toml",
 .page=portal_start,.page_bytes=portal_end-portal_start,
 .captive_url="http://192.168.4.1/",
 .connected_message="Connected. Your music stays offline.",
 .unavailable_message="WiFi unavailable or memory low. Music remains available.",
 .busy_message="WiFi busy saving original. Try again shortly.",
 .diagnostics=diagnostics,.busy=pearl_sync_busy,
 .setup_seconds=CONFIG_PEARL_WIFI_SETUP_SEC,.diagnostic_seconds=CONFIG_PEARL_WIFI_DIAGNOSTIC_SEC,
 .shutdown_wait_ms=5000,.setup_reconnect_ms=0,.configure_state_retries=0,
 .max_attempts=5,.retry_profile=WIFI_STATION_RETRY_READER,.station_options=WIFI_STATION_READER_OPTIONS,
 .empty_connect_setup=false,.connected_hidden_selection=false,.saved_hidden_fallback=true,
 .extended_snapshot=false,.max_uri_handlers=9
 };
 (void)device_wifi_init(&config);
}
pearl_network_state pearl_network_snapshot(void){return device_wifi_snapshot();}
bool pearl_network_enabled(void){return device_wifi_enabled();}
bool pearl_network_connected(void){return device_wifi_connected();}
void pearl_network_setup(void){device_wifi_setup();}
void pearl_network_connect(void){device_wifi_connect();}
void pearl_network_scan(void){device_wifi_scan();}
void pearl_network_off(void){device_wifi_off();}
bool pearl_network_shutdown(void){return device_wifi_shutdown();}
