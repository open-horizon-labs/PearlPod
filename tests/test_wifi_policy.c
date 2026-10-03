#include "wifi_policy.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
  int s[] = {-80, -50, -127, -50};
  assert(pearl_wifi_pick(s, 4, -1, 0) == 1);
  assert(pearl_wifi_pick(s, 4, 0, 1) == 1);
  s[0] = -70;
  assert(pearl_wifi_pick(s, 4, 0, 1) == 0);
  s[0] = -78;
  s[1] = -67;
  s[3] = -127;
  assert(pearl_wifi_pick(s, 4, 0, 1) == 0);
  s[1] = -66;
  assert(pearl_wifi_pick(s, 4, 0, 1) == 1);
  for (int i = 0; i < 4; i++)
    s[i] = -127;
  assert(pearl_wifi_pick(s, 4, -1, 0) == -1);
  assert(pearl_wifi_pick(s, 0, 0, 0) == -1);
  assert(pearl_wifi_pick(s, 4, 0, 1) == 0);
  puts("WiFi strongest selection, stable ties, 12 dB hysteresis, weak/strong "
       "retention and invisible profiles pass.");
}
