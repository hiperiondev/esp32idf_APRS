/**
 * @file esp_telegram_bot.h
 *
 * @author Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
 * @date 2026
 * @copyright MIT
 * @see https://github.com/hiperiondev/esp32idf_telegram_bot
 *
 * @brief Transport layer for the Telegram Bot HTTPS API on ESP-IDF.
 *
 * This component is the low level half of the project. It owns the bot
 * token, builds the API URLs, manages the TLS connections and exposes a
 * small, generic way of invoking any method of the Telegram Bot API:
 *
 * - @ref telegram_bot_client_call() invokes a method with an optional URL
 *   query string and an optional JSON body, and copies the raw JSON answer
 *   into a caller supplied buffer.
 * - @ref telegram_bot_client_call_multipart() invokes a method with a
 *   `multipart/form-data` body, which is what the Telegram API requires to
 *   upload photos and documents held in RAM.
 * - @ref telegram_bot_client_download() retrieves the binary payload of a
 *   file hosted on the Telegram file server.
 *
 * Requests are issued through a client handle. Each handle owns one
 * `esp_http_client` instance, with keep-alive enabled unless the handle
 * configuration turns it off, so consecutive calls on the same handle reuse
 * the TLS session instead of repeating the handshake. A handle serialises
 * its own requests with an internal mutex and is therefore safe to share
 * between tasks, but a task that is blocked on a long poll also blocks
 * every other user of that handle. Applications that poll for updates and
 * send messages concurrently should create one handle per activity.
 *
 * Several handles do not mean several live TLS sessions have to coexist. A
 * session that is merely being kept for later holds several kilobytes,
 * among them the single contiguous block a new handshake needs for its
 * record buffers, which is more than a small heap can spare twice over.
 * @ref telegram_bot_client_disconnect() hands one back without destroying
 * the handle, so an application can decide which of its connections is
 * worth keeping at any moment.
 *
 * The convenience helpers @ref telegram_bot_get_me() and
 * @ref telegram_bot_send_message() run on a default handle, built the first
 * time one of them is called and released by @ref telegram_bot_deinit().
 *
 * The server certificate can be validated either against the ESP-IDF
 * certificate bundle or against one or more root certificates read from the
 * file system; the choice is made in menuconfig, under "Telegram bot
 * transport". In the second case the PEM file is read once by
 * @ref telegram_bot_init() from the path held by
 * `CONFIG_TELEGRAM_BOT_CERT_PATH`, which points into the LittleFS
 * partition mounted by `main/storage.c`. The file system must therefore be
 * mounted before the transport is initialized, and the certificate can be
 * replaced from the web admin's File Storage page when Telegram rotates its
 * chain, without rebuilding the firmware.
 *
 * Higher level behaviour (long polling, command dispatch, authorization,
 * telemetry, alarms) lives in the separate `telegram_service` component,
 * which is built on top of this API.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Fixed host serving the Telegram Bot API. */
#define TELEGRAM_BOT_API_HOST "https://api.telegram.org"

/** @brief Maximum number of characters accepted for a bot token. */
#define TELEGRAM_BOT_TOKEN_MAX_LEN 128

/** @brief Maximum number of characters of a fully built request URL. */
#define TELEGRAM_BOT_URL_MAX_LEN 512

/** @brief Opaque handle representing one Telegram API client connection. */
typedef struct telegram_bot_client_s *telegram_bot_client_handle_t;

/**
 * @brief Configuration of a Telegram API client handle.
 *
 * Pass a zero-initialised structure to accept every default; fields left
 * at zero are replaced by the built-in defaults documented below.
 */
typedef struct {
    /** Socket and TLS timeout in milliseconds. Defaults to 15000. When the
     *  handle is used for long polling this value must be larger than the
     *  polling timeout requested to the Telegram servers. */
    int timeout_ms;
    /** Size in bytes of the internal `esp_http_client` receive buffer.
     *  Defaults to 1024. Larger values reduce the number of read
     *  iterations needed to drain a big `getUpdates` answer. */
    int rx_buffer_size;
    /** Set to true to disable HTTP keep-alive and close the TCP connection
     *  after every request. Keep-alive is enabled by default and is
     *  strongly recommended, as it avoids one TLS handshake per call. */
    bool disable_keep_alive;
} telegram_bot_client_config_t;

