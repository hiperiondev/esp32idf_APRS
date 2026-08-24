/**
 * @file telemetry.h
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
 * @brief Own-station APRS Telemetry subsystem (channel 0).
 *
 * Resolves the operator's Binary (digital B1-B8) channel mapping from the
 * ::sensors_local registry once per second, and periodically encodes and
 * transmits a standard APRS Telemetry Data Report ("T#...", RF and/or
 * APRS-IS) plus, at a slower cadence, the PARM/UNIT/BITS metadata messages
 * that label those channels for receiving stations - exactly mirroring the
 * pattern ::weather.c uses for the WX beacon, but for the telemetry config
 * below.
 *
 * Unlike most settings, telemetry configuration deliberately does NOT live in
 * the resident g_config struct / config.json: it persists to its own small
 * LittleFS file (/storage/telemetry.json), the same way bulletins.c keeps
 * bulletins.json separate - see bulletins.h for the full rationale. On first
 * boot, or whenever the file is missing, an empty/default set is created so
 * /storage/telemetry.json always exists once the subsystem has started.
 *
 * On-air naming: per APRS101 Chapter 13, the periodic Telemetry Data Report
 * ("T#sss,a1,a2,a3,a4,a5,bbbbbbbb") NEVER carries channel names - only the
 * sequence number and raw analog/digital values. Names/units/equations are
 * sent separately, at a slower cadence, as APRS messages addressed back to
 * the station itself (":MYCALL   :PARM....", ":MYCALL   :UNIT....",
 * ":MYCALL   :EQNS....", ":MYCALL   :BITS....,title"). This module follows
 * that split: build_tlm_data_packet() (in telemetry.c) never emits
 * tlm_bit_name[], only build_tlm_bits_packet() does, and only inside a
 * ":...:BITS." message.
 *
 * Optional base-91 comment telemetry (APRS 1.2) piggybacks the same analog
 * readings onto a station's own position-report comment as a compact
 * "|ss1122|"-style group - with the whole digital bank as one further pair
 * once all five analog channels are present - so a station that already
 * beacons position on a schedule can carry telemetry at a handful of extra
 * bytes instead of a whole separate transmission.
 * telemetry_build_comment_tlm() (telemetry.c) builds that group from the
 * identical resolved readings the "T#..." report uses, so the two forms can
 * never disagree; beacon.c appends it to
 * a station's comment only when that beacon's callsign matches this
 * module's own mycall/ssid, since the comment form is only meaningful when
 * it rides along on the telemetry station's own position report.
 */

#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "must_check.h" // APRS_MUST_CHECK: the persistence entry points below may not have their result discarded

/**
 * @brief Number of analog channels A1-A5 (APRS101 Ch.13).
 */
#define TLM_CH 5
/**
 * @brief Total number of PARM-message fields: 5 analog + 8 digital (APRS101 Ch.13).
 */
#define TLM_PARM_NUM 13
/**
 * @brief Number of digital bit channels B1-B8 (APRS101 Ch.13).
 */
#define TLM_BIT_NUM 8

/**
 * @brief Buffer size a caller must provide to telemetry_build_comment_tlm()
 *        to hold the longest APRS 1.2 base-91 comment telemetry group.
 *
 * The worst case is the opening '|', the 2-byte sequence pair, one 2-byte
 * pair per analog channel (::TLM_CH of them), the single 2-byte pair holding
 * the whole 8-bit digital bank, the closing '|' and a terminating NUL: 17
 * bytes in total. This is the one place that arithmetic is written down;
 * beacon.c sizes its own copy of the group from it.
 */
#define TLM_COMMENT_GROUP_BUF_SIZE (1 + 2 + TLM_CH * 2 + 2 + 1 + 1)

/**
 * @name Analog channel editing ranges
 *
 * Bounds the Telemetry page hands to web_field_int()/web_field_float() for the
 * per-analog-channel calibration inputs, so the browser rejects a value the
 * stored type could not hold. The raw window is the storage width of
 * @c ana_raw_min / @c ana_raw_max (@c int32_t, kept one below the type's
 * extremes so the value stays representable after the form's own signed
 * parse); the coefficient window is a sanity range on a quadratic whose terms
 * APRS101 Ch.13 does not itself bound.
 * @{
 */
