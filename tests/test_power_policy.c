#include "power_policy.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
  assert(pearl_usb_host_guard(true,false,100,0,300000));
  assert(pearl_usb_host_guard(false,true,300100,100,300000));
  assert(!pearl_usb_host_guard(false,true,300101,100,300000));
  assert(pearl_usb_host_guard(false,true,25,UINT32_MAX-24,50));
  assert(!pearl_usb_host_guard(false,false,100,0,300000));
  pearl_power_input p = {.now=400000,.last_activity=0,.paused_since=0,
    .screen_timeout=45000,.idle_timeout=300000,.button_released=true,.deep_supported=true};
  assert(pearl_power_decide(&p)==PEARL_POWER_DEEP_SLEEP);
  p.playing=true; assert(pearl_power_decide(&p)==PEARL_POWER_SCREEN_SLEEP);
  p.screen_asleep=true; assert(pearl_power_decide(&p)==PEARL_POWER_ACTIVE);
  p.playing=false; p.paused_since=399000; assert(pearl_power_decide(&p)==PEARL_POWER_ACTIVE);
  p.paused_since=0;
  p.network=true; assert(pearl_power_decide(&p)==PEARL_POWER_ACTIVE); p.network=false;
  p.busy=true; assert(pearl_power_decide(&p)==PEARL_POWER_ACTIVE); p.busy=false;
  p.usb_connected=true; assert(pearl_power_decide(&p)==PEARL_POWER_ACTIVE);
  p.screen_asleep=false; p.idle_timeout=0;
  assert(pearl_power_decide(&p)==PEARL_POWER_ACTIVE);
  p.usb_connected=false; assert(pearl_power_decide(&p)==PEARL_POWER_SCREEN_SLEEP);
  p.screen_asleep=true; p.idle_timeout=300000;
  p.button_released=false; assert(pearl_power_decide(&p)==PEARL_POWER_ACTIVE); p.button_released=true;
  p.deep_supported=false; assert(pearl_power_decide(&p)==PEARL_POWER_ACTIVE); p.deep_supported=true;
  p.idle_timeout=0; assert(pearl_power_decide(&p)==PEARL_POWER_ACTIVE);
  p.screen_asleep=false; p.screen_timeout=0; assert(pearl_power_decide(&p)==PEARL_POWER_ACTIVE);
  p.screen_timeout=45000; p.last_activity=UINT32_MAX-44999; p.now=0;
  assert(pearl_power_decide(&p)==PEARL_POWER_SCREEN_SLEEP);
  p.last_activity=UINT32_MAX-44998; assert(pearl_power_decide(&p)==PEARL_POWER_ACTIVE);
  const unsigned delays[]={1,2,4,8,16,30,30,30};
  for(unsigned i=0;i<8;i++)assert(pearl_wifi_retry_delay(i)==delays[i]);
  assert(pearl_wifi_retry_delay(UINT32_MAX)==30);
  puts("Power blockers, pause grace, timer rollover and capped backoff pass");
}