/**
 * @brief Destination buffer and result of an API call.
 *
 * The caller owns @c buffer and sets @c buffer_size before the call. The
 * transport fills in the remaining fields. The stored body is always
 * null-terminated, so it can be handed directly to a JSON parser.
 */
typedef struct {
    /** Buffer receiving the response body. May be NULL to discard it. */
    char *buffer;
    /** Capacity of @c buffer in bytes, including the null terminator. */
    size_t buffer_size;
    /** Number of bytes actually stored in @c buffer, excluding the null
     *  terminator. */
    size_t data_len;
    /** True when the response was larger than @c buffer_size and the tail
     *  had to be dropped. The stored fragment is not valid JSON. */
    bool truncated;
    /** Set to true when the caller only reads the head of the answer, as
     *  is the case for a method whose result is merely checked for
     *  success. Truncation is then a normal outcome and is not reported
     *  in the log; ::truncated is still set. */
    bool truncation_expected;
    /** HTTP status code returned by the server, or 0 when the request did
     *  not reach the response stage. */
    int status_code;
} telegram_bot_response_t;

/**
 * @brief Description of a single Telegram API request.
 *
 * @c query and @c json_body are mutually independent: supplying a body
 * turns the request into a POST, otherwise a GET is issued. Query strings
 * must already be percent-encoded, for which
 * @ref telegram_bot_url_encode() is provided.
 */
typedef struct {
    /** Name of the API method to invoke, for example "getUpdates". */
    const char *method;
    /** Percent-encoded query string without the leading '?', or NULL. */
    const char *query;
    /** JSON body to POST, or NULL to issue a GET request. */
    const char *json_body;
    /** Per-request timeout override in milliseconds, or 0 to keep the
     *  timeout configured on the handle. */
    int timeout_ms;
} telegram_bot_request_t;

/**
 * @brief One text field of a `multipart/form-data` body.
 */
typedef struct {
    /** Form field name, for example "chat_id". */
    const char *name;
    /** Field value as a null-terminated string. */
    const char *value;
} telegram_bot_form_field_t;

/**
 * @brief Initialize the transport with the Telegram bot token to use.
 *
 * Stores the token, builds the two URL prefixes every request is derived
 * from (`https://api.telegram.org/bot<token>` for API methods and
 * `https://api.telegram.org/file/bot<token>` for downloads), reads the
 * root certificate from the file system when the certificate bundle is
 * not selected, and creates the default client handle used by the
 * convenience helpers.
 *
 * Calling this function again after a successful initialization replaces
 * the token and recreates the default handle. Client handles created
 * explicitly with @ref telegram_bot_client_create() keep working and pick
 * up the new token on their next request.
 *
 * @param[in] bot_token Null-terminated bot token issued by @BotFather.
 *
 * @return
 *  - ESP_OK on success.
 *  - ESP_ERR_INVALID_ARG if @p bot_token is NULL or empty.
 *  - ESP_ERR_INVALID_SIZE if @p bot_token exceeds
 *    ::TELEGRAM_BOT_TOKEN_MAX_LEN characters, or if the certificate file
 *    is empty or larger than `CONFIG_TELEGRAM_BOT_CERT_MAX_LEN`.
 *  - ESP_ERR_NOT_FOUND if the certificate file cannot be opened, which
 *    is what happens when the file system is not mounted yet or when the
 *    file has not been uploaded.
 *  - ESP_ERR_NO_MEM if the certificate buffer or the default client
 *    handle cannot be allocated.
 */
esp_err_t telegram_bot_init(const char *bot_token);

/**
 * @brief Report the path the root certificate is read from.
 *
 * Lets the application tell an operator which file has to be uploaded to
 * the storage partition, and which one to replace when the server chain
 * changes.
 *
 * @return Null-terminated absolute path of the PEM file, or NULL when
 *         the transport validates the server against the ESP-IDF
 *         certificate bundle and reads no file at all.
 */
const char *telegram_bot_certificate_path(void);

