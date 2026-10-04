#pragma once
#include <stdbool.h>
#include <stdint.h>
#define PEARL_PATH 512
#define PEARL_NAME 160
#define PEARL_ART_SIZE 240

typedef struct {char *path,*title,*artist,*lyrics,*genre;unsigned album,track_number,disc_number;} pearl_track;
typedef struct {char *path,*title,*artist,*art;unsigned first,count;} pearl_album;
typedef enum {PEARL_ALBUMS,PEARL_ARTISTS,PEARL_FOLDERS,PEARL_PLAYLISTS} pearl_view;
typedef struct {char *title,*path;unsigned *tracks,count,capacity;pearl_view kind;} pearl_collection;
typedef struct {pearl_track *tracks;pearl_album *albums;pearl_collection *collections;unsigned track_count,album_count,collection_count,track_capacity,album_capacity,collection_capacity,skipped;bool truncated;} pearl_library;
int pearl_collection_step(const pearl_library *lib,int collection,int position,int direction);
int pearl_library_scan(pearl_library *lib,const char *root);
void pearl_library_free(pearl_library *lib);
int pearl_next(const pearl_library *lib,int current,int direction);
int pearl_volume(int current,int delta,int ceiling);

typedef struct { bool raw,stable,long_sent; uint32_t changed,pressed; } pearl_button;
typedef enum { BUTTON_NONE,BUTTON_SHORT,BUTTON_LONG } pearl_button_event;
pearl_button_event pearl_button_update(pearl_button *b,bool pressed,uint32_t now);

typedef struct { int track,volume; bool paused,ready; uint32_t seconds,milliseconds; char error[120]; } pearl_state;
void pearl_audio_start(pearl_library *lib);
void pearl_audio_play(int track);
void pearl_audio_play_collection(int collection,int position);
void pearl_library_rescan(void);
bool pearl_library_lock(void);
void pearl_library_unlock(void);
void pearl_ui_scanning(void);
bool pearl_audio_detach(void);
bool pearl_audio_release_for_sync(void);
bool pearl_audio_restore_from_sync(void);
void pearl_audio_attach(pearl_library *lib);
void pearl_audio_toggle(void);
void pearl_audio_step(int delta);
void pearl_audio_volume(int delta);
void pearl_audio_shutdown(void);
pearl_state pearl_audio_state(void);
void pearl_ui_start(void);
void pearl_ui_ready(pearl_library *lib,const char *error);

void pearl_ui_power(bool asleep);
// Called with the LVGL lock held. Cancel restores the current library view.
void pearl_ui_shutdown_begin(void);
void pearl_ui_shutdown_cancel(const char *reason);

void pearl_ui_theme_ready(void);

void pearl_library_index_refresh(void);
