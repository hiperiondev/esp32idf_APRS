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
 *   DF (direction-finding) report (APRS101 ch.16), when @c df_enable is set:
 *   the CSE/SPD block above is extended to CSE/SPD/BRG/NRQ, where BRG is the
 *   3-digit signal bearing and NRQ is the 3-digit number-of-hits/range/
 *   quality code - e.g. "088/036/270/729" for a station moving at course 088
 *   speed 036 with a bearing of 270 and NRQ 729.
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

/**
 * @brief Number of independently configurable Objects/Items ("5 blocks").
 */
#define OBJITEM_COUNT 5

/**
 * @brief APRS Object/Item name field width.
 *
 * An Object name is EXACTLY 9 characters, space-padded on air; an Item name is
 * 3..9 characters. Up to 9 characters plus a NUL are stored; padding/trimming
 * happens only at transmit time.
 */
#define OBJITEM_NAME_MAX 9

/**
 * @brief Minimum APRS Item name length (APRS101 Chapter 11). Only applies
 * when ::objitem_t.is_item is true; Object names are always exactly
 * ::OBJITEM_NAME_MAX characters, space-padded on air.
 */
#define OBJITEM_NAME_MIN 3

/**
 * @brief Maximum free-text comment length, in characters.
 *
 * Kept well under the APRS info-field ceiling so name+timestamp+position+
 * symbol+course/speed+comment always fit a single info field with margin.
 * Text is clamped on save.
 */
#define OBJITEM_COMMENT_MAX 43

/**
 * @brief How many times a freshly-killed Object/Item is still transmitted (as
 * a kill report) before its `enable` flag is auto-cleared.
 *
 * APRS101 recommends a few repeats so every listener drops the asset.
 */
#define OBJITEM_KILL_REPEATS 3

/**
 * @brief Signpost text length.
 *
 * A Signpost symbol ("\m") carries up to three characters, emitted on air as
 * {TEXT} immediately after the symbol code (APRS101 ch.6). Used only when the
 * element's symbol is the Signpost symbol.
 */
#define OBJITEM_SIGNPOST_MAX 3

/**
 * @brief DF (direction-finding) report bearing value meaning "no bearing".
 *
 * APRS101 ch.16: a DF report with no valid bearing uses 000 as the /BRG
 * field and 000 as the /NRQ number-of-hits/range/quality field, so a
 * receiver can tell an omnidirectional signal-strength-only report apart
 * from a real bearing/quality report. 0 is therefore not itself a valid
 * bearing on air.
 */
#define OBJITEM_DF_NO_BEARING 0

/**
 * @brief QRU group-membership name length.
 *
 * Mirrors YAAC's "QRU group membership" field: a short group tag (e.g. "HOSP",
 * "FUEL", "RP2M"). Stored/persisted so a future QRU responder can enumerate
 * group members.
 */
#define OBJITEM_QRU_MAX 8

/**
 * @brief Number of digipeat-path presets an Object/Item can choose from.
 *
 * Matches the four shared preset slots g_config.path[0..3] that beacon.c /
 * weather.c also select from, so the whole firmware speaks one path
 * vocabulary. An element's @c path_mask is a bitmask over these four presets
 * (bit i => g_config.path[i]).
 */
#define OBJITEM_PATH_PRESETS 4

/**
 * @brief Scope of transmission (mirrors YAAC's "Scope" choicebox).
 *
 * The effective RF/INET decision is the AND of scope and the two per-element
 * checkboxes (see objitem_effective_rf/objitem_effective_inet), so scope acts
 * as an upper bound and the checkboxes as the fine control.
 */
typedef enum {
    OBJITEM_SCOPE_PRIVATE = 0, /**< Never transmitted; visible only in this station's own config. */
    OBJITEM_SCOPE_LOCAL = 1,   /**< Transmitted on RF only, never forwarded to APRS-IS. */
    OBJITEM_SCOPE_GLOBAL = 2,  /**< Eligible for both RF and APRS-IS (subject to the per-element Send checks). */
} objitem_scope_t;

