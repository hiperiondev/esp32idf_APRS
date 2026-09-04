// @file telegram_app.c
//
// @author Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
// @date 2026
// @copyright GNU General Public License v3
// @see https://github.com/hiperiondev/esp32idf_APRS
//
// @note
// This is based on other projects:
//     VP-Digi: https://github.com/sq8vps/vp-digi
//     ESP32APRS: https://github.com/nakhonthai/ESP32APRS_Audio
//     LibAPRS: https://github.com/markqvist/LibAPRS
//
//     please contact their authors for more information.
//
// @brief Telegram bot subsystem: the /storage/telegram.json store, the
// supervised bring-up of the telegram_service component, and the diagnosis the
// web admin renders.
//
// See telegram_app.h for the design rationale (why the bot's settings live in
// their own file instead of config.json, and why bring-up runs on a supervisor
// task instead of being called inline).

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include "app_config.h"
#include "aprs_service.h"     // APRS_SOFTWARE_NAME, APRS_SOFTWARE_VERSION
#include "esp_telegram_bot.h" // telegram_bot_get_me()
#include "heap_monitor.h"     // heap_monitor_try_heavy_op()/_release_heavy_op()
#include "json_escape.h"      // json_write_escaped()
#include "json_store.h"       // shared JSON-file store scaffolding
#include "net_state.h"        // net_state_is_connected()
#include "reset_reason.h"     // reset_reason_label(): wording of the start-up notice's cause line
#include "sensors_local.h"    // sensors_local_channel_name()
#include "storage.h"          // storage_write_lock()
#include "str_append.h"       // str_append()
#include "telegram_app.h"
#include "telegram_service.h"
#include "telemetry.h" // telemetry_config_load(), telemetry_get_values()
#include "weather.h"   // weather_field_label()/_snapshot()/_format()

static const char *TAG = "telegram_app";

#define TELEGRAM_APP_TMP_PATH "/storage/telegram.json.tmp"

// Worker task geometry.
//
// The stack is sized for a TLS handshake, which the getMe probe performs on
// this task, and delivery of a routed notification also performs on it, so
// one figure covers both: a bring-up, a teardown and a notification drain are
// all "make one Telegram API call" work and share the one task this constant
// sizes. The task exists only while one of those is in progress and exits as
// soon as it is done, so those bytes are borrowed for a few seconds rather
// than held for the life of the bot: on this board the difference is most of
// the margin a polling connection needs to survive being rebuilt.
//
// Everything the bot needs while it is merely running - copying six counters,
// noticing that the uplink went away, noticing that a retry is due - is done
// by telegram_app_tick_1hz() on the existing 1 Hz service tick, which needs no
// stack of its own.
#define TELEGRAM_WORKER_STACK_BYTES 8192
#define TELEGRAM_WORKER_PRIORITY    4

// Number of routed notifications held between one drain and the next. A
// handful is enough: this is a convenience notification, not the message
// store the "Snd/Rcv Msg" page itself keeps, and a burst larger than this
// simply loses its oldest members rather than growing without bound. A
// bulletin occupies one slot however many recipients it reaches, since a
// broadcast item carries its text once and is fanned out as it is drained.
#define TELEGRAM_NOTIFY_QUEUE_LEN 8

// Recipients one broadcast pass delivers to before it stops.
//
// The addressable set is bounded by the store itself - the users table, the
// allowed group chats and the administrator - so this covers every recipient
// a correctly filled file can name and nothing is dropped on a station that
// is set up sensibly. It is still written down as a number, because the whole
// pass runs inside one Telegram transmit batch: the batch holds the single
// TLS session the service allows and the polling cycle waits for it to close,
// so the length of a pass has to be something the drain task can be read off
// here rather than something a configuration decides.
#define TELEGRAM_NOTIFY_BROADCAST_MAX (TELEGRAM_APP_USERS_MAX + TELEGRAM_APP_CHATS_MAX + 1)

// Bulletins remembered for the duplicate test.
//
// A bulletin is not sent once: its originator repeats it on a timer, every
// digipeater within earshot repeats it again, and an igated copy comes back
// from the APRS-IS feed as well, so one bulletin reaches this station many
// times over. Routing every copy would fill a chat with the same paragraph,
// so a bulletin identical to one routed inside the window is dropped. Eight
// slots cover the bulletin sets stations in one area actually run (APRS101
// allows nine general bulletins plus announcements).
//
// How long a slot stays valid is not a number here: it is the Telegram page's
// "Bulletin repeat window" field, since the interval that suits a station
// depends on how often the bulletins it hears are transmitted, which is a
// property of the channel rather than of this firmware. See
// TELEGRAM_APP_BULLETIN_WINDOW_DEFAULT in telegram_app.h.
#define TELEGRAM_BULLETIN_SEEN_SLOTS 8

// Longest bulletin addressee the routing works with. APRS101 ch.14 allows
// "BLN" plus one identifier character plus a group name of up to five, and the
// message header field it travels in is nine characters wide, so nine plus a
// terminator holds every spelling either call site can hand over.
#define TELEGRAM_BULLETIN_ADDRESSEE_MAX 10

// Longest rendered "msg from ... to ... :: ..." line the queue carries. Wide
// enough for two 9-character callsigns, the fixed wording around them, and
// the full ::MSG_TEXT_MAX message text with room to spare.
#define TELEGRAM_NOTIFY_TEXT_MAX 300

// Longest rendered start-up notice. Four short lines: the word the operator
// looks for, the cause of the boot, this station's callsign and the firmware
// build, with room for the longest label reset_reason_label() returns.
#define TELEGRAM_START_NOTICE_MAX 160

// Seconds the notice waits after the bring-up that armed it before it asks
// for a transmit session, and the bound on how long it keeps asking.
//
// The wait is what keeps the notice out of the busiest moment of the bot's
// life. A bring-up ends with the polling task opening its own TLS session,
// and the record buffers of that handshake are the largest contiguous
// allocation this firmware makes; sending anything at that instant releases
// the polling connection again and makes the poll pay for a second handshake
// while the heap is still holding everything the first one needed. A quarter
// of a minute later the allocations of the bring-up have been returned, the
// first long poll is in flight, and one send costs what a routed line costs.
//
// The bound exists because the notice may find no such moment at all - a
// station whose heap stays too fragmented for a session, or an uplink that
// went away again - and a notice describes a boot, so one delivered several
// minutes late says nothing the Telegram page's live status does not already
// say. It is dropped instead, with a line in the log.
#define TELEGRAM_START_NOTICE_DELAY_S  15
#define TELEGRAM_START_NOTICE_GIVEUP_S 120

// Pause before a failed bring-up is attempted again. Applied only to the
// failures that can clear on their own - no route yet, a TLS call that did not
// complete, a heap that was momentarily too fragmented. A configuration error
// is never retried: nothing changes until the operator changes it, and a
// station that re-reported the same fault every minute would bury the log.
#define TELEGRAM_RETRY_SECONDS 60

// Heap a bring-up must find before it is attempted, as two separate tests.
//
// They are separate because a TLS handshake asks the heap for two different
// shapes at once. The record buffers are single allocations sized by
// CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN and _OUT_CONTENT_LEN, so what governs them
// is the largest CONTIGUOUS block; a heap with plenty free in small pieces
// fails on those. Everything else the handshake consumes - the key exchange
// scalars, the certificate parse, the session context - is a crowd of small
// allocations, so what governs those is TOTAL free heap and fragmentation
// barely matters.
//
// Testing one figure against both needs would either reject a heap that could
// have served (a large total in moderate pieces) or accept one that cannot (a
// single big block with nothing behind it). So each requirement is checked
// against the figure that actually decides it.
//
// The contiguous floor is the input record buffer plus slack for its header
// and allocator overhead. The total floor is set from the measured peak of a
// successful handshake on this firmware with room to spare, since running the
// heap to its last few hundred bytes is survivable exactly once.
#define TELEGRAM_MIN_FREE_BLOCK (CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN + 4096)
#define TELEGRAM_MIN_FREE_HEAP  32768

// Host and port the bot talks to, used by the pre-flight probe below.
#define TELEGRAM_PROBE_HOST "api.telegram.org"
#define TELEGRAM_PROBE_PORT "443"

// Deadline for each stage of the pre-flight probe. Shorter than the transport's
// own 15 s so a station with no route reports which stage stalled instead of
// spending the transport's whole budget and reporting only that something did.
#define TELEGRAM_PROBE_DNS_MS     5000
#define TELEGRAM_PROBE_CONNECT_MS 5000

// Bound on the getMe answer. The document carries the bot's identifiers and
// its capability flags and nothing else, so this is several times what
// Telegram actually returns; the transport reports truncation explicitly, and
// the probe below treats it as a failed probe rather than guessing.
#define TELEGRAM_GETME_BUF 768

// Bound on one rendered sensor reading, measurement unit included. Wide enough
// for a telemetry channel carrying the longest unit an operator can enter
// alongside its value; a longer rendering is truncated rather than overflowing.
#define TELEGRAM_APP_VALUE_MAX 32

// Renders a service switch the way /status reports it.
#define TELEGRAM_ON_OFF(flag) ((flag) ? "on" : "off")

// Serializes load/save between the web save handler and the supervisor task.
static SemaphoreHandle_t s_lock;

// Guards the published snapshot. Held only across the handful of stores that
// build one, never across anything that can block.
static SemaphoreHandle_t s_status_lock;

static telegram_app_status_t s_status = {
    .state = TELEGRAM_APP_STATE_DISABLED,
    .reason = TELEGRAM_APP_REASON_DISABLED,
};

// Configuration the supervisor is working from. Loaded by
// telegram_app_apply_config() before the task is created, so the task never
// touches the filesystem itself, and static so the pointers handed to
// telegram_init() stay valid for as long as the service holds them.
static telegram_app_config_t s_cfg;

// What the worker task should do the next time one is spawned. Written by
// telegram_app_apply_config() and by the tick, read by the worker.
typedef enum {
    TELEGRAM_ACTION_NONE = 0, // nothing pending
    TELEGRAM_ACTION_START,    // bring the service up
    TELEGRAM_ACTION_STOP,     // release the service
} telegram_action_t;

static volatile telegram_action_t s_action;

// The single worker slot the bot's one short-lived task uses.
//
// A bring-up, a teardown and a notification drain are all carried out by the
// same task, spawned fresh for whichever of them is next and deleted once it
// is done, so only one instance of that task - and one 8 KB stack - is ever
// on the heap at a time on top of the service's own polling task, the web
// server's task and the handshake itself. This flag is what keeps a second
// spawn from being attempted while the first is still running: whichever
// spawn claims it runs alone and the other waits.
//
// Waiting costs nothing in either direction. A bring-up that finds the slot
// taken is attempted again by the next 1 Hz tick, and a drain that finds it
// taken leaves its items in the queue: the running worker drains them itself
// before it exits, which is the same brief delay a fresh spawn would have
// produced.
static volatile bool s_worker_busy;

// Guards the claim of s_worker_busy. Reading the flag and setting it are one
// indivisible step: the two notify entry points are reached from the modem's
// receive task and from the APRS-IS client's task, so two overlapping spawns
// must not both come away believing they own the slot, which a plain
// read-then-write would allow. A portMUX (not a mutex) because the claim is a
// handful of instructions and can then be made with no allocation and no
// init-order dependency.
static portMUX_TYPE s_worker_lock = portMUX_INITIALIZER_UNLOCKED;