/**
 * @brief Release the token and the default client handle.
 *
 * After this call the convenience helpers fail with ESP_ERR_INVALID_STATE
 * until @ref telegram_bot_init() is called again. Handles created with
 * @ref telegram_bot_client_create() are not affected and must be destroyed
 * by their owner.
 *
 * @return ESP_OK always.
 */
esp_err_t telegram_bot_deinit(void);

/**
 * @brief Report whether a bot token has been configured.
 *
 * @return true when @ref telegram_bot_init() has completed successfully
 *         and @ref telegram_bot_deinit() has not been called since.
 */
bool telegram_bot_is_initialized(void);

/**
 * @brief Create a client handle for issuing Telegram API requests.
 *
 * The handle owns one `esp_http_client` instance and one mutex. Requests
 * made through the same handle are serialised; requests made through
 * different handles run independently.
 *
 * @param[in]  config Handle configuration, or NULL to use every default.
 * @param[out] out_handle Receives the new handle on success.
 *
 * @return
 *  - ESP_OK on success.
 *  - ESP_ERR_INVALID_ARG if @p out_handle is NULL.
 *  - ESP_ERR_INVALID_STATE if the transport has not been initialized.
 *  - ESP_ERR_NO_MEM if the handle, its mutex or the HTTP client cannot be
 *    allocated.
 */
esp_err_t telegram_bot_client_create(const telegram_bot_client_config_t *config, telegram_bot_client_handle_t *out_handle);

/**
 * @brief Destroy a client handle and close its connection.
 *
 * The caller must guarantee that no other task is using the handle.
 *
 * @param[in] handle Handle returned by @ref telegram_bot_client_create().
 *
 * @return
 *  - ESP_OK on success.
 *  - ESP_ERR_INVALID_ARG if @p handle is NULL.
 */
esp_err_t telegram_bot_client_destroy(telegram_bot_client_handle_t handle);

/**
 * @brief Close the connection a client handle is holding, keeping the
 *        handle usable.
 *
 * The next request issued on @p handle opens a fresh socket and performs a
 * new TLS handshake, exactly as if the connection had never been
 * established. Everything a handshake needs while it runs - the record
 * buffers sized by `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN` and
 * `CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN`, the parsed certificate chain, the
 * session context - is returned to the heap in the meantime.
 *
 * This is what lets an application that owns several handles keep only one
 * live TLS session at a time on a device whose heap cannot hold two. The
 * record buffers are single allocations, so what governs them is the
 * largest contiguous block rather than the total free heap, and a session
 * left open for later is exactly the kind of block that splits it.
 *
 * The handle mutex is taken without waiting, so a handle busy with a
 * request is reported as such and left untouched rather than closed under
 * the task using it. Calling this on an already idle connection is
 * harmless.
 *
 * @param[in] handle Handle returned by @ref telegram_bot_client_create().
 *
 * @return
 *  - ESP_OK if the connection was closed or was already closed.
 *  - ESP_ERR_INVALID_ARG if @p handle is NULL.
 *  - ESP_ERR_INVALID_STATE if the handle is serving a request right now.
 */
esp_err_t telegram_bot_client_disconnect(telegram_bot_client_handle_t handle);

