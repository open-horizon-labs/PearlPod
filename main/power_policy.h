#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum {
  PEARL_POWER_ACTIVE,
  PEARL_POWER_SCREEN_SLEEP,
  PEARL_POWER_DEEP_SLEEP
} pearl_power_action;
typedef struct {
  uint32_t now, last_activity, paused_since, screen_timeout, idle_timeout;
  bool playing, screen_asleep, network, busy, usb_connected, button_released,
      deep_supported;
} pearl_power_input;
pearl_power_action pearl_power_decide(const pearl_power_input *in);
unsigned pearl_wifi_retry_delay(unsigned attempt);