// Takes the worker slot, reporting whether this caller is the one that got it.
static bool worker_claim(void) {
    bool busy;
    portENTER_CRITICAL(&s_worker_lock);
    busy = s_worker_busy;
    if (!busy)
        s_worker_busy = true;
    portEXIT_CRITICAL(&s_worker_lock);
    return !busy;
}

// Returns the worker slot. Called by a worker task on its way out, and by a
// spawn whose task could not be created after the slot was already claimed.
static void worker_release(void) {
    s_worker_busy = false;
}

// True between a successful bring-up and the teardown that follows it. Says
// that the service owns a task, a client pair and a TLS session, which is what
// decides whether a stop has real work to do.
static volatile bool s_service_up;

// Seconds remaining before a deferred bring-up is attempted again, or
// TELEGRAM_RETRY_NEVER when the fault is one only the operator can clear.
#define TELEGRAM_RETRY_NEVER INT32_MAX
static volatile int32_t s_retry_in;

// True once Telegram has accepted this token. Cleared whenever the operator
// saves the page, which is the only way the token can change.
//
// The getMe probe is what turns "configured" into "connected", and it is worth
// its cost the first time. On a retry it is not: the token has not changed, and
// the probe opens a second TLS session whose handshake peak lands on the same
// heap the session being retried needs. Skipping it there removes that peak
// from every attempt after the first.
static volatile bool s_token_verified;

// The operator's intent, kept separate from whether the service actually
// reached Telegram. Written under s_lock alongside s_cfg.
static bool s_enabled;

// Cached copy of s_cfg.route_station_messages, read the same way s_enabled
// is: without the lock, from telegram_app_notify_station_message(), which
// message.c's frame-decoding path calls for every received message and which
// therefore must never block on s_lock behind a save in progress.
static bool s_route_messages;

// Cached copy of s_cfg.users, read the same way s_route_messages is: without
// the lock, from telegram_app_notify_station_message(), for the same reason.
// Refreshed together with s_route_messages, under s_lock, whenever
// telegram_app_apply_config() runs, so a save landing mid-lookup is seen as
// either the whole old table or the whole new one, never a mix of both.
static telegram_app_user_t s_route_users[TELEGRAM_APP_USERS_MAX];
static uint8_t s_route_user_count;

// Cached copy of s_cfg.route_bulletins, and of the recipient set a routed
// bulletin fans out to: the users above, the allowed group chats and the
// administrator. Read without the lock from the drain task and from
// telegram_app_notify_bulletin(), and refreshed together with everything
// else above, under s_lock, whenever telegram_app_apply_config() runs.
static bool s_route_bulletins;
static telegram_app_peer_t s_route_chats[TELEGRAM_APP_CHATS_MAX];
static uint8_t s_route_chat_count;
static int64_t s_route_admin_id;

// Cached copy of s_cfg.bulletin_window_s, read without the lock from the same
// frame-decoding path and refreshed with everything else above. Held as a
// single word so a read taken while a save publishes a new one sees either
// the old window or the new one, never a half-written figure. Carries the
// compiled-in default until the first telegram_app_apply_config(), so a
// bulletin reaching this module before the store has ever been read is
// measured against the same window a fresh station uses.
static volatile uint32_t s_bulletin_window_s = TELEGRAM_APP_BULLETIN_WINDOW_DEFAULT;

// Longest chat identifier a queued item addresses, rendered as text the same
// way telegram_send_message() expects it.
#define TELEGRAM_NOTIFY_CHAT_ID_MAX 24

// ---------------------------------------------------------------------------
// Routed notifications: station messages and bulletins
//
// message.c's frame-decoding path calls telegram_app_notify_station_message()
// for every received message it decodes and telegram_app_notify_bulletin()
// for every bulletin, and that path runs on the modem's own RX task, which
// carries none of the stack a TLS handshake needs. So the call sites only
// ever format a line and push it onto s_notify_queue; the shared worker task,
// spawned on demand and draining this queue as the last thing it does before
// it exits, is what actually reaches Telegram.
//
// The two differ in who receives the line, and that difference is what the
// broadcast flag below carries. A station message names one addressee, so its
// recipients are known at the call site and one item is queued per user it
// matched. A bulletin names nobody and goes to every user, every group chat
// and the administrator, so queueing a copy per recipient would spend
// thirteen slots and thirteen copies of the same text on one bulletin; it is
// queued once instead and fanned out by the worker task, which is also where
// the recipient list is read, so a save landing in between is reflected in
// the delivery rather than in a list captured a moment earlier.
// ---------------------------------------------------------------------------

// One pending line, either addressed to one user or broadcast to all.
typedef struct {
    char chat_id[TELEGRAM_NOTIFY_CHAT_ID_MAX]; // recipient's Telegram identifier, as text; unused when broadcast
    bool broadcast;                            // true to fan out to every user, group chat and the administrator
    char text[TELEGRAM_NOTIFY_TEXT_MAX];
} telegram_notify_item_t;

// Created by telegram_app_apply_config(), on the task that owns the
// configuration, the first time an enabled bot is found with a routing switch
// on. Not created at start-up regardless: most stations leave both "Route
// Station messages" and "Route Bulletins" off, and an unused queue would hold
// its storage for the life of the firmware for nothing, so a station with
// both switches off never reaches the call below.
static QueueHandle_t s_notify_queue;

// Declared here because notify_worker_spawn() asks for the same task the
// bring-up and teardown paths use, and the worker hands the shared slot back
// and asks for a drain on its own way out, which puts these calls above their
// definitions.
static bool worker_spawn(telegram_action_t action);
static void notify_worker_kick(void);

// Creates the queue if it is not there yet. Called from one task only, which
// is what makes the test above it sound: the two notify entry points run on
// two different tasks and only ever read the handle.
static void notify_queue_ensure(void) {
    if (s_notify_queue != NULL)
        return;

    s_notify_queue = xQueueCreate(TELEGRAM_NOTIFY_QUEUE_LEN, sizeof(telegram_notify_item_t));
    if (s_notify_queue == NULL)
        ESP_LOGE(TAG, "Notification queue could not be created, nothing will be routed to Telegram");
}

// Sends one line to one chat, naming a delivery that did not happen rather
// than letting it disappear. A failure is never retried: the bot going down
// between the moment an item was queued and the moment it is drained is the
// ordinary way this happens, and this is a convenience notification, not the
// message store the "Snd/Rcv Msg" page itself keeps, so the operator has the
// same traffic there regardless.
static void notify_send_one(const char *chat_id, const char *text) {
    esp_err_t err = telegram_send_message(chat_id, text);
    if (err != ESP_OK)
        ESP_LOGW(TAG, "Line not routed to Telegram chat %s: %s", chat_id, esp_err_to_name(err));
}

// Delivers one line of a fan-out to one identifier, counting what was sent
// and what the per-pass cap left out.
static void notify_broadcast_one(int64_t id, const char *text, unsigned *sent, unsigned *skipped) {
    if (*sent >= TELEGRAM_NOTIFY_BROADCAST_MAX) {
        (*skipped)++;
        return;
    }

    char chat_id[TELEGRAM_NOTIFY_CHAT_ID_MAX];
    snprintf(chat_id, sizeof(chat_id), "%" PRId64, id);
    notify_send_one(chat_id, text);
    (*sent)++;
}

// Delivers one broadcast item to every account this station knows: every
// authorized user, the administrator when one is configured, and every
// allowed group chat.
//
// The administrator is sent to separately because telegram_init() holds it as
// an authorized user of the service while the file's own users table does not
// necessarily list it. An administrator that does appear there would then
// receive the same bulletin twice, so the identifier is checked against the
// users table first and only sent to when it is not already among them.
//
// The pass is bounded by TELEGRAM_NOTIFY_BROADCAST_MAX and reports, at INFO,
// how many recipients it reached and how long it held the transmit session.
static void notify_broadcast(const char *text) {
    int64_t started_us = esp_timer_get_time();
    unsigned sent = 0;
    unsigned skipped = 0;

    for (uint8_t i = 0; i < s_route_user_count; i++)
        notify_broadcast_one(s_route_users[i].id, text, &sent, &skipped);

    int64_t admin = s_route_admin_id;
    if (admin != 0) {
        bool already_sent = false;
        for (uint8_t i = 0; i < s_route_user_count && !already_sent; i++)
            already_sent = (s_route_users[i].id == admin);
        if (!already_sent)
            notify_broadcast_one(admin, text, &sent, &skipped);
    }

    for (uint8_t i = 0; i < s_route_chat_count; i++)
        notify_broadcast_one(s_route_chats[i].id, text, &sent, &skipped);

    // How wide a fan-out was and how long it took, so an operator reading the
    // log can tell a bulletin storm from the other things that spend heap.
    // Worded for every broadcast rather than for bulletins alone, since the
    // start-up notice is fanned out through here as well.
    ESP_LOGI(TAG, "Line broadcast to %u recipients in %" PRId64 " ms", sent, (esp_timer_get_time() - started_us) / 1000);
    if (skipped > 0)
        ESP_LOGW(TAG, "%u recipients beyond the %d per pass were not reached", skipped, TELEGRAM_NOTIFY_BROADCAST_MAX);
}

// ---------------------------------------------------------------------------
// Start-up notice
//
// One line reporting that the station came up, and why, sent to every account
// this station knows the first time the bot reaches Telegram after a power-on
// or a reset. It is what tells an operator who is not watching the web admin
// that a station they left running has restarted, and the cause line is what
// tells them whether it restarted because someone cut the power or because
// the firmware faulted.
//
// "The first time" is per boot, not per bring-up: the bot is brought up again
// whenever the operator saves the Telegram page and again after a connection
// that dropped is rebuilt, and a notice on each of those would report a
// restart that did not happen. The latch below is plain RAM, so it is clear
// on every power-on and every reset - which is exactly the event the notice
// reports - and set for the rest of the boot afterwards.
//
// Delivery goes through the same worker task and the same transmit batch a
// routed notification uses, rather than through the service's own
// announce_start: that one sends the instant the polling connection comes up
// and needs a second concurrent TLS session, which is why it stays off (see
// bring_up()). Here the notice travels over the session the batch already
// holds and costs no second handshake.
// ---------------------------------------------------------------------------

// True while this boot owes a notice that is not ready to go out yet: armed by
// the bring-up that succeeded, cleared when the countdown behind it runs out
// or when the notice is given up on.
static volatile bool s_start_notice_owed;

// True once the notice is ready for the next transmit batch, cleared by the
// pass that sends it. Kept apart from the flag above so the tail of the
// bring-up that armed the notice does not send it: that is the one pass whose
// heap cannot afford it.
static volatile bool s_start_notice_pending;

// True once a notice has been armed for this boot. Set when the notice is
// armed rather than when it is delivered, so a delivery that fails is not
// attempted again: the notice is a convenience, and the state it reports is
// on the Telegram page and in the log regardless.
static volatile bool s_start_notice_done;

// Countdowns behind the two constants above, stepped by the 1 Hz tick.
static volatile int32_t s_start_notice_wait;
static volatile int32_t s_start_notice_giveup;

