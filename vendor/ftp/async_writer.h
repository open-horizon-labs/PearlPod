#pragma once
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
typedef struct pearl_writer pearl_writer;
pearl_writer *pearl_writer_create(unsigned capacity);
bool pearl_writer_begin(pearl_writer *writer, FILE *file);
void pearl_writer_backpressure(pearl_writer *writer);
bool pearl_writer_failed(pearl_writer *writer);
unsigned pearl_writer_space(pearl_writer *writer);
bool pearl_writer_append(pearl_writer *writer,const void *data,unsigned size);
/* Ownership of file transfers to the worker. Completion includes close/flush. */
bool pearl_writer_finish(pearl_writer *writer,bool abort);
bool pearl_writer_destroy(pearl_writer *writer);
void pearl_writer_trace(pearl_writer *writer,char *out,unsigned size);
