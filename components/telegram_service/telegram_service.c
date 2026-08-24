// @file telegram_service.c
//
// @author Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
// @date 2026
// @copyright MIT
// @see https://github.com/hiperiondev/esp32idf_telegram_bot
//
// @brief Implementation of the Telegram bot service for ESP-IDF applications.
//
// Implements the service declared in telegram_service.h on top of the
// `esp_telegram_bot` transport. All the state lives in this translation
// unit: the configuration copy, the authorization and group chat lists, the
// command, callback, parameter and sensor tables, the runtime counters and
// the polling offset, all protected by a recursive mutex because the
// built-in handlers call the public API of the service themselves.
//
// Two independent transport handles are used, one dedicated to the long poll
// and one to every outgoing message, so a transmission is never delayed by a
// poll that is waiting for its timeout to expire. They take turns holding a
// live TLS session rather than holding one each: the polling connection is
// released before any outgoing request and the transmit connection before
// the next poll, which is what lets a handshake find the contiguous heap it
// needs on a station that also runs a radio modem and a web server, while
// still letting every message of one batch share a single handshake. The
// service
// task performs one `getUpdates` call per cycle, decodes the answer with
// cJSON, applies the authorization rules, dispatches commands, inline
// button presses and generic events, and drains the alert queue between two
// cycles, which is what makes @ref telegram_send_alert() safe to call from
// contexts that must not block.
//
// The list of update kinds the poll asks for is built from the
// configuration: messages and button presses are always requested, and the
// two reaction kinds are added when the application enabled them, because
// Telegram never delivers those unless they are named explicitly.
//
// The built-in command set is implemented here as well: greetings, help,
// device status, telemetry, uptime, identifiers, counters, user list,
// remote configuration, alert control, reboot and the inline menu giving
// access to the most common actions without typing.

#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "cJSON.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "esp_telegram_bot.h"
#include "telegram_service.h"

// Length of the buffers used to build one outgoing report or reply.
#define TELEGRAM_REPORT_BUFFER_LEN 1024

// Length of the buffer used to build the sensor report. Larger than the
// general one because the report holds one line per registered sensor plus
// whatever the application's sensors callback contributes, which on a station
// that maps a full set of weather fields and telemetry channels is several
// dozen lines. The buffer lives only for the duration of one /sensors answer.
#define TELEGRAM_SENSOR_REPORT_BUFFER_LEN 2048

// Length of the buffer holding the answer of the small API calls whose
// result is only checked for success. The answer of a send method carries
// a copy of the delivered message and is far longer than this, so only its
// head is kept and the response is marked as expected to be truncated.
#define TELEGRAM_ACK_BUFFER_LEN 256

// Length of the buffer holding the answer of the getFile method.
#define TELEGRAM_FILE_INFO_BUFFER_LEN 512

// Length of a stored command or parameter description.
#define TELEGRAM_DESCRIPTION_LEN 64

// Length of a stored parameter or sensor name.
#define TELEGRAM_ENTRY_NAME_LEN 24

// Length of a stored measurement unit.
#define TELEGRAM_UNIT_LEN 12

// Maximum time a task waits for the mutex protecting the internal tables.
#define TELEGRAM_LOCK_TIMEOUT_MS 5000

// Delay applied between the reply confirming a reboot and the restart, so
// the confirmation has time to reach Telegram.
#define TELEGRAM_REBOOT_DELAY_MS 1500

// Pause applied after a failed polling cycle, to avoid hammering the API
// while the network is down.
#define TELEGRAM_POLL_BACKOFF_MS 5000

// Payload prefix reserved for the buttons of the built-in menu.
#define TELEGRAM_SYSTEM_CALLBACK_PREFIX "sys:"

// Polling cycles between stack high-water reports. At the default poll
// interval this is roughly one line a minute: enough to characterise the task
// over a day, little enough to leave the log readable.
#define TELEGRAM_STACK_REPORT_CYCLES 6

static const char *TAG = "telegram_service";

// One entry of the authorization list.
typedef struct {
    int64_t id;
    char name[TELEGRAM_NAME_LEN];
    bool is_admin;
    bool used;
} telegram_user_entry_t;

// One entry of the allowed group chat list.
typedef struct {
    int64_t id;
    char name[TELEGRAM_NAME_LEN];
    bool used;
} telegram_chat_entry_t;

// One entry of the command table.
typedef struct {
    char name[TELEGRAM_COMMAND_LEN];
    char description[TELEGRAM_DESCRIPTION_LEN];
    bool admin_only;
    telegram_command_cb_t handler;
    void *ctx;
    bool used;
} telegram_command_entry_t;

// One entry of the inline button routing table.
typedef struct {
    char prefix[TELEGRAM_CALLBACK_DATA_LEN];
    telegram_callback_cb_t handler;
    void *ctx;
    bool used;
} telegram_callback_entry_t;

// One entry of the remote configuration table.
typedef struct {
    char name[TELEGRAM_ENTRY_NAME_LEN];
    char description[TELEGRAM_DESCRIPTION_LEN];
    telegram_param_type_t type;
    void *value;
    size_t value_size;
    float min_value;
    float max_value;
    bool used;
} telegram_param_entry_t;

// One entry of the telemetry table.
typedef struct {
    char name[TELEGRAM_ENTRY_NAME_LEN];
    char unit[TELEGRAM_UNIT_LEN];
    telegram_sensor_read_cb_t read_cb;
    void *ctx;
    bool used;
} telegram_sensor_entry_t;

// Item travelling through the alert queue. The text is heap allocated by
// the producer and released by the service task once delivered.
typedef struct {
    telegram_alert_level_t level;
    char *text;
} telegram_alert_item_t;

// Configuration copy owned by the service. Strings the caller supplied are
// duplicated into the buffers below so the service does not depend on the
// lifetime of the caller structure.
static telegram_service_config_t s_config;
static char s_device_name[TELEGRAM_NAME_LEN] = "ESP32";

// Connection used by the service task to poll for updates.
static telegram_bot_client_handle_t s_poll_client = NULL;

// Connection used by every outgoing message, independent of the polling
// connection so a long poll never delays a transmission.
static telegram_bot_client_handle_t s_tx_client = NULL;

// Buffer holding one getUpdates answer, owned by the service task.
static char *s_rx_buffer = NULL;
static size_t s_rx_buffer_size = 0;

// Recursive mutex protecting every table and counter below, taken
// recursively because the built-in handlers call the public API.
static SemaphoreHandle_t s_lock = NULL;

// Queue carrying the alerts waiting for delivery.
static QueueHandle_t s_alert_queue = NULL;

static telegram_user_entry_t s_users[TELEGRAM_MAX_USERS];
static telegram_chat_entry_t s_chats[TELEGRAM_MAX_CHATS];
static telegram_command_entry_t s_commands[TELEGRAM_MAX_COMMANDS];
static telegram_callback_entry_t s_callbacks[TELEGRAM_MAX_CALLBACKS];
static telegram_param_entry_t s_params[TELEGRAM_MAX_PARAMS];
static telegram_sensor_entry_t s_sensors[TELEGRAM_MAX_SENSORS];

static telegram_event_cb_t s_event_handler = NULL;
static void *s_event_ctx = NULL;

// Identifier of the first update the next poll asks for.
static int64_t s_next_offset = 0;

static bool s_initialized = false;
static bool s_running = false;
static bool s_stop_requested = false;
static bool s_alerts_enabled = true;
static int64_t s_start_time_us = 0;
static TaskHandle_t s_task_handle = NULL;
static telegram_stats_t s_stats;

static esp_err_t telegram_register_builtin_commands(void);
static esp_err_t telegram_builtin_menu_callback(const telegram_update_t *update, const char *data, void *ctx);

// Takes the table mutex, bounded so a caller can never block forever.
static bool telegram_lock(void) {
    if (s_lock == NULL) {
        return false;
    }
    return xSemaphoreTakeRecursive(s_lock, pdMS_TO_TICKS(TELEGRAM_LOCK_TIMEOUT_MS)) == pdTRUE;
}

static void telegram_unlock(void) {
    xSemaphoreGiveRecursive(s_lock);
}