// Arms the notice, if this boot has not had one. Called from the bring-up
// that succeeded, so nothing is armed on a station whose bot never reaches
// Telegram. The bring-up itself does not send it: what actually asks for a
// transmit session is start_notice_tick(), once the delay above has run out.
static void start_notice_arm(void) {
    if (s_start_notice_done)
        return;

    s_start_notice_done = true;
    s_start_notice_wait = TELEGRAM_START_NOTICE_DELAY_S;
    s_start_notice_giveup = TELEGRAM_START_NOTICE_GIVEUP_S;
    s_start_notice_owed = true;
}

// Renders the notice and fans it out. Runs inside a transmit batch, on the
// worker task, so it is subject to the same one-session rule as every other
// send.
static void start_notice_send(void) {
    s_start_notice_pending = false;

    app_config_lock();
    char call[sizeof(g_config.aprs_mycall)];
    snprintf(call, sizeof(call), "%s", g_config.aprs_mycall);
    app_config_unlock();

    const char *cause = reset_reason_label(esp_reset_reason());

    char text[TELEGRAM_START_NOTICE_MAX];
    size_t used = 0;
    str_append(text, sizeof(text), &used, "START\n");
    str_append(text, sizeof(text), &used, "Reason: %s\n", cause);
    str_append(text, sizeof(text), &used, "Station: %s\n", call);
    str_append(text, sizeof(text), &used, "Firmware: %s %s", APRS_SOFTWARE_NAME, APRS_SOFTWARE_VERSION);

    ESP_LOGI(TAG, "Sending start-up notice, reset cause: %s", cause);
    notify_broadcast(text);
}

// Spawns the shared worker to drain the notify queue, if the slot is free.
// Safe to call every time an item is queued: a worker already running takes
// the new item on its own pass, since draining is the last thing it does
// before it exits, and an item queued while a bring-up or a teardown holds
// the slot is picked up by notify_worker_kick() as soon as that action hands
// the slot back.
static void notify_worker_spawn(void) {
    worker_spawn(TELEGRAM_ACTION_NONE);
}

// Starts a drain when something is waiting and the shared slot is free.
//
// Called wherever that slot changes hands, because a queue that holds items
// nobody is about to drain is how routing stops for good rather than for a
// moment. Every line is queued by a caller that then asks for a spawn, and
// that ask is refused whenever another worker owns the slot; the refusal is
// only harmless while some later line asks again. So the question is put once
// more at the point where the slot becomes available, which is what turns a
// refused spawn into a deferred one.
static void notify_worker_kick(void) {
    if (s_notify_queue == NULL)
        return;
    if (uxQueueMessagesWaiting(s_notify_queue) == 0)
        return;

    notify_worker_spawn();
}

static void lock(void) {
    json_store_lock_take(&s_lock);
}

static void unlock(void) {
    json_store_lock_give(&s_lock);
}

// ---------------------------------------------------------------------------
// Published status
// ---------------------------------------------------------------------------

static void status_lock_ensure(void) {
    json_store_lock_ensure(&s_status_lock);
}

// Replaces the state and the diagnosis, leaving the counters alone. detail may
// be NULL for the reasons that need no free-form text.
static void status_set(telegram_app_state_t state, telegram_app_reason_t reason, const char *detail) {
    status_lock_ensure();
    if (s_status_lock != NULL)
        xSemaphoreTake(s_status_lock, portMAX_DELAY);

    s_status.state = state;
    s_status.reason = reason;
    if (detail != NULL)
        snprintf(s_status.detail, sizeof(s_status.detail), "%s", detail);
    else
        s_status.detail[0] = 0;

    if (s_status_lock != NULL)
        xSemaphoreGive(s_status_lock);
}

// Clears everything a previous run published, including the bot name and the
// counters, so a service that is taken down does not leave figures on screen
// that no longer describe anything.
static void status_reset(telegram_app_state_t state, telegram_app_reason_t reason) {
    status_lock_ensure();
    if (s_status_lock != NULL)
        xSemaphoreTake(s_status_lock, portMAX_DELAY);

    memset(&s_status, 0, sizeof(s_status));
    s_status.state = state;
    s_status.reason = reason;

    if (s_status_lock != NULL)
        xSemaphoreGive(s_status_lock);
}

static void status_set_bot_name(const char *name) {
    status_lock_ensure();
    if (s_status_lock != NULL)
        xSemaphoreTake(s_status_lock, portMAX_DELAY);

    snprintf(s_status.bot_name, sizeof(s_status.bot_name), "%s", name != NULL ? name : "");

    if (s_status_lock != NULL)
        xSemaphoreGive(s_status_lock);
}

// Folds one reading of the service's own counters into the snapshot.
static void status_set_counters(const telegram_stats_t *st) {
    status_lock_ensure();
    if (s_status_lock != NULL)
        xSemaphoreTake(s_status_lock, portMAX_DELAY);

    s_status.has_counters = true;
    s_status.updates_received = st->updates_received;
    s_status.commands_handled = st->commands_handled;
    s_status.messages_sent = st->messages_sent;
    s_status.rejected = st->rejected;
    s_status.poll_errors = st->poll_errors;
    s_status.uptime_seconds = st->uptime_seconds;

    if (s_status_lock != NULL)
        xSemaphoreGive(s_status_lock);
}

void telegram_app_status(telegram_app_status_t *out) {
    if (out == NULL)
        return;

    status_lock_ensure();
    if (s_status_lock != NULL)
        xSemaphoreTake(s_status_lock, portMAX_DELAY);

    *out = s_status;

    if (s_status_lock != NULL)
        xSemaphoreGive(s_status_lock);
}

bool telegram_app_enabled(void) {
    lock();
    bool en = s_enabled;
    unlock();
    return en;
}

bool telegram_app_routing_active(void) {
    // The same three conditions the two notify entry points below test, read
    // the same lock-free way and for the same reason: this is asked on the
    // frame-decoding path, once per frame, and must never wait behind a save
    // in progress. A caller that gets true and finds the bot gone a moment
    // later loses one routed line, which is what any of the three going false
    // costs at any other point in the sequence.
    return (s_route_messages || s_route_bulletins) && s_enabled && s_service_up;
}

