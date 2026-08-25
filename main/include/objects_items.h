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
 *     - DDHHMMz = zulu day/hour/minute timestamp, or the fixed `111111z`
 *       pseudo-timestamp when ::objitem_t.permanent is set (freqspec.txt)
 *
 *   Item (non-timestamped):
 *     )NAME!<lat>/<lon><sym>CSE/SPD<comment>
 *     - name is 3..9 chars, variable length
 *     - '!' = live, '_' = killed  (the char right after the name)
 *
 *   DF (direction-finding) report (APRS101 ch.8), when @c df_enable is set and
 *   the element carries the DF symbol: the CSE/SPD block above is extended to
 *   CSE/SPD/BRG/NRQ, where BRG is the 3-digit signal bearing and NRQ is the
 *   3-digit number-of-hits/range/quality code - e.g. "088/036/270/729" for a
 *   station moving at course 088 speed 036 with a bearing of 270 and NRQ 729.
 *
 * The choice between Object and Item is independent of permanence: an Item
 * (::objitem_t.is_item) never carries a timestamp at all, while an Object may
 * be either live-timestamped or marked permanent (::objitem_t.permanent), in
 * which case it carries the fixed `111111z` pseudo-timestamp defined by
 * freqspec.txt for recommended-frequency objects instead of the current UTC
 * `DDHHMMz`. See the YAAC object editor documentation:
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

#include "must_check.h" // APRS_MUST_CHECK: the persistence entry points below may not have their result discarded

/**
 * @brief Number of independently configurable Objects/Items ("5 blocks").
 */
#define OBJITEM_COUNT 5

/**
 * @name On-demand transmit channel selection
 *
 * Bitmask handed to ::objitems_request_transmit_all naming the legs an
 * on-demand re-announcement may use. Each bit is an upper bound only: an
 * element still has to select the leg itself through its own "send via"
 * checkboxes and scope, so the mask can withhold a leg but never add one.
 *
 * The mask exists because "?APRSO" is answerable from the APRS-IS feed, whose
 * peers are unauthenticated. Passing the leg the question arrived on keeps the
 * responder's invariant intact - an answer leaves the way its question came -
 * and is what makes it impossible for a line read off the feed to key the
 * transmitter.
 * @{
 */

/** @brief Allow the RF leg of each element. */
#define OBJITEM_TX_RF 0x01u

/** @brief Allow the APRS-IS leg of each element. */
#define OBJITEM_TX_INET 0x02u

/** @brief Allow both legs, i.e. exactly what each element's own configuration selects. */
#define OBJITEM_TX_ALL (OBJITEM_TX_RF | OBJITEM_TX_INET)

/** @} */

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
 * @brief Buffer size, in bytes, that safely holds the longest
 * ::objitem_build_freq_block output: ten-byte frequency, its own leading
 * space plus the nine-byte "FFF.FFFrx" sub-field, a leading space plus the
 * four-byte tone/DCS sub-field, a leading space plus the four-byte duplex
 * shift, a leading space plus the five-byte range sub-field, and the NUL
 * terminator (37 bytes of content, rounded up for headroom).
 */
#define OBJITEM_FREQ_BLOCK_BUF_SIZE 38

/**
 * @name APRS Area Object descriptor (APRS101 chapter 11)
 *
 * An element whose symbol is the Area symbol (`\l`, alternate table) replaces
 * the 7-byte data-extension slot with the descriptor `Tyy/Cxx`: `T` is the
 * shape digit, `yy` and `xx` are the two size codes and the two bytes between
 * them carry the colour. Colours 0..9 are written as `/C`; colours 10..15
 * replace the slash with a `1` and write the units digit, which is why the
 * field is a fixed seven bytes for every colour.
 *
 * The two size codes are the square root of the offset expressed in
 * ::OBJITEM_AREA_OFFSET_SCALE-ths of a degree, so a receiver recovers the
 * offset as `code * code / 1500` degrees. The offsets run from the shape's
 * "offset reference" corner - the upper left corner of the shape, which is the
 * position the report itself carries - to the lower right corner, or to the
 * centre for a circle.
 * @{
 */

/** @brief Highest area shape digit (`T`). */
#define OBJITEM_AREA_TYPE_MAX 9

/** @brief Area shape digit for a line drawn down and to the right. */
#define OBJITEM_AREA_TYPE_LINE_DOWN_RIGHT 1

