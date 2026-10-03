#pragma once
#include <stdbool.h>
#include <stdint.h>
#define PEARL_PATH 512
#define PEARL_NAME 160
#define PEARL_MAX_TRACKS 2048
#define PEARL_MAX_ALBUMS 256
#define PEARL_ART_SIZE 240

typedef struct { char path[PEARL_PATH], title[PEARL_NAME]; unsigned album; } pearl_track;
typedef struct { char path[PEARL_PATH], title[PEARL_NAME], art[PEARL_PATH]; unsigned first, count; } pearl_album;
typedef struct { pearl_track *tracks; pearl_album *albums; unsigned track_count,album_count; bool truncated; } pearl_library;
int pearl_library_scan(pearl_library *lib,const char *root);
void pearl_library_free(pearl_library *lib);
int pearl_next(const pearl_library *lib,int current,int direction);
int pearl_volume(int current,int delta,int ceiling);

typedef struct { bool raw,stable,long_sent; uint32_t changed,pressed; } pearl_button;
typedef enum { BUTTON_NONE,BUTTON_SHORT,BUTTON_LONG } pearl_button_event;
pearl_button_event pearl_button_update(pearl_button *b,bool pressed,uint32_t now);

typedef struct { int track,volume; bool paused,ready; uint32_t seconds; char error[120]; } pearl_state;
void pearl_audio_start(pearl_library *lib);
void pearl_audio_play(int track);
void pearl_audio_toggle(void);
void pearl_audio_step(int delta);
void pearl_audio_volume(int delta);
void pearl_audio_shutdown(void);
pearl_state pearl_audio_state(void);
void pearl_ui_start(void);
void pearl_ui_ready(pearl_library *lib,const char *error);