#define TLM_RAW_RANGE_MIN  (-2147483647L) /**< Lowest raw ADC value accepted for a channel's expected range. */
#define TLM_RAW_RANGE_MAX  2147483647L    /**< Highest raw ADC value accepted for a channel's expected range. */
#define TLM_COEF_RANGE_MIN (-1.0e9f)      /**< Lowest accepted EQNS calibration coefficient (a, b or c). */
#define TLM_COEF_RANGE_MAX 1.0e9f         /**< Highest accepted EQNS calibration coefficient (a, b or c). */
/** @} */

/**
 * @brief Own-beacon Telemetry channel 0 configuration, as loaded from /
 * saved to /storage/telemetry.json.
 *
 * @details Fields are grouped as: core beacon settings; Report Parameters
 * (APRS101 Ch.13 framing/metadata options); definition-message generation
 * toggles (PARM/UNIT/EQNS/BITS); analog channels A1-A5 (names/units in
 * @c PARM[0..4] / @c UNIT[0..4], calibration quadratic value=a*x*x+b*x+c);
 * and Binary channels B1-B8.
 *
 * The "T#..." data report carries the RAW reading of each analog channel,
 * clamped to that channel's @c ana_raw_min / @c ana_raw_max span; the a/b/c
 * coefficients that turn it into an engineering value travel separately, in
 * the @c EQNS. definition Message, which is the APRS101 Ch.13 split between
 * report and metadata. The span never rescales a reading, only bounds it, so
 * those coefficients stay valid for whatever goes on the air; a span that is
 * inverted or empty (@c ana_raw_max <= @c ana_raw_min) declares nothing and
 * is ignored.
 */
typedef struct {
    bool en;                     /**< Master enable for the telemetry subsystem. */
    bool use_station;            /**< "Use My Station Data": copy mycall from g_config.my_callsign. */
    bool tx2rf;                  /**< Master enable: transmit the telemetry beacon over RF. */
    bool tx2inet;                /**< Master enable: transmit the telemetry beacon over APRS-IS. */
    uint8_t ssid;                /**< SSID appended to @c mycall for the telemetry beacon. */
    char mycall[10];             /**< Telemetry channel-0 callsign (NUL-terminated). */
    uint8_t path;                /**< Digipeat-path selection: bitmask over g_config.path[0..3]. */
    uint16_t data_interval;      /**< Seconds between "T#..." reports; 0 = firmware default. */
    uint16_t info_interval;      /**< Seconds between PARM/UNIT/BITS metadata messages; 0 = disabled. */
    char PARM[TLM_PARM_NUM][10]; /**< Analog channel names (A1-A5) + digital bit names (B1-B8). */
    char UNIT[TLM_PARM_NUM][8];  /**< Analog channel units + digital bit ON-state labels. */

    char report_path[64];   /**< Free-text digipeater path, e.g. "WIDE1-1,WIDE2-1". */
    char tocall[7];         /**< APRS Destination / TOCALL (e.g. "APRS"). */
    bool auto_seq;          /**< Auto-increment the T# sequence number. */
    uint8_t field_width;    /**< Analog field width: 0 = minimal/auto, 3 = 3-digit zero-padded, 000-999 (APRS 1.2). */
    bool omit_trailing;     /**< Omit unused trailing channels (APRS101 Ch.13 shorthand). */
    char trail_comment[32]; /**< Optional free text appended after the bits. */
    uint8_t analog_count;   /**< Number of analog channels sent (1..5). */
    uint8_t digital_count;  /**< Number of digital bits sent (0..8). */

    bool gen_parm; /**< Emit PARM. (channel & bit names). */
    bool gen_unit; /**< Emit UNIT. (units / bit-state labels). */
    bool gen_eqns; /**< Emit EQNS. (scaling coefficients A,B,C). */
    bool gen_bits; /**< Emit BITS. (bit sense + project title). */

    bool analog_tx2rf;               /**< "Analog: Beacon via RF". */
    bool analog_tx2inet;             /**< "Analog: Beacon via Internet". */
    bool ana_enable[TLM_CH];         /**< Per-analog-channel enable (A1-A5). */
    uint8_t tlm_ana_channel[TLM_CH]; /**< Source sensor channel for each analog channel (::SENSOR_LOCAL_CH_NONE = "(none)"); persisted by driver name. */
    float ana_a[TLM_CH];             /**< Calibration coefficient a (quadratic term) per analog channel. */
    float ana_b[TLM_CH];             /**< Calibration coefficient b (linear term) per analog channel. */
    float ana_c[TLM_CH];             /**< Calibration coefficient c (constant term) per analog channel. */
    int32_t ana_raw_min[TLM_CH];     /**< Lowest raw reading expected on this channel; bounds the transmitted value. */
    int32_t ana_raw_max[TLM_CH];     /**< Highest raw reading expected on this channel; bounds the transmitted value. */
    uint8_t ana_dec[TLM_CH];         /**< Number of decimals shown per analog channel. */

    char tlm_bit_name[TLM_BIT_NUM][21];   /**< Per-bit operator-facing label (used only inside the BITS. message). */
    uint8_t tlm_bit_channel[TLM_BIT_NUM]; /**< Source sensor channel for each bit (::SENSOR_LOCAL_CH_NONE = "(none)"); persisted by driver name. */
    bool tlm_bit_igate[TLM_BIT_NUM];      /**< Per-bit routing: include this bit in the APRS-IS (IGate) beacon. */
    bool tlm_bit_rf[TLM_BIT_NUM];         /**< Per-bit routing: include this bit in the RF beacon. */
    bool bit_enable[TLM_BIT_NUM];         /**< Per-bit enable (defaults true; a disabled bit is sent as 0). */
    bool bit_sense[TLM_BIT_NUM];          /**< true = Normal (raw 1 = asserted), false = Inverted. */

    bool digital_tx2rf;   /**< "Digital: Beacon via RF". */
    bool digital_tx2inet; /**< "Digital: Beacon via Internet". */
    char proj_title[24];  /**< BITS. project title / "Name". */

    bool comment_telemetry; /**< Also carry base-91 comment telemetry (APRS 1.2) in this station's own position-report comment; off by default. Complements, and
                               never replaces, the "T#..." report. */
} telemetry_config_t;