/** @brief Area shape digit for a line drawn down and to the left. */
#define OBJITEM_AREA_TYPE_LINE_DOWN_LEFT 6

/** @brief Highest area colour code (`Cxx`), per the APRS101 colour table. */
#define OBJITEM_AREA_COLOR_MAX 15

/** @brief Highest value either two-digit area size code can hold. */
#define OBJITEM_AREA_OFFSET_CODE_MAX 99

/**
 * @brief Scale factor between an area offset in degrees and its on-air code.
 *
 * `code = sqrt(degrees * 1500)`, so the code the receiver squares and divides
 * by the same factor comes back as the original offset.
 */
#define OBJITEM_AREA_OFFSET_SCALE 1500.0

/**
 * @brief Largest area offset, in degrees, that the two-digit code can express
 * (`99 * 99 / 1500`).
 */
#define OBJITEM_AREA_OFFSET_DEG_MAX 6.534f

/**
 * @brief Largest line corridor half-width, in miles, that fits the `{www}`
 * comment token.
 */
#define OBJITEM_AREA_WIDTH_MAX 999

/** @} */

/**
 * @brief QRU group-membership name length.
 *
 * Mirrors YAAC's "QRU group membership" field: a short group tag (e.g. "HOSP",
 * "FUEL", "RP2M"). Stored/persisted and enumerated by the "?QRU?" group-query
 * responder in components/query/query.c.
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
 * then the YAAC-derived on-air parameters. The layout is deliberately compact
 * to keep the on-demand load buffer small.
 *
 * @details Optional on-air blocks are emitted only for the matching symbol:
 * the Area descriptor (@c area_type / @c area_color / @c area_lat_off /
 * @c area_lon_off) for the Area symbol ('\\','l'), followed by the "{www}"
 * corridor token (@c area_line_width) when the shape is one of the two lines;
 * the Signpost text
 * (@c signpost) for the Signpost symbol ('\\','m'); and the repeater frequency
 * block (@c freq_mhz / @c offset_khz / @c duplex / @c tone_tenths / @c range /
 * @c range_km / @c dcs_enable / @c dcs_code / @c narrow / @c rx_freq_enable /
 * @c rx_freq_mhz) for the Antenna/repeater symbols, emitted at the very start
 * of the comment text.
 * The DF ("/BRG/NRQ") block (@c df_*) is symbol-gated too, and more strictly:
 * APRS101 chapter 8 states that BRG/NRQ is only meaningful when the Symbol
 * Table ID is @c '/' and the Symbol Code is @c '\\', so it is appended to the
 * CSE/SPD block only for an element carrying that symbol pair
 * (::APRS_DF_SYMBOL_TABLE / ::APRS_DF_SYMBOL_CODE). On any other symbol
 * @c df_enable has no on-air effect and the slot holds whatever it would hold
 * without it - plain CSE/SPD, PHG, or nothing (see
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

    bool is_item; /**< true => Item (non-timestamped, ')'); false => Object (timestamped, ';'). */
    bool active;  /**< true => live report; false => kill report (YAAC "Object active"). */

    bool permanent; /**< Object only (ignored for an Item): when true, the timestamp field carries the fixed `111111z` pseudo-timestamp instead of the
                        current UTC `DDHHMMz` (freqspec.txt). This is YAAC's "Permanent" flag: it declares the Object must not be replaced by any other
                        station's similarly-named Object, only updated or moved by the same originating station. */

    char name[OBJITEM_NAME_MAX + 1]; /**< Object/Item name (1..9 chars, NUL-terminated). */

    float lat; /**< Latitude, decimal degrees (N positive). */
    float lon; /**< Longitude, decimal degrees (E positive). */

    char sym[2]; /**< APRS symbol: sym[0] = table ('/', '\\', or overlay char), sym[1] = code. */

    uint16_t course; /**< Course over ground, degrees 0..359 (0 with speed 0 => omitted). */
    uint16_t speed;  /**< Speed, knots (0 => course/speed omitted, per YAAC). */

    objitem_scope_t scope; /**< Transmission scope (see ::objitem_scope_t). */

    char comment[OBJITEM_COMMENT_MAX + 1]; /**< Free-text comment, appended last. */

    uint8_t area_type;        /**< Area shape digit, 0..::OBJITEM_AREA_TYPE_MAX: 0 = circle, 1 = line down/right, 2 = ellipse, 3 = triangle, 4 = box, 5 = filled
                                 circle, 6 = line down/left, 7 = filled ellipse, 8 = filled triangle, 9 = filled box. Emitted only for the Area symbol
                                 ('\\','l'), where "Tyy/Cxx" replaces the CSE/SPD slot. */
    uint8_t area_color;       /**< APRS area colour, 0..::OBJITEM_AREA_COLOR_MAX (Area symbol only). */
    float area_lat_off;       /**< Latitude offset to the shape's lower right corner, degrees (>=0); quantized to the APRS "yy" code at TX. */
    float area_lon_off;       /**< Longitude offset to the shape's lower right corner, degrees (>=0); quantized to the APRS "xx" code at TX. */
    uint16_t area_line_width; /**< Corridor half-width in miles, emitted as the "{www}" comment token for the two line shapes only; 0 => no corridor. */

    char signpost[OBJITEM_SIGNPOST_MAX + 1]; /**< Up to 3 chars of signpost text; emitted as "{TEXT}" for the Signpost symbol ('\\','m') only. */

    // -- DF (direction-finding) report (APRS101 ch.8, "/BRG/NRQ" extension).
    //    Fox-hunting/direction-finding stations append a bearing and receiver
    //    quality tuple right after CSE/SPD in the 7-byte data-extension slot,
    //    turning "CSE/SPD" into "CSE/SPD/BRG/NRQ". Mutually exclusive with
    //    Area/Signpost/PHG (which already repurpose that slot) since a DF
    //    report needs the CSE/SPD portion to still be present.
    bool df_enable;      /**< Enable the DF "/BRG/NRQ" extension for this element; transmitted only on the DF symbol (::APRS_DF_SYMBOL_TABLE /
                            ::APRS_DF_SYMBOL_CODE). */
    uint16_t df_bearing; /**< Signal bearing, degrees 0..359 (APRS101 ch.8 "BRG"); 0 => omnidirectional/no bearing, never a valid bearing on air. */
    uint8_t df_nrq_n;    /**< NRQ "N" digit: 0 = omnidirectional antenna, 1..8 = beam antenna with a 360/2^(N-1) degree beam width, 9 = reserved. */
    uint8_t df_nrq_r;    /**< NRQ "R" digit: 0 = received signal strength not usable, 1..9 = signal strength code (S-meter reading). */
    uint8_t df_nrq_q;    /**< NRQ "Q" digit: 0 = bearing not accurate, 1..9 = bearing accuracy code (1 = best, per the APRS101 DF quality table). */

    float freq_mhz;       /**< Repeater monitor frequency in MHz; 0 => no frequency block emitted. Emitted as the APRS frequency block (see
                             ::objitem_build_freq_block) at the start of the comment, for the Antenna/repeater symbols. */
    uint16_t offset_khz;  /**< Duplex shift magnitude in kHz (e.g. 600); used only when @c duplex != 0. */
    int8_t duplex;        /**< Duplex direction: 0 = simplex, +1 = "+", -1 = "-". */
    uint16_t tone_tenths; /**< CTCSS subaudible tone in tenths of Hz (e.g. 1000 = 100.0 Hz); 0 => "Toff". Ignored on air when @c dcs_enable is set, since the
                             tone and DCS sub-fields share the same slot. */
    uint16_t range;       /**< Coverage range magnitude, in @c range_km's unit, clamped to two digits (0..99) on air; 0 => no range sub-field emitted. */
    bool range_km;        /**< Range unit: false => miles ("Rxxm"), true => kilometers ("Rxxkm"); ignored when @c range == 0. */

    bool dcs_enable;   /**< Emit the DCS-code sub-field ("Dnnn") in place of the CTCSS tone sub-field ("Tnnn"/"Toff"); the two are mutually exclusive, per
                          freqspec.txt, since they occupy the same three-byte slot after the frequency field. */
    uint16_t dcs_code; /**< DCS code, 0..511 (a 9-bit octal code, displayed as three octal digits, e.g. 023 or 754); only meaningful when @c dcs_enable is
                          set. */
    bool narrow;       /**< Narrowband-modulation flag (freqspec.txt "tnnn"/"dnnn", lower-case): when set, the leading letter of the tone or DCS sub-field
                          ('T'/'D') is emitted lower-case ('t'/'d') instead, the spec's way of flagging narrowband modulation without a separate byte. Has no
                          on-air effect by itself; it only changes the case of the tone/DCS sub-field's leading letter. */

    bool rx_freq_enable; /**< Emit a second, independent receive-frequency sub-field ("FFF.FFFrx") right after the primary (transmit) frequency field, for a
                            repeater whose receive frequency is not the standard duplex offset from @c freq_mhz (e.g. a cross-band repeater). */
    float rx_freq_mhz;   /**< Receive frequency in MHz, emitted as the fixed ten-byte "FFF.FFFrx" sub-field when @c rx_freq_enable is set; only the
                            100.000-999.999 MHz form is defined for this sub-field. */

    uint8_t path_mask; /**< Digipeat paths: bitmask over the four shared presets g_config.path[0..3]. 0 => transmit direct (no path). When >1 bit is set,
                          proportional pathing is used (one preset per transmission, ascending bit order), and @c decay_x10 is applied after each full cycle. */

    char qru[OBJITEM_QRU_MAX + 1]; /**< QRU group-membership tag (e.g. "HOSP", "FUEL"). Stored/persisted; a non-empty tag makes the element a member of that
                                      group for the "?QRU?" group-query responder in components/query/query.c. */

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
 *
 * @note Declared ::APRS_MUST_CHECK: a call site that discards the result
 * reports success to the user for a write that may never have reached
 * flash, so ignoring it fails the build.
 */
