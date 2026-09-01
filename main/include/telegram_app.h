/**
 * @file telegram_app.h
 *
 * @author Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
 * @date 2026
 * @copyright GNU General Public License v3
 * @see https://github.com/hiperiondev/esp32idf_APRS
 *
 * @note
 * This is based on other projects:
 *     VP-Digi: https://github.com/sq8vps/vp-digi
 *     ESP32APRS: https://github.com/nakhonthai/ESP32APRS_Audio
 *     LibAPRS: https://github.com/markqvist/LibAPRS
 *
 *     please contact their authors for more information.
 *
 * @brief Telegram bot subsystem: its own JSON store on LittleFS, its
 *        supervised bring-up, and the diagnosis the web admin renders.
 *
 * @details
 * This module is the station's half of the `telegram_service` component. It
 * owns three things the component deliberately does not:
 *
 *  - **Where the bot's settings live.** Everything the bot needs is read from
 *    and written back to @c /storage/telegram.json, in the shape of the
 *    project's @c config_example.json: the token issued by @@BotFather, the
 *    administrator's numeric identifier, the optional Mini App address, and
 *    the lists of authorized users and allowed group chats. None of it is in
 *    config.json, so the whole bot configuration is one file the operator can
 *    download, edit and upload again from the File Storage page. The only key
 *    this firmware adds to that shape is @c enabled, the switch the Telegram
 *    page renders; a file that does not carry it loads with the bot off, which
 *    is what a hand-written file copied from the example should do.
 *
 *  - **When the service may come up.** @ref telegram_app_apply_config starts a
 *    small supervisor task rather than calling into the component directly,
 *    because every step of a Telegram bring-up can fail for a reason the
 *    operator has to be told apart: there is no route to the internet yet, the
 *    root certificate is not on the filesystem, the token is malformed, the
 *    token is well formed but Telegram rejects it, or there is not enough heap
 *    left for a TLS session. The supervisor performs those steps in order,
 *    from a task with a stack large enough for a TLS handshake, and records
 *    exactly which one failed.
 *
 *  - **What the page is allowed to say.** The diagnosis is published as a
 *    ::telegram_app_reason_t plus a short free-form detail (an @c esp_err_t
 *    name, a file path, or the description Telegram itself returned). The
 *    reason is what the web page turns into a translated sentence; the detail
 *    is passed through untranslated because it comes from outside this
 *    firmware. Nothing in this header depends on the translation layer or on
 *    telegram_service.h, so the page can include it without pulling either in.
 *
 * The supervisor also re-checks connectivity while the bot runs, so a station
 * that loses its uplink reports "waiting for a network route" instead of a
 * growing polling-error count with no explanation.
 */

#ifndef TELEGRAM_APP_H
#define TELEGRAM_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "must_check.h" // APRS_MUST_CHECK: the save entry point below may not have its result discarded

/** @brief Path of the bot's JSON store on the LittleFS storage partition. */
#define TELEGRAM_APP_PATH "/storage/telegram.json"

/**
 * @name Field sizes of the JSON store
 *
 * These bound what is read out of telegram.json, not what Telegram accepts.
 * The token bound matches the transport's own @c TELEGRAM_BOT_TOKEN_MAX_LEN,
 * and the user/chat table sizes match the component's @c TELEGRAM_MAX_USERS
 * and @c TELEGRAM_MAX_CHATS, so a file that fits here is a file the service
 * can be handed in full. Entries past the end of a table are dropped with a
 * warning rather than silently truncating the file on the next save.
 * @{
 */
#define TELEGRAM_APP_TOKEN_MAX    128 /**< Longest accepted bot token, terminator excluded. */
#define TELEGRAM_APP_URL_MAX      128 /**< Longest accepted Mini App address, terminator excluded. */
#define TELEGRAM_APP_NAME_MAX     40  /**< Longest accepted display name of a user or chat, terminator excluded. */
#define TELEGRAM_APP_CALLSIGN_MAX 9   /**< Longest accepted user callsign, SSID included, terminator excluded. */
#define TELEGRAM_APP_USERS_MAX    8   /**< Number of authorized users held by the store. */
#define TELEGRAM_APP_CHATS_MAX    4   /**< Number of allowed group chats held by the store. */
/** @} */