void telegram_app_notify_station_message(const char *from_call, const char *to_call, const char *text) {
    if (from_call == NULL || to_call == NULL || text == NULL)
        return;

    // Read without the lock, the same way telegram_app_tick_1hz() reads
    // s_enabled: this runs on message.c's frame-decoding path for every
    // received message, and must never wait behind a save in progress.
    if (!s_route_messages || !s_enabled || !s_service_up)
        return;

    // Read, never created here: the queue belongs to the configuration task,
    // which puts it in place when routing is turned on. A handle still absent
    // with routing on means the queue did not fit in the heap, which was
    // named in the log when the attempt was made.
    if (s_notify_queue == NULL)
        return;

    char line[TELEGRAM_NOTIFY_TEXT_MAX];
    snprintf(line, sizeof(line), "msg from %s to %s :: %s", from_call, to_call, text);

    // s_route_users is the same kind of lock-free cache s_route_messages is,
    // for the same reason: this path must never wait behind a save. The users
    // table is the only addressee set consulted here - this station's own
    // callsign has no part in the decision - and every authorized user whose
    // own callsign matches to_call exactly gets a copy on their own Telegram
    // account. to_call keeps whatever SSID it arrived with, which is what
    // lets several users share one base callsign under distinct SSIDs and
    // each be addressed on their own. A to_call that matches nobody's
    // callsign is simply not queued.
    for (uint8_t i = 0; i < s_route_user_count; i++) {
        if (s_route_users[i].callsign[0] == 0 || strcasecmp(s_route_users[i].callsign, to_call) != 0)
            continue;

        telegram_notify_item_t item;
        snprintf(item.chat_id, sizeof(item.chat_id), "%" PRId64, s_route_users[i].id);
        item.broadcast = false;
        memcpy(item.text, line, sizeof(item.text));

        // Never blocks: a full queue means notifications are arriving faster
        // than the drain task can deliver them, and the frame-decoding path
        // that called this function must not be held up waiting for
        // Telegram. The oldest undelivered notification is what a full queue
        // costs here, and it is named rather than dropped in silence, so an
        // operator who is missing lines can tell a queue that cannot keep up
        // from a switch that is off.
        if (xQueueSend(s_notify_queue, &item, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Message from %s to %s not routed, the notification queue is full", from_call, to_call);
            continue;
        }

        notify_worker_spawn();
    }
}

// One bulletin already routed, remembered so its repeats are not routed
// again. The hash identifies the bulletin by everything an operator would
// call part of it - who sent it, which bulletin slot it is, and what it says -
// so an edited bulletin is a new one and is delivered at once.
typedef struct {
    uint32_t hash;
    int64_t seen_us;
} telegram_bulletin_seen_t;

static telegram_bulletin_seen_t s_bulletin_seen[TELEGRAM_BULLETIN_SEEN_SLOTS];
static uint8_t s_bulletin_seen_next;

// FNV-1a over the three fields that identify a bulletin. A hash rather than
// the strings themselves: the table then costs twelve bytes per slot instead
// of the three hundred a stored text would, and the only cost of a collision
// is one bulletin not routed, which the next edit of its text clears.
static uint32_t bulletin_hash(const char *from_call, const char *to_call, const char *text) {
    uint32_t h = 2166136261u;
    const char *const parts[] = { from_call, to_call, text };
    for (size_t p = 0; p < sizeof(parts) / sizeof(parts[0]); p++) {
        for (const char *c = parts[p]; *c != 0; c++) {
            h ^= (uint32_t)(unsigned char)*c;
            h *= 16777619u;
        }
        h ^= 0xffu; // separator, so "AB"+"C" and "A"+"BC" are different bulletins
        h *= 16777619u;
    }
    return h;
}

// Settles on one spelling of a bulletin addressee.
//
// The two call sites hand over different ones for the same bulletin.
// bulletins.c passes the nine-character space-padded field an APRS message
// header carries ("BLN1     "), because that is what it just put on the air;
// message.c passes the trimmed addressee its frame decoder produced
// ("BLN1"). Everything below compares and renders what it is given byte for
// byte, so without this the station's own bulletin and the digipeated copy of
// it that comes back a moment later are two different bulletins: each is
// delivered on its own, and the chat receives the same announcement twice.
// Trailing blanks are the whole difference, so they are what is removed.
static void bulletin_trim_addressee(const char *in, char *out, size_t out_size) {
    snprintf(out, out_size, "%s", in);
    size_t len = strlen(out);
    while (len > 0 && out[len - 1] == ' ')
        out[--len] = 0;
}

// True when this exact bulletin was routed inside the window.
//
// Called from every task a bulletin can reach this module on: the modem's
// receive task for one heard off the air, the APRS-IS client's task for one
// from the feed, and the beacon scheduler for one this station transmits, so
// two calls can overlap. The table is left unlocked all the same. The worst
// an overlap can produce is one extra copy of a bulletin, which is the very
// thing this test only ever makes rare rather than impossible, and a mutex
// here would make a frame decode wait on a network task for a notification
// nobody is waiting for.
//
// A window of 0 is the operator asking for every copy, so the table is not
// even scanned: nothing is ever a repeat.
static bool bulletin_seen_recently(uint32_t hash) {
    uint32_t window_s = s_bulletin_window_s;
    if (window_s == 0)
        return false;

    int64_t now = esp_timer_get_time();
    int64_t window = (int64_t)window_s * 1000000;

    for (uint8_t i = 0; i < TELEGRAM_BULLETIN_SEEN_SLOTS; i++) {
        if (s_bulletin_seen[i].seen_us != 0 && s_bulletin_seen[i].hash == hash && (now - s_bulletin_seen[i].seen_us) < window)
            return true;
    }
    return false;
}

// Arms the window for one bulletin, in the oldest slot.
//
// Kept apart from the test above because of when it must happen: the window
// says "this bulletin has already been delivered", so arming it for one that
// was not is what silences a bulletin entirely. A station transmitting on the
// shortest interval the Bulletins page allows repeats every 30 s, so against
// the default window a single arming that no delivery followed swallows the
// next twenty-nine transmissions of that bulletin, and an arming that keeps
// happening on a path that keeps failing swallows all of them. So this is
// called only once the item is on the queue.
static void bulletin_seen_record(uint32_t hash) {
    s_bulletin_seen[s_bulletin_seen_next].hash = hash;
    s_bulletin_seen[s_bulletin_seen_next].seen_us = esp_timer_get_time();
    s_bulletin_seen_next = (uint8_t)((s_bulletin_seen_next + 1) % TELEGRAM_BULLETIN_SEEN_SLOTS);
}

// Why the last bulletin handed to the entry point below did or did not reach
// the queue.
typedef enum {
    BULLETIN_ROUTE_QUEUED = 0, // on its way to every recipient
    BULLETIN_ROUTE_OFF,        // the Telegram page's "Route Bulletins" switch is off
    BULLETIN_ROUTE_BOT_DOWN,   // the bot is disabled, or not connected yet
    BULLETIN_ROUTE_NO_QUEUE,   // the notification queue is not there
    BULLETIN_ROUTE_QUEUE_FULL, // delivery is not keeping up with the traffic
    BULLETIN_ROUTE_DUPLICATE,  // identical to one routed inside the window
} bulletin_route_t;

static bulletin_route_t s_bulletin_last_route = BULLETIN_ROUTE_QUEUED;

// Reports what became of a bulletin, but only when that changes.
//
// Every outcome here is worth one line and none of them is worth one per
// bulletin: a bulletin arrives as often as its originator transmits it, and
// this station's own arrives on the interval the operator set, so a station
// with the switch off or a bot that is down would otherwise write the same
// sentence to the log every few seconds forever. Reporting the transition
// instead gives the operator exactly what the silence was missing - the
// reason nothing is arriving in the chat - at the moment the reason starts
// and again at the moment it ends.
static void bulletin_report_route(bulletin_route_t outcome, const char *addressee) {
    if (outcome == s_bulletin_last_route)
        return;
    s_bulletin_last_route = outcome;

    // Read once, so the sentence and the behaviour it describes cannot
    // disagree because a save landed between the two.
    uint32_t window_s = s_bulletin_window_s;

    switch (outcome) {
        case BULLETIN_ROUTE_QUEUED:
            if (window_s == 0)
                ESP_LOGI(TAG, "Bulletin %s routed to Telegram; every repeat is routed, the bulletin repeat window is 0 s", addressee);
            else
                ESP_LOGI(TAG, "Bulletin %s routed to Telegram; identical repeats are dropped for the next %" PRIu32 " s", addressee, window_s);
            break;
        case BULLETIN_ROUTE_OFF:
            ESP_LOGI(TAG, "Bulletin %s not routed: \"Route Bulletins\" is off on the Telegram page", addressee);
            break;
        case BULLETIN_ROUTE_BOT_DOWN:
            ESP_LOGI(TAG, "Bulletin %s not routed: the bot is not running (see the Telegram page for the reason)", addressee);
            break;
        case BULLETIN_ROUTE_NO_QUEUE:
            ESP_LOGW(TAG, "Bulletin %s not routed: the notification queue does not exist", addressee);
            break;
        case BULLETIN_ROUTE_QUEUE_FULL:
            ESP_LOGW(TAG, "Bulletin %s not routed: the notification queue is full", addressee);
            break;
        case BULLETIN_ROUTE_DUPLICATE:
            ESP_LOGI(TAG, "Bulletin %s not routed: identical to one delivered within the last %" PRIu32 " s", addressee, window_s);
            break;
    }
}

void telegram_app_notify_bulletin(const char *from_call, const char *to_call, const char *text) {
    if (from_call == NULL || to_call == NULL || text == NULL)
        return;

    // One spelling of the addressee for the duplicate test, the rendered line
    // and the log alike, whichever call site this came from.
    char addressee[TELEGRAM_BULLETIN_ADDRESSEE_MAX];
    bulletin_trim_addressee(to_call, addressee, sizeof(addressee));

    // Read without the lock, the same way telegram_app_notify_station_message()
    // reads its own switch and for the same reason: this runs on the
    // frame-decoding path and must never wait behind a save in progress.
    if (!s_route_bulletins) {
        bulletin_report_route(BULLETIN_ROUTE_OFF, addressee);
        return;
    }
    if (!s_enabled || !s_service_up) {
        bulletin_report_route(BULLETIN_ROUTE_BOT_DOWN, addressee);
        return;
    }

    // Read, never created here, for the reason given at the same test in
    // telegram_app_notify_station_message().
    if (s_notify_queue == NULL) {
        bulletin_report_route(BULLETIN_ROUTE_NO_QUEUE, addressee);
        return;
    }

    // Tested before the queue is touched, so a station that only ever hears
    // repeats of one bulletin spends a hash and a table scan on each rather
    // than a queue slot and a copy of its text.
    uint32_t hash = bulletin_hash(from_call, addressee, text);
    if (bulletin_seen_recently(hash)) {
        bulletin_report_route(BULLETIN_ROUTE_DUPLICATE, addressee);
        return;
    }

    telegram_notify_item_t item;
    item.chat_id[0] = 0;
    item.broadcast = true;
    snprintf(item.text, sizeof(item.text), "bulletin from %s to %s :: %s", from_call, addressee, text);

    // Never blocks, for the reason given at the same call in
    // telegram_app_notify_station_message(): the oldest undelivered
    // notification is what a full queue costs here.
    if (xQueueSend(s_notify_queue, &item, 0) != pdTRUE) {
        bulletin_report_route(BULLETIN_ROUTE_QUEUE_FULL, addressee);
        return;
    }

    // Armed only now, on a bulletin that is actually on its way. Every path
    // above leaves the window untouched, so a bulletin that could not be
    // routed this time is routed on its next transmission instead of being
    // held back as a repeat of a delivery that never happened.
    bulletin_seen_record(hash);
    bulletin_report_route(BULLETIN_ROUTE_QUEUED, addressee);

    notify_worker_spawn();
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

// Copies a bounded string member out of a JSON object, leaving the destination
// empty when the member is absent or is not a string.
static void get_string(const cJSON *obj, const char *key, char *out, size_t out_size) {
    out[0] = 0;
    const cJSON *v = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsString(v) && v->valuestring != NULL)
        snprintf(out, out_size, "%s", v->valuestring);
}

// Reads a Telegram identifier. Both forms a hand-edited file can carry are
// accepted: the JSON number the example uses, and a quoted string, which is
// what a person copying an identifier out of a Telegram client is most likely
// to paste. valuedouble is what cJSON exposes for a number, and a double holds
// every Telegram identifier exactly - they are far below 2^53 - so the cast is
// lossless for any value the servers issue.
static int64_t get_id(const cJSON *obj, const char *key) {
    const cJSON *v = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsNumber(v))
        return (int64_t)v->valuedouble;
    if (cJSON_IsString(v) && v->valuestring != NULL)
        return (int64_t)strtoll(v->valuestring, NULL, 10);
    return 0;
}

// Reads the bulletin repeat window. A member that is absent, is not a number
// and not a number in a string, or is out of range yields the default rather
// than a value the routing path would have to defend itself against, since a
// window is the one numeric member of this file an operator is likely to edit
// by hand. Both spellings get_id() accepts are accepted here for the same
// reason: a hand-edited file quoting its numbers is still a usable file.
static uint32_t get_window(const cJSON *obj, const char *key) {
    const cJSON *v = cJSON_GetObjectItem(obj, key);

    double raw;
    if (cJSON_IsNumber(v))
        raw = v->valuedouble;
    else if (cJSON_IsString(v) && v->valuestring != NULL)
        raw = strtod(v->valuestring, NULL);
    else
        return TELEGRAM_APP_BULLETIN_WINDOW_DEFAULT;

    if (raw < TELEGRAM_APP_BULLETIN_WINDOW_MIN || raw > TELEGRAM_APP_BULLETIN_WINDOW_MAX) {
        ESP_LOGW(TAG, "%s is outside %d..%d s, the default of %d s is used", key, TELEGRAM_APP_BULLETIN_WINDOW_MIN, TELEGRAM_APP_BULLETIN_WINDOW_MAX,
                 TELEGRAM_APP_BULLETIN_WINDOW_DEFAULT);
        return TELEGRAM_APP_BULLETIN_WINDOW_DEFAULT;
    }
    return (uint32_t)raw;
}

// Reads the "chats" array into a bounded table, reporting entries that did
// not fit rather than dropping them in silence.
static uint8_t load_peers(const cJSON *doc, const char *key, telegram_app_peer_t *out, uint8_t max) {
    const cJSON *arr = cJSON_GetObjectItem(doc, key);
    if (!cJSON_IsArray(arr))
        return 0;

    int n = cJSON_GetArraySize(arr);
    if (n > (int)max) {
        ESP_LOGW(TAG, "%s holds %d entries, only the first %u are kept", key, n, (unsigned)max);
        n = (int)max;
    }

    uint8_t count = 0;
    for (int i = 0; i < n; i++) {
        const cJSON *o = cJSON_GetArrayItem(arr, i);
        if (!cJSON_IsObject(o))
            continue;
        int64_t id = get_id(o, "id");
        if (id == 0)
            continue;
        out[count].id = id;
        get_string(o, "name", out[count].name, sizeof(out[count].name));
        count++;
    }
    return count;
}

// Reads the "users" array into a bounded table, the same way load_peers()
// reads "chats", plus each entry's own callsign.
static uint8_t load_users(const cJSON *doc, const char *key, telegram_app_user_t *out, uint8_t max) {
    const cJSON *arr = cJSON_GetObjectItem(doc, key);
    if (!cJSON_IsArray(arr))
        return 0;

    int n = cJSON_GetArraySize(arr);
    if (n > (int)max) {
        ESP_LOGW(TAG, "%s holds %d entries, only the first %u are kept", key, n, (unsigned)max);
        n = (int)max;
    }

    uint8_t count = 0;
    for (int i = 0; i < n; i++) {
        const cJSON *o = cJSON_GetArrayItem(arr, i);
        if (!cJSON_IsObject(o))
            continue;
        int64_t id = get_id(o, "id");
        if (id == 0)
            continue;
        out[count].id = id;
        get_string(o, "name", out[count].name, sizeof(out[count].name));
        get_string(o, "callsign", out[count].callsign, sizeof(out[count].callsign));
        count++;
    }
    return count;
}

// Reported by load_locked() so the reason published to the page distinguishes
// a file that is absent from one that is present and unusable.
static json_store_status_t s_last_read_status = JSON_STORE_MISSING;

