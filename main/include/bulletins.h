/**
 * @file bulletins.h
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
 * @brief APRS bulletin store and periodic transmitter.
 *
 * A bulletin is an APRS message addressed to a "BLN" group, space-padded to
 * the 9-char APRS message addressee field, with no message number/ack. This
 * firmware supports ::BULLETIN_COUNT independently configurable bulletins,
 * edited on the "Bulletins" web admin page, and all three addressee forms
 * APRS101 chapter 14 defines:
 *
 *   - General bulletin:  "BLN1     " - identifier '0'..'9', no group name.
 *     Bulletins sharing an identifier replace one another on the receiver, so
 *     the identifier doubles as a slot number for multi-line bulletins.
 *   - Group bulletin:    "BLN1WX   " - identifier '0'..'9' followed by a
 *     group name of up to ::BULLETIN_GROUP_MAX characters. Only stations
 *     subscribed to that group display it.
 *   - Announcement:      "BLNQ     " - identifier 'A'..'Z' and no group name.
 *     Announcements are kept and re-displayed for far longer than bulletins by
 *     most client software, which is why the spec gives them their own
 *     identifier space.
 *
 * NTS Radiograms (also described in chapter 14) are a traffic-handling message
 * format rather than a bulletin addressee form, and are not produced here.
 *
 * Unlike almost everything else in the web admin, bulletins deliberately do
 * NOT live in the resident g_config struct: they persist to their own small
 * LittleFS file (/storage/bulletins.json) and are read back on demand (page
 * render, save, and once per transmit cycle). Keeping them out of g_config
 * keeps the always-resident configuration - and every app_config_save() - the
 * same size regardless of bulletin text length, which matters on this build's
 * tight heap.
 *
 * Expiry: each bulletin may carry an "expire after N hours" window. When set
 * and armed (enabled with a valid wall clock at save time), an absolute
 * deadline is stored. Once that deadline passes, the transmit task clears the
 * bulletin's `enable` flag, persists the change, and stops sending it - so an
 * expired bulletin both disappears from the air and shows up unchecked in the
 * web UI without any user action.
 */

#ifndef BULLETINS_H
#define BULLETINS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Number of independently configurable bulletins ("5 boxes").
 */
#define BULLETIN_COUNT 5

/**
 * @brief Maximum bulletin group-name length, in characters.
 *
 * The APRS message addressee field is 9 characters: "BLN" plus the
 * one-character identifier leaves 5 for the group name.
 */
#define BULLETIN_GROUP_MAX 5

/**
 * @brief Maximum APRS bulletin message text length, in characters.
 *
 * Same limit as an APRS text message body (APRS101). Text is clamped to this
 * on save.
 */
#define BULLETIN_TEXT_MAX 67

/**
 * @brief One configured bulletin.
 */
typedef struct {
    bool enable;                        /**< Master on/off. Auto-cleared once expired. */
    bool send_rf;                       /**< Transmit this bulletin on RF. */
    bool send_inet;                     /**< Transmit this bulletin to APRS-IS (Internet). */
    char ident;                         /**< Addressee identifier: '0'..'9' = bulletin, 'A'..'Z' = announcement. 0 or invalid = the slot's own default digit. */
    char group[BULLETIN_GROUP_MAX + 1]; /**< Group name for a group bulletin, empty for a general bulletin. Ignored when @c ident is an announcement letter. */
    char text[BULLETIN_TEXT_MAX + 1];   /**< Bulletin message text (NUL-terminated). */
    uint32_t interval_s;                /**< Transmit interval in seconds; 0 = firmware default. */
    uint32_t expire_hours;              /**< Expire window in hours; 0 = never expires. */
    int64_t expire_at;                  /**< Absolute expiry deadline (epoch seconds); 0 = never. */
} bulletin_t;

/**
 * @brief The whole set of bulletins, as loaded from / saved to LittleFS.
 */