/** @brief Longest free-form detail string published beside a reason. */
#define TELEGRAM_APP_DETAIL_MAX 96

/** @brief Longest bot user name (the @@name Telegram answers @c getMe with). */
#define TELEGRAM_APP_BOTNAME_MAX 40

/**
 * @name Bulletin repeat window
 *
 * Bounds and default of ::telegram_app_config_t::bulletin_window_s, the
 * interval during which a bulletin identical to one already routed is dropped
 * instead of being sent again. They are published here because three call
 * sites have to agree on them: the loader, which substitutes the default for a
 * file that carries no such key; the Telegram page, which renders the field
 * with these bounds and clamps what it stores; and the routing path, which
 * compares elapsed time against the stored value.
 *
 * The default is longer than the shortest interval an operator would sensibly
 * transmit a bulletin at, so a station that never touches the field behaves as
 * one that has no reason to think about it should. The ceiling is a full day,
 * which is the useful limit rather than the storage type's. The floor is 0,
 * and 0 means the test is off: every copy of every bulletin is routed, which
 * is what an operator watching a congested channel for retransmissions wants
 * and what nobody reading a chat does.
 * @{
 */
#define TELEGRAM_APP_BULLETIN_WINDOW_DEFAULT 900   /**< Window used when telegram.json carries no window, seconds. */
#define TELEGRAM_APP_BULLETIN_WINDOW_MIN     0     /**< Lowest accepted window, seconds; 0 disables the duplicate test. */
#define TELEGRAM_APP_BULLETIN_WINDOW_MAX     86400 /**< Highest accepted window, seconds (24 h). */
/** @} */

/**
 * @brief One allowed group chat.
 *
 * Telegram identifiers are 64-bit and signed: a supergroup identifier is a
 * large negative number, well outside the range of a 32-bit integer, so it is
 * carried as @c int64_t, including through the web form, which posts it as
 * text for exactly that reason.
 */
typedef struct {
    int64_t id;                           /**< Numeric Telegram identifier. */
    char name[TELEGRAM_APP_NAME_MAX + 1]; /**< Display name, for the operator's benefit only. */
} telegram_app_peer_t;

/**
 * @brief One authorized Telegram user.
 *
 * Carries the same identifier and display name as ::telegram_app_peer_t, plus
 * the amateur radio callsign that ties this Telegram account to one operator.
 * ::telegram_app_notify_station_message() is what reads @c callsign: it is
 * the addressee this user's routed messages are selected by, and the only one
 * consulted, so every incoming APRS message sent to it reaches this user's own
 * Telegram account. The match is exact (case-insensitive, SSID included), so
 * two users sharing one base callsign under different SSIDs each receive only
 * the messages sent to their own SSID.
 */
typedef struct {
    int64_t id;                                   /**< Numeric Telegram identifier. */
    char name[TELEGRAM_APP_NAME_MAX + 1];         /**< Display name, for the operator's benefit only. */
    char callsign[TELEGRAM_APP_CALLSIGN_MAX + 1]; /**< This user's own callsign, upper case, SSID included, or empty. */
} telegram_app_user_t;

/**
 * @brief Everything telegram.json holds.
 *
 * The whole file is kept as one value so a save can rewrite it without losing
 * the parts the web page does not edit: the Telegram page owns @c enable,
 * @c route_station_messages, @c route_bulletins, @c bulletin_window_s,
 * @c bot_token and @c admin_id, while @c web_app_url, @c users and @c chats
 * survive a save untouched because they were loaded into this same structure
 * first.
 */