static bool load_locked(telegram_app_config_t *out) {
    memset(out, 0, sizeof(*out));

    // Set before the read, so the configuration a station with no file at all
    // is left holding carries the default window rather than the 0 the memset
    // above wrote, which would mean the opposite: route every repeat.
    out->bulletin_window_s = TELEGRAM_APP_BULLETIN_WINDOW_DEFAULT;

    cJSON *doc = NULL;
    json_store_status_t st = json_store_read(TELEGRAM_APP_PATH, TAG, "telegram settings", &doc);
    s_last_read_status = st;
    if (st != JSON_STORE_OK)
        return false;

    // Absent means off. A file copied verbatim from the project's example
    // carries no "enabled" key, and a station that has never been configured
    // must not start reaching the internet because a file appeared.
    const cJSON *v = cJSON_GetObjectItem(doc, "enabled");
    out->enable = cJSON_IsTrue(v);

    // Absent means off, the same as "enabled": a file written before this
    // switch existed, or hand-edited from the project's example, must not
    // start routing station traffic to Telegram because a key it never
    // carried is missing.
    const cJSON *rsm = cJSON_GetObjectItem(doc, "routeStationMessages");
    out->route_station_messages = cJSON_IsTrue(rsm);

    // Absent means off, for the same reason the two switches above do.
    const cJSON *rbl = cJSON_GetObjectItem(doc, "routeBulletins");
    out->route_bulletins = cJSON_IsTrue(rbl);

    // Absent means the default, not 0, which is the one place this file
    // departs from the "absent means off" rule the switches follow. 0 is a
    // legal window here and means something specific - route every repeat -
    // so a file written before this key existed would otherwise start
    // repeating every bulletin into the chats as soon as it was loaded.
    out->bulletin_window_s = get_window(doc, "bulletinWindowSeconds");

    get_string(doc, "bot_token", out->bot_token, sizeof(out->bot_token));
    get_string(doc, "web_app_url", out->web_app_url, sizeof(out->web_app_url));
    out->admin_id = get_id(doc, "admin_id");
    out->user_count = load_users(doc, "users", out->users, TELEGRAM_APP_USERS_MAX);
    out->chat_count = load_peers(doc, "chats", out->chats, TELEGRAM_APP_CHATS_MAX);

    cJSON_Delete(doc);
    return true;
}

// Writes the "chats" array.
static void save_peers(FILE *f, const char *key, const telegram_app_peer_t *in, uint8_t count) {
    fprintf(f, ",\"%s\":[", key);
    for (uint8_t i = 0; i < count; i++) {
        fputs(i ? ",{\"id\":" : "{\"id\":", f);
        fprintf(f, "%" PRId64 ",\"name\":", in[i].id);
        json_write_escaped(f, in[i].name);
        fputc('}', f);
    }
    fputc(']', f);
}

// Writes the "users" array, the same way save_peers() writes "chats", plus
// each entry's own callsign.
static void save_users(FILE *f, const char *key, const telegram_app_user_t *in, uint8_t count) {
    fprintf(f, ",\"%s\":[", key);
    for (uint8_t i = 0; i < count; i++) {
        fputs(i ? ",{\"id\":" : "{\"id\":", f);
        fprintf(f, "%" PRId64 ",\"name\":", in[i].id);
        json_write_escaped(f, in[i].name);
        fputs(",\"callsign\":", f);
        json_write_escaped(f, in[i].callsign);
        fputc('}', f);
    }
    fputc(']', f);
}

static bool save_locked(const telegram_app_config_t *in) {
    // Entered with s_lock held (see telegram_app_save() below), which is what
    // json_store_open_tmp() asserts before handing back a stream whose stdio
    // buffer is already pinned.
    FILE *f = json_store_open_tmp(TELEGRAM_APP_TMP_PATH, TAG, s_lock);
    if (f == NULL)
        return false;

    // Written token by token straight to the file: no cJSON tree and no second
    // serialized buffer ever exist, so a save costs essentially only littlefs's
    // own write buffer on top of the stream buffer above.
    //
    // The member order matches the project's example file, with the enable
    // switch first, so a file this firmware writes and a file the operator
    // hand-edits read the same way.
    fputs("{\"enabled\":", f);
    fputs(in->enable ? "true" : "false", f);
    fputs(",\"routeStationMessages\":", f);
    fputs(in->route_station_messages ? "true" : "false", f);
    fputs(",\"routeBulletins\":", f);
    fputs(in->route_bulletins ? "true" : "false", f);
    fprintf(f, ",\"bulletinWindowSeconds\":%" PRIu32, in->bulletin_window_s);
    fputs(",\"bot_token\":", f);
    json_write_escaped(f, in->bot_token);
    fprintf(f, ",\"admin_id\":%" PRId64, in->admin_id);
    fputs(",\"web_app_url\":", f);
    json_write_escaped(f, in->web_app_url);
    save_users(f, "users", in->users, in->user_count);
    save_peers(f, "chats", in->chats, in->chat_count);
    fputc('}', f);

    return json_store_commit(f, TELEGRAM_APP_TMP_PATH, TELEGRAM_APP_PATH, TAG, "telegram settings");
}

bool telegram_app_load(telegram_app_config_t *out) {
    if (out == NULL)
        return false;
    lock();
    bool ok = load_locked(out);
    unlock();
    return ok;
}

bool telegram_app_save(const telegram_app_config_t *in) {
    if (in == NULL)
        return false;
    lock();
    // Module lock first, filesystem-wide writer gate second (storage.h): the
    // temp-file + rename sequence inside save_locked() must not overlap the
    // whole-partition format the web Storage page can start.
    storage_write_lock();
    bool ok = save_locked(in);
    storage_write_unlock();
    unlock();
    return ok;
}

// ---------------------------------------------------------------------------
// Bring-up
// ---------------------------------------------------------------------------

// A token issued by @BotFather is "<numeric bot id>:<secret>". Checking the
// shape here is worth the few lines: it separates a token that was pasted
// short, wrapped, or copied with the surrounding quotes from one Telegram will
// actually look at, and those two faults need entirely different advice.
static bool token_well_formed(const char *token) {
    const char *colon = strchr(token, ':');
    if (colon == NULL || colon == token)
        return false;
    for (const char *p = token; p < colon; p++) {
        if (*p < '0' || *p > '9')
            return false;
    }
    return colon[1] != '\0';
}

#if !CONFIG_TELEGRAM_BOT_CERT_BUNDLE
// Checked before the transport is initialized purely so the failure can be
// named. The transport applies its own, stricter test when it loads the file;
// what it cannot do is tell the operator which of the two things to do about
// it, because by then the distinction between "no file" and "not a
// certificate" has collapsed into one error code.
static telegram_app_reason_t cert_precheck(char *detail, size_t detail_size) {
    const char *path = CONFIG_TELEGRAM_BOT_CERT_PATH;

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        snprintf(detail, detail_size, "%s", path);
        return TELEGRAM_APP_REASON_CERT_MISSING;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);

    if (size <= 0 || size > CONFIG_TELEGRAM_BOT_CERT_MAX_LEN) {
        snprintf(detail, detail_size, "%s (%ld bytes)", path, size);
        return TELEGRAM_APP_REASON_CERT_INVALID;
    }
    return TELEGRAM_APP_REASON_CONNECTED;
}
#endif