/**
 * @brief One configured APRS Object or Item.
 *
 * Field order groups the three request-mandated checks first, then identity,
 * then the YAAC-derived on-air parameters. Packed layout is deliberately
 * compact (see objects_items.c static_assert) to keep the on-demand load
 * buffer small.
 *
 * @details Optional on-air blocks are emitted only for the matching symbol:
 * the Area block (@c area_*) for the Area symbol ('\\','l'); the Signpost text
 * (@c signpost) for the Signpost symbol ('\\','m'); and the repeater frequency
 * block (@c freq_mhz / @c offset_khz / @c duplex / @c tone_tenths) for the
 * Antenna/repeater symbols, emitted at the very start of the comment text.
 * The DF ("/BRG/NRQ") block (@c df_*), by contrast, is not symbol-gated: it is
 * appended to the CSE/SPD block whenever @c df_enable is set, for any element
 * that is not an Area/Signpost object and not PHG-enabled (see
 * objitem_build_info_field()).
 *
 * The optional PHG ("PHGphgd") Data Extension (@c phg_*) is emitted, when
 * @c phg_enable is set, in the 7-byte data-extension slot immediately after the
 * symbol - the same slot as CSE/SPD, with which it is mutually exclusive - so
 * it is sent only for a fixed element (speed 0) that is not an Area/Signpost
 * object. It rides along in every transmission the element is allowed to make.
 */
