/**
 * @file telegram_service.h
 *
 * @author Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
 * @date 2026
 * @copyright MIT
 * @see https://github.com/hiperiondev/esp32idf_telegram_bot
 *
 * @brief Self-contained Telegram bot service for ESP-IDF applications.
 *
 * This module turns the raw transport offered by the `esp_telegram_bot`
 * component into a complete, ready to embed bot service. It owns a
 * background task that long-polls the Telegram servers, parses every
 * incoming update, enforces an authorization list and dispatches the
 * result to the handlers registered by the application, while offering a
 * thread-safe API for pushing data in the opposite direction.
 *
 * Feature map, in both directions:
 *
 * | Feature                | Device to Telegram | Telegram to device |
 * |------------------------|--------------------|--------------------|
 * | Text messages          | @ref telegram_send_message()          | @ref telegram_register_event_handler() |
 * | Commands               | @ref telegram_set_my_commands()       | @ref telegram_register_command() |
 * | Buttons and menus      | @ref telegram_send_menu()             | reply keyboards arrive as text |
 * | Button callbacks       | @ref telegram_answer_callback()       | @ref telegram_register_callback() |
 * | Photos                 | @ref telegram_send_photo()            | ::TELEGRAM_UPDATE_PHOTO |
 * | Documents              | @ref telegram_send_document()         | ::TELEGRAM_UPDATE_DOCUMENT |
 * | Location               | @ref telegram_send_location()         | ::TELEGRAM_UPDATE_LOCATION |
 * | Sensor telemetry       | @ref telegram_register_sensor()       | built-in `/status`, `/sensors` |
 * | Alarms                 | @ref telegram_send_alert()            | not applicable |
 * | Remote configuration   | @ref telegram_register_param()        | built-in `/config`, `/set`, `/get` |
 * | Multiple users, groups | @ref telegram_broadcast()             | @ref telegram_add_user(), @ref telegram_allow_chat() |
 * | Mini Apps              | @ref telegram_send_web_app()          | ::TELEGRAM_UPDATE_WEB_APP |
 * | Reactions              | @ref telegram_set_message_reaction()  | ::TELEGRAM_UPDATE_REACTION, ::TELEGRAM_UPDATE_REACTION_COUNT |
 *
 * Typical start-up sequence, once the network is up:
 *
 * @code
 * telegram_service_config_t cfg = TELEGRAM_SERVICE_DEFAULT_CONFIG();
 * cfg.bot_token = CONFIG_MY_BOT_TOKEN;
 * cfg.admin_id  = 12345678;
 * ESP_ERROR_CHECK(telegram_init(&cfg));
 * telegram_register_command("led", "Toggle the status LED", false, led_cmd, NULL);
 * telegram_register_sensor("Temperature", "C", read_temperature, NULL);
 * ESP_ERROR_CHECK(telegram_start());
 * @endcode
 *
 * Threading model:
 *
 * - Every handler registered here runs in the context of the service task.
 *   Handlers must not block for long, because no further update is
 *   processed while one is running.
 * - The sending functions are safe to call from any task. They use a
 *   transmit connection that is independent of the polling connection, so
 *   an ongoing long poll never delays them. The two connections take turns
 *   holding a live TLS session rather than holding one each, which is what
 *   keeps a handshake able to find the contiguous heap it needs.
 * - Every function that reaches the Telegram servers performs the TLS
 *   handshake and the transfer on the stack of the calling task. Around
 *   8 kB is the practical minimum for such a task, which rules out the
 *   main task, the event task and the timer task at their default sizes.
 *   @ref telegram_send_alert() is the exception and is what those contexts
 *   should use.
 * - @ref telegram_send_alert() is asynchronous: it only queues the alert
 *   and returns immediately, which makes it usable from timer callbacks
 *   and from handlers themselves.
 */

#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum length of a chat identifier rendered as text. */
#define TELEGRAM_CHAT_ID_LEN 24

/** @brief Maximum length of a stored user or chat display name. */
#define TELEGRAM_NAME_LEN 40

/** @brief Maximum length of a command name, excluding the leading slash. */
#define TELEGRAM_COMMAND_LEN 24

/** @brief Maximum length of the argument string that follows a command. */
#define TELEGRAM_ARGS_LEN 128

/** @brief Maximum length of the incoming message text kept per update. */
#define TELEGRAM_TEXT_LEN 256

/** @brief Maximum length of a Telegram file identifier. */
#define TELEGRAM_FILE_ID_LEN 128

/** @brief Maximum length of an incoming file name or MIME type. */
#define TELEGRAM_FILE_NAME_LEN 64

/** @brief Maximum length of the payload carried by an inline button. */
#define TELEGRAM_CALLBACK_DATA_LEN 48

/** @brief Maximum length of the identifier of a callback query. */
#define TELEGRAM_CALLBACK_ID_LEN 40

/**
 * @brief Maximum length of one reaction emoji.
 *
 * A reaction emoji is a short UTF-8 sequence, at most a base character
 * followed by a variation selector, so this bound holds every emoji
 * Telegram accepts as a reaction.
 */
#define TELEGRAM_REACTION_LEN 16

/** @brief Number of authorized users the service can hold. */
#define TELEGRAM_MAX_USERS CONFIG_TELEGRAM_SERVICE_MAX_USERS

/** @brief Number of group chats the service can hold. */
#define TELEGRAM_MAX_CHATS CONFIG_TELEGRAM_SERVICE_MAX_CHATS

/** @brief Number of commands that can be registered, built-ins included. */
#define TELEGRAM_MAX_COMMANDS CONFIG_TELEGRAM_SERVICE_MAX_COMMANDS

/** @brief Number of inline button callback handlers that can be registered. */
#define TELEGRAM_MAX_CALLBACKS CONFIG_TELEGRAM_SERVICE_MAX_CALLBACKS

/** @brief Number of remotely configurable parameters. */
#define TELEGRAM_MAX_PARAMS CONFIG_TELEGRAM_SERVICE_MAX_PARAMS

/** @brief Number of sensors that can be published as telemetry. */
#define TELEGRAM_MAX_SENSORS CONFIG_TELEGRAM_SERVICE_MAX_SENSORS