bool objitems_save(const objitems_t *in) APRS_MUST_CHECK;

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
 * A pending ::objitems_request_transmit_all request is served first, as a
 * separate round that reports current state on the requested legs and leaves
 * every piece of schedule state alone; the periodic pass then runs as usual.
 *
 * The first call returns a one-time boot settle delay without transmitting.
 * Intended to be called only from the shared beacon scheduler task.
 *
 * @return Seconds until the transmitter next needs servicing (always >= 1).
 */
uint32_t objitems_service(void);

/**
 * @brief Ask the transmitter to send every enabled Object/Item once more, as
 * soon as it next runs, on the legs @p channels permits.
 *
 * This is what an APRS "?APRSO" directed query asks for (APRS101 chapter 15):
 * a re-announcement of the objects this station originates. The request only
 * raises a flag - the elements are transmitted from the shared beacon
 * scheduler task on its next pass, spaced by the usual inter-element gap, so
 * a query arriving on the radio RX task never blocks that task for the
 * duration of a burst of transmissions.
 *
 * The round reports each element's current state and nothing else: it does not
 * move the element's next-due time, does not advance the decay ramp or the
 * proportional-path rotation, and does not consume a kill repeat. A caller
 * that can be driven from off-station traffic therefore cannot reach the
 * periodic schedule through this entry point - the worst it can do is spend
 * the airtime the round itself costs, which its own rate limiter bounds.
 *
 * @param channels Bitmask of ::OBJITEM_TX_RF / ::OBJITEM_TX_INET. Each bit is
 *                 an upper bound on the corresponding leg; an element still
 *                 has to select that leg itself. A mask of 0 requests nothing
 *                 and is discarded.
 *
 * Safe to call from any task. Repeated calls before the next pass collapse
 * into one round whose mask is the union of theirs.
 */
