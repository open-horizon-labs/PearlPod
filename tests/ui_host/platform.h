#pragma once
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
#define heap_caps_malloc(n,c) malloc(n)
#define portMAX_DELAY 0
#define pdTRUE 1
#define pdMS_TO_TICKS(n) (n)
typedef struct {void *data;size_t size;unsigned capacity,head,count;} *QueueHandle_t;
static QueueHandle_t xQueueCreate(int count,size_t size){QueueHandle_t q=calloc(1,sizeof(*q));q->size=size;q->capacity=count;q->data=malloc(count*size);return q;}
static int xQueueReceive(QueueHandle_t q,void *p,int timeout){if(!q->count)return 0;memcpy(p,(char*)q->data+q->head*q->size,q->size);q->head=(q->head+1)%q->capacity;q->count--;return 1;}
static int xQueueSend(QueueHandle_t q,const void *p,int timeout){if(q->count==q->capacity)return 0;unsigned tail=(q->head+q->count)%q->capacity;memcpy((char*)q->data+tail*q->size,p,q->size);q->count++;return 1;}
static void xTaskCreate(void (*f)(void *),const char *n,int stack,void *a,int pri,void *handle){}
