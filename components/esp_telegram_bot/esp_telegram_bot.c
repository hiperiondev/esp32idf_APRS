// @file esp_telegram_bot.c
//
// @author Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
// @date 2026
// @copyright MIT
// @see https://github.com/hiperiondev/esp32idf_telegram_bot
//
// @brief Implementation of the Telegram Bot HTTPS transport for ESP-IDF.
//
// Holds the bot token, the two URL prefixes every request is derived from
// and the client handles declared in esp_telegram_bot.h. Each handle owns
// one `esp_http_client` instance with keep-alive enabled and one mutex
// serialising the requests issued through it, so several tasks may share a
// handle and several handles may work in parallel.
//
// Three request paths are implemented here. Plain method calls are driven
// with `esp_http_client_perform()` and collect the answer through an event
// handler that appends the incoming chunks to the buffer supplied by the
// caller, flagging the response when the body does not fit. Multipart
// uploads are streamed manually: the text preamble is measured first so the
// exact `Content-Length` can be announced, then the binary payload is
// written in fixed size chunks. File downloads open the connection, read
// the body explicitly and store it raw.
//
// The TLS peer is validated either against the ESP-IDF certificate bundle
// or against a root certificate read once at initialization from the file
// system, which is what lets the chain be replaced from the web admin's
// File Storage page without rebuilding the firmware.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_tls.h"

#if CONFIG_TELEGRAM_BOT_CERT_BUNDLE
#include "esp_crt_bundle.h"
#endif

#include "esp_telegram_bot.h"

// Default socket and TLS timeout applied when the client configuration
// leaves timeout_ms at zero.
#define TELEGRAM_BOT_DEFAULT_TIMEOUT_MS 15000

// Default size of the esp_http_client receive buffer.
#define TELEGRAM_BOT_DEFAULT_RX_BUFFER 1024

// Maximum time a task waits for the mutex owned by a client handle.
#define TELEGRAM_BOT_LOCK_TIMEOUT_MS 60000

// Number of times esp_http_client_perform() is attempted for a single
// request before the call is reported as failed.
#define TELEGRAM_BOT_MAX_ATTEMPTS 3

// Base delay observed between two attempts after a transport level
// failure, giving a transient condition such as heap pressure during the
// TLS handshake or a momentary loss of the Wi-Fi link time to clear before
// the socket is reopened. The delay doubles on each subsequent attempt, so
// the full retry window stays close to the time a station typically needs
// to reassociate and renew its lease.
#define TELEGRAM_BOT_RETRY_DELAY_MS 2000

// Size of the chunks written to the socket when streaming the binary part
// of a multipart body.
#define TELEGRAM_BOT_UPLOAD_CHUNK 1024

// Memory class the TLS stack allocates from, and therefore the one whose
// figures explain a handshake that could not build its buffers. Reported
// alongside a failed attempt as two numbers rather than one, because the
// record buffers are single allocations governed by the largest contiguous
// block while the rest of the handshake is a crowd of small ones governed by
// the total.
#define TELEGRAM_BOT_HEAP_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)

// Boundary marker delimiting the parts of a multipart/form-data body.
#define TELEGRAM_BOT_BOUNDARY "esp32telegramboundary7MA4YWxkTrZu0gW"

static const char *TAG = "esp_telegram_bot";

// Internal representation of a client handle: one HTTP client plus the
// mutex that serialises the requests issued through it.
struct telegram_bot_client_s {
    esp_http_client_handle_t client;
    SemaphoreHandle_t lock;
    int timeout_ms;
};

// URL prefix used by every API method call, in the form
// https://api.telegram.org/bot<token>
static char s_api_prefix[TELEGRAM_BOT_URL_MAX_LEN] = "";

// URL prefix used to download files, in the form
// https://api.telegram.org/file/bot<token>
static char s_file_prefix[TELEGRAM_BOT_URL_MAX_LEN] = "";

// True once a valid token has been stored by telegram_bot_init().
static bool s_initialized = false;

// Handle used by the convenience helpers of this component. It is built on
// the first call that needs it, so an application driving its own handles
// never pays for the HTTP client and the receive buffer this one owns.
static telegram_bot_client_handle_t s_default_client = NULL;

// Guards the construction of the default handle, so two tasks reaching a
// convenience helper at the same time end up sharing one client.
static SemaphoreHandle_t s_default_lock = NULL;

