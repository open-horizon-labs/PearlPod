#pragma once
#include <stdbool.h>
#define PEARL_WIFI_SCAN_LIMIT 20
#define PEARL_WIFI_PROFILE_LIMIT 4
typedef struct {
  char ssid[33];
  int rssi;
  bool secure;
} pearl_wifi_ap;
typedef struct {
  bool enabled, setup, connected, scanning;
  unsigned count;
  char message[120], ip[16], ap_ssid[33], ap_password[17];
  pearl_wifi_ap aps[PEARL_WIFI_SCAN_LIMIT];
} pearl_network_state;
#ifdef PEARL_UI_HOST
static inline pearl_network_state pearl_network_snapshot(void) {
  return (pearl_network_state){.message = "WiFi is off"};
}
static inline void pearl_network_setup(void) {}
static inline void pearl_network_connect(void) {}
static inline void pearl_network_off(void) {}
#else
void pearl_network_init(void);
pearl_network_state pearl_network_snapshot(void);
void pearl_network_setup(void);
void pearl_network_connect(void);
void pearl_network_scan(void);
void pearl_network_off(void);
bool pearl_network_shutdown(void);
#endif
