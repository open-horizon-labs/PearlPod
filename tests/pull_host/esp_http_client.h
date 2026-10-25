#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define HTTP_EVENT_ON_HEADER 1
typedef struct{int event_id;void *user_data;const char *header_key,*header_value;}esp_http_client_event_t;
typedef struct{const char *url;int timeout_ms,buffer_size,buffer_size_tx;bool disable_auto_redirect;esp_err_t(*event_handler)(esp_http_client_event_t*);void *user_data;}esp_http_client_config_t;
typedef struct http *esp_http_client_handle_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t*);
int esp_http_client_set_header(esp_http_client_handle_t,const char*,const char*);
int esp_http_client_open(esp_http_client_handle_t,int);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
int esp_http_client_read(esp_http_client_handle_t,void*,unsigned);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t);
void esp_http_client_close(esp_http_client_handle_t);
void esp_http_client_cleanup(esp_http_client_handle_t);
