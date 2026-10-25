#pragma once
#include <stdbool.h>
// LVGL owner/lock required. A short press leaves a brief instruction visible.
void pearl_power_hint_update(bool held, unsigned percent);
void pearl_power_hint_hide(void);