typedef struct {
    bool enable;                 /**< True to run the bot; the Telegram page's switch. */
    bool route_station_messages; /**< True to route each incoming message to its addressee's own user; see ::telegram_app_notify_station_message. */
    bool route_bulletins;        /**< True to route every received APRS bulletin to every user and group chat; see ::telegram_app_notify_bulletin. */
    uint32_t bulletin_window_s;  /**< Seconds an identical bulletin stays suppressed; 0 routes every repeat. See ::TELEGRAM_APP_BULLETIN_WINDOW_DEFAULT. */
    char bot_token[TELEGRAM_APP_TOKEN_MAX + 1];        /**< Token issued by @@BotFather. */
    int64_t admin_id;                                  /**< Identifier of the administrator, or 0 for none. */
    char web_app_url[TELEGRAM_APP_URL_MAX + 1];        /**< HTTPS address of the Mini App, or empty. */
    telegram_app_user_t users[TELEGRAM_APP_USERS_MAX]; /**< Authorized users. */
    uint8_t user_count;                                /**< Entries used in ::users. */
    telegram_app_peer_t chats[TELEGRAM_APP_CHATS_MAX]; /**< Allowed group chats. */
    uint8_t chat_count;                                /**< Entries used in ::chats. */
} telegram_app_config_t;

/**
 * @brief Where the bot currently stands.
 *
 * A coarse state, meant to be shown as one word and coloured. The precise
 * cause always travels beside it as a ::telegram_app_reason_t.
 */
typedef enum {
    TELEGRAM_APP_STATE_DISABLED = 0, /**< The switch is off; nothing is running and nothing is wrong. */
    TELEGRAM_APP_STATE_STARTING,     /**< Bring-up is in progress and has not failed yet. */
    TELEGRAM_APP_STATE_RUNNING,      /**< Telegram accepted the token and the polling task is alive. */
    TELEGRAM_APP_STATE_ERROR,        /**< Bring-up stopped at a step the operator has to fix. */
} telegram_app_state_t;

/**
 * @brief Why the bot is in the state it is in.
 *
 * Every value that is not ::TELEGRAM_APP_REASON_CONNECTED names one specific
 * thing to check, in the order the supervisor checks them. The web page maps
 * each one to a translated sentence; the ordering is what makes that sentence
 * actionable, because the first failing step is the only one worth reporting.
 */
typedef enum {
    TELEGRAM_APP_REASON_DISABLED = 0,    /**< The Telegram page's switch is off. */
    TELEGRAM_APP_REASON_FILE_MISSING,    /**< telegram.json is not on the storage partition. */
    TELEGRAM_APP_REASON_FILE_CORRUPT,    /**< telegram.json exists but does not parse as JSON. */
    TELEGRAM_APP_REASON_FILE_UNREADABLE, /**< telegram.json could not be read into memory. */
    TELEGRAM_APP_REASON_NO_TOKEN,        /**< The token field is empty. */
    TELEGRAM_APP_REASON_TOKEN_MALFORMED, /**< The token is not of the form <digits>:<secret>. */
    TELEGRAM_APP_REASON_CERT_MISSING,    /**< The root certificate file is absent; detail carries its path. */
    TELEGRAM_APP_REASON_CERT_INVALID,    /**< The root certificate file is empty, oversized or not PEM. */
    TELEGRAM_APP_REASON_WAITING_NETWORK, /**< No IP address yet, so no name resolution and no TLS. */
    TELEGRAM_APP_REASON_DNS_FAILED,      /**< api.telegram.org could not be resolved; detail carries the elapsed time. */
    TELEGRAM_APP_REASON_TCP_FAILED,      /**< A plain TCP connection to port 443 could not be opened; detail carries the address and errno. */
    TELEGRAM_APP_REASON_NO_MEMORY,       /**< A buffer, a queue or a TLS session did not fit in the heap. */
    TELEGRAM_APP_REASON_INIT_FAILED,     /**< The service refused to initialize; detail carries the esp_err_t name. */
    TELEGRAM_APP_REASON_CONNECT_FAILED,  /**< The TLS call to api.telegram.org did not complete; detail carries the esp_err_t name. */
    TELEGRAM_APP_REASON_API_REJECTED,    /**< Telegram answered and refused; detail carries its own description. */
    TELEGRAM_APP_REASON_TASK_FAILED,     /**< The polling task could not be created. */
    TELEGRAM_APP_REASON_CONNECTED,       /**< Telegram accepted the token and polling is running. */
} telegram_app_reason_t;

/**
 * @brief Snapshot of the bot's state, its diagnosis and its counters.
 *
 * Filled by ::telegram_app_status. Every counter is zero while the service has
 * never run, and @c has_counters says which of the two it is, so a page can
 * tell "nothing has happened yet" from "the service is not up".
 */