void objitems_request_transmit_all(uint8_t channels);

/**
 * @brief Build the standard APRS repeater frequency block ("FFF.FFFMHz Tnnn
 * +/-nnn Rxxm") into @p out, or the empty string when @p freq_mhz is not
 * positive or has no representation in the fixed field.
 *
 * This is the exact wire format freqspec.txt defines and objitem_t's
 * @c freq_mhz/@c tone_tenths/@c duplex/@c offset_khz/@c range/@c range_km/
 * @c dcs_enable/@c dcs_code/@c narrow/@c rx_freq_enable/@c rx_freq_mhz fields
 * already build for the Objects/Items Antenna/repeater symbols; it is exposed
 * here so any other beacon (own-station position/status reports,
 * main/beacon.c) can prepend or append the identical block instead of
 * re-deriving the format.
 *
 * The frequency itself always occupies exactly ten bytes, because receivers
 * read it as a fixed-position field: the block is what a radio auto-tunes
 * from, and what the 10x10 character displays of the D7/D700/HamHUD family
 * lay out by column. Which of the three forms freqspec.txt defines is used
 * follows from the magnitude alone:
 *
 * - below 100 MHz: the 10 kHz form @c "FFF.FF MHz", the number right-justified
 *   against its separating space (50.620 MHz becomes @c " 50.62 MHz");
 * - 100.000 to 999.999 MHz: the 1 kHz form @c "FFF.FFFMHz" (146.520 MHz
 *   becomes @c "146.520MHz");
 * - above 999.999 MHz: the microwave letter designation, one letter standing
 *   for a 100 MHz block plus the two low MHz digits (1296.000 MHz becomes
 *   @c "A96.000MHz").
 *
 * The letter table covers only the bands freqspec.txt enumerates - A (1200),
 * B (2300), C (2400), D (3400), E (5600), F (5700), G (5800), H (10100),
 * I (10200), J (10300), K (10400), L (10500), M (24000), N (24100) and
 * O (24200), each spanning its base plus 99 MHz. A frequency above
 * 999.999 MHz that falls in none of them has no ten-byte form at all, so
 * nothing is written and the omission is logged: an eleven-byte field would
 * shift every byte a receiver reads after it, which is worse than a comment
 * that simply starts with the operator's own text.
 *
 * Immediately after the primary frequency field, the optional independent
 * receive-frequency sub-field ("FFF.FFFrx") is emitted, with its own leading
 * space, when @p rx_freq_enable is set and @p rx_freq_mhz falls in the
 * 100.000-999.999 MHz range the sub-field is defined for; this is the split
 * transmit/receive form freqspec.txt shows for a repeater whose receive
 * frequency is not its standard duplex offset (e.g. a cross-band repeater).
 *
 * The optional tone/DCS, duplex and range sub-fields follow, each with its
 * own leading space, in the order the spec shows them: tone or DCS, then
 * duplex offset, then range.
 *
 * - When @p dcs_enable is false, the three-byte slot carries the CTCSS tone,
 *   @c "Tnnn" (integer Hz, tenths dropped) or @c "Toff" when @p tone_tenths
 *   is 0.
 * - When @p dcs_enable is true, the same slot instead carries the DCS code,
 *   @c "Dnnn" (three octal digits), and @p tone_tenths has no on-air effect:
 *   the tone and DCS sub-fields share one slot and are mutually exclusive.
 * - Either way, when @p narrow is set the slot's leading letter ('T' or 'D')
 *   is emitted lower-case ('t' or 'd') instead, freqspec.txt's narrowband-
 *   modulation flag.
 *
 * @param freq_mhz Repeater monitor (transmit) frequency in MHz; <= 0 =>
 *        nothing is written and @p out is left as an empty string.
 * @param tone_tenths CTCSS subaudible tone, tenths of Hz (e.g. 1000 = 100.0
 *        Hz); 0 => "Toff". Ignored on air when @p dcs_enable is true.
 * @param duplex Duplex direction: 0 = simplex (offset omitted), +1 = "+",
 *        -1 = "-".
 * @param offset_khz Duplex shift magnitude, kHz (e.g. 600); used only when
 *        @p duplex != 0.
 * @param range Coverage range magnitude in @p range_km's unit, clamped to two
 *        digits (0..99) on air; 0 => the range sub-field is omitted.
 * @param range_km Range unit: false => miles ("Rxxm"), true => kilometers
 *        ("Rxxkm"); ignored when @p range == 0.
 * @param dcs_enable true => the tone/DCS slot carries the DCS code
 *        ("Dnnn") instead of the CTCSS tone ("Tnnn"/"Toff").
 * @param dcs_code DCS code, 0..511, emitted as three octal digits; only used
 *        when @p dcs_enable is true.
 * @param narrow true => the tone/DCS slot's leading letter is emitted
 *        lower-case, freqspec.txt's narrowband-modulation flag.
 * @param rx_freq_enable true => emit the independent receive-frequency
 *        sub-field ("FFF.FFFrx") right after the primary frequency field.
 * @param rx_freq_mhz Receive frequency in MHz for the "FFF.FFFrx" sub-field;
 *        only used when @p rx_freq_enable is true and only the
 *        100.000-999.999 MHz form is defined for it - a value outside that
 *        range is silently skipped rather than shipped in a mismatched width.
 * @param out Destination buffer, always left NUL-terminated.
 *        ::OBJITEM_FREQ_BLOCK_BUF_SIZE bytes hold the longest block.
 * @param out_size Size of @p out in bytes.
 */
void objitem_build_freq_block(float freq_mhz, uint16_t tone_tenths, int8_t duplex, uint16_t offset_khz, uint16_t range, bool range_km, bool dcs_enable,
                              uint16_t dcs_code, bool narrow, bool rx_freq_enable, float rx_freq_mhz, char *out, size_t out_size);

#endif // OBJECTS_ITEMS_H