#if !CONFIG_TELEGRAM_BOT_CERT_BUNDLE
// Root certificate validating the TLS connection to api.telegram.org. It
// is read from the file system at initialization time and stays allocated
// while the transport is initialized, because every HTTP client keeps a
// pointer to it for the whole life of the connection.
static char *s_cert_pem = NULL;

// Reads the trusted root certificates from the path configured in menuconfig
// into a null terminated heap buffer. The file lives in the LittleFS
// partition, so it can be replaced from the web admin's File Storage page
// when Telegram rotates its chain, without rebuilding the firmware.
//
// It may hold more than one certificate, concatenated in PEM form. That is
// what a host served from several front-ends needs: each front-end may
// present a chain rooted at a different authority, and a file carrying only
// one of them validates only the fraction of connections that happen to land
// on a matching front-end, failing the rest at random.
static esp_err_t telegram_bot_cert_load(void) {
    if (s_cert_pem != NULL) {
        return ESP_OK;
    }

    const char *path = CONFIG_TELEGRAM_BOT_CERT_PATH;
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "Root certificate %s cannot be opened, upload it to the storage partition", path);
        return ESP_ERR_NOT_FOUND;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return ESP_FAIL;
    }
    long size = ftell(file);
    rewind(file);

    if (size <= 0 || size > CONFIG_TELEGRAM_BOT_CERT_MAX_LEN) {
        ESP_LOGE(TAG, "Root certificate %s is %ld bytes, outside the accepted range", path, size);
        fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }

    // mbedTLS parses the PEM as a string, so one byte is reserved for the
    // terminator the file itself does not carry.
    char *pem = malloc((size_t)size + 1);
    if (pem == NULL) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    size_t read_len = fread(pem, 1, (size_t)size, file);
    fclose(file);

    if (read_len != (size_t)size) {
        ESP_LOGE(TAG, "Root certificate %s could only be read partially", path);
        free(pem);
        return ESP_FAIL;
    }
    pem[read_len] = '\0';

    // mbedTLS parses every certificate the buffer holds, so a file may
    // concatenate several roots and each of them becomes a trust anchor.
    // The count is logged because it is the one property of the file that
    // decides whether a front-end presenting a chain from a different
    // authority can be validated at all, and it is invisible from anywhere
    // else on the device.
    size_t anchors = 0;
    for (const char *scan = strstr(pem, "-----BEGIN CERTIFICATE-----"); scan != NULL; scan = strstr(scan + 1, "-----BEGIN CERTIFICATE-----")) {
        anchors++;
    }
    if (anchors == 0) {
        ESP_LOGE(TAG, "Root certificate %s does not hold a PEM certificate", path);
        free(pem);
        return ESP_ERR_INVALID_ARG;
    }

    s_cert_pem = pem;
    ESP_LOGI(TAG, "Loaded %u trust anchor%s from %s, %u bytes", (unsigned)anchors, (anchors == 1) ? "" : "s", path, (unsigned)read_len);
    return ESP_OK;
}

// Releases the certificate buffer once no client can reference it.
static void telegram_bot_cert_release(void) {
    free(s_cert_pem);
    s_cert_pem = NULL;
}
#endif

void telegram_bot_url_encode(const char *src, char *dst, size_t dst_size) {
    static const char *hex_digits = "0123456789ABCDEF";
    if (dst == NULL || dst_size == 0) {
        return;
    }
    size_t out = 0;
    if (src != NULL) {
        for (size_t i = 0; src[i] != '\0' && out + 1 < dst_size; i++) {
            unsigned char c = (unsigned char)src[i];
            bool is_unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
            if (is_unreserved) {
                dst[out++] = (char)c;
            } else if (out + 3 < dst_size) {
                dst[out++] = '%';
                dst[out++] = hex_digits[(c >> 4) & 0x0F];
                dst[out++] = hex_digits[c & 0x0F];
            } else {
                break;
            }
        }
    }
    dst[out] = '\0';
}