/**
 * @brief Load the telemetry configuration from /storage/telemetry.json into
 * @p out.
 *
 * Missing/empty/corrupt file is not an error: @p out is filled with
 * all-disabled, empty defaults (tlm_bit_channel[i] == ::SENSOR_LOCAL_CH_NONE, i.e. "(none)")
 * so callers always get a usable structure.
 *
 * @param out Destination (must be non-NULL).
 * @return true if a valid file was parsed, false if defaults were substituted.
 */
bool telemetry_config_load(telemetry_config_t *out);

/**
 * @brief Persist @p in to /storage/telemetry.json (atomic: tmp file + rename).
 *
 * @param in Source configuration (must be non-NULL).
 * @return true on success.
 *
 * @note Declared ::APRS_MUST_CHECK: a call site that discards the result
 * reports success to the user for a write that may never have reached
 * flash, so ignoring it fails the build.
 */
bool telemetry_config_save(const telemetry_config_t *in) APRS_MUST_CHECK;

/**
 * @brief Fill @p out with the factory-default telemetry configuration
 * (everything disabled/empty, every Binary channel set to "(none)").
 *
 * @param out Destination (must be non-NULL).
 */
void telemetry_config_set_defaults(telemetry_config_t *out);

/**
 * @brief Bring up the telemetry subsystem: reset the sequence counter and
 *        per-bit resolved state, and make sure /storage/telemetry.json
 *        exists (creating it with defaults if this is the first boot).
 *
 * Safe to call once from application start-up (after config load and after
 * ::weather_start, which already brings up the shared ::sensors_local
 * registry via sensors_local_init()/sensors_local_init_all() - this module
 * only reads that registry, it does not initialise it again).
 */
void telemetry_start(void);

/**
 * @brief Refresh the resolved Binary (B1-B8) channel values from the local
 *        sensor registry. Must be called at ~1 Hz. Driven by the APRS
 *        service's existing 1 Hz tick, the same way ::weather_service_1hz
 *        drives the WX subsystem's refresh.
 */
void telemetry_service_1hz(void);

