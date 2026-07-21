/**
 * @file objects_items.h
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
 * @brief APRS Object / Item store and periodic transmitter.
 *
 * An APRS Object (APRS101 ch.11) is a tracked asset that is not itself a
 * transmitting station: some station transmits reports on its behalf. This
 * firmware supports ::OBJITEM_COUNT independently configurable Objects/Items,
 * edited on the "Objects and Items" web admin page.
 *
 * On-air wire format (built by ::objitem_build_info_field):
 *
 *   Object (timestamped):
 *     ;NAMExxxxx*DDHHMMz<lat>/<lon><sym>CSE/SPD<comment>
 *     - name is EXACTLY 9 chars, space-padded
 *     - '*' = live, '_' = killed
 *     - DDHHMMz = zulu day/hour/minute timestamp
 *
 *   Item (permanent / non-timestamped):
 *     )NAME!<lat>/<lon><sym>CSE/SPD<comment>
 *     - name is 3..9 chars, variable length
 *     - '!' = live, '_' = killed  (the char right after the name)
 *
 * The choice between Object and Item mirrors YAAC's "Permanent" flag: a
 * permanent asset is sent as a (non-timestamped) Item, a time-relevant asset
 * as a (timestamped) Object. See the YAAC object editor documentation:
 *   https://www.ka2ddo.org/ka2ddo/YAACdocs/objecteditor.html
 *
 * RAM policy (identical to bulletins.h): Objects/Items deliberately do NOT
 * live in the resident g_config struct. They persist to their own small
 * LittleFS file (/storage/objitems.json) and are read back on demand (page
 * render, save, and once per transmit cycle). Keeping them out of g_config
 * keeps the always-resident configuration - and every app_config_save() - the
 * same size regardless of Object/Item content, which matters on this build's
 * tight heap. Nothing here is held resident between transmit passes except a
 * few file-scope schedule timestamps (see objects_items.c).
 *
 * "Kill" behaviour (APRS101): a killed Object/Item is transmitted a small
 * number of extra times (::OBJITEM_KILL_REPEATS) so listeners reliably drop
 * it, after which its `enable` flag is cleared and the change persisted - so a
 * killed asset both leaves the air and shows up disabled in the web UI without
 * any further user action, mirroring how bulletins auto-clear on expiry.
 */

#ifndef OBJECTS_ITEMS_H
#define OBJECTS_ITEMS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Number of independently configurable Objects/Items ("5 blocks").
#define OBJITEM_COUNT 5

// APRS Object name field width. An Object name is EXACTLY 9 characters,
// space-padded on air; an Item name is 3..9 characters. We store up to 9
// characters plus a NUL; padding/trimming happens only at transmit time.
#define OBJITEM_NAME_MAX 9

// Maximum free-text comment length, in characters. Kept well under the APRS
// info-field ceiling so name+timestamp+position+symbol+course/speed+comment
// always fit a single info field with margin. Text is clamped on save.
#define OBJITEM_COMMENT_MAX 43

// How many times a freshly-killed Object/Item is still transmitted (as a kill
// report) before its `enable` flag is auto-cleared. APRS101 recommends a few
// repeats so every listener drops the asset.
#define OBJITEM_KILL_REPEATS 3

/**
 * @brief Scope of transmission (mirrors YAAC's "Scope" choicebox).
 *
 * - PRIVATE: never transmitted; visible only in this station's own config.
 * - LOCAL:   transmitted on RF only, never forwarded to APRS-IS.
 * - GLOBAL:  eligible for both RF and APRS-IS (subject to the per-element
 *            Send-via-RF / Send-via-Internet checks).
 *
 * The effective RF/INET decision is the AND of scope and the two per-element
 * checkboxes (see objitem_effective_rf/objitem_effective_inet), so scope acts
 * as an upper bound and the checkboxes as the fine control the task requested.
 */