typedef struct {
    bool enable;    /**< Master on/off. Auto-cleared after a kill finishes retransmitting. */
    bool send_rf;   /**< Transmit on RF (gated further by scope). */
    bool send_inet; /**< Transmit to APRS-IS / Internet (gated further by scope). */

    bool is_item; /**< true => Item (non-timestamped, ')'); false => Object (timestamped, ';'). Mirrors YAAC "Permanent". */
    bool active;  /**< true => live report; false => kill report (YAAC "Object active"). */

    char name[OBJITEM_NAME_MAX + 1]; /**< Object/Item name (1..9 chars, NUL-terminated). */

    float lat; /**< Latitude, decimal degrees (N positive). */
    float lon; /**< Longitude, decimal degrees (E positive). */

    char sym[2]; /**< APRS symbol: sym[0] = table ('/', '\\', or overlay char), sym[1] = code. */

    uint16_t course; /**< Course over ground, degrees 0..359 (0 with speed 0 => omitted). */
    uint16_t speed;  /**< Speed, knots (0 => course/speed omitted, per YAAC). */

    objitem_scope_t scope; /**< Transmission scope (see ::objitem_scope_t). */

    char comment[OBJITEM_COMMENT_MAX + 1]; /**< Free-text comment, appended last. */

    uint8_t area_type;  /**< Area object type 0..9: 0=circle,1=line,2=ellipse,3=triangle,4=box; +5 = colour-filled variant. Emitted only for the Area symbol
                           ('\\','l'); "Tyy/Cxx" replaces the CSE/SPD slot. */
    uint8_t area_color; /**< APRS area colour 0..15 (Area symbol only). */
    float area_lat_off; /**< Latitude corner offset in degrees (>=0); quantized to the APRS "yy" code at TX. */
    float area_lon_off; /**< Longitude corner offset in degrees (>=0); quantized to the APRS "xx" code at TX. */

    char signpost[OBJITEM_SIGNPOST_MAX + 1]; /**< Up to 3 chars of signpost text; emitted as "{TEXT}" for the Signpost symbol ('\\','m') only. */

    // -- DF (direction-finding) report (APRS101 ch.16, "/BRG/NRQ" extension).
    //    Fox-hunting/direction-finding stations append a bearing and receiver
    //    quality tuple right after CSE/SPD in the 7-byte data-extension slot,
    //    turning "CSE/SPD" into "CSE/SPD/BRG/NRQ". Mutually exclusive with
    //    Area/Signpost/PHG (which already repurpose that slot) since a DF
    //    report needs the CSE/SPD portion to still be present.
    bool df_enable;      /**< Enable the DF "/BRG/NRQ" extension for this element. */
    uint16_t df_bearing; /**< Signal bearing, degrees 0..359 (APRS101 ch.16 "BRG"); OBJITEM_DF_NO_BEARING (0) => omnidirectional/no bearing. */
    uint8_t df_nrq_n;    /**< NRQ "N" digit: 0 = omnidirectional antenna, 1..8 = beam antenna with a 360/2^(N-1) degree beam width, 9 = reserved. */
    uint8_t df_nrq_r;    /**< NRQ "R" digit: 0 = received signal strength not usable, 1..9 = signal strength code (S-meter reading). */
    uint8_t df_nrq_q;    /**< NRQ "Q" digit: 0 = bearing not accurate, 1..9 = bearing accuracy code (1 = best, per the APRS101 DF quality table). */

    float freq_mhz; /**< Repeater monitor frequency in MHz; 0 => no frequency block emitted. Emitted as the APRS frequency block ("FFF.FFFMHz Tnnn +/-nnn") at
                       the start of the comment, for the Antenna/repeater symbols. */
    uint16_t offset_khz;  /**< Duplex shift magnitude in kHz (e.g. 600); used only when @c duplex != 0. */
    int8_t duplex;        /**< Duplex direction: 0 = simplex, +1 = "+", -1 = "-". */
    uint16_t tone_tenths; /**< CTCSS subaudible tone in tenths of Hz (e.g. 1000 = 100.0 Hz); 0 => "Toff". */

    uint8_t path_mask; /**< Digipeat paths: bitmask over the four shared presets g_config.path[0..3]. 0 => transmit direct (no path). When >1 bit is set,
                          proportional pathing is used (one preset per transmission, ascending bit order), and @c decay_x10 is applied after each full cycle. */

    char qru[OBJITEM_QRU_MAX + 1]; /**< QRU group-membership tag (e.g. "HOSP", "FUEL"). Stored/persisted; the QRU responder that answers ?QRU? is a separate
                                      receive-side feature, not implemented by this transmitter. */

    uint32_t interval_s; /**< Initial repeat rate in seconds (YAAC "Initial object repeat rate"); 0 = firmware default. */

    uint32_t slow_interval_s; /**< Decay: longest (slow) interval, seconds; 0 or <= @c interval_s => no decay. With decay active, the live interval starts at @c
                                 interval_s and is multiplied by (@c decay_x10 / 10) after each proportional-path cycle until it reaches this, then holds. Any
                                 edit restarts it at @c interval_s. */
    uint16_t decay_x10;       /**< Decay ratio x10 (e.g. 20 => 2.0x); < 10 => no decay. */

    // -- PHG (Power-Height-Gain-Directivity) radio-coverage, mirroring the "My
    //    Station" PHG block on the Station page. The web "Objects and Items"
    //    page shows these same four sub-fields per element. When
    //    @c phg_use_station is set, the values are a snapshot of g_config.my_phg_*
    //    taken at save time (the same "Use My Station Data" convention the
    //    IGate/Digipeater/Tracker/WX pages use) and are not editable on the page.
    bool phg_enable;      /**< Enable the PHG block for this element. */
    bool phg_use_station; /**< Use the shared "My Station" PHG instead of this element's own values. */
    uint16_t phg_power;   /**< PHG radio TX power, Watts (APRS P-digit code-table value). */
    float phg_gain;       /**< PHG antenna gain, dB. */
    uint16_t phg_height;  /**< PHG antenna height, stored in feet (the APRS code-table unit); the page displays/edits it in meters. */
    uint8_t phg_dir;      /**< PHG directivity: 0=Omni, 1-8 = N,NE,E,SE,S,SW,W,NW. */
    char phg[8];          /**< Computed PHG string (e.g. "PHG5132"), for form redisplay/parity with the Station page. */

    bool compress; /**< Use APRS compressed position format for this element. Silently falls back to uncompressed for Area/Signpost objects (which use the
                      7-byte data-extension slot for their own descriptor, not CSE/SPD - the compressed format has no equivalent for that slot) and whenever
                      @c phg_enable is set (compressed format has no PHG equivalent, per APRS101 ch.9). */

    uint8_t kill_left; /**< Runtime: remaining kill retransmissions (not user-edited; persisted so a reboot mid-kill still completes). */
} objitem_t;

/**
 * @brief The whole set of Objects/Items, as loaded from / saved to LittleFS.
 */
typedef struct {
    objitem_t item[OBJITEM_COUNT]; /**< The ::OBJITEM_COUNT configured Objects/Items. */
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
 *
 * @return Seconds until the transmitter next needs servicing (always >= 1).
 */
uint32_t objitems_service(void);

#endif // OBJECTS_ITEMS_H