void telegram_bot_json_escape(const char *src, char *dst, size_t dst_size) {
    if (dst == NULL || dst_size == 0) {
        return;
    }
    size_t out = 0;
    if (src != NULL) {
        for (size_t i = 0; src[i] != '\0'; i++) {
            unsigned char c = (unsigned char)src[i];
            char sequence[7];
            const char *escaped = NULL;
            switch (c) {
                case '\\':
                    escaped = "\\\\";
                    break;
                case '"':
                    escaped = "\\\"";
                    break;
                case '\n':
                    escaped = "\\n";
                    break;
                case '\r':
                    escaped = "\\r";
                    break;
                case '\t':
                    escaped = "\\t";
                    break;
                case '\b':
                    escaped = "\\b";
                    break;
                case '\f':
                    escaped = "\\f";
                    break;
                default:
                    if (c < 0x20) {
                        snprintf(sequence, sizeof(sequence), "\\u%04X", c);
                        escaped = sequence;
                    }
                    break;
            }
            if (escaped != NULL) {
                size_t escaped_len = strlen(escaped);
                if (out + escaped_len + 1 > dst_size) {
                    break;
                }
                memcpy(dst + out, escaped, escaped_len);
                out += escaped_len;
            } else {
                if (out + 2 > dst_size) {
                    break;
                }
                dst[out++] = (char)c;
            }
        }
    }
    dst[out] = '\0';
}

// Appends up to len bytes of data to the response buffer, keeping it null
// terminated and flagging the response when the buffer runs out of room.
static void telegram_bot_response_append(telegram_bot_response_t *response, const char *data, size_t len) {
    if (response == NULL || response->buffer == NULL || response->buffer_size == 0) {
        return;
    }
    size_t space = response->buffer_size - 1 - response->data_len;
    size_t to_copy = (len < space) ? len : space;
    if (to_copy > 0) {
        memcpy(response->buffer + response->data_len, data, to_copy);
        response->data_len += to_copy;
        response->buffer[response->data_len] = '\0';
    }
    if (to_copy < len) {
        response->truncated = true;
    }
}

// Event handler shared by every request driven with
// esp_http_client_perform(). The response descriptor travels through the
// user_data field, so concurrent requests on different handles never share
// state.
static esp_err_t telegram_bot_http_event_handler(esp_http_client_event_t *evt) {
    telegram_bot_response_t *response = (telegram_bot_response_t *)evt->user_data;
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            // Both plain and chunked bodies are accumulated the same way,
            // bounded by the capacity of the caller supplied buffer.
            telegram_bot_response_append(response, (const char *)evt->data, (size_t)evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED: {
            int mbedtls_err = 0;
            int cert_flags = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, &cert_flags);
            if (cert_flags != 0) {
                // A non-zero verification bitmask means the chain the server
                // presented was refused, which is a configuration answer and
                // not a network one: it says nothing was wrong with the link,
                // the peer simply proved an identity this device does not
                // trust. It is reported at warning level with the trust
                // anchor in use, because the log is otherwise identical to a
                // refused connection and the two are fixed in opposite ways.
                ESP_LOGW(TAG, "Peer certificate refused, verification flags 0x%08x, validating against %s", (unsigned)cert_flags,
                         telegram_bot_certificate_path() != NULL ? telegram_bot_certificate_path() : "the ESP-IDF certificate bundle");
            } else if (err != ESP_OK) {
                ESP_LOGD(TAG, "Disconnected, last esp error 0x%x, last mbedtls failure 0x%x", err, mbedtls_err);
            }
            break;
        }
        default:
            break;
    }
    return ESP_OK;
}

bool telegram_bot_is_initialized(void) {
    return s_initialized;
}

// Takes the mutex owned by a client handle, bounded by
// TELEGRAM_BOT_LOCK_TIMEOUT_MS so a stuck request cannot block a caller
// forever.
static bool telegram_bot_lock(telegram_bot_client_handle_t handle) {
    return xSemaphoreTake(handle->lock, pdMS_TO_TICKS(TELEGRAM_BOT_LOCK_TIMEOUT_MS)) == pdTRUE;
}

static void telegram_bot_unlock(telegram_bot_client_handle_t handle) {
    xSemaphoreGive(handle->lock);
}