typedef enum {
    OBJITEM_SCOPE_PRIVATE = 0,
    OBJITEM_SCOPE_LOCAL = 1,
    OBJITEM_SCOPE_GLOBAL = 2,
} objitem_scope_t;

/**
 * @brief One configured APRS Object or Item.
 *
 * Field order groups the three request-mandated checks first, then identity,
 * then the YAAC-derived on-air parameters. Packed layout is deliberately
 * compact (see objects_items.c static_assert) to keep the on-demand load
 * buffer small.
 */
typedef struct {
    bool enable;      /**< Master on/off. Auto-cleared after a kill finishes retransmitting. */
    bool send_rf;     /**< Transmit on RF (gated further by scope). */
    bool send_inet;   /**< Transmit to APRS-IS / Internet (gated further by scope). */

    bool is_item;     /**< true => Item (non-timestamped, ')'); false => Object (timestamped, ';'). Mirrors YAAC "Permanent". */
    bool active;      /**< true => live report; false => kill report (YAAC "Object active"). */

    char name[OBJITEM_NAME_MAX + 1]; /**< Object/Item name (1..9 chars, NUL-terminated). */

    float lat;        /**< Latitude, decimal degrees (N positive). */
    float lon;        /**< Longitude, decimal degrees (E positive). */

    char sym[2];      /**< APRS symbol: sym[0] = table ('/', '\\', or overlay char), sym[1] = code. */

    uint16_t course;  /**< Course over ground, degrees 0..359 (0 with speed 0 => omitted). */
    uint16_t speed;   /**< Speed, knots (0 => course/speed omitted, per YAAC). */

    objitem_scope_t scope; /**< Transmission scope (see objitem_scope_t). */

    char comment[OBJITEM_COMMENT_MAX + 1]; /**< Free-text comment, appended last. */

    uint32_t interval_s; /**< Transmit interval in seconds; 0 = firmware default. */

    uint8_t kill_left;   /**< Runtime: remaining kill retransmissions (not user-edited; persisted so a reboot mid-kill still completes). */
} objitem_t;

/**
 * @brief The whole set of Objects/Items, as loaded from / saved to LittleFS.
 */
typedef struct {
    objitem_t item[OBJITEM_COUNT];
} objitems_t;

/**
 * @brief Load the Object/Item set from /storage/objitems.json into @p out.
 *
 * Missing/empty/corrupt file is not an error: @p out is filled with
 * all-disabled, empty defaults so callers always get a usable structure.
 *
 * @param out Destination (must be non-NULL).
 * @return true if a valid file was parsed, false if defaults were substituted.
 */
bool objitems_load(objitems_t *out);

/**
 * @brief Persist @p in to /storage/objitems.json (atomic: tmp file + rename).
 *
 * Name and comment are clamped to their maxima. Written token-by-token
 * straight to the file (no cJSON tree, no second serialized buffer), the same
 * low-RAM approach app_config_save() and bulletins_save() use.
 *
 * @param in Source set (must be non-NULL).
 * @return true on success.
 */
bool objitems_save(const objitems_t *in);

/**
 * @brief Prepare the subsystem (creates the LittleFS lock and logs state).
 *
 * Objects/Items are transmitted from the shared beacon scheduler
 * (beacon_scheduler_start()), which calls ::objitems_service. Safe to call
 * once from app startup, before beacon_scheduler_start().
 */
void objitems_start(void);

/**
 * @brief Service the Object/Item transmitter: transmit each enabled element
 * whose per-element interval is due, advance/finish any kill sequence
 * (disabling and persisting an element once its kill repeats are exhausted),
 * and return the number of seconds until the transmitter next needs servicing
 * (always >= 1, capped so web edits are picked up promptly).
 *
 * The first call returns a one-time boot settle delay without transmitting.
 * Intended to be called only from the shared beacon scheduler task.
 */
uint32_t objitems_service(void);

#endif // OBJECTS_ITEMS_H
