#pragma once
#include <stddef.h>
#include <stdint.h>
// Returns owned JPEG/PNG payload, bounded to 400 KiB. Caller frees.
uint8_t *pearl_art_payload(const char *path,size_t *size);

uint8_t *pearl_art_pixels(const char *path);