typedef struct {
    telegram_app_state_t state;                  /**< Coarse state. */
    telegram_app_reason_t reason;                /**< Precise cause of that state. */
    char detail[TELEGRAM_APP_DETAIL_MAX + 1];    /**< Untranslated detail from outside this firmware, or empty. */
    char bot_name[TELEGRAM_APP_BOTNAME_MAX + 1]; /**< Bot user name Telegram reported, or empty. */
    bool has_counters;                           /**< True when the counters below have been read from a running service. */
    uint32_t updates_received;                   /**< Updates decoded since the service started. */
    uint32_t commands_handled;                   /**< Commands dispatched. */
    uint32_t messages_sent;                      /**< Outgoing messages Telegram accepted. */
    uint32_t rejected;                           /**< Updates dropped because the sender is not authorized. */
    uint32_t poll_errors;                        /**< Consecutive failed polling cycles. */
    int64_t uptime_seconds;                      /**< Seconds since the polling task started. */
} telegram_app_status_t;

/**
 * @brief Read telegram.json into @p out.
 *
 * A missing file yields an all-default, disabled configuration and is not an
 * error: it is the state of a station that has never opened the Telegram page.
 * A corrupt one is left on the filesystem untouched for the operator to look
 * at, and is reported through ::telegram_app_status rather than repaired,
 * because overwriting it would destroy the very thing that has to be examined.
 *
 * @param[out] out Receives the parsed configuration. Must not be NULL.
 * @return true when @p out reflects a file that was read and parsed.
 */
bool telegram_app_load(telegram_app_config_t *out);

/**
 * @brief Write @p in to telegram.json.
 *
 * The file is written token by token to a temp file whose stdio buffer is
 * pinned, then committed over the live one with a single rename, which is the
 * same shape every other store in this firmware uses (see json_store.h).
 *
 * @param[in] in Configuration to persist. Must not be NULL.
 * @return true when the live file holds the new content.
 *
 * @note Declared ::APRS_MUST_CHECK: a call site that discards the result tells
 * the operator the settings are stored for a write that may never have reached
 * flash, so ignoring it fails the build.
 */
bool telegram_app_save(const telegram_app_config_t *in) APRS_MUST_CHECK;

/**
 * @brief Bring the bot up or take it down to match telegram.json.
 *
 * Re-reads the store and compares its @c enable flag with what is running.
 * Enabling starts the supervisor task, which performs the bring-up in the
 * background and publishes its progress through ::telegram_app_status; the
 * call itself returns immediately and never blocks on the network. Disabling
 * asks the supervisor to stop, waits for it to release the service, and leaves
 * no task, no socket and no TLS session behind.
 *
 * Neither bringing the bot up nor taking it down happens on the calling task:
 * both are queued for ::telegram_app_tick_1hz to hand to a short-lived worker.
 * That keeps a save from blocking a browser for the tens of seconds a running
 * service can take to leave a long poll, and it keeps the handshake-sized
 * stack a bring-up needs off the web server.
 *
 * This is also where the queue behind ::telegram_app_notify_station_message
 * and ::telegram_app_notify_bulletin is created, when either routing switch is
 * found on. Doing it here rather than on the first routed line means the two
 * notify entry points - which are reached from the modem's receive task and
 * from the APRS-IS client's task - never allocate anything themselves, while a
 * station that leaves both switches off still never pays for a queue it will
 * not use.
 *
 * Safe to call repeatedly. Called once during start-up and again from the
 * Telegram page's save handler, so moving the switch needs no reboot.
 */
void telegram_app_apply_config(void);

