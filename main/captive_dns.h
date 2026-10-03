#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Wildcard A reply, empty NOERROR for AAAA/HTTPS; rejects malformed questions. */
int pearl_captive_dns_reply(const uint8_t *query, size_t size, uint8_t *reply, size_t capacity);
#ifdef ESP_PLATFORM
bool pearl_captive_dns_start(void);
bool pearl_captive_dns_active(void);
unsigned pearl_captive_dns_replies(void);
bool pearl_captive_dns_stop(void);
#endif