/**
 * @brief Nature of an incoming update.
 *
 * The two reaction kinds only reach the device when
 * ::telegram_service_config_t::receive_reactions is set, because Telegram
 * excludes them from the default set of updates it delivers, and only when
 * the bot is an administrator of the chat the reaction was placed in. A
 * comment under a channel post belongs to the linked discussion group, so
 * reactions on comments carry the identifier of that group rather than the
 * one of the channel.
 *
 * Which of the two kinds arrives is decided by Telegram, not by the
 * device: chats that keep the identity of the reacting users visible send
 * ::TELEGRAM_UPDATE_REACTION, while the ones that hide it send the
 * aggregated ::TELEGRAM_UPDATE_REACTION_COUNT instead.
 */
typedef enum {
    TELEGRAM_UPDATE_UNKNOWN = 0,    /**< Update of a kind this service does not decode. */
    TELEGRAM_UPDATE_TEXT,           /**< Plain text message. */
    TELEGRAM_UPDATE_COMMAND,        /**< Text message starting with a slash. */
    TELEGRAM_UPDATE_CALLBACK,       /**< Inline keyboard button press. */
    TELEGRAM_UPDATE_PHOTO,          /**< Photo, with the largest size selected. */
    TELEGRAM_UPDATE_DOCUMENT,       /**< Document, audio or video file. */
    TELEGRAM_UPDATE_LOCATION,       /**< Shared GPS position. */
    TELEGRAM_UPDATE_CONTACT,        /**< Shared contact card. */
    TELEGRAM_UPDATE_WEB_APP,        /**< Data posted by a Mini App button. */
    TELEGRAM_UPDATE_REACTION,       /**< A user added, changed or withdrew a reaction on a message. */
    TELEGRAM_UPDATE_REACTION_COUNT, /**< Anonymous reaction totals of a message changed. */
} telegram_update_type_t;

/**
 * @brief Severity attached to an alert.
 *
 * The severity selects the prefix prepended to the alert text and decides
 * who receives it: informational and warning alerts reach every authorized
 * user, while alarm and critical alerts additionally reach the allowed
 * group chats.
 */
typedef enum {
    TELEGRAM_ALERT_INFO = 0, /**< Routine notification. */
    TELEGRAM_ALERT_WARNING,  /**< Condition worth watching. */
    TELEGRAM_ALERT_ALARM,    /**< Threshold crossed, action expected. */
    TELEGRAM_ALERT_CRITICAL, /**< Failure requiring immediate attention. */
} telegram_alert_level_t;

/**
 * @brief Text formatting applied by Telegram to an outgoing message.
 */
typedef enum {
    TELEGRAM_PARSE_NONE = 0, /**< Deliver the text verbatim. */
    TELEGRAM_PARSE_MARKDOWN, /**< Interpret MarkdownV2 markup. */
    TELEGRAM_PARSE_HTML,     /**< Interpret the supported HTML subset. */
} telegram_parse_mode_t;

/**
 * @brief Data type of a remotely configurable parameter.
 */
typedef enum {
    TELEGRAM_PARAM_INT = 0, /**< Storage is an int32_t. */
    TELEGRAM_PARAM_FLOAT,   /**< Storage is a float. */
    TELEGRAM_PARAM_BOOL,    /**< Storage is a bool. */
    TELEGRAM_PARAM_STRING,  /**< Storage is a char array. */
} telegram_param_type_t;

/**
 * @brief Decoded incoming update handed to the application handlers.
 *
 * String fields are always null-terminated and are empty when the incoming
 * update does not carry the corresponding information.
 *
 * The three reaction fields are filled for ::TELEGRAM_UPDATE_REACTION and
 * ::TELEGRAM_UPDATE_REACTION_COUNT only, and hold slightly different
 * things in each case. For a per user update, @c reaction is the first
 * emoji the sender now holds on the message and @c reaction_previous the
 * first one they held before, so an empty @c reaction next to a filled
 * @c reaction_previous is a withdrawal; @c reaction_count is how many
 * reactions that one sender now holds. For an aggregated update there is
 * no sender: @c reaction is the most used emoji of the message,
 * @c reaction_count how many users have set it, and @c reaction_previous
 * stays empty. A custom or paid reaction carries no printable emoji, so it
 * is counted but leaves @c reaction empty.
 *
 * An aggregated update names nobody, so @c user_id, @c user_name,
 * @c first_name, @c authorized and @c is_admin are all left at zero for
 * it: the allowed chat list is the only gate it went through.
 */
typedef struct {
    telegram_update_type_t type;                    /**< Kind of update. */
    int64_t update_id;                              /**< Sequence number assigned by Telegram. */
    int64_t chat_id;                                /**< Numeric identifier of the originating chat. */
    char chat_id_str[TELEGRAM_CHAT_ID_LEN];         /**< Same identifier rendered as text, ready to be passed to the sending functions. */
    bool is_group;                                  /**< True when the chat is a group, supergroup or channel. */
    int64_t user_id;                                /**< Numeric identifier of the sender. */
    char user_name[TELEGRAM_NAME_LEN];              /**< Telegram username of the sender, without the leading at sign. */
    char first_name[TELEGRAM_NAME_LEN];             /**< Given name of the sender. */
    int32_t message_id;                             /**< Identifier of the message inside its chat. */
    bool authorized;                                /**< True when the sender passed @ref telegram_is_authorized(). */
    bool is_admin;                                  /**< True when the sender holds administrator rights. */
    char text[TELEGRAM_TEXT_LEN];                   /**< Message text, caption, or Mini App payload. */
    char command[TELEGRAM_COMMAND_LEN];             /**< Command name without the leading slash and without the bot suffix. */
    char args[TELEGRAM_ARGS_LEN];                   /**< Everything that follows the command name. */
    char callback_id[TELEGRAM_CALLBACK_ID_LEN];     /**< Identifier a callback query must be answered with. */
    char callback_data[TELEGRAM_CALLBACK_DATA_LEN]; /**< Payload attached to the pressed inline button. */
    char file_id[TELEGRAM_FILE_ID_LEN];             /**< Identifier of the attached photo or document. */
    char file_name[TELEGRAM_FILE_NAME_LEN];         /**< Original name of the attached document. */
    char mime_type[TELEGRAM_FILE_NAME_LEN];         /**< MIME type reported for the attachment. */
    int file_size;                                  /**< Size of the attachment in bytes, or 0 when unknown. */
    double latitude;                                /**< Latitude in degrees for a shared location. */
    double longitude;                               /**< Longitude in degrees for a shared location. */
    char reaction[TELEGRAM_REACTION_LEN];           /**< Emoji now standing on the message, empty when none does. */
    char reaction_previous[TELEGRAM_REACTION_LEN];  /**< Emoji the same sender held before the change, empty when there was none. */
    int reaction_count;                             /**< Reactions the sender now holds, or users having set ::reaction for a count update. */
} telegram_update_t;

