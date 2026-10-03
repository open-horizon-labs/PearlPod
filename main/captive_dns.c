// Captive DNS adapted from T-Dongle main/portal.c (ce0e172).
// MIT notice: vendor/captive/LICENSE. Lifecycle follows the acknowledged stop
// pattern in roon-knob-integration tough_app/main/dns_server.c (e1872dfa).
#include "captive_dns.h"
#include <string.h>
int pearl_captive_dns_reply(const uint8_t *q, size_t size, uint8_t *out, size_t capacity) {
    if (!q || !out || size < 17 || size > 512 || (q[2] & 0xf8) || q[4] || q[5] != 1) return -1;
    size_t pos = 12;
    while (pos < size && q[pos]) {
        unsigned label = q[pos++];
        if (label > 63 || label > size - pos) return -1;
        pos += label;
        if (pos - 12 > 255) return -1;
    }
    if (pos >= size || size - pos < 5) return -1;
    unsigned type = (q[pos + 1] << 8) | q[pos + 2];
    if (q[pos + 3] || q[pos + 4] != 1) return -1;
    size_t end = pos + 5, length = end + (type == 1 ? 16 : 0);
    if (length > capacity) return -1;
    memcpy(out, q, end);
    out[2] = 0x80 | (q[2] & 1); out[3] = 0x80;
    out[6] = 0; out[7] = type == 1;
    memset(out + 8, 0, 4);
    if (type == 1) {
        static const uint8_t answer[] = {0xc0,0x0c,0,1,0,1,0,0,0,60,0,4,192,168,4,1};
        memcpy(out + end, answer, sizeof(answer));
    }
    return (int)length;
}
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "lwip/sockets.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>
static atomic_bool running;
static bool stopping;
static TaskHandle_t task_handle;
static SemaphoreHandle_t stopped;
static atomic_uint replies;
typedef struct { int fd; uint8_t packets[1024]; } dns_context;
static void dns_task(void *arg) {
    dns_context *context = arg;
    int fd = context->fd;
    uint8_t *buffers = context->packets;
    {
        while (atomic_load(&running)) {
            struct sockaddr_in peer;
            socklen_t peer_size = sizeof(peer);
            int n = recvfrom(fd, buffers, 512, 0, (struct sockaddr *)&peer, &peer_size);
            if (n <= 0 || !atomic_load(&running)) continue;
            int length = pearl_captive_dns_reply(buffers, n, buffers + 512, 512);
            if (length > 0 && sendto(fd, buffers + 512, length, 0, (struct sockaddr *)&peer, peer_size) == length)
                atomic_fetch_add(&replies,1);
        }
        free(context);
    }
    atomic_store(&running, false);
    close(fd); /* Task owns the socket; it cannot be reused before acknowledgement. */
    xSemaphoreGive(stopped);
    vTaskDelete(NULL);
}
static bool reap(void) {
    if (!stopping && atomic_load(&running)) return true;
    if (!task_handle) return true;
    if (xSemaphoreTake(stopped, 0) != pdTRUE) return false;
    task_handle = NULL; stopping = false;
    return true;
}
bool pearl_captive_dns_start(void) {
    if (!reap()) return false;
    if (atomic_load(&running)) return true;
    if (!stopped) stopped = xSemaphoreCreateBinary();
    if (!stopped) return false;
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return false;
    struct sockaddr_in address = {.sin_family=AF_INET,.sin_port=htons(53)};
    address.sin_addr.s_addr = htonl(0xc0a80401); /* Only the captive AP's interface. */
    struct timeval timeout = {.tv_sec=0,.tv_usec=100000};
    if (bind(fd,(struct sockaddr *)&address,sizeof(address)) ||
        setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))) {
        close(fd); return false;
    }
    dns_context *context = heap_caps_malloc(sizeof(*context),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!context) { close(fd);return false; }
    context->fd=fd;atomic_store(&replies,0);
    atomic_store(&running,true);
    if (xTaskCreate(dns_task,"captive_dns",3072,context,2,&task_handle)!=pdPASS) {
        atomic_store(&running,false); task_handle=NULL;free(context);close(fd);return false;
    }
    return true;
}
bool pearl_captive_dns_active(void) { return atomic_load(&running); }
unsigned pearl_captive_dns_replies(void) { return atomic_load(&replies); }
bool pearl_captive_dns_stop(void) {
    if (!task_handle) return true;
    atomic_store(&running,false);stopping=true;
    if (reap()) return true;
    if (xSemaphoreTake(stopped,pdMS_TO_TICKS(500))!=pdTRUE) return false;
    task_handle=NULL;stopping=false;return true;
}
#endif