// Copies src into a fixed size destination buffer, always null terminating
// and silently truncating what does not fit.
static void telegram_copy_string(char *dst, size_t dst_size, const char *src) {
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    size_t len = strlen(src);
    if (len >= dst_size) {
        len = dst_size - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

// Appends a formatted fragment to a text buffer and returns the new
// offset, never letting it run past the capacity of the buffer.
static size_t telegram_append(char *buffer, size_t size, size_t offset, const char *format, ...) {
    if (buffer == NULL || size == 0 || offset >= size - 1) {
        return offset;
    }
    va_list args;
    va_start(args, format);
    int written = vsnprintf(buffer + offset, size - offset, format, args);
    va_end(args);
    if (written < 0) {
        return offset;
    }
    offset += (size_t)written;
    if (offset > size - 1) {
        offset = size - 1;
    }
    return offset;
}

// Normalises a command name: drops a leading slash, cuts the "@botname"
// suffix Telegram appends inside groups and lowercases the result.
static void telegram_normalize_command(const char *src, char *dst, size_t dst_size) {
    if (dst == NULL || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    if (src == NULL) {
        return;
    }
    if (*src == '/') {
        src++;
    }
    size_t out = 0;
    while (src[out] != '\0' && src[out] != '@' && src[out] != ' ' && out + 1 < dst_size) {
        char c = src[out];
        dst[out] = (char)((c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c);
        out++;
    }
    dst[out] = '\0';
}

// Renders a numeric chat identifier as the text form expected by the
// sending functions.
static void telegram_format_chat_id(int64_t chat_id, char *dst, size_t dst_size) {
    snprintf(dst, dst_size, "%" PRId64, chat_id);
}

// Locates a user entry by identifier. The caller must hold the mutex.
static telegram_user_entry_t *telegram_find_user(int64_t user_id) {
    for (size_t i = 0; i < TELEGRAM_MAX_USERS; i++) {
        if (s_users[i].used && s_users[i].id == user_id) {
            return &s_users[i];
        }
    }
    return NULL;
}

// Reports whether the authorization list is empty and no administrator was
// configured, which is the bring-up situation where every sender is let
// through so the operator can discover their own identifier.
static bool telegram_access_list_empty(void) {
    for (size_t i = 0; i < TELEGRAM_MAX_USERS; i++) {
        if (s_users[i].used) {
            return false;
        }
    }
    return true;
}

esp_err_t telegram_add_user(int64_t user_id, const char *name, bool is_admin) {
    if (user_id == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || !telegram_lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_ERR_NO_MEM;
    telegram_user_entry_t *entry = telegram_find_user(user_id);
    if (entry == NULL) {
        for (size_t i = 0; i < TELEGRAM_MAX_USERS; i++) {
            if (!s_users[i].used) {
                entry = &s_users[i];
                entry->used = true;
                entry->id = user_id;
                entry->name[0] = '\0';
                break;
            }
        }
    }
    if (entry != NULL) {
        if (name != NULL && name[0] != '\0') {
            telegram_copy_string(entry->name, sizeof(entry->name), name);
        }
        entry->is_admin = is_admin;
        err = ESP_OK;
    }

    telegram_unlock();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "User %" PRId64 " authorized%s", user_id, is_admin ? " as administrator" : "");
    } else {
        ESP_LOGE(TAG, "Authorization list full, user %" PRId64 " rejected", user_id);
    }
    return err;
}

esp_err_t telegram_remove_user(int64_t user_id) {
    if (!s_initialized || !telegram_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_ERR_NOT_FOUND;
    telegram_user_entry_t *entry = telegram_find_user(user_id);
    if (entry != NULL) {
        memset(entry, 0, sizeof(*entry));
        err = ESP_OK;
    }
    telegram_unlock();
    return err;
}

bool telegram_is_authorized(int64_t user_id) {
    if (!s_initialized) {
        return false;
    }
    if (s_config.open_access) {
        return true;
    }
    if (!telegram_lock()) {
        return false;
    }
    bool allowed = telegram_access_list_empty() || (telegram_find_user(user_id) != NULL);
    telegram_unlock();
    return allowed;
}

bool telegram_is_admin(int64_t user_id) {
    if (!s_initialized || !telegram_lock()) {
        return false;
    }
    telegram_user_entry_t *entry = telegram_find_user(user_id);
    bool admin = (entry != NULL && entry->is_admin);
    if (!admin && s_config.open_access) {
        admin = true;
    }
    telegram_unlock();
    return admin;
}

esp_err_t telegram_allow_chat(int64_t chat_id, const char *name) {
    if (chat_id == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || !telegram_lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_ERR_NO_MEM;
    for (size_t i = 0; i < TELEGRAM_MAX_CHATS; i++) {
        if (s_chats[i].used && s_chats[i].id == chat_id) {
            telegram_copy_string(s_chats[i].name, sizeof(s_chats[i].name), name);
            err = ESP_OK;
            break;
        }
    }
    if (err != ESP_OK) {
        for (size_t i = 0; i < TELEGRAM_MAX_CHATS; i++) {
            if (!s_chats[i].used) {
                s_chats[i].used = true;
                s_chats[i].id = chat_id;
                telegram_copy_string(s_chats[i].name, sizeof(s_chats[i].name), name);
                err = ESP_OK;
                break;
            }
        }
    }

    telegram_unlock();
    return err;
}

bool telegram_is_chat_allowed(int64_t chat_id) {
    if (!s_initialized) {
        return false;
    }
    if (s_config.open_access) {
        return true;
    }
    if (!telegram_lock()) {
        return false;
    }
    bool allowed = false;
    for (size_t i = 0; i < TELEGRAM_MAX_CHATS; i++) {
        if (s_chats[i].used && s_chats[i].id == chat_id) {
            allowed = true;
            break;
        }
    }
    telegram_unlock();
    return allowed;
}

size_t telegram_user_count(void) {
    if (!s_initialized || !telegram_lock()) {
        return 0;
    }
    size_t count = 0;
    for (size_t i = 0; i < TELEGRAM_MAX_USERS; i++) {
        if (s_users[i].used) {
            count++;
        }
    }
    telegram_unlock();
    return count;
}

esp_err_t telegram_init(const telegram_service_config_t *config) {
    if (config == NULL || config->bot_token == NULL || config->bot_token[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(s_users, 0, sizeof(s_users));
    memset(s_chats, 0, sizeof(s_chats));
    memset(s_commands, 0, sizeof(s_commands));
    memset(s_callbacks, 0, sizeof(s_callbacks));
    memset(s_params, 0, sizeof(s_params));
    memset(s_sensors, 0, sizeof(s_sensors));
    memset(&s_stats, 0, sizeof(s_stats));
    s_event_handler = NULL;
    s_event_ctx = NULL;
    s_next_offset = 0;
    s_stop_requested = false;
    s_running = false;
    s_alerts_enabled = true;

    s_config = *config;
    telegram_copy_string(s_device_name, sizeof(s_device_name), (config->device_name != NULL) ? config->device_name : "ESP32");
    s_config.device_name = s_device_name;
    if (s_config.poll_timeout_s <= 0) {
        s_config.poll_timeout_s = 10;
    }
    if (s_config.poll_interval_ms < 0) {
        s_config.poll_interval_ms = 0;
    }
    if (s_config.rx_buffer_size <= 0) {
        s_config.rx_buffer_size = CONFIG_TELEGRAM_SERVICE_RX_BUFFER_SIZE;
    }
    if (s_config.task_stack_size <= 0) {
        s_config.task_stack_size = CONFIG_TELEGRAM_SERVICE_TASK_STACK_SIZE;
    }
    if (s_config.task_priority <= 0) {
        s_config.task_priority = 5;
    }
    if (s_config.alert_queue_len <= 0) {
        s_config.alert_queue_len = 8;
    }

    s_lock = xSemaphoreCreateRecursiveMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_alert_queue = xQueueCreate((UBaseType_t)s_config.alert_queue_len, sizeof(telegram_alert_item_t));
    if (s_alert_queue == NULL) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_rx_buffer_size = (size_t)s_config.rx_buffer_size;
    s_rx_buffer = malloc(s_rx_buffer_size);
    if (s_rx_buffer == NULL) {
        vQueueDelete(s_alert_queue);
        s_alert_queue = NULL;
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = telegram_bot_init(config->bot_token);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Transport initialization failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    // The polling connection waits for the long poll to expire, so its
    // socket timeout must outlive the timeout requested to the servers.
    telegram_bot_client_config_t poll_config = {
        .timeout_ms = (s_config.poll_timeout_s + 10) * 1000,
        // Sized for the smallest device this service targets: a smaller
        // receive buffer costs more read iterations to drain one answer and
        // buys back heap that a TLS handshake needs in one contiguous piece.
        .rx_buffer_size = 1024,
    };
    err = telegram_bot_client_create(&poll_config, &s_poll_client);
    if (err != ESP_OK) {
        goto cleanup;
    }

    // Keep-alive is on, but the session it preserves is short lived by
    // construction: it is opened by the first transmission of a batch and
    // released again before the next poll. What it saves is the handshake in
    // front of every message after the first, and a batch of button presses
    // is precisely where that matters - a menu press answers the query, then
    // sends a report, and Telegram invalidates a callback query in seconds.
    // Paying a handshake per message inside one batch is what pushes the last
    // presses of a burst past that deadline.
    telegram_bot_client_config_t tx_config = {
        .timeout_ms = 15000,
        // The transmit path only ever reads back a short confirmation, so this
        // is smaller still than the polling client's.
        .rx_buffer_size = 512,
    };
    err = telegram_bot_client_create(&tx_config, &s_tx_client);
    if (err != ESP_OK) {
        goto cleanup;
    }

    s_initialized = true;

    if (config->admin_id != 0) {
        telegram_add_user(config->admin_id, "administrator", true);
    }
    if (s_config.register_builtin_commands) {
        err = telegram_register_builtin_commands();
        if (err != ESP_OK) {
            s_initialized = false;
            goto cleanup;
        }
        telegram_register_callback(TELEGRAM_SYSTEM_CALLBACK_PREFIX, telegram_builtin_menu_callback, NULL);
    }

    ESP_LOGI(TAG, "Service initialized for device \"%s\"", s_device_name);
    return ESP_OK;

cleanup:
    if (s_tx_client != NULL) {
        telegram_bot_client_destroy(s_tx_client);
        s_tx_client = NULL;
    }
    if (s_poll_client != NULL) {
        telegram_bot_client_destroy(s_poll_client);
        s_poll_client = NULL;
    }
    telegram_bot_deinit();
    free(s_rx_buffer);
    s_rx_buffer = NULL;
    vQueueDelete(s_alert_queue);
    s_alert_queue = NULL;
    vSemaphoreDelete(s_lock);
    s_lock = NULL;
    return err;
}

esp_err_t telegram_deinit(void) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    telegram_stop();

    telegram_alert_item_t item;
    while (xQueueReceive(s_alert_queue, &item, 0) == pdTRUE) {
        free(item.text);
    }

    if (s_tx_client != NULL) {
        telegram_bot_client_destroy(s_tx_client);
        s_tx_client = NULL;
    }
    if (s_poll_client != NULL) {
        telegram_bot_client_destroy(s_poll_client);
        s_poll_client = NULL;
    }
    telegram_bot_deinit();

    free(s_rx_buffer);
    s_rx_buffer = NULL;
    s_rx_buffer_size = 0;

    vQueueDelete(s_alert_queue);
    s_alert_queue = NULL;

    s_initialized = false;

    vSemaphoreDelete(s_lock);
    s_lock = NULL;

    ESP_LOGI(TAG, "Service released");
    return ESP_OK;
}

bool telegram_is_running(void) {
    return s_running;
}

esp_err_t telegram_get_stats(telegram_stats_t *out_stats) {
    if (out_stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || !telegram_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    *out_stats = s_stats;
    out_stats->uptime_seconds = (s_start_time_us > 0) ? (esp_timer_get_time() - s_start_time_us) / 1000000 : 0;
    telegram_unlock();
    return ESP_OK;
}

// Hands the polling connection back to the heap before an outgoing request
// opens one of its own.
//
// A fresh handshake needs its record buffers in single contiguous blocks,
// several kilobytes on this heap, and an open polling session holds a
// comparable amount for as long as it is kept. Both at once is what the
// transmit path cannot find on a station that also runs the radio modem, the
// Wi-Fi stack and the web server, and a reply is requested at the worst
// possible instant: right after a batch of updates arrived, with the payload
// and the decoded tree still in memory. Releasing the poll costs one extra
// handshake on the next cycle and buys the transmit path a heap in the same
// shape it had when the service started.
//
// A handle busy with a request is left alone, which is the case when an
// application task sends a message while the service task sits in its long
// poll. That poll owns a session that is genuinely in use, and the transmit
// path takes its chances against it.
static void telegram_release_poll_connection(void) {
    if (s_poll_client != NULL) {
        telegram_bot_client_disconnect(s_poll_client);
    }
}

// The mirror image, applied before the poll reopens its own connection.
//
// The transmit handle keeps its session alive so the messages of one batch
// share a single handshake, which means something is left to release once the
// batch is done. Releasing it here rather than after each message is what
// gives the two handles their turn: the poll owns the connection while it
// waits for updates, the transmit path owns it while it answers them, and the
// pair of them still only ever hold one live TLS session between them.
static void telegram_release_tx_connection(void) {
    if (s_tx_client != NULL) {
        telegram_bot_client_disconnect(s_tx_client);
    }
}

// Issues an API method with a JSON body built by the caller and checks
// that Telegram accepted it. The cJSON object is released here, whatever
// the outcome.
static esp_err_t telegram_api_call(const char *method, cJSON *root) {
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (!s_initialized || s_tx_client == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_STATE;
    }

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // Telegram echoes the whole message it just delivered, which does not
    // fit in the buffer and does not need to: only the acknowledgement at
    // the head of the answer is read.
    char ack[TELEGRAM_ACK_BUFFER_LEN];
    telegram_bot_request_t request = {
        .method = method,
        .json_body = body,
    };
    telegram_bot_response_t response = {
        .buffer = ack,
        .buffer_size = sizeof(ack),
        .truncation_expected = true,
    };

    telegram_release_poll_connection();

    esp_err_t err = telegram_bot_client_call(s_tx_client, &request, &response);
    cJSON_free(body);

    if (err != ESP_OK) {
        return err;
    }
    if (response.status_code != 200 || strstr(ack, "\"ok\":true") == NULL) {
        ESP_LOGE(TAG, "%s rejected with status %d: %s", method, response.status_code, ack);
        return ESP_FAIL;
    }

    if (telegram_lock()) {
        s_stats.messages_sent++;
        telegram_unlock();
    }
    return ESP_OK;
}

// Adds the parse_mode field matching the requested formatting, if any.
static void telegram_add_parse_mode(cJSON *root, telegram_parse_mode_t mode) {
    switch (mode) {
        case TELEGRAM_PARSE_MARKDOWN:
            cJSON_AddStringToObject(root, "parse_mode", "MarkdownV2");
            break;
        case TELEGRAM_PARSE_HTML:
            cJSON_AddStringToObject(root, "parse_mode", "HTML");
            break;
        default:
            break;
    }
}

esp_err_t telegram_send_message_ex(const char *chat_id, const char *text, const telegram_send_options_t *options) {
    if (chat_id == NULL || text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "chat_id", chat_id);
    cJSON_AddStringToObject(root, "text", text);

    if (options != NULL) {
        telegram_add_parse_mode(root, options->parse_mode);
        if (options->reply_to_message_id != 0) {
            cJSON_AddNumberToObject(root, "reply_to_message_id", options->reply_to_message_id);
        }
        if (options->disable_notification) {
            cJSON_AddBoolToObject(root, "disable_notification", true);
        }
        if (options->reply_markup != NULL) {
            cJSON_AddItemToObject(root, "reply_markup", cJSON_CreateRaw(options->reply_markup));
        }
    }

    return telegram_api_call("sendMessage", root);
}

esp_err_t telegram_send_message(const char *chat_id, const char *text) {
    return telegram_send_message_ex(chat_id, text, NULL);
}

esp_err_t telegram_send_message_fmt(const char *chat_id, const char *format, ...) {
    if (chat_id == NULL || format == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    char *text = malloc(TELEGRAM_REPORT_BUFFER_LEN);
    if (text == NULL) {
        return ESP_ERR_NO_MEM;
    }
    va_list args;
    va_start(args, format);
    vsnprintf(text, TELEGRAM_REPORT_BUFFER_LEN, format, args);
    va_end(args);

    esp_err_t err = telegram_send_message(chat_id, text);
    free(text);
    return err;
}

esp_err_t telegram_edit_message(const char *chat_id, int32_t message_id, const char *text, const char *reply_markup) {
    if (chat_id == NULL || text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "chat_id", chat_id);
    cJSON_AddNumberToObject(root, "message_id", message_id);
    cJSON_AddStringToObject(root, "text", text);
    if (reply_markup != NULL) {
        cJSON_AddItemToObject(root, "reply_markup", cJSON_CreateRaw(reply_markup));
    }
    return telegram_api_call("editMessageText", root);
}

esp_err_t telegram_send_typing(const char *chat_id) {
    if (chat_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "chat_id", chat_id);
    cJSON_AddStringToObject(root, "action", "typing");
    return telegram_api_call("sendChatAction", root);
}

esp_err_t telegram_broadcast(const char *text) {
    if (text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || !telegram_lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    // The recipient list is copied out so the messages are sent without
    // holding the mutex.
    int64_t recipients[TELEGRAM_MAX_USERS];
    size_t count = 0;
    for (size_t i = 0; i < TELEGRAM_MAX_USERS; i++) {
        if (s_users[i].used) {
            recipients[count++] = s_users[i].id;
        }
    }
    telegram_unlock();

    if (count == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t result = ESP_FAIL;
    for (size_t i = 0; i < count; i++) {
        char chat_id[TELEGRAM_CHAT_ID_LEN];
        telegram_format_chat_id(recipients[i], chat_id, sizeof(chat_id));
        esp_err_t err = telegram_send_message(chat_id, text);
        if (err == ESP_OK) {
            result = ESP_OK;
        } else {
            ESP_LOGW(TAG, "Broadcast to %s failed: %s", chat_id, esp_err_to_name(err));
        }
    }
    return result;
}

// Delivers an alert to the users and, for the highest severities, to the
// allowed group chats. Runs in the service task.
static void telegram_deliver_alert(const telegram_alert_item_t *item) {
    telegram_broadcast(item->text);

    if (item->level >= TELEGRAM_ALERT_ALARM && telegram_lock()) {
        int64_t chats[TELEGRAM_MAX_CHATS];
        size_t count = 0;
        for (size_t i = 0; i < TELEGRAM_MAX_CHATS; i++) {
            if (s_chats[i].used) {
                chats[count++] = s_chats[i].id;
            }
        }
        telegram_unlock();
        for (size_t i = 0; i < count; i++) {
            char chat_id[TELEGRAM_CHAT_ID_LEN];
            telegram_format_chat_id(chats[i], chat_id, sizeof(chat_id));
            telegram_send_message(chat_id, item->text);
        }
    }

    if (telegram_lock()) {
        s_stats.alerts_sent++;
        telegram_unlock();
    }
}

// Marker prepended to an alert text according to its severity.
static const char *telegram_alert_prefix(telegram_alert_level_t level) {
    switch (level) {
        case TELEGRAM_ALERT_WARNING:
            return "[WARNING]";
        case TELEGRAM_ALERT_ALARM:
            return "[ALARM]";
        case TELEGRAM_ALERT_CRITICAL:
            return "[CRITICAL]";
        default:
            return "[INFO]";
    }
}

esp_err_t telegram_send_alert(telegram_alert_level_t level, const char *format, ...) {
    if (format == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || s_alert_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    char *text = malloc(TELEGRAM_REPORT_BUFFER_LEN);
    if (text == NULL) {
        return ESP_ERR_NO_MEM;
    }

    int written = snprintf(text, TELEGRAM_REPORT_BUFFER_LEN, "%s %s\n", telegram_alert_prefix(level), s_device_name);
    if (written < 0 || written >= TELEGRAM_REPORT_BUFFER_LEN) {
        written = 0;
    }

    va_list args;
    va_start(args, format);
    vsnprintf(text + written, (size_t)(TELEGRAM_REPORT_BUFFER_LEN - written), format, args);
    va_end(args);

    telegram_alert_item_t item = {
        .level = level,
        .text = text,
    };
    if (xQueueSend(s_alert_queue, &item, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Alert queue full, dropping: %s", text);
        free(text);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void telegram_set_alerts_enabled(bool enabled) {
    s_alerts_enabled = enabled;
}

bool telegram_alerts_enabled(void) {
    return s_alerts_enabled;
}

esp_err_t telegram_send_location(const char *chat_id, double latitude, double longitude) {
    if (chat_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "chat_id", chat_id);
    cJSON_AddNumberToObject(root, "latitude", latitude);
    cJSON_AddNumberToObject(root, "longitude", longitude);
    return telegram_api_call("sendLocation", root);
}

esp_err_t telegram_send_photo_url(const char *chat_id, const char *caption, const char *url_or_file_id) {
    if (chat_id == NULL || url_or_file_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "chat_id", chat_id);
    cJSON_AddStringToObject(root, "photo", url_or_file_id);
    if (caption != NULL) {
        cJSON_AddStringToObject(root, "caption", caption);
    }
    return telegram_api_call("sendPhoto", root);
}

// Uploads a binary payload as the given form field and checks the answer.
static esp_err_t telegram_upload(const char *method, const char *chat_id, const char *caption, const char *field, const char *file_name, const char *mime_type,
                                 const uint8_t *data, size_t len) {
    if (chat_id == NULL || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || s_tx_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    telegram_bot_form_field_t fields[2];
    size_t field_count = 0;
    fields[field_count].name = "chat_id";
    fields[field_count].value = chat_id;
    field_count++;
    if (caption != NULL && caption[0] != '\0') {
        fields[field_count].name = "caption";
        fields[field_count].value = caption;
        field_count++;
    }

    // Only the acknowledgement is read here too; the description of the
    // uploaded file that follows it is of no use to the service.
    char ack[TELEGRAM_ACK_BUFFER_LEN];
    telegram_bot_response_t response = {
        .buffer = ack,
        .buffer_size = sizeof(ack),
        .truncation_expected = true,
    };

    telegram_release_poll_connection();

    esp_err_t err = telegram_bot_client_call_multipart(s_tx_client, method, fields, field_count, field, file_name, mime_type, data, len, &response);
    if (err != ESP_OK) {
        return err;
    }
    if (response.status_code != 200 || strstr(ack, "\"ok\":true") == NULL) {
        ESP_LOGE(TAG, "%s rejected with status %d: %s", method, response.status_code, ack);
        return ESP_FAIL;
    }

    if (telegram_lock()) {
        s_stats.messages_sent++;
        telegram_unlock();
    }
    ESP_LOGI(TAG, "%s delivered, %d bytes", method, (int)len);
    return ESP_OK;
}

esp_err_t telegram_send_photo(const char *chat_id, const char *caption, const uint8_t *data, size_t len, const char *file_name) {
    return telegram_upload("sendPhoto", chat_id, caption, "photo", (file_name != NULL) ? file_name : "photo.jpg", "image/jpeg", data, len);
}

esp_err_t telegram_send_document(const char *chat_id, const char *caption, const char *file_name, const char *mime_type, const uint8_t *data, size_t len) {
    return telegram_upload("sendDocument", chat_id, caption, "document", (file_name != NULL) ? file_name : "file.bin",
                           (mime_type != NULL) ? mime_type : "application/octet-stream", data, len);
}

esp_err_t telegram_download_file(const char *file_id, uint8_t *out_data, size_t out_capacity, size_t *out_len) {
    if (file_id == NULL || out_data == NULL || out_capacity == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || s_tx_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // The identifier is first resolved into a server side path with the
    // getFile method, which is what the file endpoint expects.
    char query[TELEGRAM_FILE_ID_LEN * 3 + 16];
    char encoded[TELEGRAM_FILE_ID_LEN * 3];
    telegram_bot_url_encode(file_id, encoded, sizeof(encoded));
    snprintf(query, sizeof(query), "file_id=%s", encoded);

    char *info = malloc(TELEGRAM_FILE_INFO_BUFFER_LEN);
    if (info == NULL) {
        return ESP_ERR_NO_MEM;
    }

    telegram_bot_request_t request = {
        .method = "getFile",
        .query = query,
    };
    telegram_bot_response_t response = {
        .buffer = info,
        .buffer_size = TELEGRAM_FILE_INFO_BUFFER_LEN,
    };

    telegram_release_poll_connection();

    esp_err_t err = telegram_bot_client_call(s_tx_client, &request, &response);
    if (err != ESP_OK) {
        free(info);
        return err;
    }

    char path[192] = "";
    cJSON *root = cJSON_Parse(info);
    if (root != NULL) {
        cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
        cJSON *file_path = cJSON_GetObjectItemCaseSensitive(result, "file_path");
        if (cJSON_IsString(file_path) && file_path->valuestring != NULL) {
            telegram_copy_string(path, sizeof(path), file_path->valuestring);
        }
        cJSON_Delete(root);
    }
    free(info);

    if (path[0] == '\0') {
        ESP_LOGE(TAG, "No path returned for file %s", file_id);
        return ESP_ERR_NOT_FOUND;
    }

    err = telegram_bot_client_download(s_tx_client, path, out_data, out_capacity, out_len);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Downloaded %s, %d bytes", path, (out_len != NULL) ? (int)*out_len : 0);
    }
    return err;
}

// Sends a text message with an already built keyboard description. The
// markup object is consumed here.
static esp_err_t telegram_send_with_markup(const char *chat_id, const char *text, cJSON *markup) {
    if (chat_id == NULL || text == NULL || markup == NULL) {
        cJSON_Delete(markup);
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        cJSON_Delete(markup);
        return ESP_ERR_INVALID_STATE;
    }
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        cJSON_Delete(markup);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "chat_id", chat_id);
    cJSON_AddStringToObject(root, "text", text);
    cJSON_AddItemToObject(root, "reply_markup", markup);
    return telegram_api_call("sendMessage", root);
}

// Builds the inline_keyboard description of a button grid.
static cJSON *telegram_build_inline_keyboard(const telegram_button_t *buttons, size_t button_count, int columns) {
    if (columns < 1) {
        columns = 1;
    }
    cJSON *markup = cJSON_CreateObject();
    cJSON *keyboard = cJSON_CreateArray();
    if (markup == NULL || keyboard == NULL) {
        cJSON_Delete(markup);
        cJSON_Delete(keyboard);
        return NULL;
    }
    cJSON_AddItemToObject(markup, "inline_keyboard", keyboard);

    cJSON *row = NULL;
    size_t placed = 0;
    for (size_t i = 0; i < button_count; i++) {
        if (buttons[i].label == NULL) {
            continue;
        }
        // The rows are filled according to the number of buttons actually
        // placed, so an entry skipped for lack of a label does not leave a
        // hole in the grid.
        if ((placed % (size_t)columns) == 0 || row == NULL) {
            row = cJSON_CreateArray();
            if (row == NULL) {
                cJSON_Delete(markup);
                return NULL;
            }
            cJSON_AddItemToArray(keyboard, row);
        }
        cJSON *button = cJSON_CreateObject();
        if (button == NULL) {
            cJSON_Delete(markup);
            return NULL;
        }
        cJSON_AddStringToObject(button, "text", buttons[i].label);
        if (buttons[i].data != NULL) {
            cJSON_AddStringToObject(button, "callback_data", buttons[i].data);
        } else if (buttons[i].web_app_url != NULL) {
            cJSON *web_app = cJSON_CreateObject();
            cJSON_AddStringToObject(web_app, "url", buttons[i].web_app_url);
            cJSON_AddItemToObject(button, "web_app", web_app);
        } else if (buttons[i].url != NULL) {
            cJSON_AddStringToObject(button, "url", buttons[i].url);
        } else {
            // A button carrying no action would be refused by Telegram, so
            // it is given a payload derived from its own label.
            cJSON_AddStringToObject(button, "callback_data", buttons[i].label);
        }
        cJSON_AddItemToArray(row, button);
        placed++;
    }
    return markup;
}

esp_err_t telegram_send_menu(const char *chat_id, const char *text, const telegram_button_t *buttons, size_t button_count, int columns) {
    if (chat_id == NULL || text == NULL || buttons == NULL || button_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    cJSON *markup = telegram_build_inline_keyboard(buttons, button_count, columns);
    if (markup == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return telegram_send_with_markup(chat_id, text, markup);
}

esp_err_t telegram_send_keyboard(const char *chat_id, const char *text, const char *const *labels, size_t label_count, int columns, bool one_time) {
    if (chat_id == NULL || text == NULL || labels == NULL || label_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (columns < 1) {
        columns = 1;
    }

    cJSON *markup = cJSON_CreateObject();
    cJSON *keyboard = cJSON_CreateArray();
    if (markup == NULL || keyboard == NULL) {
        cJSON_Delete(markup);
        cJSON_Delete(keyboard);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddItemToObject(markup, "keyboard", keyboard);
    cJSON_AddBoolToObject(markup, "resize_keyboard", true);
    cJSON_AddBoolToObject(markup, "one_time_keyboard", one_time);

    cJSON *row = NULL;
    size_t placed = 0;
    for (size_t i = 0; i < label_count; i++) {
        if (labels[i] == NULL) {
            continue;
        }
        // The rows follow the number of labels actually placed, so a NULL
        // entry does not leave a hole in the grid.
        if ((placed % (size_t)columns) == 0 || row == NULL) {
            row = cJSON_CreateArray();
            if (row == NULL) {
                cJSON_Delete(markup);
                return ESP_ERR_NO_MEM;
            }
            cJSON_AddItemToArray(keyboard, row);
        }
        cJSON *button = cJSON_CreateObject();
        if (button == NULL) {
            cJSON_Delete(markup);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(button, "text", labels[i]);
        cJSON_AddItemToArray(row, button);
        placed++;
    }

    return telegram_send_with_markup(chat_id, text, markup);
}

esp_err_t telegram_remove_keyboard(const char *chat_id, const char *text) {
    if (chat_id == NULL || text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    cJSON *markup = cJSON_CreateObject();
    if (markup == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddBoolToObject(markup, "remove_keyboard", true);
    return telegram_send_with_markup(chat_id, text, markup);
}

esp_err_t telegram_send_web_app(const char *chat_id, const char *text, const char *button_label, const char *web_app_url) {
    if (chat_id == NULL || text == NULL || button_label == NULL || web_app_url == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // The button is placed on the reply keyboard rather than under the
    // message: Telegram only lets a Mini App opened from a reply keyboard
    // push data back with sendData(), which is what turns the page into a
    // control panel instead of a read-only view.
    cJSON *markup = cJSON_CreateObject();
    cJSON *keyboard = cJSON_CreateArray();
    cJSON *row = cJSON_CreateArray();
    cJSON *button = cJSON_CreateObject();
    cJSON *web_app = cJSON_CreateObject();
    if (markup == NULL || keyboard == NULL || row == NULL || button == NULL || web_app == NULL) {
        cJSON_Delete(markup);
        cJSON_Delete(keyboard);
        cJSON_Delete(row);
        cJSON_Delete(button);
        cJSON_Delete(web_app);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(web_app, "url", web_app_url);
    cJSON_AddStringToObject(button, "text", button_label);
    cJSON_AddItemToObject(button, "web_app", web_app);
    cJSON_AddItemToArray(row, button);
    cJSON_AddItemToArray(keyboard, row);
    cJSON_AddItemToObject(markup, "keyboard", keyboard);
    cJSON_AddBoolToObject(markup, "resize_keyboard", true);
    cJSON_AddBoolToObject(markup, "is_persistent", true);

    return telegram_send_with_markup(chat_id, text, markup);
}

esp_err_t telegram_answer_callback(const char *callback_id, const char *text, bool show_alert) {
    if (callback_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "callback_query_id", callback_id);
    if (text != NULL && text[0] != '\0') {
        cJSON_AddStringToObject(root, "text", text);
    }
    if (show_alert) {
        cJSON_AddBoolToObject(root, "show_alert", true);
    }

    return telegram_api_call("answerCallbackQuery", root);
}

esp_err_t telegram_set_message_reaction(const char *chat_id, int32_t message_id, const char *emoji, bool is_big) {
    if (chat_id == NULL || message_id == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "chat_id", chat_id);
    cJSON_AddNumberToObject(root, "message_id", (double)message_id);

    // An empty array is what withdraws the reaction the bot holds; the
    // field is mandatory either way, so it is always emitted.
    cJSON *list = cJSON_AddArrayToObject(root, "reaction");
    if (list == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    if (emoji != NULL && emoji[0] != '\0') {
        cJSON *entry = cJSON_CreateObject();
        if (entry == NULL) {
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(entry, "type", "emoji");
        cJSON_AddStringToObject(entry, "emoji", emoji);
        cJSON_AddItemToArray(list, entry);
    }
    if (is_big) {
        cJSON_AddBoolToObject(root, "is_big", true);
    }

    return telegram_api_call("setMessageReaction", root);
}

esp_err_t telegram_register_command(const char *name, const char *description, bool admin_only, telegram_command_cb_t handler, void *ctx) {
    if (name == NULL || handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    char normalized[TELEGRAM_COMMAND_LEN];
    telegram_normalize_command(name, normalized, sizeof(normalized));
    if (normalized[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || !telegram_lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    telegram_command_entry_t *entry = NULL;
    for (size_t i = 0; i < TELEGRAM_MAX_COMMANDS; i++) {
        if (s_commands[i].used && strcmp(s_commands[i].name, normalized) == 0) {
            entry = &s_commands[i];
            break;
        }
    }
    if (entry == NULL) {
        for (size_t i = 0; i < TELEGRAM_MAX_COMMANDS; i++) {
            if (!s_commands[i].used) {
                entry = &s_commands[i];
                entry->used = true;
                break;
            }
        }
    }

    esp_err_t err = ESP_ERR_NO_MEM;
    if (entry != NULL) {
        telegram_copy_string(entry->name, sizeof(entry->name), normalized);
        telegram_copy_string(entry->description, sizeof(entry->description), description);
        entry->admin_only = admin_only;
        entry->handler = handler;
        entry->ctx = ctx;
        err = ESP_OK;
    } else {
        ESP_LOGE(TAG, "Command table full, /%s not registered", normalized);
    }

    telegram_unlock();
    return err;
}

esp_err_t telegram_register_callback(const char *prefix, telegram_callback_cb_t handler, void *ctx) {
    if (prefix == NULL || handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || !telegram_lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_ERR_NO_MEM;
    for (size_t i = 0; i < TELEGRAM_MAX_CALLBACKS; i++) {
        if (!s_callbacks[i].used) {
            s_callbacks[i].used = true;
            telegram_copy_string(s_callbacks[i].prefix, sizeof(s_callbacks[i].prefix), prefix);
            s_callbacks[i].handler = handler;
            s_callbacks[i].ctx = ctx;
            err = ESP_OK;
            break;
        }
    }

    telegram_unlock();
    return err;
}

esp_err_t telegram_register_event_handler(telegram_event_cb_t handler, void *ctx) {
    if (!s_initialized || !telegram_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    s_event_handler = handler;
    s_event_ctx = ctx;
    telegram_unlock();
    return ESP_OK;
}

esp_err_t telegram_register_sensor(const char *name, const char *unit, telegram_sensor_read_cb_t read_cb, void *ctx) {
    if (name == NULL || read_cb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || !telegram_lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_ERR_NO_MEM;
    for (size_t i = 0; i < TELEGRAM_MAX_SENSORS; i++) {
        if (!s_sensors[i].used) {
            s_sensors[i].used = true;
            telegram_copy_string(s_sensors[i].name, sizeof(s_sensors[i].name), name);
            telegram_copy_string(s_sensors[i].unit, sizeof(s_sensors[i].unit), unit);
            s_sensors[i].read_cb = read_cb;
            s_sensors[i].ctx = ctx;
            err = ESP_OK;
            break;
        }
    }

    telegram_unlock();
    return err;
}

esp_err_t telegram_register_param(const char *name, telegram_param_type_t type, void *value, size_t value_size, float min_value, float max_value,
                                  const char *description) {
    if (name == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (type == TELEGRAM_PARAM_STRING && value_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || !telegram_lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_ERR_NO_MEM;
    for (size_t i = 0; i < TELEGRAM_MAX_PARAMS; i++) {
        if (!s_params[i].used) {
            s_params[i].used = true;
            telegram_copy_string(s_params[i].name, sizeof(s_params[i].name), name);
            telegram_copy_string(s_params[i].description, sizeof(s_params[i].description), description);
            s_params[i].type = type;
            s_params[i].value = value;
            s_params[i].value_size = value_size;
            s_params[i].min_value = min_value;
            s_params[i].max_value = max_value;
            err = ESP_OK;
            break;
        }
    }

    telegram_unlock();
    return err;
}

esp_err_t telegram_set_my_commands(void) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *list = cJSON_CreateArray();
    if (root == NULL || list == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(list);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddItemToObject(root, "commands", list);

    if (!telegram_lock()) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < TELEGRAM_MAX_COMMANDS; i++) {
        if (!s_commands[i].used) {
            continue;
        }
        cJSON *item = cJSON_CreateObject();
        if (item == NULL) {
            continue;
        }
        cJSON_AddStringToObject(item, "command", s_commands[i].name);
        cJSON_AddStringToObject(item, "description", (s_commands[i].description[0] != '\0') ? s_commands[i].description : s_commands[i].name);
        cJSON_AddItemToArray(list, item);
    }
    telegram_unlock();

    return telegram_api_call("setMyCommands", root);
}

// Renders the current value of a parameter as text.
static void telegram_format_param(const telegram_param_entry_t *param, char *dst, size_t dst_size) {
    switch (param->type) {
        case TELEGRAM_PARAM_INT:
            snprintf(dst, dst_size, "%" PRId32, *(int32_t *)param->value);
            break;
        case TELEGRAM_PARAM_FLOAT:
            snprintf(dst, dst_size, "%.3f", *(float *)param->value);
            break;
        case TELEGRAM_PARAM_BOOL:
            snprintf(dst, dst_size, "%s", (*(bool *)param->value) ? "on" : "off");
            break;
        case TELEGRAM_PARAM_STRING:
            snprintf(dst, dst_size, "%s", (const char *)param->value);
            break;
        default:
            snprintf(dst, dst_size, "?");
            break;
    }
}

// Locates a parameter by name, case-insensitively. The caller must hold
// the mutex.
static telegram_param_entry_t *telegram_find_param(const char *name) {
    for (size_t i = 0; i < TELEGRAM_MAX_PARAMS; i++) {
        if (s_params[i].used && strcasecmp(s_params[i].name, name) == 0) {
            return &s_params[i];
        }
    }
    return NULL;
}

// Applies a new value to a parameter, enforcing the registered range and
// reporting the outcome in a human readable form.
static esp_err_t telegram_apply_param(telegram_param_entry_t *param, const char *text, char *feedback, size_t feedback_size) {
    bool has_range = (param->max_value > param->min_value);
    switch (param->type) {
        case TELEGRAM_PARAM_INT: {
            char *end = NULL;
            long parsed = strtol(text, &end, 0);
            if (end == text) {
                snprintf(feedback, feedback_size, "\"%s\" is not an integer", text);
                return ESP_ERR_INVALID_ARG;
            }
            if (has_range && (parsed < (long)param->min_value || parsed > (long)param->max_value)) {
                snprintf(feedback, feedback_size, "%s must be between %ld and %ld", param->name, (long)param->min_value, (long)param->max_value);
                return ESP_ERR_INVALID_ARG;
            }
            *(int32_t *)param->value = (int32_t)parsed;
            break;
        }
        case TELEGRAM_PARAM_FLOAT: {
            char *end = NULL;
            float parsed = strtof(text, &end);
            if (end == text) {
                snprintf(feedback, feedback_size, "\"%s\" is not a number", text);
                return ESP_ERR_INVALID_ARG;
            }
            if (has_range && (parsed < param->min_value || parsed > param->max_value)) {
                snprintf(feedback, feedback_size, "%s must be between %.3f and %.3f", param->name, param->min_value, param->max_value);
                return ESP_ERR_INVALID_ARG;
            }
            *(float *)param->value = parsed;
            break;
        }
        case TELEGRAM_PARAM_BOOL: {
            bool on = (strcasecmp(text, "on") == 0) || (strcasecmp(text, "true") == 0) || (strcasecmp(text, "1") == 0) || (strcasecmp(text, "yes") == 0);
            bool off = (strcasecmp(text, "off") == 0) || (strcasecmp(text, "false") == 0) || (strcasecmp(text, "0") == 0) || (strcasecmp(text, "no") == 0);
            if (!on && !off) {
                snprintf(feedback, feedback_size, "%s accepts on or off", param->name);
                return ESP_ERR_INVALID_ARG;
            }
            *(bool *)param->value = on;
            break;
        }
        case TELEGRAM_PARAM_STRING:
            telegram_copy_string((char *)param->value, param->value_size, text);
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    char rendered[64];
    telegram_format_param(param, rendered, sizeof(rendered));
    snprintf(feedback, feedback_size, "%s set to %s", param->name, rendered);
    return ESP_OK;
}

esp_err_t telegram_send_sensor_report(const char *chat_id) {
    if (chat_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    char *report = malloc(TELEGRAM_SENSOR_REPORT_BUFFER_LEN);
    if (report == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t offset = telegram_append(report, TELEGRAM_SENSOR_REPORT_BUFFER_LEN, 0, "Telemetry of %s\n", s_device_name);
    size_t published = 0;

    for (size_t i = 0; i < TELEGRAM_MAX_SENSORS && offset < TELEGRAM_SENSOR_REPORT_BUFFER_LEN - 1; i++) {
        telegram_sensor_read_cb_t read_cb = NULL;
        void *ctx = NULL;
        char name[TELEGRAM_ENTRY_NAME_LEN];
        char unit[TELEGRAM_UNIT_LEN];

        if (!telegram_lock()) {
            break;
        }
        if (s_sensors[i].used) {
            read_cb = s_sensors[i].read_cb;
            ctx = s_sensors[i].ctx;
            telegram_copy_string(name, sizeof(name), s_sensors[i].name);
            telegram_copy_string(unit, sizeof(unit), s_sensors[i].unit);
        }
        telegram_unlock();

        if (read_cb == NULL) {
            continue;
        }
        published++;

        float value = 0.0f;
        if (read_cb(&value, ctx) == ESP_OK) {
            offset = telegram_append(report, TELEGRAM_SENSOR_REPORT_BUFFER_LEN, offset, "- %s: %.2f %s\n", name, value, unit);
        } else {
            offset = telegram_append(report, TELEGRAM_SENSOR_REPORT_BUFFER_LEN, offset, "- %s: unavailable\n", name);
        }
    }

    // Lines the application contributes for the readings it decides at run
    // time. They are appended after the registered sensors and count towards
    // the report just as those do, so a station whose whole sensor set is
    // configured rather than registered still answers with its readings
    // instead of reporting that it has none.
    if (s_config.sensors_cb != NULL && offset < TELEGRAM_SENSOR_REPORT_BUFFER_LEN - 1) {
        s_config.sensors_cb(report + offset, TELEGRAM_SENSOR_REPORT_BUFFER_LEN - offset, s_config.sensors_ctx);
        if (report[offset] != '\0') {
            published++;
        }
    }

    esp_err_t err;
    if (published == 0) {
        err = ESP_ERR_NOT_FOUND;
    } else {
        err = telegram_send_message(chat_id, report);
    }

    free(report);
    return err;
}

// Fills a buffer with the multi-line device status served by /status.
static void telegram_build_status(char *buffer, size_t size) {
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    int64_t uptime = (s_start_time_us > 0) ? (esp_timer_get_time() - s_start_time_us) / 1000000 : 0;

    size_t offset = telegram_append(buffer, size, 0,
                                    "Status of %s\n"
                                    "Uptime: %02d:%02d:%02d\n"
                                    "Free heap: %u bytes (minimum %u)\n"
                                    "Cores: %d, revision %d\n"
                                    "Framework: %s\n"
                                    "Last reset: %d\n",
                                    s_device_name, (int)(uptime / 3600), (int)((uptime / 60) % 60), (int)(uptime % 60), (unsigned)esp_get_free_heap_size(),
                                    (unsigned)esp_get_minimum_free_heap_size(), chip.cores, chip.revision, esp_get_idf_version(), (int)esp_reset_reason());

    wifi_ap_record_t ap;
    if (offset < size && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        offset = telegram_append(buffer, size, offset, "Wi-Fi: %s, %d dBm\n", (const char *)ap.ssid, ap.rssi);
    }

    if (offset < size) {
        offset = telegram_append(buffer, size, offset, "Users: %u, alerts %s\n", (unsigned)telegram_user_count(), s_alerts_enabled ? "enabled" : "disabled");
    }

    if (s_config.status_cb != NULL && offset < size) {
        s_config.status_cb(buffer + offset, size - offset, s_config.status_ctx);
    }
}

// Built-in /start command: greets the sender and points at /help.
static esp_err_t telegram_cmd_start(const telegram_update_t *update, const char *args, void *ctx) {
    (void)args;
    (void)ctx;
    return telegram_send_message_fmt(update->chat_id_str,
                                     "%s is online.\n"
                                     "Send /help for the list of commands, /status for the current "
                                     "state and /menu for the button interface.",
                                     s_device_name);
}

// Built-in /help command: lists every registered command.
static esp_err_t telegram_cmd_help(const telegram_update_t *update, const char *args, void *ctx) {
    (void)args;
    (void)ctx;

    char *text = malloc(TELEGRAM_REPORT_BUFFER_LEN);
    if (text == NULL) {
        return ESP_ERR_NO_MEM;
    }
    size_t offset = telegram_append(text, TELEGRAM_REPORT_BUFFER_LEN, 0, "Commands of %s\n", s_device_name);

    if (telegram_lock()) {
        for (size_t i = 0; i < TELEGRAM_MAX_COMMANDS && offset < TELEGRAM_REPORT_BUFFER_LEN - 1; i++) {
            if (!s_commands[i].used) {
                continue;
            }
            if (s_commands[i].admin_only && !update->is_admin) {
                continue;
            }
            offset = telegram_append(text, TELEGRAM_REPORT_BUFFER_LEN, offset, "/%s - %s%s\n", s_commands[i].name, s_commands[i].description,
                                     s_commands[i].admin_only ? " (admin)" : "");
        }
        telegram_unlock();
    }

    esp_err_t err = telegram_send_message(update->chat_id_str, text);
    free(text);
    return err;
}

// Built-in /status command: reports the state of the device.
static esp_err_t telegram_cmd_status(const telegram_update_t *update, const char *args, void *ctx) {
    (void)args;
    (void)ctx;

    char *text = malloc(TELEGRAM_REPORT_BUFFER_LEN);
    if (text == NULL) {
        return ESP_ERR_NO_MEM;
    }
    telegram_build_status(text, TELEGRAM_REPORT_BUFFER_LEN);
    esp_err_t err = telegram_send_message(update->chat_id_str, text);
    free(text);
    return err;
}

// Built-in /sensors command: reports every registered sensor plus the lines
// the application's sensors callback contributes.
static esp_err_t telegram_cmd_sensors(const telegram_update_t *update, const char *args, void *ctx) {
    (void)args;
    (void)ctx;
    esp_err_t err = telegram_send_sensor_report(update->chat_id_str);
    if (err == ESP_ERR_NOT_FOUND) {
        err = telegram_send_message(update->chat_id_str, "No sensor reading is available on this device.");
    }
    return err;
}

// Built-in /uptime command.
static esp_err_t telegram_cmd_uptime(const telegram_update_t *update, const char *args, void *ctx) {
    (void)args;
    (void)ctx;
    int64_t uptime = (s_start_time_us > 0) ? (esp_timer_get_time() - s_start_time_us) / 1000000 : 0;
    return telegram_send_message_fmt(update->chat_id_str, "Uptime: %d days %02d:%02d:%02d", (int)(uptime / 86400), (int)((uptime / 3600) % 24),
                                     (int)((uptime / 60) % 60), (int)(uptime % 60));
}

// Built-in /whoami command: reports the identifiers needed to authorize a
// user or a group.
static esp_err_t telegram_cmd_whoami(const telegram_update_t *update, const char *args, void *ctx) {
    (void)args;
    (void)ctx;
    return telegram_send_message_fmt(update->chat_id_str,
                                     "User id: %" PRId64 "\n"
                                     "Chat id: %" PRId64 "\n"
                                     "Name: %s %s\n"
                                     "Rights: %s",
                                     update->user_id, update->chat_id, update->first_name, update->user_name,
                                     update->is_admin ? "administrator" : (update->authorized ? "user" : "none"));
}

// Built-in /stats command: reports the counters of the service.
static esp_err_t telegram_cmd_stats(const telegram_update_t *update, const char *args, void *ctx) {
    (void)args;
    (void)ctx;
    telegram_stats_t stats;
    esp_err_t err = telegram_get_stats(&stats);
    if (err != ESP_OK) {
        return err;
    }
    return telegram_send_message_fmt(update->chat_id_str,
                                     "Updates: %" PRIu32 "\n"
                                     "Commands: %" PRIu32 "\n"
                                     "Messages sent: %" PRIu32 "\n"
                                     "Alerts sent: %" PRIu32 "\n"
                                     "Rejected: %" PRIu32 "\n"
                                     "Poll errors: %" PRIu32,
                                     stats.updates_received, stats.commands_handled, stats.messages_sent, stats.alerts_sent, stats.rejected, stats.poll_errors);
}

// Built-in /users command: lists the authorized users and group chats.
static esp_err_t telegram_cmd_users(const telegram_update_t *update, const char *args, void *ctx) {
    (void)args;
    (void)ctx;

    char *text = malloc(TELEGRAM_REPORT_BUFFER_LEN);
    if (text == NULL) {
        return ESP_ERR_NO_MEM;
    }
    size_t offset = telegram_append(text, TELEGRAM_REPORT_BUFFER_LEN, 0, "Authorized users\n");

    if (telegram_lock()) {
        for (size_t i = 0; i < TELEGRAM_MAX_USERS && offset < TELEGRAM_REPORT_BUFFER_LEN - 1; i++) {
            if (!s_users[i].used) {
                continue;
            }
            offset = telegram_append(text, TELEGRAM_REPORT_BUFFER_LEN, offset, "- %" PRId64 " %s%s\n", s_users[i].id, s_users[i].name,
                                     s_users[i].is_admin ? " (admin)" : "");
        }
        offset = telegram_append(text, TELEGRAM_REPORT_BUFFER_LEN, offset, "Allowed groups\n");
        for (size_t i = 0; i < TELEGRAM_MAX_CHATS && offset < TELEGRAM_REPORT_BUFFER_LEN - 1; i++) {
            if (!s_chats[i].used) {
                continue;
            }
            offset = telegram_append(text, TELEGRAM_REPORT_BUFFER_LEN, offset, "- %" PRId64 " %s\n", s_chats[i].id, s_chats[i].name);
        }
        telegram_unlock();
    }

    esp_err_t err = telegram_send_message(update->chat_id_str, text);
    free(text);
    return err;
}

// Built-in /config command: lists the parameters and their current values.
static esp_err_t telegram_cmd_config(const telegram_update_t *update, const char *args, void *ctx) {
    (void)args;
    (void)ctx;

    char *text = malloc(TELEGRAM_REPORT_BUFFER_LEN);
    if (text == NULL) {
        return ESP_ERR_NO_MEM;
    }
    size_t offset = telegram_append(text, TELEGRAM_REPORT_BUFFER_LEN, 0, "Configuration of %s\n", s_device_name);
    size_t published = 0;

    if (telegram_lock()) {
        for (size_t i = 0; i < TELEGRAM_MAX_PARAMS && offset < TELEGRAM_REPORT_BUFFER_LEN - 1; i++) {
            if (!s_params[i].used) {
                continue;
            }
            char rendered[64];
            telegram_format_param(&s_params[i], rendered, sizeof(rendered));
            offset = telegram_append(text, TELEGRAM_REPORT_BUFFER_LEN, offset, "- %s = %s : %s\n", s_params[i].name, rendered, s_params[i].description);
            published++;
        }
        telegram_unlock();
    }

    if (published == 0) {
        snprintf(text, TELEGRAM_REPORT_BUFFER_LEN, "No parameter is exposed on this device.");
    } else {
        telegram_append(text, TELEGRAM_REPORT_BUFFER_LEN, offset, "Use /get <name> and /set <name> <value>.");
    }

    esp_err_t err = telegram_send_message(update->chat_id_str, text);
    free(text);
    return err;
}

// Built-in /get command: reports the value of one parameter.
static esp_err_t telegram_cmd_get(const telegram_update_t *update, const char *args, void *ctx) {
    (void)ctx;
    if (args == NULL || args[0] == '\0') {
        return telegram_send_message(update->chat_id_str, "Usage: /get <name>");
    }

    char rendered[64] = "";
    char description[TELEGRAM_DESCRIPTION_LEN] = "";
    bool found = false;

    if (telegram_lock()) {
        telegram_param_entry_t *param = telegram_find_param(args);
        if (param != NULL) {
            telegram_format_param(param, rendered, sizeof(rendered));
            telegram_copy_string(description, sizeof(description), param->description);
            found = true;
        }
        telegram_unlock();
    }

    if (!found) {
        return telegram_send_message_fmt(update->chat_id_str, "Unknown parameter \"%s\".", args);
    }
    return telegram_send_message_fmt(update->chat_id_str, "%s = %s\n%s", args, rendered, description);
}

// Built-in /set command: changes the value of one parameter.
static esp_err_t telegram_cmd_set(const telegram_update_t *update, const char *args, void *ctx) {
    (void)ctx;
    if (args == NULL || args[0] == '\0') {
        return telegram_send_message(update->chat_id_str, "Usage: /set <name> <value>");
    }

    char name[TELEGRAM_ENTRY_NAME_LEN] = "";
    const char *separator = strchr(args, ' ');
    if (separator == NULL) {
        return telegram_send_message(update->chat_id_str, "Usage: /set <name> <value>");
    }
    size_t name_len = (size_t)(separator - args);
    if (name_len >= sizeof(name)) {
        name_len = sizeof(name) - 1;
    }
    memcpy(name, args, name_len);
    name[name_len] = '\0';

    const char *value = separator + 1;
    while (*value == ' ') {
        value++;
    }

    char feedback[128] = "";
    bool found = false;
    if (telegram_lock()) {
        telegram_param_entry_t *param = telegram_find_param(name);
        if (param != NULL) {
            found = true;
            telegram_apply_param(param, value, feedback, sizeof(feedback));
        }
        telegram_unlock();
    }

    if (!found) {
        return telegram_send_message_fmt(update->chat_id_str, "Unknown parameter \"%s\".", name);
    }
    ESP_LOGI(TAG, "Parameter change requested by %" PRId64 ": %s", update->user_id, feedback);
    return telegram_send_message(update->chat_id_str, feedback);
}

// Built-in /alerts command: enables or disables alert delivery.
static esp_err_t telegram_cmd_alerts(const telegram_update_t *update, const char *args, void *ctx) {
    (void)ctx;
    if (args != NULL && strcasecmp(args, "on") == 0) {
        telegram_set_alerts_enabled(true);
    } else if (args != NULL && strcasecmp(args, "off") == 0) {
        telegram_set_alerts_enabled(false);
    } else {
        return telegram_send_message_fmt(update->chat_id_str, "Alerts are %s. Usage: /alerts on|off", s_alerts_enabled ? "enabled" : "disabled");
    }
    return telegram_send_message_fmt(update->chat_id_str, "Alerts %s.", s_alerts_enabled ? "enabled" : "disabled");
}

// Built-in /reboot command: confirms and restarts the device.
static esp_err_t telegram_cmd_reboot(const telegram_update_t *update, const char *args, void *ctx) {
    (void)args;
    (void)ctx;
    telegram_send_message_fmt(update->chat_id_str, "%s is restarting now.", s_device_name);
    ESP_LOGW(TAG, "Restart requested by user %" PRId64, update->user_id);
    vTaskDelay(pdMS_TO_TICKS(TELEGRAM_REBOOT_DELAY_MS));
    esp_restart();
    return ESP_OK;
}

// Built-in /menu command: publishes the inline keyboard giving access to
// the most common built-in actions without typing.
static esp_err_t telegram_cmd_menu(const telegram_update_t *update, const char *args, void *ctx) {
    (void)args;
    (void)ctx;
    const telegram_button_t buttons[] = {
        { .label = "Status", .data = TELEGRAM_SYSTEM_CALLBACK_PREFIX "status" },
        { .label = "Sensors", .data = TELEGRAM_SYSTEM_CALLBACK_PREFIX "sensors" },
        { .label = "Uptime", .data = TELEGRAM_SYSTEM_CALLBACK_PREFIX "uptime" },
        { .label = "Settings", .data = TELEGRAM_SYSTEM_CALLBACK_PREFIX "config" },
    };
    return telegram_send_menu(update->chat_id_str, "Select an action:", buttons, sizeof(buttons) / sizeof(buttons[0]), 2);
}

// Routes the buttons of the built-in menu to the matching built-in
// command handler.
static esp_err_t telegram_builtin_menu_callback(const telegram_update_t *update, const char *data, void *ctx) {
    (void)ctx;
    const char *action = data + strlen(TELEGRAM_SYSTEM_CALLBACK_PREFIX);

    if (strcmp(action, "status") == 0) {
        return telegram_cmd_status(update, "", NULL);
    }
    if (strcmp(action, "sensors") == 0) {
        return telegram_cmd_sensors(update, "", NULL);
    }
    if (strcmp(action, "uptime") == 0) {
        return telegram_cmd_uptime(update, "", NULL);
    }
    if (strcmp(action, "config") == 0) {
        return telegram_cmd_config(update, "", NULL);
    }
    return telegram_send_message_fmt(update->chat_id_str, "Unhandled action \"%s\".", action);
}

// Registers the command set every device shares.
static esp_err_t telegram_register_builtin_commands(void) {
    struct {
        const char *name;
        const char *description;
        bool admin_only;
        telegram_command_cb_t handler;
    } builtins[] = {
        { "start", "Greet the bot and show the entry points", false, telegram_cmd_start },
        { "help", "List the available commands", false, telegram_cmd_help },
        { "status", "Report the state of the device", false, telegram_cmd_status },
        { "sensors", "Report the sensor readings", false, telegram_cmd_sensors },
        { "uptime", "Report how long the device has been up", false, telegram_cmd_uptime },
        { "whoami", "Report the user and chat identifiers", false, telegram_cmd_whoami },
        { "menu", "Show the button interface", false, telegram_cmd_menu },
        { "stats", "Report the service counters", false, telegram_cmd_stats },
        { "config", "List the configurable parameters", true, telegram_cmd_config },
        { "get", "Read one parameter", true, telegram_cmd_get },
        { "set", "Change one parameter", true, telegram_cmd_set },
        { "users", "List the authorized users", true, telegram_cmd_users },
        { "alerts", "Enable or disable the alerts", true, telegram_cmd_alerts },
    };

    for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
        esp_err_t err = telegram_register_command(builtins[i].name, builtins[i].description, builtins[i].admin_only, builtins[i].handler, NULL);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (s_config.allow_reboot) {
        return telegram_register_command("reboot", "Restart the device", true, telegram_cmd_reboot, NULL);
    }
    return ESP_OK;
}

esp_err_t telegram_process_command(const telegram_update_t *update) {
    if (update == NULL || update->command[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!update->authorized) {
        return ESP_ERR_NOT_ALLOWED;
    }

    char name[TELEGRAM_COMMAND_LEN];
    telegram_normalize_command(update->command, name, sizeof(name));

    telegram_command_cb_t handler = NULL;
    void *ctx = NULL;
    bool admin_only = false;
    bool found = false;

    if (!telegram_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < TELEGRAM_MAX_COMMANDS; i++) {
        if (s_commands[i].used && strcmp(s_commands[i].name, name) == 0) {
            handler = s_commands[i].handler;
            ctx = s_commands[i].ctx;
            admin_only = s_commands[i].admin_only;
            found = true;
            break;
        }
    }
    telegram_unlock();

    if (!found) {
        return ESP_ERR_NOT_FOUND;
    }
    if (admin_only && !update->is_admin) {
        return ESP_ERR_NOT_ALLOWED;
    }

    esp_err_t err = handler(update, update->args, ctx);
    if (err == ESP_OK && telegram_lock()) {
        s_stats.commands_handled++;
        telegram_unlock();
    }
    return err;
}

esp_err_t telegram_execute_command(const char *chat_id, const char *command_line) {
    if (chat_id == NULL || command_line == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // The update descriptor is close to a kilobyte, and this function runs
    // on the stack of whichever task injects the command, so it is built on
    // the heap instead of on that stack.
    telegram_update_t *update = calloc(1, sizeof(telegram_update_t));
    if (update == NULL) {
        return ESP_ERR_NO_MEM;
    }
    update->type = TELEGRAM_UPDATE_COMMAND;
    update->authorized = true;
    update->is_admin = true;
    telegram_copy_string(update->chat_id_str, sizeof(update->chat_id_str), chat_id);
    update->chat_id = strtoll(chat_id, NULL, 10);
    telegram_copy_string(update->text, sizeof(update->text), command_line);

    telegram_normalize_command(command_line, update->command, sizeof(update->command));
    const char *separator = strchr(command_line, ' ');
    if (separator != NULL) {
        while (*separator == ' ') {
            separator++;
        }
        telegram_copy_string(update->args, sizeof(update->args), separator);
    }

    esp_err_t err = telegram_process_command(update);
    free(update);
    return err;
}

// Copies a string field of a JSON object into a fixed size buffer.
static void telegram_json_copy_string(const cJSON *object, const char *key, char *dst, size_t dst_size) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        telegram_copy_string(dst, dst_size, item->valuestring);
    } else {
        dst[0] = '\0';
    }
}

// Reads a numeric field of a JSON object, falling back to zero when it is
// absent. Chat and user identifiers exceed the range of a float only for
// values Telegram does not currently issue, so the double storage of cJSON
// holds them exactly.
static int64_t telegram_json_get_int(const cJSON *object, const char *key) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsNumber(item)) {
        return (int64_t)item->valuedouble;
    }
    return 0;
}

// Fills the sender related fields of an update from a Telegram "User"
// object.
static void telegram_parse_sender(const cJSON *from, telegram_update_t *update) {
    update->user_id = telegram_json_get_int(from, "id");
    telegram_json_copy_string(from, "username", update->user_name, sizeof(update->user_name));
    telegram_json_copy_string(from, "first_name", update->first_name, sizeof(update->first_name));
}

// Fills the chat related fields of an update from a Telegram "Chat"
// object.
static void telegram_parse_chat(const cJSON *chat, telegram_update_t *update) {
    update->chat_id = telegram_json_get_int(chat, "id");
    telegram_format_chat_id(update->chat_id, update->chat_id_str, sizeof(update->chat_id_str));

    char type[16];
    telegram_json_copy_string(chat, "type", type, sizeof(type));
    update->is_group = (strcmp(type, "group") == 0) || (strcmp(type, "supergroup") == 0) || (strcmp(type, "channel") == 0);
}

// Extracts the command name and its arguments from a message starting with
// a slash.
static void telegram_parse_command_line(const char *text, telegram_update_t *update) {
    telegram_normalize_command(text, update->command, sizeof(update->command));
    const char *separator = strchr(text, ' ');
    if (separator != NULL) {
        while (*separator == ' ') {
            separator++;
        }
        telegram_copy_string(update->args, sizeof(update->args), separator);
    }
}

// Decodes a Telegram "Message" object into an update descriptor.
static void telegram_parse_message(const cJSON *message, telegram_update_t *update) {
    update->message_id = (int32_t)telegram_json_get_int(message, "message_id");
    telegram_parse_sender(cJSON_GetObjectItemCaseSensitive(message, "from"), update);
    telegram_parse_chat(cJSON_GetObjectItemCaseSensitive(message, "chat"), update);

    const cJSON *text = cJSON_GetObjectItemCaseSensitive(message, "text");
    const cJSON *caption = cJSON_GetObjectItemCaseSensitive(message, "caption");
    if (cJSON_IsString(text) && text->valuestring != NULL) {
        telegram_copy_string(update->text, sizeof(update->text), text->valuestring);
    } else if (cJSON_IsString(caption) && caption->valuestring != NULL) {
        telegram_copy_string(update->text, sizeof(update->text), caption->valuestring);
    }

    const cJSON *photo = cJSON_GetObjectItemCaseSensitive(message, "photo");
    const cJSON *document = cJSON_GetObjectItemCaseSensitive(message, "document");
    const cJSON *location = cJSON_GetObjectItemCaseSensitive(message, "location");
    const cJSON *contact = cJSON_GetObjectItemCaseSensitive(message, "contact");
    const cJSON *web_app_data = cJSON_GetObjectItemCaseSensitive(message, "web_app_data");

    if (cJSON_IsArray(photo)) {
        // Telegram lists the same photo in several resolutions, ordered
        // from the smallest to the largest; the last entry is the one
        // worth downloading.
        int count = cJSON_GetArraySize(photo);
        const cJSON *largest = (count > 0) ? cJSON_GetArrayItem(photo, count - 1) : NULL;
        if (largest != NULL) {
            telegram_json_copy_string(largest, "file_id", update->file_id, sizeof(update->file_id));
            update->file_size = (int)telegram_json_get_int(largest, "file_size");
            telegram_copy_string(update->mime_type, sizeof(update->mime_type), "image/jpeg");
            update->type = TELEGRAM_UPDATE_PHOTO;
        }
    } else if (cJSON_IsObject(document)) {
        telegram_json_copy_string(document, "file_id", update->file_id, sizeof(update->file_id));
        telegram_json_copy_string(document, "file_name", update->file_name, sizeof(update->file_name));
        telegram_json_copy_string(document, "mime_type", update->mime_type, sizeof(update->mime_type));
        update->file_size = (int)telegram_json_get_int(document, "file_size");
        update->type = TELEGRAM_UPDATE_DOCUMENT;
    } else if (cJSON_IsObject(location)) {
        const cJSON *latitude = cJSON_GetObjectItemCaseSensitive(location, "latitude");
        const cJSON *longitude = cJSON_GetObjectItemCaseSensitive(location, "longitude");
        update->latitude = cJSON_IsNumber(latitude) ? latitude->valuedouble : 0.0;
        update->longitude = cJSON_IsNumber(longitude) ? longitude->valuedouble : 0.0;
        update->type = TELEGRAM_UPDATE_LOCATION;
    } else if (cJSON_IsObject(contact)) {
        telegram_json_copy_string(contact, "phone_number", update->text, sizeof(update->text));
        update->type = TELEGRAM_UPDATE_CONTACT;
    } else if (cJSON_IsObject(web_app_data)) {
        telegram_json_copy_string(web_app_data, "data", update->text, sizeof(update->text));
        update->type = TELEGRAM_UPDATE_WEB_APP;
    } else if (update->text[0] == '/') {
        telegram_parse_command_line(update->text, update);
        update->type = TELEGRAM_UPDATE_COMMAND;
    } else if (update->text[0] != '\0') {
        update->type = TELEGRAM_UPDATE_TEXT;
    }
}

// Copies the printable emoji of a Telegram "ReactionType" object into a
// fixed size buffer. Only the emoji variant carries one; a custom or a
// paid reaction leaves the buffer empty and is only accounted for in the
// entry count.
static void telegram_parse_reaction_type(const cJSON *reaction, char *dst, size_t dst_size) {
    dst[0] = '\0';
    if (!cJSON_IsObject(reaction)) {
        return;
    }
    char kind[16];
    telegram_json_copy_string(reaction, "type", kind, sizeof(kind));
    if (strcmp(kind, "emoji") == 0) {
        telegram_json_copy_string(reaction, "emoji", dst, dst_size);
    }
}

// Copies the emoji of the first entry of a "ReactionType" array and
// reports how many entries the array holds.
static int telegram_parse_reaction_list(const cJSON *list, char *dst, size_t dst_size) {
    dst[0] = '\0';
    if (!cJSON_IsArray(list)) {
        return 0;
    }
    int count = cJSON_GetArraySize(list);
    if (count > 0) {
        telegram_parse_reaction_type(cJSON_GetArrayItem(list, 0), dst, dst_size);
    }
    return count;
}

// Decodes a Telegram "MessageReactionUpdated" object into an update
// descriptor. The sender sits in "user" rather than in the "from" field
// every other update uses, and is absent altogether when the reaction was
// placed anonymously on behalf of a chat, in which case the update is left
// with no sender and the identifier of that chat is reported instead.
static void telegram_parse_reaction(const cJSON *reaction, telegram_update_t *update) {
    update->type = TELEGRAM_UPDATE_REACTION;
    update->message_id = (int32_t)telegram_json_get_int(reaction, "message_id");
    telegram_parse_chat(cJSON_GetObjectItemCaseSensitive(reaction, "chat"), update);

    const cJSON *user = cJSON_GetObjectItemCaseSensitive(reaction, "user");
    if (cJSON_IsObject(user)) {
        telegram_parse_sender(user, update);
    } else {
        const cJSON *actor = cJSON_GetObjectItemCaseSensitive(reaction, "actor_chat");
        telegram_json_copy_string(actor, "title", update->first_name, sizeof(update->first_name));
    }

    update->reaction_count =
        telegram_parse_reaction_list(cJSON_GetObjectItemCaseSensitive(reaction, "new_reaction"), update->reaction, sizeof(update->reaction));
    telegram_parse_reaction_list(cJSON_GetObjectItemCaseSensitive(reaction, "old_reaction"), update->reaction_previous, sizeof(update->reaction_previous));
}

// Decodes a Telegram "MessageReactionCountUpdated" object into an update
// descriptor. This form carries no sender at all, only the totals per
// emoji, of which the most used one is reported.
static void telegram_parse_reaction_count(const cJSON *reaction, telegram_update_t *update) {
    update->type = TELEGRAM_UPDATE_REACTION_COUNT;
    update->message_id = (int32_t)telegram_json_get_int(reaction, "message_id");
    telegram_parse_chat(cJSON_GetObjectItemCaseSensitive(reaction, "chat"), update);

    const cJSON *totals = cJSON_GetObjectItemCaseSensitive(reaction, "reactions");
    if (!cJSON_IsArray(totals)) {
        return;
    }

    const cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, totals) {
        int total = (int)telegram_json_get_int(entry, "total_count");
        if (total <= update->reaction_count) {
            continue;
        }
        update->reaction_count = total;
        telegram_parse_reaction_type(cJSON_GetObjectItemCaseSensitive(entry, "type"), update->reaction, sizeof(update->reaction));
    }
}

// Decodes a Telegram "CallbackQuery" object into an update descriptor.
static void telegram_parse_callback(const cJSON *query, telegram_update_t *update) {
    update->type = TELEGRAM_UPDATE_CALLBACK;
    telegram_json_copy_string(query, "id", update->callback_id, sizeof(update->callback_id));
    telegram_json_copy_string(query, "data", update->callback_data, sizeof(update->callback_data));
    telegram_parse_sender(cJSON_GetObjectItemCaseSensitive(query, "from"), update);

    const cJSON *message = cJSON_GetObjectItemCaseSensitive(query, "message");
    if (cJSON_IsObject(message)) {
        update->message_id = (int32_t)telegram_json_get_int(message, "message_id");
        telegram_parse_chat(cJSON_GetObjectItemCaseSensitive(message, "chat"), update);
    } else {
        telegram_format_chat_id(update->user_id, update->chat_id_str, sizeof(update->chat_id_str));
        update->chat_id = update->user_id;
    }
}

// Routes a decoded button press to the handler whose prefix matches its
// payload, then answers the query so the client stops waiting.
static void telegram_dispatch_callback(const telegram_update_t *update) {
    telegram_callback_cb_t handler = NULL;
    void *ctx = NULL;
    size_t best_len = 0;
    bool found = false;

    if (telegram_lock()) {
        for (size_t i = 0; i < TELEGRAM_MAX_CALLBACKS; i++) {
            if (!s_callbacks[i].used) {
                continue;
            }
            size_t prefix_len = strlen(s_callbacks[i].prefix);
            if (strncmp(update->callback_data, s_callbacks[i].prefix, prefix_len) != 0) {
                continue;
            }
            // The longest matching prefix wins, so a catch-all handler
            // registered with an empty prefix never shadows a specific one.
            if (!found || prefix_len >= best_len) {
                handler = s_callbacks[i].handler;
                ctx = s_callbacks[i].ctx;
                best_len = prefix_len;
                found = true;
            }
        }
        telegram_unlock();
    }

    // The query is answered before the handler runs, not after it. Telegram
    // holds a callback query open for a few seconds only, and a handler on
    // this device spends exactly that budget: it builds a report and sends it
    // over a connection that may have to be handshaked first. Answering last
    // means the answer is refused as "query is too old" whenever a burst of
    // button presses arrives as one batch, and every button after the first
    // keeps its progress indicator spinning until the client gives up.
    // Answering first costs the same single request and stops the indicator
    // while the query is still valid, whatever the handler goes on to do.
    if (update->callback_id[0] != '\0') {
        telegram_answer_callback(update->callback_id, NULL, false);
    }

    if (found) {
        esp_err_t err = handler(update, update->callback_data, ctx);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Callback handler for \"%s\" returned %s", update->callback_data, esp_err_to_name(err));
        }
    } else {
        ESP_LOGW(TAG, "No handler registered for callback \"%s\"", update->callback_data);
    }
}

// Applies the authorization rules to a decoded update and reports whether
// it deserves to be processed.
static bool telegram_authorize_update(telegram_update_t *update) {
    update->authorized = telegram_is_authorized(update->user_id);
    update->is_admin = update->authorized && telegram_is_admin(update->user_id);

    if (update->is_group && !telegram_is_chat_allowed(update->chat_id)) {
        ESP_LOGW(TAG, "Update from unlisted group %" PRId64 " ignored", update->chat_id);
        return false;
    }
    // An aggregated reaction update names no sender by construction:
    // Telegram delivers it exactly where the identity of the reacting
    // users is hidden. There is nobody to look up, so the allowed chat
    // list checked above is the only gate that applies to it.
    if (update->type == TELEGRAM_UPDATE_REACTION_COUNT) {
        return true;
    }
    if (!update->authorized) {
        ESP_LOGW(TAG, "Update from unauthorized user %" PRId64 " ignored", update->user_id);
        if (telegram_lock()) {
            s_stats.rejected++;
            telegram_unlock();
        }
        // Answering a private request with the identifier of the sender is
        // what lets an operator authorize themselves from the device side.
        if (!update->is_group && update->type == TELEGRAM_UPDATE_COMMAND) {
            telegram_send_message_fmt(update->chat_id_str,
                                      "This device is restricted. Ask an administrator to authorize "
                                      "the user id %" PRId64 ".",
                                      update->user_id);
        }
        return false;
    }
    return true;
}

// Dispatches one decoded update to the matching handler.
static void telegram_dispatch_update(telegram_update_t *update) {
    if (telegram_lock()) {
        s_stats.updates_received++;
        telegram_unlock();
    }

    if (!telegram_authorize_update(update)) {
        return;
    }

    switch (update->type) {
        case TELEGRAM_UPDATE_COMMAND: {
            esp_err_t err = telegram_process_command(update);
            if (err == ESP_ERR_NOT_FOUND) {
                telegram_send_message_fmt(update->chat_id_str, "Unknown command \"/%s\". Send /help for the list.", update->command);
            } else if (err == ESP_ERR_NOT_ALLOWED) {
                telegram_send_message(update->chat_id_str, "This command is reserved for administrators.");
            } else if (err != ESP_OK) {
                telegram_send_message_fmt(update->chat_id_str, "The command failed: %s", esp_err_to_name(err));
            }
            break;
        }
        case TELEGRAM_UPDATE_CALLBACK:
            telegram_dispatch_callback(update);
            break;
        default: {
            telegram_event_cb_t handler = NULL;
            void *ctx = NULL;
            if (telegram_lock()) {
                handler = s_event_handler;
                ctx = s_event_ctx;
                telegram_unlock();
            }
            if (handler != NULL) {
                esp_err_t err = handler(update, ctx);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "Event handler returned %s", esp_err_to_name(err));
                }
            } else {
                ESP_LOGD(TAG, "Update of type %d ignored, no event handler registered", (int)update->type);
            }
            break;
        }
    }
}

// Decodes one getUpdates answer and dispatches every update it carries.
static void telegram_process_updates(const char *payload) {
    cJSON *root = cJSON_Parse(payload);
    if (root == NULL) {
        ESP_LOGE(TAG, "Malformed update payload");
        return;
    }

    const cJSON *ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
    const cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (!cJSON_IsTrue(ok) || !cJSON_IsArray(result)) {
        const cJSON *description = cJSON_GetObjectItemCaseSensitive(root, "description");
        ESP_LOGE(TAG, "getUpdates refused: %s", cJSON_IsString(description) ? description->valuestring : "unknown reason");
        cJSON_Delete(root);
        return;
    }

    // The descriptor is close to a kilobyte and the handlers dispatched
    // below open TLS connections on this same task, so it is kept on the
    // heap and reused for every update of the batch rather than eating
    // that much of the service task stack.
    telegram_update_t *update = malloc(sizeof(telegram_update_t));
    if (update == NULL) {
        ESP_LOGE(TAG, "Not enough memory to decode the update batch");
        cJSON_Delete(root);
        return;
    }

    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, result) {
        int64_t update_id = telegram_json_get_int(item, "update_id");
        if (update_id >= s_next_offset) {
            s_next_offset = update_id + 1;
        }

        memset(update, 0, sizeof(*update));
        update->update_id = update_id;

        const cJSON *message = cJSON_GetObjectItemCaseSensitive(item, "message");
        const cJSON *callback = cJSON_GetObjectItemCaseSensitive(item, "callback_query");
        const cJSON *reaction = cJSON_GetObjectItemCaseSensitive(item, "message_reaction");
        const cJSON *reaction_count = cJSON_GetObjectItemCaseSensitive(item, "message_reaction_count");
        if (cJSON_IsObject(message)) {
            telegram_parse_message(message, update);
        } else if (cJSON_IsObject(callback)) {
            telegram_parse_callback(callback, update);
        } else if (cJSON_IsObject(reaction)) {
            telegram_parse_reaction(reaction, update);
        } else if (cJSON_IsObject(reaction_count)) {
            telegram_parse_reaction_count(reaction_count, update);
        } else {
            continue;
        }

        ESP_LOGI(TAG, "Update %" PRId64 " type %d from %" PRId64 " in chat %s", update_id, (int)update->type, update->user_id, update->chat_id_str);
        telegram_dispatch_update(update);
    }

    free(update);
    cJSON_Delete(root);
}

// Reads the identifier of the first update of a getUpdates answer straight
// from the text of the payload. Telegram places that field at the head of
// each element, so it stays readable in the leading fragment stored when
// the batch does not fit in the reception buffer.
static bool telegram_scan_first_update_id(const char *payload, int64_t *out_id) {
    static const char marker[] = "\"update_id\":";
    const char *found = strstr(payload, marker);
    if (found == NULL) {
        return false;
    }
    const char *digits = found + sizeof(marker) - 1;
    char *end = NULL;
    long long parsed = strtoll(digits, &end, 10);
    if (end == digits) {
        return false;
    }
    *out_id = (int64_t)parsed;
    return true;
}

// Performs one getUpdates call and hands the answer to the decoder.
static esp_err_t telegram_poll_once(void) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddNumberToObject(root, "offset", (double)s_next_offset);
    cJSON_AddNumberToObject(root, "timeout", s_config.poll_timeout_s);
    cJSON_AddNumberToObject(root, "limit", 5);

    cJSON *allowed = cJSON_CreateArray();
    if (allowed != NULL) {
        cJSON_AddItemToArray(allowed, cJSON_CreateString("message"));
        cJSON_AddItemToArray(allowed, cJSON_CreateString("callback_query"));
        // Telegram keeps both reaction kinds out of the set it delivers by
        // default, so they only arrive when they are asked for by name.
        if (s_config.receive_reactions) {
            cJSON_AddItemToArray(allowed, cJSON_CreateString("message_reaction"));
            cJSON_AddItemToArray(allowed, cJSON_CreateString("message_reaction_count"));
        }
        cJSON_AddItemToObject(root, "allowed_updates", allowed);
    }

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL) {
        return ESP_ERR_NO_MEM;
    }

    telegram_bot_request_t request = {
        .method = "getUpdates",
        .json_body = body,
        .timeout_ms = (s_config.poll_timeout_s + 10) * 1000,
    };
    telegram_bot_response_t response = {
        .buffer = s_rx_buffer,
        .buffer_size = s_rx_buffer_size,
    };

    telegram_release_tx_connection();

    esp_err_t err = telegram_bot_client_call(s_poll_client, &request, &response);
    cJSON_free(body);

    if (err != ESP_OK) {
        return err;
    }
    if (response.truncated) {
        // The batch cannot be decoded, so the offset is moved past the
        // first update it carried; the identifier is taken from the head
        // of the stored fragment, which Telegram always fills before the
        // payload of the update. Without a usable identifier the offset
        // stays put, because an arbitrary one would acknowledge updates
        // that were never seen.
        int64_t first_id = 0;
        if (telegram_scan_first_update_id(s_rx_buffer, &first_id) && first_id >= s_next_offset) {
            s_next_offset = first_id + 1;
            ESP_LOGE(TAG, "Update batch larger than the %u byte reception buffer, update %" PRId64 " skipped", (unsigned)s_rx_buffer_size, first_id);
        } else {
            ESP_LOGE(TAG, "Update batch larger than the %u byte reception buffer", (unsigned)s_rx_buffer_size);
        }
        return ESP_ERR_INVALID_SIZE;
    }
    if (response.status_code != 200) {
        ESP_LOGE(TAG, "getUpdates answered with status %d", response.status_code);
        return ESP_FAIL;
    }

    telegram_process_updates(s_rx_buffer);
    return ESP_OK;
}

// Sends the alerts that accumulated in the queue since the last cycle.
static void telegram_drain_alerts(void) {
    telegram_alert_item_t item;
    while (xQueueReceive(s_alert_queue, &item, 0) == pdTRUE) {
        if (s_alerts_enabled) {
            telegram_deliver_alert(&item);
        } else {
            ESP_LOGD(TAG, "Alert dropped while delivery is disabled");
        }
        free(item.text);
    }
}

// Acknowledges every update Telegram kept while the device was offline, so
// the service starts from a clean state.
static void telegram_discard_pending_updates(void) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }
    cJSON_AddNumberToObject(root, "offset", -1);
    cJSON_AddNumberToObject(root, "timeout", 0);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL) {
        return;
    }

    telegram_bot_request_t request = {
        .method = "getUpdates",
        .json_body = body,
        .timeout_ms = 15000,
    };
    telegram_bot_response_t response = {
        .buffer = s_rx_buffer,
        .buffer_size = s_rx_buffer_size,
    };

    telegram_release_tx_connection();

    if (telegram_bot_client_call(s_poll_client, &request, &response) == ESP_OK && !response.truncated) {
        cJSON *answer = cJSON_Parse(s_rx_buffer);
        if (answer != NULL) {
            const cJSON *result = cJSON_GetObjectItemCaseSensitive(answer, "result");
            int count = cJSON_IsArray(result) ? cJSON_GetArraySize(result) : 0;
            if (count > 0) {
                const cJSON *last = cJSON_GetArrayItem(result, count - 1);
                s_next_offset = telegram_json_get_int(last, "update_id") + 1;
                ESP_LOGI(TAG, "Discarded the updates received while offline");
            }
            cJSON_Delete(answer);
        }
    }
    cJSON_free(body);
}

void telegram_task(void *arg) {
    (void)arg;

    s_running = true;
    s_stop_requested = false;
    s_start_time_us = esp_timer_get_time();

    ESP_LOGI(TAG, "Service task started");

    if (s_config.drop_pending_updates) {
        telegram_discard_pending_updates();
    }
    if (s_config.publish_commands) {
        telegram_set_my_commands();
    }
    if (s_config.announce_start) {
        telegram_send_alert(TELEGRAM_ALERT_INFO, "Device online and listening for commands.");
    }

    unsigned cycles = 0;

    while (!s_stop_requested) {
        telegram_drain_alerts();

        esp_err_t err = telegram_poll_once();
        if (err == ESP_OK) {
            if (telegram_lock()) {
                s_stats.poll_errors = 0;
                telegram_unlock();
            }
        } else {
            uint32_t consecutive = 1;
            if (telegram_lock()) {
                s_stats.poll_errors++;
                consecutive = s_stats.poll_errors;
                telegram_unlock();
            }
            ESP_LOGW(TAG, "Polling cycle failed: %s", esp_err_to_name(err));
            // The backoff starts at the second consecutive failure. A single
            // one has already cost the retries inside the transport, several
            // seconds during which Telegram queued whatever arrived, and the
            // callback queries in that queue expire while they wait. Standing
            // still for another five seconds after one hiccup buys nothing
            // and ages the batch that is about to be read; once failures
            // repeat, the network really is down and not hammering it is what
            // matters.
            if (consecutive > 1) {
                vTaskDelay(pdMS_TO_TICKS(TELEGRAM_POLL_BACKOFF_MS));
            }
        }

        telegram_drain_alerts();

        // Stack high-water mark, reported once a minute of polling cycles.
        // This task carries a TLS handshake whenever its connection has to be
        // rebuilt, so its stack is sized for the worst path rather than the
        // common one; reporting what it actually reaches is what lets the
        // figure be trimmed on a memory-constrained device instead of guessed
        // at in either direction.
        if (++cycles >= TELEGRAM_STACK_REPORT_CYCLES) {
            cycles = 0;
            ESP_LOGI(TAG, "Poll task stack high-water %u bytes free of %d", (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)),
                     CONFIG_TELEGRAM_SERVICE_TASK_STACK_SIZE);
        }

        if (s_config.poll_interval_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(s_config.poll_interval_ms));
        }
    }

    ESP_LOGI(TAG, "Service task stopped");
    s_running = false;
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t telegram_start(void) {
    if (!s_initialized || s_running) {
        return ESP_ERR_INVALID_STATE;
    }

    // The running flag is raised before the task exists, so a stop or a
    // release requested right after this call waits for the task instead
    // of tearing the service down under it.
    s_stop_requested = false;
    s_running = true;

    BaseType_t created;
    if (s_config.task_core_id >= 0) {
        created = xTaskCreatePinnedToCore(telegram_task, "telegram_service", (uint32_t)s_config.task_stack_size, NULL, (UBaseType_t)s_config.task_priority,
                                          &s_task_handle, s_config.task_core_id);
    } else {
        created = xTaskCreate(telegram_task, "telegram_service", (uint32_t)s_config.task_stack_size, NULL, (UBaseType_t)s_config.task_priority, &s_task_handle);
    }

    if (created != pdPASS) {
        s_running = false;
        s_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t telegram_stop(void) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_running) {
        return ESP_OK;
    }

    s_stop_requested = true;

    // The task can be blocked in a long poll, so the wait covers the
    // polling timeout plus the transport margin.
    int wait_ms = (s_config.poll_timeout_s + 20) * 1000;
    while (s_running && wait_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        wait_ms -= 100;
    }

    if (s_running) {
        ESP_LOGW(TAG, "Service task did not stop within the expected delay");
    }
    return ESP_OK;
}