/**
 * @brief One button of an inline keyboard.
 *
 * Exactly one action field should be filled in. A button with @c data set
 * sends a callback query back to the device; a button with @c url set
 * opens a web page; a button with @c web_app_url set opens a Telegram Mini
 * App. When several are set, @c data takes precedence, then @c web_app_url.
 */
typedef struct {
    const char *label;       /**< Text displayed on the button. */
    const char *data;        /**< Callback payload, at most ::TELEGRAM_CALLBACK_DATA_LEN characters. */
    const char *url;         /**< Web page opened by the button, or NULL. */
    const char *web_app_url; /**< HTTPS address of the Mini App opened by the button, or NULL. */
} telegram_button_t;

/**
 * @brief Extra options accepted when sending a message.
 */
typedef struct {
    telegram_parse_mode_t parse_mode; /**< Markup interpretation of the text. */
    int32_t reply_to_message_id;      /**< Message this one replies to, or 0. */
    bool disable_notification;        /**< True to deliver the message silently. */
    const char *reply_markup;         /**< Raw JSON keyboard description attached to the message, or NULL. */
} telegram_send_options_t;

/**
 * @brief Runtime counters exposed by @ref telegram_get_stats().
 */
typedef struct {
    uint32_t updates_received; /**< Updates decoded since the service started. */
    uint32_t commands_handled; /**< Commands successfully dispatched. */
    uint32_t messages_sent;    /**< Outgoing messages accepted by Telegram. */
    uint32_t alerts_sent;      /**< Alerts delivered from the alert queue. */
    uint32_t poll_errors;      /**< Consecutive failed polling cycles. */
    uint32_t rejected;         /**< Updates dropped because the sender was not authorized. */
    int64_t uptime_seconds;    /**< Seconds elapsed since @ref telegram_start(). */
} telegram_stats_t;

/**
 * @brief Handler invoked when a registered command is received.
 *
 * @param[in] update Decoded update that carried the command.
 * @param[in] args   Argument string that followed the command name; empty
 *                   when the command was sent alone.
 * @param[in] ctx    Opaque pointer supplied at registration time.
 *
 * @return ESP_OK when the command was handled. Any other value makes the
 *         service reply to the sender with a generic failure notice.
 */
typedef esp_err_t (*telegram_command_cb_t)(const telegram_update_t *update, const char *args, void *ctx);

/**
 * @brief Handler invoked when an inline button is pressed.
 *
 * The service answers the callback query automatically after the handler
 * returns, so the button stops showing its progress indicator. Call
 * @ref telegram_answer_callback() from the handler to display a custom
 * notice instead.
 *
 * @param[in] update Decoded update that carried the callback query.
 * @param[in] data   Payload attached to the pressed button.
 * @param[in] ctx    Opaque pointer supplied at registration time.
 *
 * @return ESP_OK when the press was handled.
 */
typedef esp_err_t (*telegram_callback_cb_t)(const telegram_update_t *update, const char *data, void *ctx);

/**
 * @brief Handler invoked for every update that is not a known command.
 *
 * This is where plain text, photos, documents, locations, contacts, Mini
 * App payloads and reactions are received.
 *
 * @param[in] update Decoded update.
 * @param[in] ctx    Opaque pointer supplied at registration time.
 *
 * @return ESP_OK when the update was handled.
 */
typedef esp_err_t (*telegram_event_cb_t)(const telegram_update_t *update, void *ctx);

/**
 * @brief Callback reading the current value of a registered sensor.
 *
 * @param[out] out_value Receives the reading.
 * @param[in]  ctx       Opaque pointer supplied at registration time.
 *
 * @return ESP_OK when @p out_value holds a valid reading. Any other value
 *         makes the service report the sensor as unavailable.
 */
typedef esp_err_t (*telegram_sensor_read_cb_t)(float *out_value, void *ctx);

/**
 * @brief Callback appending application specific lines to `/status`.
 *
 * @param[out] buffer Destination buffer to write a null-terminated string into.
 * @param[in]  size   Capacity of @p buffer in bytes.
 * @param[in]  ctx    Opaque pointer supplied in the service configuration.
 */
typedef void (*telegram_status_cb_t)(char *buffer, size_t size, void *ctx);

/**
 * @brief Callback appending application specific lines to `/sensors`.
 *
 * The service publishes the sensors registered through
 * @ref telegram_register_sensor(), which are a static table of name, unit and
 * read callback. An application whose set of readings is decided by the
 * operator at run time - a mapping that changes whenever a configuration page
 * is saved - describes it here instead, so every report is built from what is
 * configured at the moment the command arrives.
 *
 * Lines written here are appended after the registered sensors, and a report
 * that carries only these lines is still a report: the service answers "no
 * sensor" only when neither source produced anything.
 *
 * @param[out] buffer Destination buffer to write a null-terminated string into.
 * @param[in]  size   Capacity of @p buffer in bytes.
 * @param[in]  ctx    Opaque pointer supplied in the service configuration.
 */
typedef void (*telegram_sensors_cb_t)(char *buffer, size_t size, void *ctx);

/**
 * @brief Configuration of the Telegram service.
 *
 * Initialise it with ::TELEGRAM_SERVICE_DEFAULT_CONFIG and override only
 * what the application needs.
 *
 * ::receive_reactions deserves a word: Telegram keeps reaction updates out
 * of the set it delivers by default, and only sends them to a bot that is
 * an administrator of the chat the reaction was placed in. Setting the
 * flag is therefore necessary but not sufficient, and costs nothing where
 * the second condition is not met, the updates simply never arrive.
 */
