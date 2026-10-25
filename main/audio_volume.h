#pragma once
#include <math.h>
#include <stdint.h>

#define PEARL_VOLUME_MAX 100

/* Keep quiet settings close to their former level after removing the DAC's
 * fixed attenuation. Full volume is unity, never digital amplification. */
static inline float pearl_volume_gain(int volume) {
    if (volume <= 0) return 0.f;
    if (volume >= PEARL_VOLUME_MAX) return 1.f;
    return powf(10.f, (-80.f + 0.8f * volume) / 20.f);
}

static inline int32_t pearl_volume_slot(float sample, float gain) {
    float scaled = sample * gain;
    if (scaled > 32767.f) scaled = 32767.f;
    if (scaled < -32768.f) scaled = -32768.f;
    return (int32_t)scaled * 65536;
}
