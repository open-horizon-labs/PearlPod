#include "power_policy.h"
pearl_power_action pearl_power_decide(const pearl_power_input *in) {
  if (!in)
    return PEARL_POWER_ACTIVE;
  uint32_t idle = in->now - in->last_activity,
           paused = in->now - in->paused_since;
  if (in->idle_timeout && idle >= in->idle_timeout &&
      paused >= in->idle_timeout && !in->playing && !in->network && !in->busy &&
      !in->usb_connected && in->button_released && in->deep_supported)
    return PEARL_POWER_DEEP_SLEEP;
  if (in->screen_timeout && idle >= in->screen_timeout && !in->screen_asleep &&
      in->button_released)
    return PEARL_POWER_SCREEN_SLEEP;
  return PEARL_POWER_ACTIVE;
}
unsigned pearl_wifi_retry_delay(unsigned attempt) {
  if (attempt > 5)
    attempt = 5;
  unsigned delay = 1u << attempt;
  return delay > 30 ? 30 : delay;
}