typedef struct {
    const char *bot_token;            /**< Token issued by @BotFather. Required. */
    const char *device_name;          /**< Name used in greetings, status reports and alerts. */
    int64_t admin_id;                 /**< Identifier of the first administrator, or 0 to add none here. */
    bool open_access;                 /**< True to accept every sender. Bring-up only; the only way in for a sender the list does not name. */
    bool register_builtin_commands;   /**< True to register the built-in command set at initialization. */
    bool publish_commands;            /**< True to push the command list to Telegram when the service starts, so clients show it in the command menu. */
    bool announce_start;              /**< True to send a start-up notice to the administrators. */
    bool drop_pending_updates;        /**< True to discard the updates that accumulated while the device was offline. */
    bool allow_reboot;                /**< True to enable the built-in `/reboot` command. */
    bool receive_reactions;           /**< True to ask Telegram for reaction updates. */
    int poll_timeout_s;               /**< Long polling timeout in seconds passed to `getUpdates`. */
    int poll_interval_ms;             /**< Pause between two polling cycles. */
    int rx_buffer_size;               /**< Size of the buffer holding one `getUpdates` answer. */
    int task_stack_size;              /**< Stack size of the service task in bytes. */
    int task_priority;                /**< FreeRTOS priority of the service task. */
    int task_core_id;                 /**< Core the service task is pinned to, or -1 for no affinity. */
    int alert_queue_len;              /**< Number of alerts that can wait in the queue. */
    telegram_status_cb_t status_cb;   /**< Optional callback appending lines to `/status`. */
    void *status_ctx;                 /**< Opaque pointer forwarded to @c status_cb. */
    telegram_sensors_cb_t sensors_cb; /**< Optional callback appending lines to `/sensors`. */
    void *sensors_ctx;                /**< Opaque pointer forwarded to @c sensors_cb. */
} telegram_service_config_t;

/**
 * @brief Configuration initialiser holding sensible defaults.
 *
 * Only ::telegram_service_config_t::bot_token has to be filled in
 * afterwards for the service to be usable.
 */
#define TELEGRAM_SERVICE_DEFAULT_CONFIG()                                                                                                                      \
    (telegram_service_config_t) {                                                                                                                              \
        .bot_token = NULL, .device_name = "ESP32", .admin_id = 0, .open_access = false, .register_builtin_commands = true, .publish_commands = true,           \
        .announce_start = true, .drop_pending_updates = true, .allow_reboot = true, .receive_reactions = false, .poll_timeout_s = 10, .poll_interval_ms = 500, \
        .rx_buffer_size = CONFIG_TELEGRAM_SERVICE_RX_BUFFER_SIZE, .task_stack_size = CONFIG_TELEGRAM_SERVICE_TASK_STACK_SIZE, .task_priority = 5,              \
        .task_core_id = -1, .alert_queue_len = 8, .status_cb = NULL, .status_ctx = NULL, .sensors_cb = NULL, .sensors_ctx = NULL,                              \
    }

/**
 * @name Lifecycle
 * Creating, starting, stopping and releasing the service.
 * @{
 */

/**
 * @brief Initialize the service and its transport connections.
 *
 * Validates the configuration, initializes the `esp_telegram_bot`
 * transport with the supplied token, creates the polling and transmitting
 * connections, the alert queue and the internal mutex, seeds the
 * authorization list with ::telegram_service_config_t::admin_id and, when
 * requested, registers the built-in commands.
 *
 * The network must already be up, because the transport resolves and
 * connects lazily but the optional pending update purge performed by
 * @ref telegram_start() needs connectivity.
 *
 * @param[in] config Service configuration. Must not be NULL and must carry
 *                   a non-empty bot token.
 *
 * @return
 *  - ESP_OK on success.
 *  - ESP_ERR_INVALID_ARG if @p config or its token is missing.
 *  - ESP_ERR_INVALID_STATE if the service is already initialized.
 *  - ESP_ERR_NO_MEM if a buffer, the queue or the mutex cannot be created.
 *  - Any error propagated by the transport initialization.
 */
esp_err_t telegram_init(const telegram_service_config_t *config);

/**
 * @brief Stop the service, if running, and release every resource.
 *
 * @return
 *  - ESP_OK on success.
 *  - ESP_ERR_INVALID_STATE if the service was not initialized.
 */
esp_err_t telegram_deinit(void);

/**
 * @brief Create the background task that polls for incoming updates.
 *
 * The task runs @ref telegram_task(). Applications that need to own the
 * task creation themselves can skip this function and pass
 * @ref telegram_task to `xTaskCreate()` directly.
 *
 * @return
 *  - ESP_OK on success.
 *  - ESP_ERR_INVALID_STATE if the service is not initialized or is already
 *    running.
 *  - ESP_ERR_NO_MEM if the task cannot be created.
 */
esp_err_t telegram_start(void);

/**
 * @brief Ask the service task to finish and wait until it has stopped.
 *
 * The call blocks until the current polling cycle completes, which can
 * take up to ::telegram_service_config_t::poll_timeout_s seconds.
 *
 * @return
 *  - ESP_OK once the task has terminated, or if it was not running.
 *  - ESP_ERR_INVALID_STATE if the service is not initialized.
 */
esp_err_t telegram_stop(void);

/**
 * @brief Body of the service task.
 *
 * Polls the Telegram servers with `getUpdates`, decodes each update,
 * enforces authorization, dispatches commands, button callbacks and
 * events, and drains the alert queue between two polling cycles. The task
 * deletes itself when @ref telegram_stop() is called.
 *
 * @param[in] arg Unused; present to match the FreeRTOS task signature.
 */
void telegram_task(void *arg);

/**
 * @brief Report whether the service task is currently polling.
 *
 * @return true when the service task is running.
 */
bool telegram_is_running(void);

/** @} */

/**
 * @name Users, groups and authorization
 * Deciding who is allowed to interact with the bot.
 * @{
 */

/**
 * @brief Add a user to the authorization list.
 *
 * Calling it again with an identifier already present updates the stored
 * name and administrator flag instead of creating a duplicate.
 *
 * @param[in] user_id  Numeric Telegram identifier of the user.
 * @param[in] name     Display name kept for `/users`, or NULL.
 * @param[in] is_admin True to grant administrator rights, which unlock the
 *                     commands registered as administrator-only.
 *
 * @return
 *  - ESP_OK on success.
 *  - ESP_ERR_INVALID_ARG if @p user_id is zero.
 *  - ESP_ERR_INVALID_STATE if the service is not initialized.
 *  - ESP_ERR_NO_MEM if the list already holds ::TELEGRAM_MAX_USERS entries.
 */
esp_err_t telegram_add_user(int64_t user_id, const char *name, bool is_admin);

/**
 * @brief Remove a user from the authorization list.
 *
 * @param[in] user_id Numeric Telegram identifier of the user.
 *
 * @return
 *  - ESP_OK on success.
 *  - ESP_ERR_NOT_FOUND if the user is not in the list.
 *  - ESP_ERR_INVALID_STATE if the service is not initialized.
 */