/**
 * @brief One second of the bot's life. Call at 1 Hz from an existing task.
 *
 * This is where the bot lives while it is merely running: it republishes the
 * service's counters, notices an uplink that went away, notices a polling task
 * that ended, and counts down to a deferred retry. None of that needs a task
 * of its own, which is the point - a permanent task would have to carry a
 * stack large enough for the TLS handshake it performs only at bring-up, and
 * on a station this size those bytes are most of the margin the polling
 * connection needs in order to be rebuilt after it drops.
 *
 * When a bring-up or a teardown does become due, this spawns a worker task to
 * perform it and returns immediately; the worker exits as soon as its one job
 * is done, returning its stack to the heap. So the large stack exists for the
 * few seconds it is genuinely needed and at no other time.
 *
 * That worker and the one that delivers routed notifications share a single
 * slot, since each is sized for a TLS handshake and the two together are more
 * stack than this board can hold at once. An action that finds the slot taken
 * by a notification drain stays pending and is offered again a second later,
 * so nothing is lost by the wait.
 *
 * Cheap and non-blocking: in the steady state it copies six counters and
 * returns. Safe to call before ::telegram_app_apply_config has ever run.
 */
void telegram_app_tick_1hz(void);

/**
 * @brief Publish the current state, diagnosis and counters.
 *
 * Safe to call from any task. The counters are read from the service under its
 * own lock, so a snapshot taken while an update is being processed is either
 * the state before it or the state after it.
 *
 * @param[out] out Receives the snapshot. Must not be NULL.
 */
void telegram_app_status(telegram_app_status_t *out);

/**
 * @brief True while the bot is switched on in telegram.json.
 *
 * Reports the operator's intent, not the outcome: a station whose token
 * Telegram rejects is still "enabled". Use ::telegram_app_status to find out
 * whether it actually reached the servers.
 */
bool telegram_app_enabled(void);

/**
 * @brief True while a line handed to one of the notify entry points below
 * could actually be delivered.
 *
 * Reports the conjunction the two of them test before they queue anything:
 * the bot is switched on, it is currently connected, and at least one of the
 * *Telegram* page's two routing switches is on. It is published because the
 * frame-decoding path is reached through gates of its own - APRS messaging
 * and the query responder each have an enable switch - and Telegram routing
 * is a third consumer of the same frames, independent of both. A caller that
 * asks this before deciding whether to parse an incoming frame keeps routing
 * working on a station that runs the bot with APRS messaging switched off.
 *
 * Safe to call from any task and from any stack: it reads cached copies of
 * the switches rather than taking the lock a save holds.
 */
bool telegram_app_routing_active(void);

/**
 * @brief Forward one APRS message to the Telegram account of the
 * authorized user whose own callsign it was addressed to, when the operator
 * has turned that routing on.
 *
 * Both origins of a message are routed on the same terms: one received over
 * the air or from the APRS-IS feed, and one this station itself originates
 * from the *Snd/Rcv Msg* page. What the line says changes accordingly - the
 * sender of a locally originated message is this station's own Message
 * callsign - but the recipient is selected the same way in both cases, so an
 * operator listed on the *Telegram* page reads what was sent to them whether
 * it came from another station or from this one's web admin.
 *
 * The addressee set is the *Telegram* page's authorized-users table and
 * nothing else. This station's own callsign - the *Station* page's My
 * Callsign - plays no part in the decision, so a message addressed to any
 * user listed there is routed to that user whether or not this station reads
 * the frame on its own account, and every user is served from their own
 * Callsign field rather than all of them sharing one station callsign.
 *
 * A no-op unless every one of these holds: the *Telegram* page's "Route
 * Station messages" switch is on, the bot is enabled, and the bot is
 * currently running. Nothing is buffered for later delivery when any of
 * those does not hold - a message that arrives while the bot is off or still
 * connecting is simply not routed, the same way it would not reach an
 * operator who has not opened Telegram at all.
 *
 * @p to_call keeps whatever SSID it was sent with, which is what lets several
 * users share one base callsign under distinct SSIDs and each be addressed
 * individually: the message is delivered to every authorized user whose
 * ::telegram_app_user_t::callsign matches @p to_call exactly
 * (case-insensitive). A @p to_call that matches no configured user's callsign
 * is not delivered to anyone, and a user whose callsign field is left empty
 * never matches. Delivery never reaches a group chat.
 *
 * The rendered line always reads
 * @code
 * msg from <sender callsign> to <addressee callsign> :: <message text>
 * @endcode
 * Delivery happens off a small internal queue rather than inline, which is
 * what makes this function safe to call from both paths that reach it:
 * message.c's frame decoder, which runs on a task with no stack to spare for
 * the TLS handshake a call reaching Telegram needs, and the web server's own
 * task, where an inline network call would hold a browser open for the
 * length of a Telegram round trip.
 *
 * @param[in] from_call Callsign of the station that sent the message, upper
 *                       case, SSID included when the sender used one.
 * @param[in] to_call    Callsign this message was addressed to, upper case,
 *                        SSID included when the sender used one.
 * @param[in] text       Message text, decoded and trimmed.
 */