esp_err_t telegram_bot_client_create(const telegram_bot_client_config_t *config, telegram_bot_client_handle_t *out_handle) {
    if (out_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    *out_handle = NULL;

    int timeout_ms = (config != NULL && config->timeout_ms > 0) ? config->timeout_ms : TELEGRAM_BOT_DEFAULT_TIMEOUT_MS;
    int rx_buffer_size = (config != NULL && config->rx_buffer_size > 0) ? config->rx_buffer_size : TELEGRAM_BOT_DEFAULT_RX_BUFFER;
    bool keep_alive = (config == NULL) || !config->disable_keep_alive;

    struct telegram_bot_client_s *handle = calloc(1, sizeof(struct telegram_bot_client_s));
    if (handle == NULL) {
        return ESP_ERR_NO_MEM;
    }

    handle->lock = xSemaphoreCreateMutex();
    if (handle->lock == NULL) {
        free(handle);
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t http_config = {
        .url = TELEGRAM_BOT_API_HOST,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .event_handler = telegram_bot_http_event_handler,
        .timeout_ms = timeout_ms,
        .buffer_size = rx_buffer_size,
        .keep_alive_enable = keep_alive,
#if CONFIG_TELEGRAM_BOT_CERT_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#else
        .cert_pem = s_cert_pem,
#endif
    };

    handle->client = esp_http_client_init(&http_config);
    if (handle->client == NULL) {
        vSemaphoreDelete(handle->lock);
        free(handle);
        return ESP_ERR_NO_MEM;
    }
    handle->timeout_ms = timeout_ms;

    *out_handle = handle;
    return ESP_OK;
}

esp_err_t telegram_bot_client_destroy(telegram_bot_client_handle_t handle) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (handle->client != NULL) {
        esp_http_client_close(handle->client);
        esp_http_client_cleanup(handle->client);
    }
    if (handle->lock != NULL) {
        vSemaphoreDelete(handle->lock);
    }
    free(handle);
    return ESP_OK;
}

esp_err_t telegram_bot_client_disconnect(telegram_bot_client_handle_t handle) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // The mutex is taken without waiting: a handle busy with a request owns a
    // connection that is in use, and the only correct thing to do with it is
    // to leave it alone. Waiting here would also stall the caller for as long
    // as a long poll takes to expire.
    if (xSemaphoreTake(handle->lock, 0) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_http_client_close(handle->client);
    telegram_bot_unlock(handle);
    return ESP_OK;
}

// Returns the handle backing the convenience helpers, building it the first
// time one of them is called.
static telegram_bot_client_handle_t telegram_bot_default_client(void) {
    if (!s_initialized || s_default_lock == NULL) {
        return NULL;
    }
    if (xSemaphoreTake(s_default_lock, pdMS_TO_TICKS(TELEGRAM_BOT_LOCK_TIMEOUT_MS)) != pdTRUE) {
        return NULL;
    }
    if (s_default_client == NULL) {
        esp_err_t err = telegram_bot_client_create(NULL, &s_default_client);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Default client cannot be created: %s", esp_err_to_name(err));
            s_default_client = NULL;
        }
    }
    telegram_bot_client_handle_t handle = s_default_client;
    xSemaphoreGive(s_default_lock);
    return handle;
}

