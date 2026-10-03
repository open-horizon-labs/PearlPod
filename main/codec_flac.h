#pragma once
#include "metadata.h"
/* Shared by device playback and host compatibility tests. Keep CRC validation on. */
typedef struct {FILE *file;long base;} pearl_flac_stream;
static size_t pearl_flac_read(void *user,void *buffer,size_t bytes){pearl_flac_stream *s=user;return fread(buffer,1,bytes,s->file);}
static drflac_bool32 pearl_flac_seek(void *user,int offset,drflac_seek_origin origin){pearl_flac_stream *s=user;return fseek(s->file,origin==DRFLAC_SEEK_SET?s->base+offset:offset,origin==DRFLAC_SEEK_SET?SEEK_SET:origin==DRFLAC_SEEK_END?SEEK_END:SEEK_CUR)==0;}
static drflac_bool32 pearl_flac_tell(void *user,drflac_int64 *cursor){pearl_flac_stream *s=user;long at=ftell(s->file);if(at<s->base)return DRFLAC_FALSE;*cursor=at-s->base;return DRFLAC_TRUE;}
static drflac *pearl_flac_open(const char *path,pearl_flac_stream *s){s->file=fopen(path,"rb");if(!s->file)return NULL;setvbuf(s->file,NULL,_IOFBF,16384);s->base=pearl_audio_offset(s->file);drflac *decoder=drflac_open(pearl_flac_read,pearl_flac_seek,pearl_flac_tell,s,NULL);if(!decoder){fclose(s->file);s->file=NULL;}return decoder;}
static void pearl_flac_close(drflac *decoder,pearl_flac_stream *s){drflac_close(decoder);if(s->file)fclose(s->file);s->file=NULL;}