typedef struct {
    bulletin_t item[BULLETIN_COUNT]; /**< Fixed-size slot array; a slot is inactive when its @c enable flag is clear, so indices stay stable across saves. */
} bulletins_t;

/**
 * @brief Load the bulletin set from /storage/bulletins.json into @p out.
 *
 * Missing/empty/corrupt file is not an error: @p out is filled with
 * all-disabled, empty defaults so callers always get a usable structure.
 *
 * @param out Destination (must be non-NULL).
 * @return true if a valid file was parsed, false if defaults were substituted.
 */
bool bulletins_load(bulletins_t *out);

/**
 * @brief Persist @p in to /storage/bulletins.json (atomic: tmp file + rename).
 *
 * Text fields are clamped to ::BULLETIN_TEXT_MAX. This does NOT (re)compute
 * expiry deadlines - callers that change enable/expire_hours should arm the
 * deadlines first with bulletins_arm_expiry().
 *
 * @param in Source set (must be non-NULL).
 * @return true on success.
 */
bool bulletins_save(const bulletins_t *in);

/**
 * @brief Build the 9-character, space-padded APRS addressee a bulletin is
 * transmitted to ("BLN1     ", "BLN1WX   ", "BLNQ     ").
 *
 * Normalizes as it goes, so a hand-edited bulletins.json can never put an
 * illegal addressee on the air: an identifier that is not '0'..'9' or
 * 'A'..'Z' falls back to the slot's own default digit ('1' + @p idx), the
 * group name is uppercased and stripped of anything outside A-Z/0-9, and an
 * announcement identifier suppresses the group name entirely (APRS101 gives
 * announcements no group field).
 *
 * @param b   Bulletin to address.
 * @param idx Slot index 0..::BULLETIN_COUNT-1, source of the fallback digit.
 * @param out Destination buffer, NUL-terminated on return.
 * @param out_size Size of @p out; must be at least 10 for the full addressee.
 */
void bulletins_build_addressee(const bulletin_t *b, int idx, char *out, size_t out_size);

/**
 * @brief Recompute every bulletin's absolute expiry deadline from its
 * `enable`/`expire_hours` fields, using the current wall clock.
 *
 * For each bulletin: if it is enabled and expire_hours > 0 and the wall clock
 * is valid, expire_at is set to now + expire_hours*3600; otherwise expire_at
 * is cleared (0 = never). Call this from the web save handler, after parsing
 * the form and before bulletins_save(), so that saving (re)arms the expiry
 * window from the moment of save.
 *
 * @param b Set to update in place.
 */
void bulletins_arm_expiry(bulletins_t *b);

/**
 * @brief Apply expiry to a loaded set: disable any enabled bulletin whose
 * absolute deadline has already passed (and clear its deadline).
 *
 * Does nothing if the wall clock is not yet valid (unsynced), so a bulletin
 * is never expired against a bogus boot-time clock. Used by both the transmit
 * task and the web page (so an expired bulletin shows up unchecked in the UI).
 *
 * @param b Set to update in place.
 * @return true if any bulletin was disabled (caller should persist).
 */
bool bulletins_apply_expiry(bulletins_t *b);

/**
 * @brief Prepare the bulletin subsystem (creates the LittleFS lock and logs
 * the configured state). The bulletins are not transmitted from their own
 * task: they are driven by the shared beacon scheduler
 * (beacon_scheduler_start()), which calls ::bulletins_service. Safe to call
 * once from app startup.
 */
void bulletins_start(void);

/**
 * @brief Service the bulletin transmitter: transmit each enabled, non-expired
 * bulletin whose per-bulletin interval is due, enforce expiry (disabling and
 * persisting any bulletin whose deadline has passed), and return the number of
 * seconds until the transmitter next needs servicing (always >= 1, capped so
 * web edits and expiry are picked up promptly).
 *
 * The first call returns a one-time boot settle delay without transmitting.
 * Intended to be called only from the shared beacon scheduler task.
 */
uint32_t bulletins_service(void);

#endif // BULLETINS_H