esp_err_t telegram_remove_user(int64_t user_id);

/**
 * @brief Report whether a user may interact with the bot.
 *
 * A user is authorized when the identifier is present in the authorization
 * list, or when the service runs in open access mode
 * (::telegram_service_config_t::open_access).
 *
 * An empty authorization list denies everyone. Opening the bot to unlisted
 * senders is never implicit: it takes the flag above, so a device that has a
 * token but no administrator yet answers nobody instead of answering anyone.
 * The operator still learns their own identifier without being let in,
 * because a private command from an unlisted sender is refused with a reply
 * that carries that sender's identifier.
 *
 * @param[in] user_id Numeric Telegram identifier of the user.
 *
 * @return true when the user is allowed to issue commands.
 */
bool telegram_is_authorized(int64_t user_id);

/**
 * @brief Report whether a user holds administrator rights.
 *
 * @param[in] user_id Numeric Telegram identifier of the user.
 *
 * @return true when the user is registered as an administrator.
 */
bool telegram_is_admin(int64_t user_id);

/**
 * @brief Allow a group or channel chat to be served by the bot.
 *
 * Updates coming from a group are accepted when the group is on this list
 * and the sender is an authorized user.
 *
 * @param[in] chat_id Numeric identifier of the group chat, normally a
 *                    negative number.
 * @param[in] name    Display name kept for `/users`, or NULL.
 *
 * @return
 *  - ESP_OK on success.
 *  - ESP_ERR_INVALID_ARG if @p chat_id is zero.
 *  - ESP_ERR_INVALID_STATE if the service is not initialized.
 *  - ESP_ERR_NO_MEM if the list already holds ::TELEGRAM_MAX_CHATS entries.
 */
esp_err_t telegram_allow_chat(int64_t chat_id, const char *name);

/**
 * @brief Report whether a group chat is served by the bot.
 *
 * @param[in] chat_id Numeric identifier of the chat.
 *
 * @return true when the chat is allowed, or when open access is enabled.
 */
bool telegram_is_chat_allowed(int64_t chat_id);

/**
 * @brief Count the users currently held in the authorization list.
 *
 * @return Number of registered users.
 */
size_t telegram_user_count(void);

/** @} */

/**
 * @name Outgoing messages
 * Pushing text, alerts and notifications towards the operators.
 * @{
 */

/**
 * @brief Send a plain text message to a chat.
 *
 * @param[in] chat_id Destination chat as text, for example "12345678",
 *                    "-1001234567890" or "@channel_name".
 * @param[in] text    Message body, at most 4096 characters.
 *
 * @return
 *  - ESP_OK when Telegram accepted the message.
 *  - ESP_ERR_INVALID_ARG on NULL arguments.
 *  - ESP_ERR_INVALID_STATE if the service is not initialized.
 *  - ESP_FAIL if the API answered with an error.
 */
esp_err_t telegram_send_message(const char *chat_id, const char *text);

/**
 * @brief Send a text message built from a printf style format string.
 *
 * @param[in] chat_id Destination chat as text.
 * @param[in] format  printf style format string.
 * @param[in] ...     Arguments consumed by @p format.
 *
 * @return Same values as @ref telegram_send_message().
 */
esp_err_t telegram_send_message_fmt(const char *chat_id, const char *format, ...) __attribute__((format(printf, 2, 3)));

/**
 * @brief Send a text message with formatting, reply and keyboard options.
 *
 * @param[in] chat_id Destination chat as text.
 * @param[in] text    Message body.
 * @param[in] options Extra options, or NULL to send a plain message.
 *
 * @return Same values as @ref telegram_send_message().
 */
esp_err_t telegram_send_message_ex(const char *chat_id, const char *text, const telegram_send_options_t *options);

/**
 * @brief Replace the text of a message already delivered.
 *
 * Mostly used to refresh a menu in place after one of its inline buttons
 * was pressed.
 *
 * @param[in] chat_id      Chat holding the message, as text.
 * @param[in] message_id   Identifier of the message to edit.
 * @param[in] text         New message body.
 * @param[in] reply_markup Raw JSON keyboard to keep or replace, or NULL to
 *                         drop the keyboard.
 *
 * @return Same values as @ref telegram_send_message().
 */
esp_err_t telegram_edit_message(const char *chat_id, int32_t message_id, const char *text, const char *reply_markup);

/**
 * @brief Send the same text to every authorized user.
 *
 * Delivery failures on individual recipients are logged and do not stop
 * the broadcast.
 *
 * @param[in] text Message body.
 *
 * @return
 *  - ESP_OK if at least one recipient accepted the message.
 *  - ESP_ERR_INVALID_ARG if @p text is NULL.
 *  - ESP_ERR_INVALID_STATE if the service is not initialized.
 *  - ESP_ERR_NOT_FOUND if the authorization list is empty.
 */
esp_err_t telegram_broadcast(const char *text);

/**
 * @brief Queue an alert for delivery to the operators.
 *
 * The call formats the text, prefixes it with a marker matching @p level
 * and returns as soon as the alert is queued, without touching the
 * network. The service task performs the actual delivery, so this function
 * is safe to call from contexts that must not block, including other
 * handlers of this service.
 *
 * Informational and warning alerts are delivered to every authorized user;
 * alarm and critical alerts are also delivered to the allowed group chats.
 *
 * @param[in] level  Severity of the alert.
 * @param[in] format printf style format string.
 * @param[in] ...    Arguments consumed by @p format.
 *
 * @return
 *  - ESP_OK when the alert was queued.
 *  - ESP_ERR_INVALID_ARG if @p format is NULL.
 *  - ESP_ERR_INVALID_STATE if the service is not initialized.
 *  - ESP_ERR_NO_MEM if the alert cannot be allocated.
 *  - ESP_ERR_TIMEOUT if the alert queue is full.
 */
esp_err_t telegram_send_alert(telegram_alert_level_t level, const char *format, ...) __attribute__((format(printf, 2, 3)));

/**
 * @brief Enable or disable the delivery of queued alerts.
 *
 * Alerts queued while delivery is disabled are discarded when the service
 * task reaches them, which keeps the queue from filling up during a
 * maintenance window.
 *
 * @param[in] enabled True to deliver alerts, false to suppress them.
 */
void telegram_set_alerts_enabled(bool enabled);

/**
 * @brief Report whether alert delivery is currently enabled.
 *
 * @return true when queued alerts are delivered.
 */
