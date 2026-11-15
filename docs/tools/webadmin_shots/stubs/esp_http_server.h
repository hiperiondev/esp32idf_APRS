#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <sys/types.h>
typedef enum { HTTP_DELETE=0, HTTP_GET=1, HTTP_HEAD=2, HTTP_POST=3 } httpd_method_t;
typedef struct httpd_req { const char *uri; int method; size_t content_len; void *user_ctx; void *aux; } httpd_req_t;
typedef void *httpd_handle_t;
typedef struct { const char *uri; httpd_method_t method; esp_err_t (*handler)(httpd_req_t *); void *user_ctx; } httpd_uri_t;
typedef struct { int max_uri_handlers; int stack_size; int max_open_sockets; int lru_purge_enable; int recv_wait_timeout; int send_wait_timeout; int core_id; int task_priority; void *uri_match_fn; int max_resp_headers; } httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() {0}
#define HTTPD_RESP_USE_STRLEN -1
#define HTTPD_SOCK_ERR_TIMEOUT -3
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
#define HTTPD_400_BAD_REQUEST 400
#define HTTPD_404_NOT_FOUND 404
esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t len);
esp_err_t httpd_resp_sendstr(httpd_req_t *r, const char *s);
esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *buf, ssize_t len);
esp_err_t httpd_resp_sendstr_chunk(httpd_req_t *r, const char *s);
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *t);
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *f, const char *v);
esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *s);
esp_err_t httpd_resp_send_500(httpd_req_t *r);
esp_err_t httpd_resp_send_404(httpd_req_t *r);
esp_err_t httpd_resp_send_err(httpd_req_t *r, int code, const char *msg);
int httpd_req_recv(httpd_req_t *r, char *buf, size_t len);
size_t httpd_req_get_hdr_value_len(httpd_req_t *r, const char *f);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r, const char *f, char *v, size_t l);
size_t httpd_req_get_url_query_len(httpd_req_t *r);
esp_err_t httpd_req_get_url_query_str(httpd_req_t *r, char *buf, size_t l);
esp_err_t httpd_query_key_value(const char *q, const char *k, char *v, size_t l);
int httpd_req_to_sockfd(httpd_req_t *r);
esp_err_t httpd_start(httpd_handle_t *h, const httpd_config_t *c);
esp_err_t httpd_register_uri_handler(httpd_handle_t h, const httpd_uri_t *u);