void telegram_app_notify_station_message(const char *from_call, const char *to_call, const char *text);

/**
 * @brief Forward one APRS bulletin to every Telegram account this station
 * knows, when the operator has turned bulletin routing on.
 *
 * A bulletin is a broadcast: it names no individual station, so unlike
 * ::telegram_app_notify_station_message there is no addressee to match and no
 * callsign to select a recipient by. It is therefore delivered to everyone the
 * bot is configured to talk to - every authorized user of the *Telegram*
 * page's users table, the administrator when one is configured, and every
 * allowed group chat. An administrator who is also listed in the users table
 * receives one copy, not two.
 *
 * Every bulletin this station handles is routed, whatever its origin: one
 * heard off the air, one that arrived from the APRS-IS feed, and one this
 * station transmits itself from its own *Bulletins* page. The last of those
 * is routed as its scheduler sends it, so the operators reading the bot see
 * the station's own announcements in the same chat and in the same form as
 * everyone else's.
 *
 * A no-op unless every one of these holds: the *Telegram* page's "Route
 * Bulletins" switch is on, the bot is enabled, and the bot is currently
 * running. Nothing is buffered for later delivery when any of those does not
 * hold, exactly as for a routed station message.
 *
 * A bulletin is transmitted over and over by its originator and is heard again
 * through every digipeater that repeats it, so the same one reaches this
 * station many times. A bulletin whose sender, addressee and text match one
 * routed inside the window the *Telegram* page's "Bulletin repeat window"
 * field sets (::telegram_app_config_t::bulletin_window_s, 900 s unless the
 * operator changes it) is dropped rather than sent again, which is what keeps
 * a periodic bulletin from filling a chat with copies of itself. A window of
 * 0 turns the test off and routes every copy. Editing the text, or a different
 * station sending it, makes it a new bulletin and it is routed at once. This
 * is also what keeps one of this station's own bulletins to a single copy
 * when the digipeated frame comes back within the window: it carries the same
 * sender, addressee and text, so the returning copy is recognised as the
 * repeat it is. @p to_call is compared with its
 * trailing blanks removed, so the space-padded nine-character addressee an
 * APRS message header carries and the trimmed one a frame decoder produces
 * are the same bulletin.
 *
 * That window is armed by a delivery, never by an attempt: a bulletin that
 * could not be handed over - the switch off, the bot down, the queue full -
 * leaves it untouched and is routed on its next transmission instead. Which
 * of those happened is reported to the log whenever it changes, so a chat
 * that stays quiet says why once rather than either in silence or on every
 * repeat.
 *
 * The rendered line always reads
 * @code
 * bulletin from <sender callsign> to <bulletin addressee> :: <bulletin text>
 * @endcode
 * Delivery happens off the same internal queue routed station messages use,
 * as one queued item that fans out to every recipient when it is drained, so
 * this is safe to call from every task that reaches it: message.c's frame
 * decoder and the shared beacon scheduler alike carry none of the stack a TLS
 * handshake needs.
 *
 * @param[in] from_call Callsign of the station that originated the bulletin,
 *                       upper case, SSID included when one was used.
 * @param[in] to_call    Bulletin addressee the sender used ("BLN1", "BLNA",
 *                        "BLN1WX", ...), upper case. Either the trimmed form
 *                        or the space-padded nine-character message-header
 *                        field may be passed; trailing blanks are removed
 *                        before the addressee is compared or rendered.
 * @param[in] text       Bulletin text, decoded and trimmed.
 */
void telegram_app_notify_bulletin(const char *from_call, const char *to_call, const char *text);

#endif // TELEGRAM_APP_H