/**
 * @brief Invoke a Telegram API method and capture its JSON answer.
 *
 * Builds the request URL from the configured token, the method name and
 * the optional query string, performs the request over TLS and copies the
 * response body into @p response.
 *
 * A response that does not fit in the supplied buffer is stored truncated
 * and flagged through ::telegram_bot_response_t::truncated; the call still
 * returns ESP_OK when the HTTP transaction itself succeeded, so callers
 * that parse the body must check the flag. A caller that deliberately
 * sizes the buffer for the head of the answer only sets
 * ::telegram_bot_response_t::truncation_expected to keep the log quiet.
 *
 * A transport level failure is treated as a sign of a transient condition
 * such as an idle keep-alive session silently closed by the peer, a
 * momentary loss of the Wi-Fi link or a heap that could not serve the TLS
 * record buffers: the connection is force-closed and the request is
 * retried on a freshly opened socket, waiting longer between each
 * successive attempt so the retry window stays close to the time a
 * station typically needs to reassociate and renew its lease. This is
 * transparent to the caller and only adds the cost of the extra TLS
 * handshakes, and only on the request that hits the failure. Each failed
 * attempt is logged with the free heap and the largest free block, which
 * is what distinguishes a link that went away from a heap that could not
 * fit the handshake; freeing the latter is the caller's decision, taken
 * through @ref telegram_bot_client_disconnect().
 *
 * @param[in]     handle   Client handle to issue the request on.
 * @param[in]     request  Method, query string, body and timeout to use.
 * @param[in,out] response Destination buffer and result of the call. May
 *                         be NULL to perform the request and discard the
 *                         answer.
 *
 * @return
 *  - ESP_OK if the request completed, whatever the HTTP status code.
 *  - ESP_ERR_INVALID_ARG if @p handle or @p request is NULL, or if
 *    ::telegram_bot_request_t::method is NULL or empty.
 *  - ESP_ERR_INVALID_STATE if the transport has not been initialized.
 *  - ESP_ERR_INVALID_SIZE if the built URL exceeds
 *    ::TELEGRAM_BOT_URL_MAX_LEN characters.
 *  - ESP_ERR_TIMEOUT if the handle mutex could not be taken.
 *  - Any error propagated by `esp_http_client_perform()` otherwise.
 */
esp_err_t telegram_bot_client_call(telegram_bot_client_handle_t handle, const telegram_bot_request_t *request, telegram_bot_response_t *response);

/**
 * @brief Invoke a Telegram API method with a `multipart/form-data` body.
 *
 * Sends the text fields in @p fields followed by one binary part holding
 * @p file_data, which is what `sendPhoto`, `sendDocument`, `sendAudio` and
 * `sendVideo` expect when the payload is uploaded from the device instead
 * of being referenced by URL or by file identifier.
 *
 * The binary part is optional: pass NULL for @p file_field to send only
 * the text fields.
 *
 * @param[in]     handle      Client handle to issue the request on.
 * @param[in]     method      API method name, for example "sendPhoto".
 * @param[in]     fields      Array of text form fields, or NULL.
 * @param[in]     field_count Number of entries in @p fields.
 * @param[in]     file_field  Form field name of the binary part, for
 *                            example "photo" or "document". NULL to omit
 *                            the binary part.
 * @param[in]     file_name   File name announced to Telegram, for example
 *                            "capture.jpg". Ignored when @p file_field is
 *                            NULL; defaults to "file" when NULL.
 * @param[in]     file_mime   MIME type of the binary part. Ignored when
 *                            @p file_field is NULL; defaults to
 *                            "application/octet-stream" when NULL.
 * @param[in]     file_data   Pointer to the binary payload.
 * @param[in]     file_len    Length of @p file_data in bytes.
 * @param[in,out] response    Destination buffer and result of the call, or
 *                            NULL to discard the answer.
 *
 * @return
 *  - ESP_OK if the request completed, whatever the HTTP status code.
 *  - ESP_ERR_INVALID_ARG on a NULL handle or method, or when a binary part
 *    is requested without data.
 *  - ESP_ERR_INVALID_STATE if the transport has not been initialized.
 *  - ESP_ERR_NO_MEM if the multipart preamble cannot be allocated.
 *  - ESP_ERR_TIMEOUT if the handle mutex could not be taken.
 *  - Any error propagated by the `esp_http_client` write path otherwise.
 */
esp_err_t telegram_bot_client_call_multipart(telegram_bot_client_handle_t handle, const char *method, const telegram_bot_form_field_t *fields,
                                             size_t field_count, const char *file_field, const char *file_name, const char *file_mime, const uint8_t *file_data,
                                             size_t file_len, telegram_bot_response_t *response);

