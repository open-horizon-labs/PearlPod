/* Adapted from tdongle-tailnet-firmware alternative/tailnet/main/wifi_policy.h.
 * Copyright (c) 2026 白一百 baiyibai. SPDX-License-Identifier: MIT
 * See licenses/tdongle-MIT.txt. Stable strongest-signal selection; no flapping.
 */
#pragma once
static inline int pearl_wifi_pick(const int *signal, unsigned count,
                                  int current, int connected) {
  int best = -1;
  for (unsigned i = 0; i < count; i++)
    if (signal[i] > -127 && (best < 0 || signal[i] > signal[best]))
      best = (int)i;
  if (connected && current >= 0 && (unsigned)current < count) {
    if (best < 0 || signal[current] > -75 ||
        signal[best] < signal[current] + 12)
      return current;
  }
  return best;
}
