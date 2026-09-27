/**
 * @file esp_log.h
 * @brief Host stand-in for the ESP-IDF logging API.
 *
 * Every log macro discards its arguments. esp_log_level_get() reports
 * ::ESP_LOG_NONE, so code that only builds a log line when the tag's level
 * admits it skips that work entirely on the host.
 */

#pragma once

/** @brief Log verbosity levels, with the same ordering as ESP-IDF. */
typedef enum {
    ESP_LOG_NONE = 0, /**< No output. */
    ESP_LOG_ERROR,    /**< Errors only. */
    ESP_LOG_WARN,     /**< Warnings and errors. */
    ESP_LOG_INFO,     /**< Informational messages. */
    ESP_LOG_DEBUG,    /**< Debug messages. */
    ESP_LOG_VERBOSE,  /**< Everything. */
} esp_log_level_t;

/**
 * @brief Report the current log level of a tag.
 *
 * @param tag Log tag; ignored.
 * @return Always ::ESP_LOG_NONE.
 */
static inline esp_log_level_t esp_log_level_get(const char *tag) {
    (void)tag;
    return ESP_LOG_NONE;
}

/**
 * @brief Swallow a log call.
 *
 * Taking the arguments through a function, rather than dropping them in the
 * macro, keeps them evaluated and type-checked against the format string, so
 * a variable that exists only to be logged is still used.
 *
 * @param tag Log tag; ignored.
 * @param fmt printf-style format; ignored.
 * @param ... Format arguments; ignored.
 */
static inline __attribute__((format(printf, 2, 3))) void esp_log_discard(const char *tag, const char *fmt, ...) {
    (void)tag;
    (void)fmt;
}

/** @brief Error log; discarded on the host. */
#define ESP_LOGE(tag, fmt, ...) esp_log_discard(tag, fmt, ##__VA_ARGS__)
/** @brief Warning log; discarded on the host. */
#define ESP_LOGW(tag, fmt, ...) esp_log_discard(tag, fmt, ##__VA_ARGS__)
/** @brief Info log; discarded on the host. */
#define ESP_LOGI(tag, fmt, ...) esp_log_discard(tag, fmt, ##__VA_ARGS__)
/** @brief Debug log; discarded on the host. */
#define ESP_LOGD(tag, fmt, ...) esp_log_discard(tag, fmt, ##__VA_ARGS__)
/** @brief Verbose log; discarded on the host. */
#define ESP_LOGV(tag, fmt, ...) esp_log_discard(tag, fmt, ##__VA_ARGS__)