/**
 * @brief Service the Telemetry beacon: transmit a T# data report if one is
 *        due (data_interval), and a PARM/UNIT/BITS metadata message set if
 *        one is due (info_interval); return the number of seconds until this
 *        service next needs attention (always >= 1).
 *
 * Reloads /storage/telemetry.json on every call (same reload-on-each-call
 * model bulletins_service() uses), so on/off, interval, and mapping changes
 * from the web admin page take effect without a reboot.
 *
 * Intended to be called only from the shared beacon scheduler task
 * (beacon_scheduler.c).
 *
 * @return Seconds until this service next needs attention (always >= 1).
 */
uint32_t telemetry_beacon_service(void);

/**
 * @brief Get the currently configured own-beacon Telemetry callsign
 *        (channel 0), or an empty string if none is set.
 *
 * Used by aprs_service.c's own-report echo detection. Telemetry configuration
 * lives in its own file rather than g_config, so this is the supported
 * accessor for the callsign.
 *
 * @param out      Destination buffer.
 * @param out_size Size of @p out (should be >= sizeof(telemetry_config_t.mycall)).
 */
void telemetry_get_mycall(char *out, size_t out_size);

/**
 * @brief Build the optional APRS 1.2 base-91 comment telemetry group
 *        ("|ss1122334455bb|") from the same resolved readings the "T#..."
 *        Telemetry Data Report carries, for a caller (beacon.c) to append to
 *        a position-report comment.
 *
 * Encodes the current sequence number and each enabled, resolved analog
 * channel (up to ::TLM_CH) as a base-91 pair, using the same
 * data-in-flight snapshot build_tlm_data_packet() reads, so the comment
 * form and the concurrent "T#..." report always decode to the same raw
 * numbers and advance the same sequence number together.
 *
 * Two rules from APRS101 Ch.13 shape what comes out:
 *
 *  - The extension must carry the sequence counter @b and at least one
 *    channel, so a station with no analog channel currently enabled and
 *    resolved emits no group at all rather than a bare "|ss|".
 *  - The 8-bit digital bank travels as one further base-91 pair whose least
 *    significant bit is B1 and whose eighth bit is B8, and that pair is only
 *    legal after all five analog pairs. It is therefore emitted only when
 *    every analog channel resolved and the digital bank is routed with at
 *    least one channel configured; with fewer analog pairs present, a
 *    receiver would read it as the next analog channel instead.
 *
 * @param out     Destination buffer for the group, including both '|'
 *                delimiters and a terminating NUL; ::TLM_COMMENT_GROUP_BUF_SIZE
 *                bytes always suffice.
 * @param out_max Size of @p out in bytes.
 * @return Group length in bytes (at most ::TLM_COMMENT_GROUP_BUF_SIZE - 1),
 *         or 0 if comment telemetry is disabled, no callsign is configured,
 *         no analog channel is currently resolved, or @p out_max is too small
 *         to hold the group.
 */
size_t telemetry_build_comment_tlm(char *out, size_t out_max);

/**
 * @brief The readings the 1 Hz refresh resolved for every analog channel and
 *        every binary bit, as one consistent snapshot.
 *
 * This is what the encoder works from, exposed for the callers that present
 * readings to a person rather than transmitting them (the Telegram bot's
 * `/sensors` answer). "Present" is the same notion the encoder uses: the
 * channel has a source sensor mapped and that sensor reported on the last
 * refresh, so a channel that is enabled but unmapped, or mapped to a driver
 * that is not answering, reads as absent instead of as a zero.
 */
typedef struct {
    bool analog_present[TLM_CH];       /**< Whether A1-A5 resolved on the last refresh. */
    double analog_raw[TLM_CH];         /**< Raw reading of A1-A5, before the a/b/c calibration, as transmitted on air. */
    bool digital_present[TLM_BIT_NUM]; /**< Whether B1-B8 resolved on the last refresh. */
    bool digital_value[TLM_BIT_NUM];   /**< Raw state of B1-B8, before @c bit_sense is applied. */
} telemetry_values_t;

/**
 * @brief Copy the latest resolved readings into @p out.
 *
 * Reads the values cached by the 1 Hz refresh under the module lock; it never
 * touches a sensor bus itself, so it is safe to call from any task and costs
 * nothing beyond the copy.
 *
 * @param out Destination (ignored when NULL). Every channel reads as absent
 *            until the beacon scheduler's first pass has published a channel
 *            mapping for the refresh to work from.
 */
void telemetry_get_values(telemetry_values_t *out);

#endif // TELEMETRY_H