// Extra lines appended to the built-in /status answer, so the bot reports the
// station rather than only the board it runs on. Called from the service task
// with a buffer it owns.
//
// Every service that has a master switch is listed, whether it is on or off:
// an absent line would be indistinguishable from a service this firmware does
// not carry, and the whole point of the answer is to say what this station is
// doing right now. The telemetry switch is the one that does not live in
// g_config - the telemetry subsystem keeps its settings in their own file -
// so it is read through its own loader.
static void telegram_status_lines(char *buffer, size_t size, void *ctx) {
    (void)ctx;

    app_config_lock();
    char call[sizeof(g_config.aprs_mycall)];
    snprintf(call, sizeof(call), "%s", g_config.aprs_mycall);
    bool igate = g_config.igate_en;
    bool digi = g_config.digi_en;
    bool trk = g_config.trk_en;
    bool wx = g_config.wx_en;
    bool msg = g_config.msg_enable;
    bool query = g_config.query_en;
    bool bm = g_config.bm_en;
    bool gps = g_config.gps_en;
    bool modem = g_config.audio_modem_en;
    bool duty = g_config.duty_cycle_en;
    bool ntp = g_config.synctime;
    app_config_unlock();

    // Loaded on the heap: the telemetry settings are a large structure and
    // this runs on the service task, whose stack is sized for a TLS handshake
    // rather than for a copy of it.
    bool tlm = false;
    telemetry_config_t *tcfg = malloc(sizeof(*tcfg));
    if (tcfg != NULL) {
        telemetry_config_load(tcfg);
        tlm = tcfg->en;
        free(tcfg);
    }

    size_t used = 0;
    str_append(buffer, size, &used, "Callsign: %s\n", call);
    str_append(buffer, size, &used, "IGate: %s\n", TELEGRAM_ON_OFF(igate));
    str_append(buffer, size, &used, "Digipeater: %s\n", TELEGRAM_ON_OFF(digi));
    str_append(buffer, size, &used, "Tracker: %s\n", TELEGRAM_ON_OFF(trk));
    str_append(buffer, size, &used, "Weather: %s\n", TELEGRAM_ON_OFF(wx));
    str_append(buffer, size, &used, "Telemetry: %s\n", TELEGRAM_ON_OFF(tlm));
    str_append(buffer, size, &used, "Messaging: %s\n", TELEGRAM_ON_OFF(msg));
    str_append(buffer, size, &used, "Query responder: %s\n", TELEGRAM_ON_OFF(query));
    str_append(buffer, size, &used, "BrandMeister: %s\n", TELEGRAM_ON_OFF(bm));
    str_append(buffer, size, &used, "GNSS receiver: %s\n", TELEGRAM_ON_OFF(gps));
    str_append(buffer, size, &used, "AFSK modem: %s\n", TELEGRAM_ON_OFF(modem));
    str_append(buffer, size, &used, "TX duty-cycle limiter: %s\n", TELEGRAM_ON_OFF(duty));
    str_append(buffer, size, &used, "SNTP time sync: %s\n", TELEGRAM_ON_OFF(ntp));
    str_append(buffer, size, &used, "Free heap: %u bytes", (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
}

// Lines appended to the built-in /sensors answer: every weather field and
// every telemetry channel the operator has both enabled and mapped to a
// sensor channel, with the sensor driver each one resolves to and its
// current reading.
//
// Built from the live configuration on every call rather than from sensors
// registered once at bring-up, because the set is the operator's to change:
// a save on the Weather or Telemetry page re-maps channels while the bot
// keeps running, and the next answer must describe what is mapped then.
//
// An enabled field with no sensor channel assigned is skipped rather than
// listed, since it names nothing the operator can act on. A field that is
// enabled and mapped but whose sensor did not answer on the last refresh is
// still reported, with its reading shown as "no reading", because that is
// exactly what an operator asks this command about.
static void telegram_sensor_lines(char *buffer, size_t size, void *ctx) {
    (void)ctx;

    size_t used = 0;

    // Weather: the per-field enable and channel mapping live in g_config,
    // snapshotted here so no configuration lock is held while the weather
    // container's own lock is taken below.
    bool wx_enabled[WX_SENSOR_NUM];
    uint8_t wx_channel[WX_SENSOR_NUM];
    app_config_lock();
    memcpy(wx_enabled, g_config.wx_sensor_enable, sizeof(wx_enabled));
    memcpy(wx_channel, g_config.wx_sensor_ch, sizeof(wx_channel));
    app_config_unlock();

    bool header = false;
    for (int f = 0; f < WX_SENSOR_NUM; f++) {
        uint8_t ch = wx_channel[f];
        if (!wx_enabled[f] || ch == SENSOR_LOCAL_CH_NONE)
            continue;

        if (!header) {
            str_append(buffer, size, &used, "Weather sensors:\n");
            header = true;
        }

        const char *label = weather_field_label((wx_field_id_t)f);
        const char *source = sensors_local_channel_name(ch);

        double value = 0.0;
        char rendered[TELEGRAM_APP_VALUE_MAX];
        if (weather_field_snapshot((wx_field_id_t)f, &value))
            weather_field_format((wx_field_id_t)f, value, rendered, sizeof(rendered));
        else
            snprintf(rendered, sizeof(rendered), "no reading");

        str_append(buffer, size, &used, "- %s: %s (%s)\n", label, rendered, source);
    }

    // Telemetry: settings in their own file, readings from the snapshot the
    // 1 Hz refresh publishes.
    telemetry_config_t *tcfg = malloc(sizeof(*tcfg));
    if (tcfg == NULL) {
        // The weather part of the answer, if any, still stands; the telemetry
        // part is named as missing rather than silently absent.
        str_append(buffer, size, &used, "Telemetry channels: unavailable, not enough memory to read the settings\n");
        return;
    }
    telemetry_config_load(tcfg);

    telemetry_values_t values;
    telemetry_get_values(&values);

    header = false;
    for (int a = 0; a < TLM_CH; a++) {
        uint8_t ch = tcfg->tlm_ana_channel[a];
        if (!tcfg->ana_enable[a] || ch == SENSOR_LOCAL_CH_NONE)
            continue;

        if (!header) {
            str_append(buffer, size, &used, "Telemetry analog channels:\n");
            header = true;
        }

        const char *source = sensors_local_channel_name(ch);
        const char *name = tcfg->PARM[a];
        const char *unit = tcfg->UNIT[a];

        char rendered[TELEGRAM_APP_VALUE_MAX];
        if (values.analog_present[a])
            snprintf(rendered, sizeof(rendered), "%.*f %s", (int)tcfg->ana_dec[a], values.analog_raw[a], unit);
        else
            snprintf(rendered, sizeof(rendered), "no reading");

        str_append(buffer, size, &used, "- A%d %s: %s (%s)\n", a + 1, name, rendered, source);
    }

    header = false;
    for (int bit = 0; bit < TLM_BIT_NUM; bit++) {
        uint8_t ch = tcfg->tlm_bit_channel[bit];
        if (!tcfg->bit_enable[bit] || ch == SENSOR_LOCAL_CH_NONE)
            continue;

        if (!header) {
            str_append(buffer, size, &used, "Telemetry binary channels:\n");
            header = true;
        }

        const char *source = sensors_local_channel_name(ch);
        const char *name = tcfg->tlm_bit_name[bit];

        // Reported as it is transmitted: bit_sense selects whether a raw 1
        // means asserted, so an inverted bit reads the same way here as it
        // does on air.
        char rendered[TELEGRAM_APP_VALUE_MAX];
        if (values.digital_present[bit]) {
            bool state = tcfg->bit_sense[bit] ? values.digital_value[bit] : !values.digital_value[bit];
            snprintf(rendered, sizeof(rendered), "%d", state ? 1 : 0);
        } else {
            snprintf(rendered, sizeof(rendered), "no reading");
        }

        str_append(buffer, size, &used, "- B%d %s: %s (%s)\n", bit + 1, name, rendered, source);
    }

    free(tcfg);

    if (used == 0)
        str_append(buffer, size, &used, "No weather or telemetry channel is enabled and mapped to a sensor.\n");
}

// Asks Telegram who this bot is. This is the only step that proves the token
// end to end - the transport builds a URL out of any string, and the service
// starts a polling task whether or not the servers ever answer - so it is what
// turns "configured" into "connected" on the page, and it is where a rejected
// token is caught with the servers' own wording instead of a timeout.
// Appends the two heap figures that decide whether a TLS session can be set up
// at all. Free heap on its own is misleading here: mbedtls_ssl_setup() needs
// its record buffers in one contiguous piece each, so a fragmented heap fails
// with tens of kilobytes still free. Reporting both is what lets an operator
// tell "this board is out of memory" from "this board has memory but not in
// one piece".
static void append_heap_figures(char *detail, size_t detail_size) {
    size_t used = strlen(detail);
    if (used >= detail_size)
        return;
    snprintf(detail + used, detail_size - used, "%s(free heap %u, largest block %u)", used ? " " : "", (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

// Logs what the radio and the IP stack believe about the uplink.
//
// Reported at INFO on purpose: this firmware is built with the log ceiling at
// INFO, so anything logged at DEBUG does not exist in the binary. These four
// facts are the ones that separate "the link is fine" from "the link is up in
// name only", and they cost one line per bring-up attempt.
static void log_link_state(void) {
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
        ESP_LOGI(TAG, "Uplink: SSID \"%s\", channel %u, RSSI %d dBm", (const char *)ap.ssid, (unsigned)ap.primary, ap.rssi);
    else
        ESP_LOGW(TAG, "Uplink: not associated to an access point");

    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    if (sta != NULL && esp_netif_get_ip_info(sta, &ip) == ESP_OK)
        ESP_LOGI(TAG, "Address " IPSTR ", gateway " IPSTR ", netmask " IPSTR, IP2STR(&ip.ip), IP2STR(&ip.gw), IP2STR(&ip.netmask));

    esp_netif_dns_info_t dns;
    if (sta != NULL && esp_netif_get_dns_info(sta, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK)
        ESP_LOGI(TAG, "Name server " IPSTR, IP2STR(&dns.ip.u_addr.ip4));
    else
        ESP_LOGW(TAG, "No name server configured - every host lookup will run to its timeout");
}

// Opens a plain TCP connection to api.telegram.org:443 and closes it again,
// timing the name lookup and the connect separately.
//
// This exists because the three ways the bot can fail to reach Telegram are
// indistinguishable from the transport's error: a name that will not resolve,
// a route that will not carry a packet, and a TLS handshake the far end
// refuses all surface as one ESP_ERR_HTTP_CONNECT after the full timeout. The
// probe costs one socket and a few hundred milliseconds on a healthy station,
// and on an unhealthy one it says which of the three it is, with the number of
// milliseconds each stage took.
//
// A probe that succeeds and a handshake that then fails is itself the answer:
// it means name resolution and routing are sound and the fault is in TLS.
static telegram_app_reason_t net_probe(char *detail, size_t detail_size) {
    log_link_state();

    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res = NULL;

    int64_t t0 = esp_timer_get_time();
    int rc = getaddrinfo(TELEGRAM_PROBE_HOST, TELEGRAM_PROBE_PORT, &hints, &res);
    int dns_ms = (int)((esp_timer_get_time() - t0) / 1000);

    if (rc != 0 || res == NULL) {
        if (res != NULL)
            freeaddrinfo(res);
        snprintf(detail, detail_size, "DNS failed after %d ms ", dns_ms);
        ESP_LOGE(TAG, "Probe: name lookup of %s failed after %d ms (getaddrinfo %d)", TELEGRAM_PROBE_HOST, dns_ms, rc);
        return TELEGRAM_APP_REASON_DNS_FAILED;
    }

    struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
    char ip[16];
    esp_ip4_addr_t a = { .addr = addr->sin_addr.s_addr };
    esp_ip4addr_ntoa(&a, ip, sizeof(ip));
    ESP_LOGI(TAG, "Probe: %s resolved to %s in %d ms", TELEGRAM_PROBE_HOST, ip, dns_ms);

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
        snprintf(detail, detail_size, "no socket ");
        ESP_LOGE(TAG, "Probe: no socket available (errno %d)", errno);
        return TELEGRAM_APP_REASON_TCP_FAILED;
    }

    // Non-blocking connect plus select, because a blocking connect() ignores
    // the socket timeouts and would sit for the stack's own retry schedule.
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);

    t0 = esp_timer_get_time();
    rc = connect(sock, res->ai_addr, res->ai_addrlen);
    bool connected = (rc == 0);

    if (!connected && errno == EINPROGRESS) {
        fd_set wset;
        FD_ZERO(&wset);
        FD_SET(sock, &wset);
        struct timeval tv = {
            .tv_sec = TELEGRAM_PROBE_CONNECT_MS / 1000,
            .tv_usec = (TELEGRAM_PROBE_CONNECT_MS % 1000) * 1000,
        };
        if (select(sock + 1, NULL, &wset, NULL, &tv) > 0) {
            int err = 0;
            socklen_t len = sizeof(err);
            connected = (getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0);
            if (!connected)
                errno = err;
        }
    }

    int connect_ms = (int)((esp_timer_get_time() - t0) / 1000);
    int saved_errno = errno;

    close(sock);
    freeaddrinfo(res);

    if (!connected) {
        snprintf(detail, detail_size, "TCP to %s:443 failed after %d ms (errno %d) ", ip, connect_ms, saved_errno);
        ESP_LOGE(TAG, "Probe: TCP connect to %s:443 failed after %d ms (errno %d)", ip, connect_ms, saved_errno);
        return TELEGRAM_APP_REASON_TCP_FAILED;
    }

    ESP_LOGI(TAG, "Probe: TCP connect to %s:443 succeeded in %d ms - name resolution and routing are sound", ip, connect_ms);
    return TELEGRAM_APP_REASON_CONNECTED;
}

static telegram_app_reason_t verify_token(char *detail, size_t detail_size) {
    char *buf = malloc(TELEGRAM_GETME_BUF);
    if (buf == NULL)
        return TELEGRAM_APP_REASON_NO_MEMORY;
    buf[0] = 0;

    // Probed over a client of its own, with keep-alive disabled, which is then
    // destroyed. The transport's default handle would outlive this question and
    // hold its TLS session for the whole life of the bot, and this station
    // cannot afford a session that exists only to have answered one question at
    // start-up: a session costs its record buffers plus the contiguous block a
    // later handshake will need, and the service keeps exactly one alive at any
    // instant. Releasing this one before the service starts is what leaves that
    // budget intact.
    telegram_bot_client_config_t probe_cfg = {
        .timeout_ms = 15000,
        .rx_buffer_size = 1024,
        .disable_keep_alive = true,
    };

    telegram_bot_client_handle_t probe = NULL;
    esp_err_t err = telegram_bot_client_create(&probe_cfg, &probe);
    if (err != ESP_OK) {
        snprintf(detail, detail_size, "%s ", esp_err_to_name(err));
        append_heap_figures(detail, detail_size);
        free(buf);
        return (err == ESP_ERR_NO_MEM) ? TELEGRAM_APP_REASON_NO_MEMORY : TELEGRAM_APP_REASON_CONNECT_FAILED;
    }

    telegram_bot_request_t request = {
        .method = "getMe",
    };
    telegram_bot_response_t response = {
        .buffer = buf,
        .buffer_size = TELEGRAM_GETME_BUF,
    };

    err = telegram_bot_client_call(probe, &request, &response);

    // Released before anything is decided, so the session is gone whichever
    // way the probe went.
    telegram_bot_client_destroy(probe);

    if (err != ESP_OK) {
        // A refused TLS handshake and an exhausted heap both surface here as
        // one transport error, because the allocation that failed happened
        // several layers below this call. The figures are what separate them.
        snprintf(detail, detail_size, "%s ", esp_err_to_name(err));
        append_heap_figures(detail, detail_size);
        free(buf);
        return (err == ESP_ERR_NO_MEM) ? TELEGRAM_APP_REASON_NO_MEMORY : TELEGRAM_APP_REASON_CONNECT_FAILED;
    }

    cJSON *doc = cJSON_Parse(buf);
    free(buf);
    if (doc == NULL) {
        snprintf(detail, detail_size, "unreadable answer from api.telegram.org");
        return TELEGRAM_APP_REASON_CONNECT_FAILED;
    }

    telegram_app_reason_t reason;
    const cJSON *ok = cJSON_GetObjectItem(doc, "ok");
    if (cJSON_IsTrue(ok)) {
        const cJSON *result = cJSON_GetObjectItem(doc, "result");
        char name[TELEGRAM_APP_BOTNAME_MAX + 1] = "";
        if (cJSON_IsObject(result))
            get_string(result, "username", name, sizeof(name));
        status_set_bot_name(name);
        detail[0] = 0;
        reason = TELEGRAM_APP_REASON_CONNECTED;
    } else {
        // Telegram's own description is passed through untranslated. It is the
        // single most useful string in this whole path - "Unauthorized" for a
        // revoked or mistyped token, "Not Found" for one whose bot no longer
        // exists - and rewording it would only put this firmware between the
        // operator and the answer.
        const cJSON *code = cJSON_GetObjectItem(doc, "error_code");
        const cJSON *desc = cJSON_GetObjectItem(doc, "description");
        int code_n = cJSON_IsNumber(code) ? (int)code->valuedouble : 0;
        const char *desc_s = (cJSON_IsString(desc) && desc->valuestring != NULL) ? desc->valuestring : "no description";
        snprintf(detail, detail_size, "%d %s", code_n, desc_s);
        reason = TELEGRAM_APP_REASON_API_REJECTED;
    }

    cJSON_Delete(doc);
    return reason;
}

// Performs one full bring-up attempt, publishing the step that failed. On
// success the service is initialized, verified and polling.
//
// Called only from the worker task: it runs a TLS handshake on the caller's
// stack and must never run on the service tick.
static telegram_app_reason_t bring_up(void) {
    char detail[TELEGRAM_APP_DETAIL_MAX + 1] = "";

    // Snapshotted once, under the lock, before anything else runs. A save
    // landing on s_cfg while this attempt is mid-flight (net_probe() and the
    // TLS handshake below can take tens of seconds) must not be able to hand
    // this function a token, user list or chat list that is half-old,
    // half-new. `local` is an automatic variable that lives for the rest of
    // this call, so cfg.bot_token below can point straight into it: it stays
    // valid for as long as telegram_init()/telegram_start() need it, and it
    // can never be rewritten out from under them by telegram_app_apply_config().
    telegram_app_config_t local;
    lock();
    local = s_cfg;
    unlock();

    if (local.bot_token[0] == '\0')
        return TELEGRAM_APP_REASON_NO_TOKEN;
    if (!token_well_formed(local.bot_token))
        return TELEGRAM_APP_REASON_TOKEN_MALFORMED;

#if !CONFIG_TELEGRAM_BOT_CERT_BUNDLE
    telegram_app_reason_t cert = cert_precheck(detail, sizeof(detail));
    if (cert != TELEGRAM_APP_REASON_CONNECTED) {
        status_set(TELEGRAM_APP_STATE_ERROR, cert, detail);
        return cert;
    }
#endif

    // No route means no name resolution and no TLS. The wait itself belongs to
    // the tick, which spawned this task only once a route existed, so finding
    // none here means it went away in between and the attempt is simply
    // deferred rather than waited out on this stack.
    if (!net_state_is_connected()) {
        status_set(TELEGRAM_APP_STATE_STARTING, TELEGRAM_APP_REASON_WAITING_NETWORK, NULL);
        return TELEGRAM_APP_REASON_WAITING_NETWORK;
    }

    // Checked, together with the shared heap_monitor_try_heavy_op() lock,
    // before anything is allocated, so a station that is simply too busy
    // right now retries later instead of taking memory from services that
    // are already running. The lock is what stops this handshake from
    // landing in the same instant as the APRS-IS uplink's own TCP connect:
    // a free-heap floor checked here is only a snapshot of this instant, and
    // says nothing about a second heavy allocation starting a moment later,
    // so serializing the two is what actually prevents the pair from running
    // the heap dry together. Treated as the same transient, retry-later fault
    // as a heap that is genuinely too low, rather than a state of its own.
    if (!heap_monitor_try_heavy_op()) {
        ESP_LOGW(TAG, "Bring-up deferred, heavy network op lock held elsewhere");
        status_set(TELEGRAM_APP_STATE_ERROR, TELEGRAM_APP_REASON_NO_MEMORY, NULL);
        return TELEGRAM_APP_REASON_NO_MEMORY;
    }
    if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < TELEGRAM_MIN_FREE_BLOCK || heap_caps_get_free_size(MALLOC_CAP_8BIT) < TELEGRAM_MIN_FREE_HEAP) {
        append_heap_figures(detail, sizeof(detail));
        ESP_LOGW(TAG, "Bring-up deferred, not enough contiguous memory for a TLS session: %s", detail);
        status_set(TELEGRAM_APP_STATE_ERROR, TELEGRAM_APP_REASON_NO_MEMORY, detail);
        heap_monitor_release_heavy_op();
        return TELEGRAM_APP_REASON_NO_MEMORY;
    }

    // Run before anything is allocated, so a station that cannot reach the
    // servers says so cheaply instead of building a service it will have to
    // tear down again.
    telegram_app_reason_t probe = net_probe(detail, sizeof(detail));
    if (probe != TELEGRAM_APP_REASON_CONNECTED) {
        append_heap_figures(detail, sizeof(detail));
        status_set(TELEGRAM_APP_STATE_ERROR, probe, detail);
        heap_monitor_release_heavy_op();
        return probe;
    }

    status_set(TELEGRAM_APP_STATE_STARTING, TELEGRAM_APP_REASON_CONNECTED, NULL);

    telegram_service_config_t cfg = TELEGRAM_SERVICE_DEFAULT_CONFIG();
    cfg.bot_token = local.bot_token;
    cfg.admin_id = local.admin_id;
    cfg.device_name = APRS_SOFTWARE_NAME;
    cfg.status_cb = telegram_status_lines;
    cfg.sensors_cb = telegram_sensor_lines;
    // The reboot command is left out: this station carries a transmitter and a
    // scheduler with timed obligations, and the web admin already offers a
    // restart behind the admin login.
    cfg.allow_reboot = false;

    // Both of these send the instant the polling connection comes up, so they
    // ask for a second TLS session while the first one's buffers are still
    // held. On a station that also carries the radio modem's DMA buffers that
    // second session does not fit, and the only result is a pair of errors in
    // the log at every start-up. The command list they publish is a
    // convenience - the commands themselves work whether or not Telegram has
    // been told about them - and the start-up notice tells an administrator
    // something the web admin already shows.
    //
    // Both are worth turning back on if this firmware ever runs where two
    // concurrent sessions fit: a board with PSRAM, or one built with
    // CONFIG_MBEDTLS_DYNAMIC_BUFFER.
    cfg.publish_commands = false;
    cfg.announce_start = false;

    esp_err_t err = telegram_init(&cfg);
    if (err != ESP_OK) {
        snprintf(detail, sizeof(detail), "%s ", esp_err_to_name(err));
        append_heap_figures(detail, sizeof(detail));

        // ESP_ERR_NO_MEM out of the service means one of two unrelated
        // things: the heap could not satisfy an allocation, or one of the
        // service's fixed-size tables was too small for what it registers.
        // The heap figures decide which. Blaming the heap when it is plainly
        // healthy sends the operator hunting for memory that is already
        // there, so a failure with room to spare is reported as an
        // initialization fault whose detail carries the numbers that rule
        // memory out.
        bool heap_is_healthy =
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= TELEGRAM_MIN_FREE_BLOCK && heap_caps_get_free_size(MALLOC_CAP_8BIT) >= TELEGRAM_MIN_FREE_HEAP;
        telegram_app_reason_t reason = (err == ESP_ERR_NO_MEM && !heap_is_healthy) ? TELEGRAM_APP_REASON_NO_MEMORY : TELEGRAM_APP_REASON_INIT_FAILED;
        status_set(TELEGRAM_APP_STATE_ERROR, reason, detail);
        heap_monitor_release_heavy_op();
        return reason;
    }

    if (!s_token_verified) {
        telegram_app_reason_t verified = verify_token(detail, sizeof(detail));
        if (verified != TELEGRAM_APP_REASON_CONNECTED) {
            telegram_deinit();
            status_set(TELEGRAM_APP_STATE_ERROR, verified, detail);
            heap_monitor_release_heavy_op();
            return verified;
        }
        s_token_verified = true;
    }

    // Seeded after the token is known good, so a rejected token never leaves
    // half a service configured. The administrator is already in the list -
    // telegram_init() adds admin_id itself - and re-adding an identifier the
    // component already holds updates that entry rather than consuming a slot.
    for (uint8_t i = 0; i < local.user_count; i++)
        telegram_add_user(local.users[i].id, local.users[i].name, false);
    for (uint8_t i = 0; i < local.chat_count; i++)
        telegram_allow_chat(local.chats[i].id, local.chats[i].name);

    err = telegram_start();

    // Released here regardless of outcome: the bring-up window this lock
    // protects ends the moment telegram_start() returns, whether it handed
    // back a running polling connection or a failure telegram_deinit()
    // already unwound. A connection that did come up settles onto keep-alive
    // from here on, which is why the lock only needs to span the handshake
    // above, not the life of the resulting session.
    heap_monitor_release_heavy_op();

    if (err != ESP_OK) {
        snprintf(detail, sizeof(detail), "%s ", esp_err_to_name(err));
        append_heap_figures(detail, sizeof(detail));
        telegram_deinit();
        status_set(TELEGRAM_APP_STATE_ERROR, TELEGRAM_APP_REASON_TASK_FAILED, detail);
        return TELEGRAM_APP_REASON_TASK_FAILED;
    }

    status_set(TELEGRAM_APP_STATE_RUNNING, TELEGRAM_APP_REASON_CONNECTED, NULL);
    ESP_LOGI(TAG, "Telegram bot running");
    return TELEGRAM_APP_REASON_CONNECTED;
}

// True for the faults that can clear without the operator touching anything,
// and which are therefore worth attempting again after a pause.
static bool reason_is_transient(telegram_app_reason_t reason) {
    return reason == TELEGRAM_APP_REASON_WAITING_NETWORK || reason == TELEGRAM_APP_REASON_NO_MEMORY || reason == TELEGRAM_APP_REASON_CONNECT_FAILED ||
           reason == TELEGRAM_APP_REASON_INIT_FAILED || reason == TELEGRAM_APP_REASON_TASK_FAILED || reason == TELEGRAM_APP_REASON_DNS_FAILED ||
           reason == TELEGRAM_APP_REASON_TCP_FAILED;
}

// Carries out one pending action, if any, then drains whatever the routed-
// notification queue holds, and exits.
//
// A bring-up, a teardown and a notification drain are the same class of work
// - at most one Telegram API call held open at a time - so one task does
// whichever is pending and then folds a drain into the same pass before it
// gives its stack back, instead of a second task being spawned for it.
// TELEGRAM_ACTION_NONE is what a spawn asking only for a drain passes: the
// action stage is skipped entirely and the task goes straight to the queue.
//
// The task is spawned by the tick or by whichever notify entry point queued
// something, does its work and deletes itself, which is what returns its
// stack to the heap between jobs. It is also the only place telegram_deinit()
// may run: that call waits for the service's polling task to leave a long
// poll, which must not happen on the service tick or on the web server's
// stack.
static void telegram_worker_task(void *arg) {
    telegram_action_t action = (telegram_action_t)(intptr_t)arg;
    bool stopped = false;

    if (action != TELEGRAM_ACTION_NONE) {
        if (action == TELEGRAM_ACTION_STOP || s_service_up) {
            if (s_service_up) {
                telegram_deinit();
                s_service_up = false;
                ESP_LOGI(TAG, "Telegram bot stopped");
            }
            if (action == TELEGRAM_ACTION_STOP) {
                status_reset(TELEGRAM_APP_STATE_DISABLED, TELEGRAM_APP_REASON_DISABLED);
                s_retry_in = TELEGRAM_RETRY_NEVER;
                stopped = true;
            }
        }

        if (!stopped) {
            telegram_app_reason_t reason = bring_up();

            if (reason == TELEGRAM_APP_REASON_CONNECTED) {
                s_service_up = true;
                s_retry_in = TELEGRAM_RETRY_NEVER;
                start_notice_arm();
            } else if (reason_is_transient(reason)) {
                s_retry_in = TELEGRAM_RETRY_SECONDS;
            } else {
                // Nothing changes until the operator changes it, and changing
                // it goes through the Telegram page, which arms a fresh
                // attempt. So the fault stays published and no retry is
                // scheduled.
                ESP_LOGE(TAG, "Telegram bring-up stopped, waiting for a configuration change");
                s_retry_in = TELEGRAM_RETRY_NEVER;
            }

            // Reported so the stack above can be cut to what this task
            // actually uses instead of the figure it was guessed at. Sized
            // for a TLS handshake, this is the largest stack the bot
            // allocates, and it is the one worth measuring before trimming.
            ESP_LOGI(TAG, "Worker finished, stack high-water %u bytes free of %d", (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)),
                     TELEGRAM_WORKER_STACK_BYTES);
        }
    }

    // The whole pass is one Telegram transmit batch, so every line sent here -
    // the start-up notice and every drained item, fan-outs included - travels
    // over the session a bring-up above just opened, or over a session of its
    // own when this task was spawned for the queue alone. The batch is opened
    // only when there is something to put in it, since opening one releases
    // the polling connection.
    if (s_start_notice_pending || s_notify_queue != NULL) {
        telegram_tx_batch_begin();

        // First in the batch: an operator reading the chat sees that the
        // station restarted before whatever else the same pass carries. A
        // notice is only ever ready here once start_notice_tick() has released
        // it, which is why the tail of the bring-up that armed it goes past
        // this without sending anything.
        if (s_start_notice_pending)
            start_notice_send();

        if (s_notify_queue != NULL) {
            telegram_notify_item_t item;
            while (xQueueReceive(s_notify_queue, &item, 0) == pdTRUE) {
                if (item.broadcast)
                    notify_broadcast(item.text);
                else
                    notify_send_one(item.chat_id, item.text);
            }
        }

        telegram_tx_batch_end();
    }

    // Between the last empty receive above and this task actually deleting
    // itself there is a window where a new item can be queued while the
    // worker slot is still held, so notify_worker_spawn() skips spawning for
    // it. The slot is released first and the queue is looked at again
    // afterwards, so that item leaves on a fresh spawn instead of waiting for
    // a routed line that may never come: a bulletin repeats, but its repeats
    // are recognised as such and dropped, so nothing would arrive to trigger
    // the spawn. This is also what delivers whatever was routed while a
    // bring-up held the slot, instead of leaving it there until the next
    // routed line asks for a drain.
    worker_release();
    notify_worker_kick();
    vTaskDelete(NULL);
}

// Spawns the shared worker for one action, or for a queue drain alone when
// action is TELEGRAM_ACTION_NONE, if the slot is free. Reports whether it
// did, because a slot held by another spawn only postpones the action: the
// caller keeps it pending and offers it again a second later, and treating a
// refused spawn as a performed one would drop the operator's enable or
// disable on the floor.
static bool worker_spawn(telegram_action_t action) {
    if (!worker_claim())
        return false;

    if (xTaskCreate(telegram_worker_task, "telegram_wk", TELEGRAM_WORKER_STACK_BYTES, (void *)(intptr_t)action, TELEGRAM_WORKER_PRIORITY, NULL) != pdPASS) {
        worker_release();
        ESP_LOGE(TAG, "Worker task could not be created");
        if (action != TELEGRAM_ACTION_NONE) {
            status_reset(TELEGRAM_APP_STATE_ERROR, TELEGRAM_APP_REASON_TASK_FAILED);
            s_retry_in = TELEGRAM_RETRY_SECONDS;
        }
    }

    // Reported as spawned either way: a task that could not be created has
    // published its fault and armed a retry, so the action has been dealt
    // with and must not be offered again by the caller.
    return true;
}

// One second of a pending start-up notice.
//
// Runs only while the service is up and the worker slot is free, since it is
// the tick's running branch that calls it. Spawning the shared worker with no
// action is what delivers the notice: that pass opens a transmit batch, finds
// the notice pending and sends it before draining whatever else is queued.
static void start_notice_tick(void) {
    if (s_start_notice_giveup > 0)
        s_start_notice_giveup--;

    if (s_start_notice_giveup == 0) {
        s_start_notice_owed = false;
        ESP_LOGW(TAG, "Start-up notice dropped, no moment with room for a transmit session");
        return;
    }

    if (s_start_notice_wait > 0) {
        s_start_notice_wait--;
        return;
    }

    // The same two floors a bring-up tests, for the same reason: a send opens
    // a TLS session of its own and the polling connection pays for another one
    // when it takes its turn back, so a heap that cannot hold that pair is
    // asked again a second later instead of being made to fail twice.
    if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < TELEGRAM_MIN_FREE_BLOCK || heap_caps_get_free_size(MALLOC_CAP_8BIT) < TELEGRAM_MIN_FREE_HEAP)
        return;

    // Handed over before the spawn, so a spawn refused because another worker
    // holds the slot costs nothing: that worker's own tail finds the notice
    // ready and takes it.
    s_start_notice_owed = false;
    s_start_notice_pending = true;
    notify_worker_spawn();
}

void telegram_app_tick_1hz(void) {
    // A pending enable or disable takes priority over anything else: it is the
    // operator's most recent instruction.
    telegram_action_t pending = s_action;
    if (pending != TELEGRAM_ACTION_NONE) {
        // Cleared only once the worker owns it. The slot can be taken between
        // the test above and the claim inside worker_spawn() - a notification
        // drain is spawned from the modem's receive task, from the APRS-IS
        // client's task and from the beacon scheduler, none of which is
        // serialized with this tick - and an action cleared for a worker that
        // was never created is one the operator has to save the page again to
        // repeat.
        if (worker_spawn(pending))
            s_action = TELEGRAM_ACTION_NONE;
        return;
    }

    if (!s_enabled || s_worker_busy)
        return;

    if (s_service_up) {
        telegram_stats_t st;
        if (telegram_get_stats(&st) == ESP_OK)
            status_set_counters(&st);

        // A service whose polling task has gone needs a teardown before
        // anything can be rebuilt, and the worker is the only place that may
        // run one.
        if (!telegram_is_running()) {
            ESP_LOGW(TAG, "Service stopped unexpectedly, restarting");
            status_set(TELEGRAM_APP_STATE_ERROR, TELEGRAM_APP_REASON_CONNECT_FAILED, "polling task ended");
            worker_spawn(TELEGRAM_ACTION_START);
            return;
        }

        // Said out loud rather than left to show as a polling-error count that
        // climbs for no stated reason.
        if (!net_state_is_connected()) {
            status_set(TELEGRAM_APP_STATE_STARTING, TELEGRAM_APP_REASON_WAITING_NETWORK, NULL);
            return;
        }

        status_set(TELEGRAM_APP_STATE_RUNNING, TELEGRAM_APP_REASON_CONNECTED, NULL);

        // Last, and only on a bot that is both up and reachable: the notice
        // is worth a transmit session only where a routed line would be.
        if (s_start_notice_owed)
            start_notice_tick();
        return;
    }

    if (s_retry_in == TELEGRAM_RETRY_NEVER)
        return;

    if (s_retry_in > 0) {
        s_retry_in--;
        return;
    }

    // The route is checked here, on a task that costs nothing to keep waiting,
    // so a station that boots before its access point does not hold a
    // handshake-sized stack while it waits.
    if (!net_state_is_connected()) {
        status_set(TELEGRAM_APP_STATE_STARTING, TELEGRAM_APP_REASON_WAITING_NETWORK, NULL);
        return;
    }

    worker_spawn(TELEGRAM_ACTION_START);
}

void telegram_app_apply_config(void) {
    telegram_app_config_t cfg;
    bool read_ok = telegram_app_load(&cfg);
    json_store_status_t read_status = s_last_read_status;

    lock();

    // Put the queue in place before the routing switches below are published,
    // so a line routed the instant they go on already has somewhere to go.
    // This is the only place the queue is created: the two notify entry points
    // are reached from the modem's receive task and from the APRS-IS client's
    // task and only ever read the handle, and this call is serialized with
    // every other one by the same lock a save takes.
    if (cfg.enable && (cfg.route_station_messages || cfg.route_bulletins))
        notify_queue_ensure();

    s_cfg = cfg;
    s_enabled = cfg.enable;
    s_route_messages = cfg.route_station_messages;
    memcpy(s_route_users, cfg.users, sizeof(s_route_users));
    s_route_user_count = cfg.user_count;
    s_route_bulletins = cfg.route_bulletins;
    s_bulletin_window_s = cfg.bulletin_window_s;
    memcpy(s_route_chats, cfg.chats, sizeof(s_route_chats));
    s_route_chat_count = cfg.chat_count;
    s_route_admin_id = cfg.admin_id;
    unlock();

    if (!cfg.enable) {
        // A file that could not be read is reported even with the switch off,
        // because "off" and "unreadable" call for different actions and the
        // page has no other way to tell them apart.
        if (!read_ok && read_status != JSON_STORE_MISSING) {
            telegram_app_reason_t reason = (read_status == JSON_STORE_OOM) ? TELEGRAM_APP_REASON_FILE_UNREADABLE : TELEGRAM_APP_REASON_FILE_CORRUPT;
            status_reset(TELEGRAM_APP_STATE_ERROR, reason);
            status_set(TELEGRAM_APP_STATE_ERROR, reason, TELEGRAM_APP_PATH);
        }
        // Queued rather than performed here. Releasing the service waits for
        // its polling task to leave a long poll, which can take tens of
        // seconds, and this runs on the web server's stack while a browser
        // waits for the save to answer.
        s_action = TELEGRAM_ACTION_STOP;
        return;
    }

    // Settings may have moved under a running bot, and the token is only read
    // at initialization, so the queued action restarts it: the worker tears
    // down whatever is up before bringing the new configuration on.
    status_reset(TELEGRAM_APP_STATE_STARTING, TELEGRAM_APP_REASON_WAITING_NETWORK);
    s_token_verified = false;
    s_retry_in = 0;
    s_action = TELEGRAM_ACTION_START;
}
