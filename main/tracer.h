#pragma once
#include <stdbool.h>
#ifdef PEARL_TRACER_HOST
#include "platform.h"
#else
#include "sdmmc_cmd.h"
#endif
void pearl_trace_attach(sdmmc_card_t *card);
bool pearl_trace_start(void);
void pearl_trace_stop(void);
void pearl_trace_snapshot(void);
void pearl_trace_events(unsigned count);
void pearl_trace_sd(const char *source);
void pearl_trace_cancel(void);
bool pearl_trace_sd_busy(void);
bool pearl_trace_ram_sink(void);
void pearl_trace_probe(bool enabled);