bool telegram_alerts_enabled(void);

/**
 * @brief Show the typing indicator in a chat.
 *
 * Useful before a command whose handler takes a noticeable time to answer.
 *
 * @param[in] chat_id Destination chat as text.
 *
 * @return Same values as @ref telegram_send_message().
 */
esp_err_t telegram_send_typing(const char *chat_id);

/** @} */

/**
 * @name Media, location and files
 * Uploading pictures and documents, sharing a position and retrieving attachments.
 * @{
 */

/**
 * @brief Upload a photo held in RAM.
 *
 * The buffer must contain an encoded image, typically the JPEG frame
 * produced by a camera driver. Telegram limits photo uploads to 10 MB, and
 * the practical limit on the device is the amount of contiguous memory the
 * frame occupies.
 *
 * @param[in] chat_id   Destination chat as text.
 * @param[in] caption   Caption placed under the photo, or NULL.
 * @param[in] data      Encoded image bytes.
 * @param[in] len       Number of bytes in @p data.
 * @param[in] file_name File name announced to Telegram, or NULL to use a
 *                      default of "photo.jpg".
 *
 * @return
 *  - ESP_OK when Telegram accepted the upload.
 *  - ESP_ERR_INVALID_ARG on NULL arguments or zero length.
 *  - ESP_ERR_INVALID_STATE if the service is not initialized.
 *  - ESP_FAIL if the API answered with an error.
 */
esp_err_t telegram_send_photo(const char *chat_id, const char *caption, const uint8_t *data, size_t len, const char *file_name);

/**
 * @brief Send a photo that Telegram already hosts, or one available online.
 *
 * @param[in] chat_id           Destination chat as text.
 * @param[in] caption           Caption placed under the photo, or NULL.
 * @param[in] url_or_file_id    Public HTTPS address of the image, or a file
 *                              identifier returned by a previous upload.
 *
 * @return Same values as @ref telegram_send_photo().
 */
esp_err_t telegram_send_photo_url(const char *chat_id, const char *caption, const char *url_or_file_id);

/**
 * @brief Upload a document held in RAM.
 *
 * Typical uses are shipping a log file, a configuration dump or a captured
 * data set to the operator.
 *
 * @param[in] chat_id   Destination chat as text.
 * @param[in] caption   Caption placed under the document, or NULL.
 * @param[in] file_name File name announced to Telegram, or NULL to use a
 *                      default of "file.bin".
 * @param[in] mime_type MIME type of the payload, or NULL to announce
 *                      "application/octet-stream".
 * @param[in] data      Payload bytes.
 * @param[in] len       Number of bytes in @p data.
 *
 * @return Same values as @ref telegram_send_photo().
 */
esp_err_t telegram_send_document(const char *chat_id, const char *caption, const char *file_name, const char *mime_type, const uint8_t *data, size_t len);

/**
 * @brief Send a geographic position to a chat.
 *
 * @param[in] chat_id   Destination chat as text.
 * @param[in] latitude  Latitude in degrees.
 * @param[in] longitude Longitude in degrees.
 *
 * @return Same values as @ref telegram_send_message().
 */
esp_err_t telegram_send_location(const char *chat_id, double latitude, double longitude);

/**
 * @brief Retrieve into RAM a file attached to an incoming update.
 *
 * Resolves the file identifier with the `getFile` method and downloads the
 * resulting path from the Telegram file server. Telegram only allows
 * downloading files of up to 20 MB, and the caller is responsible for
 * providing a buffer large enough for the payload; the size announced in
 * ::telegram_update_t::file_size can be used to size it.
 *
 * @param[in]  file_id      Identifier taken from an incoming update.
 * @param[out] out_data     Buffer receiving the payload.
 * @param[in]  out_capacity Capacity of @p out_data in bytes.
 * @param[out] out_len      Receives the number of bytes stored, or NULL.
 *
 * @return
 *  - ESP_OK when the whole file was stored.
 *  - ESP_ERR_INVALID_ARG on NULL arguments or zero capacity.
 *  - ESP_ERR_INVALID_STATE if the service is not initialized.
 *  - ESP_ERR_NOT_FOUND if the API did not return a path for the file.
 *  - ESP_ERR_INVALID_SIZE if the file does not fit in @p out_data.
 *  - Any error propagated by the transport otherwise.
 */
esp_err_t telegram_download_file(const char *file_id, uint8_t *out_data, size_t out_capacity, size_t *out_len);

/** @} */

/**
 * @name Buttons, menus and Mini Apps
 * Publishing the interactive interface and answering it.
 * @{
 */

/**
 * @brief Send a message carrying an inline keyboard.
 *
 * Buttons holding a callback payload deliver a
 * ::TELEGRAM_UPDATE_CALLBACK update when pressed, which is routed to the
 * handlers registered with @ref telegram_register_callback().
 *
 * @param[in] chat_id      Destination chat as text.
 * @param[in] text         Message shown above the keyboard.
 * @param[in] buttons      Array of button descriptions.
 * @param[in] button_count Number of entries in @p buttons.
 * @param[in] columns      Buttons per row; values below 1 are treated as 1.
 *
 * @return
 *  - ESP_OK when Telegram accepted the message.
 *  - ESP_ERR_INVALID_ARG on NULL arguments or an empty button array.
 *  - ESP_ERR_INVALID_STATE if the service is not initialized.
 *  - ESP_ERR_NO_MEM if the keyboard description cannot be built.
 *  - ESP_FAIL if the API answered with an error.
 */
esp_err_t telegram_send_menu(const char *chat_id, const char *text, const telegram_button_t *buttons, size_t button_count, int columns);

/**
 * @brief Send a message carrying a reply keyboard.
 *
 * A reply keyboard replaces the on-screen keyboard of the client with a
 * grid of labels. Pressing one sends its label as an ordinary text
 * message, so labels that match a registered command name reach the
 * command handlers directly.
 *
 * @param[in] chat_id     Destination chat as text.
 * @param[in] text        Message shown above the keyboard.
 * @param[in] labels      Array of button labels.
 * @param[in] label_count Number of entries in @p labels.
 * @param[in] columns     Buttons per row; values below 1 are treated as 1.
 * @param[in] one_time    True to hide the keyboard after one press.
 *
 * @return Same values as @ref telegram_send_menu().
 */
esp_err_t telegram_send_keyboard(const char *chat_id, const char *text, const char *const *labels, size_t label_count, int columns, bool one_time);

