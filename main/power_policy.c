#include "power_policy.h"
#include "device_power.h"
#include "wifi_station.h"
pearl_power_action pearl_power_decide(const pearl_power_input *in) {
  if (!in) return PEARL_POWER_ACTIVE;
  device_power_input policy = {
    .now=in->now, .last_activity=in->last_activity, .paused_since=in->paused_since,
    .screen_timeout=in->screen_timeout, .idle_timeout=in->idle_timeout,
    .playing=in->playing, .screen_asleep=in->screen_asleep,
    .inhibit_sleep=in->network, .busy=in->busy, .usb_connected=in->usb_connected,
    .button_released=in->button_released, .deep_supported=in->deep_supported
  };
  return (pearl_power_action)device_power_decide(&policy);
}
bool pearl_usb_host_guard(bool connected, bool seen, uint32_t now,
                          uint32_t last_seen, uint32_t grace_ms) {
  return device_usb_host_guard(connected, seen, now, last_seen, grace_ms);
}
unsigned pearl_wifi_retry_delay(unsigned attempt) {
  return wifi_station_retry_delay_ms(attempt, WIFI_STATION_RETRY_READER)/1000;
}