/**
 * @brief Download a file from the Telegram file server into RAM.
 *
 * @p file_path is the value of the `file_path` field returned by the
 * `getFile` method; it is appended to the file URL prefix built from the
 * bot token. The payload is stored raw, without any null terminator.
 *
 * @param[in]  handle       Client handle to issue the request on.
 * @param[in]  file_path    Server side path of the file to retrieve.
 * @param[out] out_data     Buffer receiving the payload.
 * @param[in]  out_capacity Capacity of @p out_data in bytes.
 * @param[out] out_len      Receives the number of bytes stored. May be
 *                          NULL.
 *
 * @return
 *  - ESP_OK when the whole file was stored.
 *  - ESP_ERR_INVALID_ARG on NULL arguments or zero capacity.
 *  - ESP_ERR_INVALID_STATE if the transport has not been initialized.
 *  - ESP_ERR_INVALID_SIZE if the file is larger than @p out_capacity; the
 *    buffer then holds the leading bytes and @p out_len their count.
 *  - ESP_ERR_TIMEOUT if the handle mutex could not be taken.
 *  - ESP_FAIL if the server answered with a non-2xx status code.
 *  - Any error propagated by the `esp_http_client` read path otherwise.
 */
esp_err_t telegram_bot_client_download(telegram_bot_client_handle_t handle, const char *file_path, uint8_t *out_data, size_t out_capacity, size_t *out_len);

/**
 * @brief Query the bot identity through the `getMe` method.
 *
 * Convenience wrapper around @ref telegram_bot_client_call() using the
 * default handle, which is built the first time one of the convenience
 * helpers is called and released by @ref telegram_bot_deinit(). It is the
 * quickest way of checking that the token is valid and that the device can
 * reach the Telegram servers.
 *
 * @param[out] response_buffer Buffer receiving the raw JSON answer. May be
 *                             NULL to discard it.
 * @param[in]  buffer_size     Capacity of @p response_buffer in bytes.
 *
 * @return
 *  - ESP_OK if the request completed.
 *  - ESP_ERR_INVALID_STATE if the transport has not been initialized.
 *  - Any error propagated by @ref telegram_bot_client_call() otherwise.
 */
esp_err_t telegram_bot_get_me(char *response_buffer, size_t buffer_size);

/**
 * @brief Send a plain text message through the `sendMessage` method.
 *
 * Convenience wrapper that builds the JSON body, escapes both arguments
 * and posts it on the default handle, which is built the first time one of
 * the convenience helpers is called. The `telegram_service` component
 * offers richer variants supporting formatting, keyboards and replies.
 *
 * @param[in] chat_id Destination chat. Either a numeric identifier such as
 *                    "12345678" or a public username such as "@channel".
 * @param[in] text    Message body. Telegram limits it to 4096 characters.
 *
 * @return
 *  - ESP_OK if the request completed.
 *  - ESP_ERR_INVALID_ARG if @p chat_id or @p text is NULL.
 *  - ESP_ERR_INVALID_STATE if the transport has not been initialized.
 *  - ESP_ERR_NO_MEM if the JSON body cannot be allocated.
 *  - Any error propagated by @ref telegram_bot_client_call() otherwise.
 */
esp_err_t telegram_bot_send_message(const char *chat_id, const char *text);

/**
 * @brief Percent-encode a string for use as a URL query parameter value.
 *
 * Unreserved characters (letters, digits and `-_.~`) are copied verbatim,
 * every other byte is replaced by its `%XX` escape sequence. The result is
 * always null-terminated and is truncated on a character boundary when the
 * destination is too small.
 *
 * @param[in]  src      Null-terminated source string.
 * @param[out] dst      Destination buffer.
 * @param[in]  dst_size Capacity of @p dst in bytes, including the null
 *                      terminator.
 */
void telegram_bot_url_encode(const char *src, char *dst, size_t dst_size);

/**
 * @brief Escape a string so it can be embedded in a JSON string literal.
 *
 * Backslashes and double quotes are prefixed with a backslash, the usual
 * control characters are turned into their short escape sequences and any
 * remaining byte below 0x20 becomes a `\\u00XX` sequence. The result is
 * always null-terminated and is truncated on an escape boundary when the
 * destination is too small.
 *
 * @param[in]  src      Null-terminated source string.
 * @param[out] dst      Destination buffer.
 * @param[in]  dst_size Capacity of @p dst in bytes, including the null
 *                      terminator.
 */
void telegram_bot_json_escape(const char *src, char *dst, size_t dst_size);

#ifdef __cplusplus
}
#endif