/**
 * @brief Remove the reply keyboard previously shown in a chat.
 *
 * @param[in] chat_id Destination chat as text.
 * @param[in] text    Message delivered together with the removal.
 *
 * @return Same values as @ref telegram_send_message().
 */
esp_err_t telegram_remove_keyboard(const char *chat_id, const char *text);

/**
 * @brief Send a button that opens a Telegram Mini App.
 *
 * A Mini App is a web page hosted outside the device and displayed inside
 * the Telegram client; the device only publishes the entry point, since
 * Telegram requires the page to be served over HTTPS from a public
 * address.
 *
 * The button is placed on the reply keyboard, which is what lets the page
 * call `Telegram.WebApp.sendData()`. Whatever it sends comes back as an
 * ordinary update of type ::TELEGRAM_UPDATE_WEB_APP, with the payload in
 * ::telegram_update_t::text, so a Mini App drives the device through the
 * same path as any other message and needs no backend of its own. A Mini
 * App opened from an inline button cannot do this, which is why the reply
 * keyboard is used here.
 *
 * @param[in] chat_id      Destination chat as text.
 * @param[in] text         Message shown above the button.
 * @param[in] button_label Text displayed on the button.
 * @param[in] web_app_url  HTTPS address of the Mini App page.
 *
 * @return Same values as @ref telegram_send_menu().
 */
esp_err_t telegram_send_web_app(const char *chat_id, const char *text, const char *button_label, const char *web_app_url);

/**
 * @brief Answer a callback query.
 *
 * Every callback query must be answered so the client stops showing the
 * progress indicator on the pressed button. The service answers a query it
 * dispatches itself, before the handler runs, because Telegram invalidates
 * a query within seconds and a handler on this device can easily spend them
 * building and sending its reply. A handler therefore has nothing to answer
 * and should surface a notice with @ref telegram_send_message() instead:
 * Telegram refuses a second answer for the same query.
 *
 * What remains for this function is a query an application obtained outside
 * the service dispatch path and is answering on its own.
 *
 * @param[in] callback_id Identifier taken from
 *                        ::telegram_update_t::callback_id.
 * @param[in] text        Notice shown to the user, or NULL for none.
 * @param[in] show_alert  True to display the notice as a modal dialog
 *                        instead of a transient toast.
 *
 * @return Same values as @ref telegram_send_message().
 */
esp_err_t telegram_answer_callback(const char *callback_id, const char *text, bool show_alert);

/**
 * @brief Set or withdraw the reaction of the bot on a message.
 *
 * A reaction is the cheapest acknowledgement available: it marks a message
 * the device has processed without adding anything to the conversation.
 * The bot may react to any message it can see, and needs no administrator
 * rights in the chat to react to one of its own.
 *
 * Telegram accepts a single reaction per message and per sender, so a new
 * emoji replaces the previous one instead of being added next to it.
 *
 * Only the emoji from the set Telegram publishes for reactions are
 * accepted; anything else is refused by the server and reported as
 * ESP_FAIL. Custom and paid reactions are not available to bots.
 *
 * Like every other function reaching the Telegram servers, this one opens
 * its TLS connection on the stack of the calling task and counts towards
 * ::telegram_stats_t::messages_sent when it succeeds.
 *
 * @param[in] chat_id    Destination chat as text, as carried by
 *                       ::telegram_update_t::chat_id_str.
 * @param[in] message_id Message the reaction is placed on, as carried by
 *                       ::telegram_update_t::message_id.
 * @param[in] emoji      Reaction to set, for example "\U0001F44D". Pass
 *                       NULL or an empty string to withdraw the reaction
 *                       the bot currently holds.
 * @param[in] is_big     True to play the large animation on the clients.
 *
 * @return Same values as @ref telegram_send_message().
 */
esp_err_t telegram_set_message_reaction(const char *chat_id, int32_t message_id, const char *emoji, bool is_big);

/** @} */

/**
 * @name Commands and handlers
 * Registering what the device reacts to, and dispatching it.
 * @{
 */

/**
 * @brief Register a command handler.
 *
 * The name is matched case-insensitively, with or without the leading
 * slash and with the `@botname` suffix that Telegram appends in groups
 * removed. Registering an existing name replaces its handler.
 *
 * @param[in] name        Command name, with or without the leading slash.
 * @param[in] description One line description shown by `/help` and in the
 *                        command menu of the clients.
 * @param[in] admin_only  True to restrict the command to administrators.
 * @param[in] handler     Function invoked when the command is received.
 * @param[in] ctx         Opaque pointer forwarded to @p handler.
 *
 * @return
 *  - ESP_OK on success.
 *  - ESP_ERR_INVALID_ARG if @p name or @p handler is NULL, or if the name
 *    is empty.
 *  - ESP_ERR_INVALID_STATE if the service is not initialized.
 *  - ESP_ERR_NO_MEM if the table already holds ::TELEGRAM_MAX_COMMANDS
 *    entries.
 */
esp_err_t telegram_register_command(const char *name, const char *description, bool admin_only, telegram_command_cb_t handler, void *ctx);

/**
 * @brief Register a handler for inline button presses.
 *
 * The handler is selected by matching @p prefix against the beginning of
 * the payload carried by the pressed button, which allows one handler to
 * serve a whole family of buttons such as "led:on" and "led:off". An empty
 * prefix matches every payload and acts as a catch-all.
 *
 * @param[in] prefix  Leading portion of the payloads to route here.
 * @param[in] handler Function invoked when a matching button is pressed.
 * @param[in] ctx     Opaque pointer forwarded to @p handler.
 *
 * @return
 *  - ESP_OK on success.
 *  - ESP_ERR_INVALID_ARG if @p prefix or @p handler is NULL.
 *  - ESP_ERR_INVALID_STATE if the service is not initialized.
 *  - ESP_ERR_NO_MEM if the table already holds ::TELEGRAM_MAX_CALLBACKS
 *    entries.
 */
esp_err_t telegram_register_callback(const char *prefix, telegram_callback_cb_t handler, void *ctx);

/**
 * @brief Register the handler receiving every non-command update.
 *
 * Only one such handler exists; registering a new one replaces it. Pass
 * NULL as @p handler to remove it.
 *
 * @param[in] handler Function invoked for text, photos, documents,
 *                    locations, contacts and Mini App payloads.
 * @param[in] ctx     Opaque pointer forwarded to @p handler.
 *
 * @return
 *  - ESP_OK on success.
 *  - ESP_ERR_INVALID_STATE if the service is not initialized.
 */
