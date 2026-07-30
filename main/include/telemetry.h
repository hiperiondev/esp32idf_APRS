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
 */

#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Number of analog channels A1-A5 (APRS101 Ch.13).
 */
#define TLM_CH       5
/**
 * @brief Total number of PARM-message fields: 5 analog + 8 digital (APRS101 Ch.13).
 */
#define TLM_PARM_NUM 13
/**
 * @brief Number of digital bit channels B1-B8 (APRS101 Ch.13).
 */
#define TLM_BIT_NUM  8

/**
 * @brief Own-beacon Telemetry channel 0 configuration, as loaded from /
 * saved to /storage/telemetry.json.
 *
 * Analog PARM/UNIT names (@c PARM / @c UNIT) are carried for forward
 * compatibility with an eventual analog A1-A5 mapping; only the Binary
 * (digital) bank is currently produced on-air (see telemetry.c).
 *
 * @details Fields are grouped as: core beacon settings; Report Parameters
 * (APRS101 Ch.13 framing/metadata options); definition-message generation
 * toggles (PARM/UNIT/EQNS/BITS); analog channels A1-A5 (names/units in
 * @c PARM[0..4] / @c UNIT[0..4], calibration quadratic value=a*x*x+b*x+c);
 * and Binary channels B1-B8. Everything except the Binary bank is persisted
 * for a full round-trip but is not yet produced on-air by telemetry.c.
 */
typedef struct {
    bool en;                      /**< Master enable for the telemetry subsystem. */
    bool use_station;             /**< "Use My Station Data": copy mycall from g_config.my_callsign. */
    bool tx2rf;                   /**< Master enable: transmit the telemetry beacon over RF. */
    bool tx2inet;                 /**< Master enable: transmit the telemetry beacon over APRS-IS. */
    uint8_t ssid;                 /**< SSID appended to @c mycall for the telemetry beacon. */
    char mycall[10];              /**< Telemetry channel-0 callsign (NUL-terminated). */
    uint8_t path;                 /**< Digipeat-path selection: bitmask over g_config.path[0..3]. */
    uint16_t data_interval;       /**< Seconds between "T#..." reports; 0 = firmware default. */
    uint16_t info_interval;       /**< Seconds between PARM/UNIT/BITS metadata messages; 0 = disabled. */
    char PARM[TLM_PARM_NUM][10];  /**< Analog channel names (A1-A5) + digital bit names (B1-B8). */
    char UNIT[TLM_PARM_NUM][8];   /**< Analog channel units + digital bit ON-state labels. */

    char report_path[64];         /**< Free-text digipeater path, e.g. "WIDE1-1,WIDE2-1". */
    char tocall[7];               /**< APRS Destination / TOCALL (e.g. "APRS"). */
    bool auto_seq;                /**< Auto-increment the T# sequence number. */
    uint8_t field_width;          /**< Analog field width: 0 = minimal/auto, 3 = 3-digit zero-padded. */
    bool omit_trailing;           /**< Omit unused trailing channels (APRS101 Ch.13 shorthand). */
    char trail_comment[32];       /**< Optional free text appended after the bits. */
    uint8_t analog_count;         /**< Number of analog channels sent (1..5). */
    uint8_t digital_count;        /**< Number of digital bits sent (0..8). */

    bool gen_parm;                /**< Emit PARM. (channel & bit names). */
    bool gen_unit;                /**< Emit UNIT. (units / bit-state labels). */
    bool gen_eqns;                /**< Emit EQNS. (scaling coefficients A,B,C). */
    bool gen_bits;                /**< Emit BITS. (bit sense + project title). */

    bool analog_tx2rf;               /**< "Analog: Beacon via RF". */
    bool analog_tx2inet;             /**< "Analog: Beacon via Internet". */
    bool ana_enable[TLM_CH];         /**< Per-analog-channel enable (A1-A5). */
    uint8_t tlm_ana_channel[TLM_CH]; /**< Source sensor channel index for each analog channel (0xFF = "(none)"). */
    float ana_a[TLM_CH];             /**< Calibration coefficient a (quadratic term) per analog channel. */
    float ana_b[TLM_CH];             /**< Calibration coefficient b (linear term) per analog channel. */
    float ana_c[TLM_CH];             /**< Calibration coefficient c (constant term) per analog channel. */
    int32_t ana_raw_min[TLM_CH];     /**< Expected minimum raw ADC input per analog channel. */
    int32_t ana_raw_max[TLM_CH];     /**< Expected maximum raw ADC input per analog channel. */
    uint8_t ana_dec[TLM_CH];         /**< Number of decimals shown per analog channel. */

    char tlm_bit_name[TLM_BIT_NUM][21];  /**< Per-bit operator-facing label (used only inside the BITS. message). */
    uint8_t tlm_bit_channel[TLM_BIT_NUM];/**< Source sensor channel index for each bit (0xFF = "(none)"). */
    bool tlm_bit_igate[TLM_BIT_NUM];     /**< Per-bit routing: include this bit in the APRS-IS (IGate) beacon. */
    bool tlm_bit_rf[TLM_BIT_NUM];        /**< Per-bit routing: include this bit in the RF beacon. */
    bool bit_enable[TLM_BIT_NUM];        /**< Per-bit enable (defaults true; a disabled bit is sent as 0). */
    bool bit_sense[TLM_BIT_NUM];         /**< true = Normal (raw 1 = asserted), false = Inverted. */

    bool digital_tx2rf;           /**< "Digital: Beacon via RF". */
    bool digital_tx2inet;         /**< "Digital: Beacon via Internet". */
    char proj_title[24];          /**< BITS. project title / "Name". */
} telemetry_config_t;

/**
 * @brief Load the telemetry configuration from /storage/telemetry.json into
 * @p out.
 *
 * Missing/empty/corrupt file is not an error: @p out is filled with
 * all-disabled, empty defaults (tlm_bit_channel[i] == 0xFF, i.e. "(none)")
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
 */
bool telemetry_config_save(const telemetry_config_t *in);

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

#endif // TELEMETRY_H
