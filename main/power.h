#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef PEARL_UI_HOST
static inline void pearl_power_activity(void) {}
#else
void pearl_power_activity(void);
uint32_t pearl_power_last_activity(void);
bool pearl_power_screen_asleep(void);
bool pearl_power_deep_supported(void);
void pearl_library_counts(unsigned *albums, unsigned *tracks);
#endif