esp_err_t telegram_register_event_handler(telegram_event_cb_t handler, void *ctx);

/**
 * @brief Dispatch a decoded update to the matching command handler.
 *
 * This is the function the service task uses internally for updates of
 * type ::TELEGRAM_UPDATE_COMMAND. It is exposed so an application can
 * inject a command from another source, for example a serial console or a
 * scheduled job, and obtain exactly the same behaviour as if it had
 * arrived from Telegram.
 *
 * Authorization is enforced here: an update whose
 * ::telegram_update_t::authorized field is false is refused.
 *
 * @param[in] update Decoded update carrying a command name.
 *
 * @return
 *  - ESP_OK when a handler ran and reported success.
 *  - ESP_ERR_INVALID_ARG if @p update is NULL or carries no command.
 *  - ESP_ERR_INVALID_STATE if the service is not initialized.
 *  - ESP_ERR_NOT_FOUND if no command with that name is registered.
 *  - ESP_ERR_NOT_ALLOWED if the sender lacks the required rights.
 *  - The value returned by the handler otherwise.
 */
esp_err_t telegram_process_command(const telegram_update_t *update);

/**
 * @brief Build a decoded update from a command line and dispatch it.
 *
 * Convenience wrapper around @ref telegram_process_command() that splits
 * @p command_line into a command name and its arguments and marks the
 * resulting update as coming from an authorized administrator. Replies
 * produced by the handler are delivered to @p chat_id.
 *
 * The handler runs on the calling task, so a command whose handler answers
 * over the network requires a task stack sized as described in the
 * threading model above.
 *
 * @param[in] chat_id      Chat the handler replies to, as text.
 * @param[in] command_line Full command line, for example "/set period 30".
 *
 * @return
 *  - Same values as @ref telegram_process_command().
 *  - ESP_ERR_NO_MEM if the update descriptor cannot be allocated.
 */
esp_err_t telegram_execute_command(const char *chat_id, const char *command_line);

/**
 * @brief Publish the registered command list to Telegram.
 *
 * Calls the `setMyCommands` method so clients offer the commands in their
 * menu. Commands restricted to administrators are published as well,
 * because Telegram has no way of hiding them per user; the service still
 * refuses them at dispatch time.
 *
 * @return
 *  - ESP_OK when Telegram accepted the list.
 *  - ESP_ERR_INVALID_STATE if the service is not initialized.
 *  - ESP_ERR_NO_MEM if the list cannot be built.
 *  - ESP_FAIL if the API answered with an error.
 */
esp_err_t telegram_set_my_commands(void);

/** @} */

/**
 * @name Telemetry and remote configuration
 * Publishing readings and exposing parameters operators can change.
 * @{
 */

/**
 * @brief Register a sensor exposed through the telemetry commands.
 *
 * Registered sensors are read on demand by the built-in `/sensors` command and
 * by @ref telegram_send_sensor_report(). An application whose set of readings
 * is decided at run time rather than at registration time publishes it through
 * ::telegram_service_config_t::sensors_cb instead; both sources end up in the
 * same report.
 *
 * @param[in] name    Name shown in the report.
 * @param[in] unit    Unit appended to the reading, or NULL for none.
 * @param[in] read_cb Callback returning the current reading.
 * @param[in] ctx     Opaque pointer forwarded to @p read_cb.
 *
 * @return
 *  - ESP_OK on success.
 *  - ESP_ERR_INVALID_ARG if @p name or @p read_cb is NULL.
 *  - ESP_ERR_INVALID_STATE if the service is not initialized.
 *  - ESP_ERR_NO_MEM if the table already holds ::TELEGRAM_MAX_SENSORS
 *    entries.
 */
esp_err_t telegram_register_sensor(const char *name, const char *unit, telegram_sensor_read_cb_t read_cb, void *ctx);

/**
 * @brief Send a telemetry report holding every registered sensor, followed by
 *        the lines ::telegram_service_config_t::sensors_cb contributes.
 *
 * This is what the built-in `/sensors` command sends.
 *
 * @param[in] chat_id Destination chat as text.
 *
 * @return
 *  - ESP_OK when Telegram accepted the report.
 *  - ESP_ERR_INVALID_STATE if the service is not initialized.
 *  - ESP_ERR_NOT_FOUND if no sensor is registered and the callback wrote
 *    nothing, so there is no reading to report.
 *  - ESP_FAIL if the API answered with an error.
 */
esp_err_t telegram_send_sensor_report(const char *chat_id);

/**
 * @brief Register a parameter that operators can read and change remotely.
 *
 * The storage pointed to by @p value is read and written directly by the
 * built-in `/config`, `/get` and `/set` commands, so it must remain valid
 * for as long as the service runs. Writes are clamped to the range given
 * by @p min_value and @p max_value for the numeric types; passing an equal
 * minimum and maximum disables the range check.
 *
 * @param[in] name        Parameter name used on the command line.
 * @param[in] type        Data type of the storage.
 * @param[in] value       Pointer to the storage.
 * @param[in] value_size  Capacity of the storage for
 *                        ::TELEGRAM_PARAM_STRING, ignored otherwise.
 * @param[in] min_value   Lowest accepted value for the numeric types.
 * @param[in] max_value   Highest accepted value for the numeric types.
 * @param[in] description One line description shown by `/config`.
 *
 * @return
 *  - ESP_OK on success.
 *  - ESP_ERR_INVALID_ARG if @p name or @p value is NULL, or if a string
 *    parameter is registered with a zero capacity.
 *  - ESP_ERR_INVALID_STATE if the service is not initialized.
 *  - ESP_ERR_NO_MEM if the table already holds ::TELEGRAM_MAX_PARAMS
 *    entries.
 */
esp_err_t telegram_register_param(const char *name, telegram_param_type_t type, void *value, size_t value_size, float min_value, float max_value,
                                  const char *description);

/**
 * @brief Read the counters accumulated since the service started.
 *
 * @param[out] out_stats Structure receiving the counters.
 *
 * @return
 *  - ESP_OK on success.
 *  - ESP_ERR_INVALID_ARG if @p out_stats is NULL.
 *  - ESP_ERR_INVALID_STATE if the service is not initialized.
 */
esp_err_t telegram_get_stats(telegram_stats_t *out_stats);

/** @} */

#ifdef __cplusplus
}
#endif