esp_err_t telegram_bot_init(const char *bot_token) {
    if (bot_token == NULL || bot_token[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(bot_token) > TELEGRAM_BOT_TOKEN_MAX_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (s_default_client != NULL) {
        telegram_bot_client_destroy(s_default_client);
        s_default_client = NULL;
    }

    int written = snprintf(s_api_prefix, sizeof(s_api_prefix), "%s/bot%s", TELEGRAM_BOT_API_HOST, bot_token);
    if (written < 0 || (size_t)written >= sizeof(s_api_prefix)) {
        s_api_prefix[0] = '\0';
        s_initialized = false;
        return ESP_ERR_INVALID_SIZE;
    }

    written = snprintf(s_file_prefix, sizeof(s_file_prefix), "%s/file/bot%s", TELEGRAM_BOT_API_HOST, bot_token);
    if (written < 0 || (size_t)written >= sizeof(s_file_prefix)) {
        s_api_prefix[0] = '\0';
        s_file_prefix[0] = '\0';
        s_initialized = false;
        return ESP_ERR_INVALID_SIZE;
    }

#if !CONFIG_TELEGRAM_BOT_CERT_BUNDLE
    // The certificate is loaded before the first client is created,
    // because every client keeps a pointer to it.
    esp_err_t cert_err = telegram_bot_cert_load();
    if (cert_err != ESP_OK) {
        s_api_prefix[0] = '\0';
        s_file_prefix[0] = '\0';
        return cert_err;
    }
#endif

    if (s_default_lock == NULL) {
        s_default_lock = xSemaphoreCreateMutex();
        if (s_default_lock == NULL) {
            s_api_prefix[0] = '\0';
            s_file_prefix[0] = '\0';
#if !CONFIG_TELEGRAM_BOT_CERT_BUNDLE
            telegram_bot_cert_release();
#endif
            return ESP_ERR_NO_MEM;
        }
    }

    s_initialized = true;

    ESP_LOGI(TAG, "Telegram transport initialized");
    return ESP_OK;
}

esp_err_t telegram_bot_deinit(void) {
    if (s_default_client != NULL) {
        telegram_bot_client_destroy(s_default_client);
        s_default_client = NULL;
    }
    if (s_default_lock != NULL) {
        vSemaphoreDelete(s_default_lock);
        s_default_lock = NULL;
    }
    memset(s_api_prefix, 0, sizeof(s_api_prefix));
    memset(s_file_prefix, 0, sizeof(s_file_prefix));
    s_initialized = false;
#if !CONFIG_TELEGRAM_BOT_CERT_BUNDLE
    telegram_bot_cert_release();
#endif
    return ESP_OK;
}

const char *telegram_bot_certificate_path(void) {
#if CONFIG_TELEGRAM_BOT_CERT_BUNDLE
    return NULL;
#else
    return CONFIG_TELEGRAM_BOT_CERT_PATH;
#endif
}

esp_err_t telegram_bot_client_call(telegram_bot_client_handle_t handle, const telegram_bot_request_t *request, telegram_bot_response_t *response) {
    if (handle == NULL || request == NULL || request->method == NULL || request->method[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    char url[TELEGRAM_BOT_URL_MAX_LEN];
    int written;
    if (request->query != NULL && request->query[0] != '\0') {
        written = snprintf(url, sizeof(url), "%s/%s?%s", s_api_prefix, request->method, request->query);
    } else {
        written = snprintf(url, sizeof(url), "%s/%s", s_api_prefix, request->method);
    }
    if (written < 0 || (size_t)written >= sizeof(url)) {
        ESP_LOGE(TAG, "Request URL for method %s does not fit in %d bytes", request->method, (int)sizeof(url));
        return ESP_ERR_INVALID_SIZE;
    }

    if (response != NULL) {
        response->data_len = 0;
        response->truncated = false;
        response->status_code = 0;
        if (response->buffer != NULL && response->buffer_size > 0) {
            response->buffer[0] = '\0';
        }
    }

    if (!telegram_bot_lock(handle)) {
        ESP_LOGE(TAG, "Timed out waiting for the client handle lock");
        return ESP_ERR_TIMEOUT;
    }

    esp_http_client_set_url(handle->client, url);
    esp_http_client_set_timeout_ms(handle->client, request->timeout_ms > 0 ? request->timeout_ms : handle->timeout_ms);
    esp_http_client_set_user_data(handle->client, response);

    if (request->json_body != NULL) {
        esp_http_client_set_method(handle->client, HTTP_METHOD_POST);
        esp_http_client_set_header(handle->client, "Content-Type", "application/json");
        esp_http_client_set_post_field(handle->client, request->json_body, strlen(request->json_body));
    } else {
        esp_http_client_set_method(handle->client, HTTP_METHOD_GET);
        esp_http_client_set_post_field(handle->client, NULL, 0);
    }

    // A keep-alive connection that sat idle long enough for the peer or an
    // intermediate NAT to drop it silently fails on the first read of the
    // next request, since the local socket still believes it is connected.
    // A connect failure while the station is momentarily off the air during
    // a reassociation or a lease renewal surfaces the same way, and so does
    // a heap that cannot serve the record buffers of a fresh handshake in
    // one contiguous piece. Every case is handled by the same loop: the
    // connection is closed and the request is performed again on a freshly
    // opened socket, after a delay that grows with each attempt so the
    // retry window stays close to the time such a condition typically needs
    // to clear. This is transparent to the caller and only costs extra TLS
    // handshakes on the cycles where an attempt genuinely failed.
    //
    // Waiting is what a link coming back needs, and it is all this layer can
    // offer for the heap: a handshake competes with whatever else the device
    // holds at that instant, and the memory that matters most is the memory
    // held by the caller's own other sessions. Releasing those is the
    // caller's decision, which is what telegram_bot_client_disconnect()
    // exists for; the figures logged below are what tell the two apart.
    esp_err_t err;
    int attempt = 0;
    while (true) {
        attempt++;
        err = esp_http_client_perform(handle->client);
        if (err == ESP_OK || attempt >= TELEGRAM_BOT_MAX_ATTEMPTS) {
            break;
        }
        ESP_LOGW(TAG, "%s failed on attempt %d/%d (%s), %u bytes free with a %u byte largest block, reopening and retrying", request->method, attempt,
                 TELEGRAM_BOT_MAX_ATTEMPTS, esp_err_to_name(err), (unsigned)heap_caps_get_free_size(TELEGRAM_BOT_HEAP_CAPS),
                 (unsigned)heap_caps_get_largest_free_block(TELEGRAM_BOT_HEAP_CAPS));
        esp_http_client_close(handle->client);
        if (response != NULL) {
            response->data_len = 0;
            response->truncated = false;
            response->status_code = 0;
            if (response->buffer != NULL && response->buffer_size > 0) {
                response->buffer[0] = '\0';
            }
        }
        if (request->json_body != NULL) {
            esp_http_client_set_post_field(handle->client, request->json_body, strlen(request->json_body));
        }
        vTaskDelay(pdMS_TO_TICKS(TELEGRAM_BOT_RETRY_DELAY_MS * attempt));
    }

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(handle->client);
        if (response != NULL) {
            response->status_code = status;
        }
        ESP_LOGD(TAG, "%s -> status %d, %d bytes", request->method, status, response != NULL ? (int)response->data_len : 0);
        if (response != NULL && response->truncated && !response->truncation_expected) {
            ESP_LOGW(TAG, "%s response truncated, increase the response buffer", request->method);
        }
    } else {
        ESP_LOGE(TAG, "%s request failed after %d attempts: %s, %u bytes free with a %u byte largest block", request->method, attempt, esp_err_to_name(err),
                 (unsigned)heap_caps_get_free_size(TELEGRAM_BOT_HEAP_CAPS), (unsigned)heap_caps_get_largest_free_block(TELEGRAM_BOT_HEAP_CAPS));
        // The failed connection is not reused on the next call either, since
        // exhausting every attempt usually means the network itself is down
        // rather than one stale socket or one transient allocation failure.
        esp_http_client_close(handle->client);
    }

    // The post field points at caller memory that is only valid for the
    // duration of this call.
    esp_http_client_set_post_field(handle->client, NULL, 0);
    esp_http_client_set_user_data(handle->client, NULL);

    telegram_bot_unlock(handle);
    return err;
}

// Drains the body of a request driven with esp_http_client_open() into the
// response descriptor. The perform() event handler is not involved in this
// path, so the data is read explicitly.
static esp_err_t telegram_bot_read_body(esp_http_client_handle_t client, telegram_bot_response_t *response) {
    char chunk[256];
    while (true) {
        int read_len = esp_http_client_read(client, chunk, sizeof(chunk));
        if (read_len < 0) {
            return ESP_FAIL;
        }
        if (read_len == 0) {
            break;
        }
        telegram_bot_response_append(response, chunk, (size_t)read_len);
    }
    return ESP_OK;
}

esp_err_t telegram_bot_client_call_multipart(telegram_bot_client_handle_t handle, const char *method, const telegram_bot_form_field_t *fields,
                                             size_t field_count, const char *file_field, const char *file_name, const char *file_mime, const uint8_t *file_data,
                                             size_t file_len, telegram_bot_response_t *response) {
    if (handle == NULL || method == NULL || method[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (file_field != NULL && (file_data == NULL || file_len == 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (file_name == NULL) {
        file_name = "file";
    }
    if (file_mime == NULL) {
        file_mime = "application/octet-stream";
    }

    // Measure the text preamble first so the exact Content-Length can be
    // announced before the body is streamed.
    size_t preamble_len = 0;
    for (size_t i = 0; i < field_count; i++) {
        if (fields[i].name == NULL || fields[i].value == NULL) {
            continue;
        }
        preamble_len += strlen("--" TELEGRAM_BOT_BOUNDARY "\r\n");
        preamble_len += strlen("Content-Disposition: form-data; name=\"\"\r\n\r\n");
        preamble_len += strlen(fields[i].name) + strlen(fields[i].value) + strlen("\r\n");
    }
    if (file_field != NULL) {
        preamble_len += strlen("--" TELEGRAM_BOT_BOUNDARY "\r\n");
        preamble_len += strlen("Content-Disposition: form-data; name=\"\"; filename=\"\"\r\n");
        preamble_len += strlen(file_field) + strlen(file_name);
        preamble_len += strlen("Content-Type: \r\n\r\n") + strlen(file_mime);
    }

    char *preamble = malloc(preamble_len + 1);
    if (preamble == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t offset = 0;
    for (size_t i = 0; i < field_count; i++) {
        if (fields[i].name == NULL || fields[i].value == NULL) {
            continue;
        }
        offset += snprintf(preamble + offset, preamble_len + 1 - offset,
                           "--" TELEGRAM_BOT_BOUNDARY "\r\n"
                           "Content-Disposition: form-data; name=\"%s\"\r\n\r\n%s\r\n",
                           fields[i].name, fields[i].value);
    }
    if (file_field != NULL) {
        offset += snprintf(preamble + offset, preamble_len + 1 - offset,
                           "--" TELEGRAM_BOT_BOUNDARY "\r\n"
                           "Content-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\n"
                           "Content-Type: %s\r\n\r\n",
                           file_field, file_name, file_mime);
    }

    static const char epilogue_with_file[] = "\r\n--" TELEGRAM_BOT_BOUNDARY "--\r\n";
    static const char epilogue_plain[] = "--" TELEGRAM_BOT_BOUNDARY "--\r\n";
    const char *epilogue = (file_field != NULL) ? epilogue_with_file : epilogue_plain;
    size_t epilogue_len = strlen(epilogue);

    size_t total_len = offset + epilogue_len + ((file_field != NULL) ? file_len : 0);

    char url[TELEGRAM_BOT_URL_MAX_LEN];
    int written = snprintf(url, sizeof(url), "%s/%s", s_api_prefix, method);
    if (written < 0 || (size_t)written >= sizeof(url)) {
        free(preamble);
        return ESP_ERR_INVALID_SIZE;
    }

    if (response != NULL) {
        response->data_len = 0;
        response->truncated = false;
        response->status_code = 0;
        if (response->buffer != NULL && response->buffer_size > 0) {
            response->buffer[0] = '\0';
        }
    }

    if (!telegram_bot_lock(handle)) {
        free(preamble);
        return ESP_ERR_TIMEOUT;
    }

    esp_http_client_set_url(handle->client, url);
    esp_http_client_set_method(handle->client, HTTP_METHOD_POST);
    esp_http_client_set_timeout_ms(handle->client, handle->timeout_ms);
    esp_http_client_set_post_field(handle->client, NULL, 0);
    esp_http_client_set_user_data(handle->client, NULL);
    esp_http_client_set_header(handle->client, "Content-Type", "multipart/form-data; boundary=" TELEGRAM_BOT_BOUNDARY);

    esp_err_t err = esp_http_client_open(handle->client, (int)total_len);
    if (err == ESP_OK) {
        if (esp_http_client_write(handle->client, preamble, offset) != (int)offset) {
            err = ESP_FAIL;
        }
    }

    if (err == ESP_OK && file_field != NULL) {
        size_t sent = 0;
        while (sent < file_len) {
            size_t chunk = file_len - sent;
            if (chunk > TELEGRAM_BOT_UPLOAD_CHUNK) {
                chunk = TELEGRAM_BOT_UPLOAD_CHUNK;
            }
            int wlen = esp_http_client_write(handle->client, (const char *)file_data + sent, chunk);
            if (wlen <= 0) {
                err = ESP_FAIL;
                break;
            }
            sent += (size_t)wlen;
        }
    }

    if (err == ESP_OK) {
        if (esp_http_client_write(handle->client, epilogue, epilogue_len) != (int)epilogue_len) {
            err = ESP_FAIL;
        }
    }

    if (err == ESP_OK) {
        if (esp_http_client_fetch_headers(handle->client) < 0) {
            err = ESP_FAIL;
        }
    }

    if (err == ESP_OK) {
        err = telegram_bot_read_body(handle->client, response);
        int status = esp_http_client_get_status_code(handle->client);
        if (response != NULL) {
            response->status_code = status;
        }
        ESP_LOGD(TAG, "%s (multipart, %d bytes) -> status %d", method, (int)total_len, status);
    } else {
        ESP_LOGE(TAG, "%s multipart request failed: %s", method, esp_err_to_name(err));
    }

    esp_http_client_close(handle->client);
    // The multipart content type must not leak into the JSON requests that
    // follow on this handle.
    esp_http_client_set_header(handle->client, "Content-Type", "application/json");

    telegram_bot_unlock(handle);
    free(preamble);
    return err;
}

esp_err_t telegram_bot_client_download(telegram_bot_client_handle_t handle, const char *file_path, uint8_t *out_data, size_t out_capacity, size_t *out_len) {
    if (handle == NULL || file_path == NULL || out_data == NULL || out_capacity == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (out_len != NULL) {
        *out_len = 0;
    }

    char url[TELEGRAM_BOT_URL_MAX_LEN];
    int written = snprintf(url, sizeof(url), "%s/%s", s_file_prefix, file_path);
    if (written < 0 || (size_t)written >= sizeof(url)) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (!telegram_bot_lock(handle)) {
        return ESP_ERR_TIMEOUT;
    }

    esp_http_client_set_url(handle->client, url);
    esp_http_client_set_method(handle->client, HTTP_METHOD_GET);
    esp_http_client_set_timeout_ms(handle->client, handle->timeout_ms);
    esp_http_client_set_post_field(handle->client, NULL, 0);
    esp_http_client_set_user_data(handle->client, NULL);

    esp_err_t err = esp_http_client_open(handle->client, 0);
    if (err == ESP_OK) {
        if (esp_http_client_fetch_headers(handle->client) < 0) {
            err = ESP_FAIL;
        }
    }

    size_t stored = 0;
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(handle->client);
        if (status < 200 || status > 299) {
            ESP_LOGE(TAG, "File download rejected with status %d", status);
            err = ESP_FAIL;
        } else {
            while (stored < out_capacity) {
                int read_len = esp_http_client_read(handle->client, (char *)out_data + stored, (int)(out_capacity - stored));
                if (read_len < 0) {
                    err = ESP_FAIL;
                    break;
                }
                if (read_len == 0) {
                    break;
                }
                stored += (size_t)read_len;
            }
            if (err == ESP_OK && stored == out_capacity && !esp_http_client_is_complete_data_received(handle->client)) {
                ESP_LOGW(TAG, "File larger than the %d byte destination buffer", (int)out_capacity);
                err = ESP_ERR_INVALID_SIZE;
            }
        }
    }

    if (out_len != NULL) {
        *out_len = stored;
    }

    esp_http_client_close(handle->client);
    telegram_bot_unlock(handle);
    return err;
}

esp_err_t telegram_bot_get_me(char *response_buffer, size_t buffer_size) {
    telegram_bot_client_handle_t handle = telegram_bot_default_client();
    if (handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    telegram_bot_request_t request = {
        .method = "getMe",
    };
    telegram_bot_response_t response = {
        .buffer = response_buffer,
        .buffer_size = buffer_size,
    };
    esp_err_t err = telegram_bot_client_call(handle, &request, (response_buffer != NULL) ? &response : NULL);
    if (err == ESP_OK && response_buffer != NULL) {
        ESP_LOGI(TAG, "getMe -> %s", response_buffer);
    }
    return err;
}

esp_err_t telegram_bot_send_message(const char *chat_id, const char *text) {
    if (chat_id == NULL || text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    telegram_bot_client_handle_t handle = telegram_bot_default_client();
    if (handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t body_size = (strlen(chat_id) + strlen(text)) * 6 + 64;
    char *body = malloc(body_size);
    if (body == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t escaped_chat_size = strlen(chat_id) * 6 + 1;
    size_t escaped_text_size = strlen(text) * 6 + 1;
    char *escaped_chat = malloc(escaped_chat_size);
    char *escaped_text = malloc(escaped_text_size);
    if (escaped_chat == NULL || escaped_text == NULL) {
        free(escaped_chat);
        free(escaped_text);
        free(body);
        return ESP_ERR_NO_MEM;
    }

    telegram_bot_json_escape(chat_id, escaped_chat, escaped_chat_size);
    telegram_bot_json_escape(text, escaped_text, escaped_text_size);
    snprintf(body, body_size, "{\"chat_id\":\"%s\",\"text\":\"%s\"}", escaped_chat, escaped_text);

    telegram_bot_request_t request = {
        .method = "sendMessage",
        .json_body = body,
    };
    esp_err_t err = telegram_bot_client_call(handle, &request, NULL);

    free(escaped_chat);
    free(escaped_text);
    free(body);
    return err;
}
