#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#define PEARL_THEME_PIXELS (240*240*2)
typedef struct {char heading[96], phrase[128];_Atomic(uint8_t *) pixels;} pearl_theme_scene;
typedef struct {
 char name[49],id[49],title[65];
 uint32_t background,surface,text,accent,secondary;
 pearl_theme_scene welcome,farewell;
} pearl_theme;
/* Card task only, before publishing to UI. Zero card access during UI startup. */
void pearl_theme_load(const char *root,uint32_t sequence);
/* Read after playback is ready; caller owns the result until publication. */
uint8_t *pearl_theme_read_farewell(void);
/* Publish under the display lock; an already-rendered fallback stays valid. */
void pearl_theme_publish_farewell(uint8_t *pixels);
const pearl_theme *pearl_theme_current(void);
void pearl_theme_clear(void);
