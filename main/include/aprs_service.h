/**
 * @file aprs_service.h
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
 * @brief APRS application layer: wires the digirepeater, igate and message
 * components together.
 *
 * Installs the modem RX callback (see on_rx_frame/aprs_msg_callback in
 * aprs_service.c, called by esp32idf_radioamateur_modem for every decoded frame),
 * starts the APRS-IS client task, and runs the periodic message-retry tick. All
 * three components read their settings from g_config; nothing here duplicates
 * configuration state.
 */

#ifndef APRS_SERVICE_H
#define APRS_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_version.h"
#include "esp32idf_radioamateur_modem.h"

/**
 * @name RF TX buffers valid range
 *
 * Inclusive valid range for g_config.rf_tx_buffers - the "TX buffers" field on
 * the Radiomodem page's Audio/AFSK section. This is the single source of truth
 * shared by aprs_service.c (which clamps and reads g_config.rf_tx_buffers on
 * every transmit), page_radio.c (which builds the dropdown and clamps the
 * saved form value), and app_config.c (which clamps the value loaded from
 * flash). Deriving the max from ::AX25_TX_FRAME_RING_MAX - the TX frame ring's
 * real usable depth in ax25.c - keeps every caller in sync with the ring's
 * actual capacity, so the config layer can never accept a value the ring
 * could not actually hold.
 * @{
 */
#define RF_TX_BUFFERS_MIN 1                      /**< Minimum allowed value of g_config.rf_tx_buffers. */
#define RF_TX_BUFFERS_MAX AX25_TX_FRAME_RING_MAX /**< Maximum allowed value, derived from the TX ring's true usable depth. */
/** @} */

/**
 * @name Channel-access timing valid ranges
 *
 * Inclusive valid ranges for the four channel-access settings on the
 * Radiomodem page, shared by page_radio.c (which builds the inputs and clamps
 * the posted form value) and app_config.c (which clamps what it loads from
 * flash), so a value that reaches aprs_service_build_modem_config() - which
 * copies all four straight into the modem's runtime configuration - is always
 * one the radio can sanely transmit with.
 *
 * The preamble ceiling is the one that matters most on a shared channel:
 * Ax25TxDelay() turns g_config.preamble into a flag-byte count, so at 1200 Bd
 * every extra 1000 ms of TXDelay is another 150 flag bytes of unmodulated
 * carrier ahead of *every* frame. ::RF_PREAMBLE_MS_MAX keeps that worst case
 * at two seconds; the floor keeps enough preamble for a receiving
 * demodulator's PLL and AGC to settle before the first data bit.
 * @{
 */
#define RF_PREAMBLE_MS_MIN    50    /**< Shortest TXDelay, ms: enough preamble for a distant receiver to lock. */
#define RF_PREAMBLE_MS_MAX    2000  /**< Longest TXDelay, ms: caps the dead carrier sent ahead of every frame. */
#define RF_TX_TIMESLOT_MS_MIN 0     /**< Shortest CSMA slot, ms (0 = transmit as soon as the channel is heard clear). */
#define RF_TX_TIMESLOT_MS_MAX 10000 /**< Longest CSMA slot, ms. */
#define PTT_MIN_UNKEY_MS_MIN  0     /**< Shortest extra PTT-off hold, ms (0 = only the fixed one-tick release holdoff). */
#define PTT_MIN_UNKEY_MS_MAX  5000  /**< Longest extra PTT-off hold, ms. */
#define CSMA_PERSIST_MIN      1     /**< Lowest p-persistence: 0 is refused, it would suppress transmission entirely. */
#define CSMA_PERSIST_MAX      255   /**< Highest p-persistence: transmit on the first clear slot every time. */
/** @} */

/**
 * @name Long-term TX duty-cycle ceiling valid range
 *
 * Inclusive valid range for g_config.duty_cycle_pct - the "Duty cycle limit"
 * field on the Radiomodem page's Audio/AFSK section - shared by page_radio.c
 * (which clamps the posted form value) and app_config.c (which clamps the
 * value loaded from flash).
 *
 * Unlike ::CSMA_PERSIST_MIN/MAX above, which only prevent two stations from
 * transmitting over each other, this bounds this station's OWN cumulative
 * transmit airtime over the rolling window aprs_service.c measures it
 * against (see DUTY_CYCLE_WINDOW_MS there), independent of how often the
 * channel is otherwise clear. It exists so a scheduler pass in which every
 * periodic report (beacon, objects/items, weather, telemetry, bulletins)
 * falls due together cannot key the transmitter indefinitely just because
 * each individual frame passed CSMA - see g_config.duty_cycle_en.
 * @{
 */
#define DUTY_CYCLE_PCT_MIN 1   /**< Lowest duty-cycle ceiling, percent of the rolling window: 0 is refused, it would suppress every non-critical TX. */
#define DUTY_CYCLE_PCT_MAX 100 /**< Highest duty-cycle ceiling, percent of the rolling window: effectively unlimited (never holds non-critical TX back). */
/** @} */

/**
 * @name TNC2 text length limit
 *
 * The RF leg encodes a TNC2 line into an AX.25 frame that has to fit
 * ::AX25_FRAME_MAX_SIZE bytes, so aprs_service_send_tnc2() refuses any text
 * whose length reaches that figure. These two constants publish that single
 * limit to every packet builder in the firmware (beacon.c, weather.c,
 * objects_items.c), so each one sizes its output buffer from the same number
 * the transmit path enforces and reports failure when the assembled line
 * would not fit - rather than returning a line that is logged as built and
 * sent over APRS-IS, but dropped on its way to the modem.
 *
 * A builder therefore declares `char packet[APRS_TNC2_BUF_SIZE]` and returns
 * 0 whenever the assembled length exceeds ::APRS_TNC2_MAX_LEN, which for a
 * buffer of exactly that size is the same condition as snprintf() reporting
 * truncation.
 * @{
 */
#define APRS_TNC2_MAX_LEN  (AX25_FRAME_MAX_SIZE - 1) /**< Longest TNC2 text, in bytes, that aprs_service_send_tnc2() accepts. */
#define APRS_TNC2_BUF_SIZE (AX25_FRAME_MAX_SIZE)     /**< Buffer size a builder must use: ::APRS_TNC2_MAX_LEN bytes plus the terminating NUL. */
/** @} */

/**
 * @name Status report length limits
 *
 * APRS101 chapter 16 defines a status report as the DTI @c '>', followed
 * either by an optional seven-character DHM zulu timestamp and up to
 * 55 characters of status text, or by no timestamp and up to 62 characters
 * of status text. Both cases give the same 63-byte ceiling for the whole
 * information field: the timestamp is carved out of the 62-character budget,
 * not added on top of it.
 *
 * This is a much tighter ceiling than ::APRS_TNC2_MAX_LEN, which only bounds
 * what the AX.25 frame can carry, and it is the one that decides whether
 * receivers show the report the way it was meant: everything the status
 * report can carry beyond the operator's own text - the timestamp, the
 * frequency block and the Maidenhead locator - is spent out of that same
 * 63-byte information field. buildStatusPacket() in beacon.c is the single
 * consumer, and it drops its optional blocks in a defined order rather than
 * let the assembled field pass ::APRS_STATUS_INFO_MAX.
 * @{
 */
#define APRS_STATUS_TEXT_MAX 62                         /**< Longest status text, in characters, APRS101 ch.16 allows with no timestamp. */
#define APRS_STATUS_INFO_MAX (1 + APRS_STATUS_TEXT_MAX) /**< Longest status information field, in bytes: the same 63-byte ceiling either way. */
/** @} */

/**
 * @name APRS destination callsign (TOCALL)
 *
 * The AX.25 destination address every packet this firmware builds carries in
 * its header, per APRS 1.1 (page 14) and the aprsorg/aprs-deviceid tocalls.yaml
 * registry. Single source of truth for beacon.c, bulletins.c, objects_items.c,
 * telemetry.c, weather.c, message.c, query.c and aprs_service.c's own
 * third-party wrapping, so every packet type this station transmits identifies
 * itself the same way and a future allocation only needs to change this one
 * definition.
 *
 * "APE32I" is not yet an allocated TOCALL: as of this writing it has no exact
 * entry in tocalls.yaml, so it only falls under the generic "APE???" wildcard
 * (model: Telemetry devices, no vendor, no class), and device-ID consumers
 * (aprs.fi, findu, YAAC) show this station as an anonymous telemetry device
 * rather than identifying it as this firmware. A specific allocation for
 * "APE32I" has been requested at github.com/aprsorg/aprs-deviceid; once it is
 * granted, an exact match takes priority over the wildcard for every
 * consumer that follows the registry's longest-match rule, and this station
 * is then identified by name without any further code change.
 *
 * There is no alternate-net destination beside it. APRS101 chapter 4 lets a
 * group of stations agree on a destination address of their own so that their
 * traffic forms a private net inside the same channel; every packet this
 * firmware originates carries this TOCALL instead, which is also what keeps
 * the device identification above meaningful. Nothing is lost on the receive
 * side: an alternate net is a convention among senders, and this station
 * classifies, gates and digipeats what it hears by data type and path, never
 * by destination address, so alternate-net traffic is carried like any other.
 * @{
 */
#define APRS_TOCALL "APE32I" /**< TOCALL for this firmware, pending allocation; see the registration note above. */
/** @} */

/**
 * @name APRS-IS software identification (@c vers clause)
 *
 * The name and version this station announces in the @c vers clause of its
 * APRS-IS login line, as in
 * @c "user CALL pass 12345 vers esp32_APRS_igate 1.0.0".
 *
 * This clause is how an APRS-IS server operator knows which software is
 * connected, and it is the only channel through which the author of a
 * misbehaving client can be reached; the public APRS-IS client statistics
 * group sessions by the same pair. It therefore has to name @e this firmware:
 * announcing the name of a project this one is only derived from sends
 * operators to an author who cannot change anything here, and leaves this
 * firmware invisible in those statistics.
 *
 * ::APRS_SOFTWARE_NAME plays on the Internet side the role ::APRS_TOCALL
 * plays on the air, and is likewise the single source of truth for it -
 * igate.c, which builds the login line, is the only consumer of either macro
 * here. The version is ::FIRMWARE_INFO rather than a second literal, so the
 * numbers in app_version.h are the only place a release has to be bumped.
 * @{
 */
#define APRS_SOFTWARE_NAME    "esp32_APRS_igate" /**< Software name sent in the APRS-IS @c vers clause; one token, no spaces. */
#define APRS_SOFTWARE_VERSION FIRMWARE_INFO      /**< Version sent right after ::APRS_SOFTWARE_NAME, taken from the firmware version macros. */
/** @} */

/**
 * @name Mic-E device identifier (Manufacturer + Version)
 *
 * The two bytes every Mic-E report this firmware transmits carries at the end
 * of its information field, per aprs12/mic-e-types.txt: the manufacturer byte
 * followed by the version byte, separated from the free text by a single
 * space. Single source of truth for beacon.c, in the same role ::APRS_TOCALL
 * plays for every other packet type.
 *
 * A Mic-E frame puts position data in its AX.25 destination address, so the
 * APxxxx TOCALL cannot identify the sending device the way it does elsewhere,
 * and this pair is the only channel left: without it a station is anonymous
 * to aprs.fi, findu and YAAC, and indistinguishable from a one-way tracker.
 *
 * "T1" is not an allocated identifier. The aprsorg/aprs-deviceid registry
 * (tocalls.yaml, `mice:` section - the master database for these allocations
 * since 2022-02-19) hands out the manufacturer byte, and no allocation there
 * uses 'T': the allocated manufacturer bytes are '_' (Yaesu), '(' (Anytone),
 * '|' (Byonics), '^' (HinzTec), '*' (KissOZ, NOR), ':' (SQ8L, SCS), '['
 * (APRSdroid), ' ' (SainSonic) and the legacy Kenwood '>' and ']' prefixes.
 * This uses the free 'T' space for the same reason ::APRS_TOCALL names
 * "APE32I" ahead of its own allocation - an identifier that collides with
 * nobody - pending a proper allocation request at
 * github.com/aprsorg/aprs-deviceid, after which only this one definition
 * changes.
 * @{
 */
#define APRS_MICE_DEVICE_ID "T1" /**< Experimental (unallocated) Mic-E Manufacturer/Version pair; see the allocation note above. */
/** @} */

/**
 * @brief Start the APRS application layer: message queue init, modem RX
 * callback, IGate APRS-IS client task, and the 1 Hz service tick (message
 * retry). Call once from app_task() after app_config_load()/wifi_init() and
 * BEFORE modem_init() - the modem starts delivering frames from inside
 * modem_init(), and this is what installs the callback they go to. Safe
 * no-op-ish if the relevant g_config.*_en flags are false (services just idle).
 */
void aprs_service_start(void);

/**
 * @brief Queue a TNC2-format packet ("SRC-N>DST,PATH:info") for transmission
 * on RF.
 *
 * The modem's modem_send_tnc2() takes a NUL-terminated string, while every
 * caller here has a pointer+length into a larger buffer, so this performs that
 * conversion, and the AX25_FRAME_MAX_SIZE bounds check, once and centrally.
 *
 * This is the RF leg only - it never touches the APRS-IS/IGate socket
 * (see igate_send_raw()), and a discard here (modem not ready, RF TX
 * buffer still busy, or packet too long) never affects it either. Callers
 * that also send the same packet over IGate do so via their own,
 * independent igate_send_raw() call.
 *
 * @param packet TNC2 text, not necessarily NUL-terminated at @p len.
 * @param len    Length, in bytes, of the packet text. Anything longer than
 *               ::APRS_TNC2_MAX_LEN cannot be encoded into an AX.25 frame and
 *               is discarded here.
 * @return true if the packet was handed to the modem for transmission,
 *         false if it was discarded (see log for the reason).
 */
bool aprs_service_send_tnc2(const char *packet, size_t len);

/**
 * @brief Application-level counters for the web dashboard's STATISTICS
 * panel, tracked independently of whether the digipeater or IGate features
 * are enabled - unlike igate_get_stats(), whose counters only move while
 * g_config.igate_en is on, these always reflect real RF/traffic activity.
 */
typedef struct {
    uint32_t radio_rx; /**< Frames decoded off RF (every frame the modem hands up), regardless of what happened to them after. */
    uint32_t radio_tx; /**< Frames successfully transmitted on RF (beacons, digipeats, INET2RF relays, messages, etc). */
    uint32_t rf2inet;  /**< Frames relayed from RF to APRS-IS (IGate actually uplinked them). */
    uint32_t inet2rf;  /**< Lines relayed from APRS-IS to RF (IGate actually transmitted them). */
    uint32_t
        digi; /**< Frames digipeated (path rewritten and re-transmitted). Only ever moves while digi_en is on - there is nothing to digipeat with it off. */
    uint32_t drop; /**< Frames received but discarded at the RX/service level (e.g. placeholder/invalid source callsign) - tracked regardless of
                      digi_en/igate_en, unlike igate_get_stats()'s own drop counters. */
    uint32_t
        err; /**< Frames the modem handed up that failed to decode as a valid APRS (UI, no-layer-3) AX.25 frame - tracked regardless of digi_en/igate_en. */
    uint32_t tx_queue_depth;   /**< Frames currently sitting in the RF TX ring right now (waiting to key up, or on the air). An at-a-glance view of the backlog
                                  the "TX buffers" limit caps - a value that stays pinned near tx_queue_limit while drops climb is the visible symptom of a
                                  saturated RF leg (see the drain-wait in aprs_service_send_tnc2()). */
    uint32_t tx_queue_limit;   /**< The effective "TX buffers" cap (g_config.rf_tx_buffers, clamped): new frames are dropped once tx_queue_depth reaches this.
                                  Shown alongside tx_queue_depth so the dashboard reads like the console's "n/n pending" line. */
    uint32_t csma_busy_forced; /**< Key-ups in which the CSMA anti-starvation floor transmitted over a channel that was still busy after the whole backoff
                                  run. A congestion figure about the frequency: the frame was sent, nothing was lost. Read live from
                                  modem_channel_busy_count(). */
    uint32_t csma_persist_forced;  /**< Key-ups in which the CSMA anti-starvation floor transmitted after a backoff run that found the channel clear every
                                      slot and missed the persistence roll every time. This one measures only the configured CSMA persistence: with the
                                      standard value of 63 about one key-up in ten lands here, so a figure near a tenth of PACKET TX is normal and a much
                                      larger share means persistence is set too low. Read live from modem_persistence_missed_count(). */
    uint32_t tx_duty_cycle_pct;    /**< This station's own estimated transmit airtime right now, as a percentage of the rolling window
                                      aprs_service.c measures it over (see DUTY_CYCLE_WINDOW_MS there) - independent of channel congestion, unlike the two
                                      CSMA figures above. Kept live even while duty_cycle_limit_pct reads 0 (limiter disabled), so the dashboard can show
                                      the figure an operator would be capping if they turned the limiter on. */
    uint32_t duty_cycle_limit_pct; /**< The configured duty-cycle ceiling (g_config.duty_cycle_pct, clamped to DUTY_CYCLE_PCT_MIN..MAX) that
                                      tx_duty_cycle_pct is measured against, or 0 if g_config.duty_cycle_en is off (no ceiling enforced). Once
                                      tx_duty_cycle_pct reaches this value, non-critical RF TX (beacons, objects/items, weather, telemetry, bulk IGate
                                      INET->RF relay) is held back - see DROP_TX_DUTY_CYCLE in igate.h - while message traffic and digipeat repeats keep
                                      transmitting. */
} aprs_service_stats_t;

/**
 * @brief Snapshot of the current dashboard statistics counters.
 * @return Current counter values. Safe to call from any task.
 */
aprs_service_stats_t aprs_service_get_stats(void);

/**
 * @brief Register the calling task as the periodic-beacon scheduler context.
 *
 * In that one task - and only there - aprs_service_send_tnc2() is permitted to
 * wait briefly for the RF TX ring to drain to below the "TX buffers" limit
 * before dropping a frame, instead of discarding it the instant the ring is
 * full. That staggers own-station beacons which fall due together in a single
 * synchronous scheduler pass, so every due beacon eventually keys up even with
 * the factory-default "TX buffers = 1", while preserving the "one packet on the
 * ring at a time" discipline. Every other caller (RX/digipeat, INET2RF, message
 * TX) keeps the original non-blocking drop-if-full behavior, so a busy RF leg
 * never stalls the RX decode or the APRS-IS socket task.
 *
 * Called once, from inside the beacon scheduler task, on entry.
 */
void aprs_service_set_beacon_context(void);

/**
 * @brief Build the modem component's runtime configuration from g_config.
 *
 * The single mapping point between the web admin's Radio / Modem page and
 * modem_config_t, shared by the boot-time modem_init() in main.c, the live
 * re-apply on Save (aprs_service_apply_modem_config()) and the LOOP TEST.
 *
 * Note that the audio pins (ADC, DAC, and PTT), PTT active level, ADC
 * attenuation and sample rates are NOT part of this: the modem component
 * takes them as compile-time constants (see the idf_build_set_property()
 * block in the top-level CMakeLists.txt). Software squelch, RX volume and the
 * AGC ceiling have no equivalent at all in the modem component, so there are
 * no g_config fields (sql_level / volume / agc_max_gain) for them.
 *
 * @param cfg         Destination configuration, filled completely.
 * @param full_duplex true to transmit without waiting for a clear channel.
 *                    Pass false for normal on-air operation; only the LOOP
 *                    TEST passes true, because a DAC->ADC wire means the node
 *                    always hears its own carrier and CSMA would never key up.
 */
void aprs_service_build_modem_config(modem_config_t *cfg, bool full_duplex);

/**
 * @brief Re-apply the current g_config modem settings to the running modem.
 *
 * Called from the Radio page's Save handler so a changed modulation, preamble,
 * time slot, flat-audio flag or FX.25 mode takes effect without a reboot.
 * No-op when the modem was never brought up (see aprs_service_modem_ready()).
 */
void aprs_service_apply_modem_config(void);

/**
 * @brief Tell aprs_service.c that modem_init() has actually been called and
 * succeeded (the audio ADC/DAC AFSK modem hardware is up), so
 * aprs_loop_test_run() can tell "disabled in config" apart from "enabled but
 * not yet applied - reboot needed" whenever the webconfig checkbox is toggled
 * without a reboot. Call once from main.c, right after a successful
 * modem_init().
 */
void aprs_service_notify_modem_ready(void);

/**
 * @brief Whether the audio ADC/DAC AFSK modem hardware has been brought up
 * this boot (i.e. aprs_service_notify_modem_ready() has been called).
 */
bool aprs_service_modem_ready(void);

/**
 * @brief Whether this station is currently able to transmit on RF at all.
 *
 * True only when the audio ADC/DAC AFSK modem hardware has been brought up
 * this boot (see aprs_service_modem_ready()) and the "Enable audio ADC/DAC
 * modem" setting (g_config.audio_modem_en) is on. This is the single source
 * of truth for raw hardware/config transmit capability, used by the LOOP TEST
 * availability check. It says nothing about whether the IGate is actually
 * configured to relay APRS-IS traffic back to RF for a given station - see
 * aprs_service_can_gate_to_rf() for that.
 *
 * @return true if this station can currently transmit, false otherwise.
 */
bool aprs_service_can_transmit(void);

/**
 * @brief Whether this IGate can currently gate messages from APRS-IS to RF.
 *
 * True only when the station can transmit at all (see
 * aprs_service_can_transmit()), the IGate service is enabled
 * (g_config.igate_en) and the APRS-IS -> RF gateway direction is enabled
 * (g_config.inet2rf). This is the criterion QCON actually asks for when
 * choosing between the qAR and qAO q constructs on a frame gated from RF:
 * qAR marks an IGate that can gate messages to RF for the station being
 * gated, qAO marks one that cannot - which is a stronger condition than
 * merely having a working transmitter, since a station with a working
 * transmitter but INET->RF relaying turned off can never actually deliver a
 * message to RF.
 *
 * @return true if this IGate can currently gate messages to RF, false
 *         otherwise.
 */
bool aprs_service_can_gate_to_rf(void);

/**
 * @brief Audio ADC/DAC AFSK modem self-test ("LOOP TEST" button on the
 * Radio/Modem webconfig page). Requires the ADC and DAC GPIOs (MODEM_ADC_GPIO
 * and MODEM_DAC_GPIO, set in the top-level CMakeLists.txt) to be wired
 * together as a physical audio loopback so whatever the modem transmits is
 * immediately re-received on the same board.
 *
 * Builds a small APRS packet with a random one-time token, temporarily
 * diverts decoded frames to its own hook (so the test frame is never treated
 * as real RF traffic / digipeated / uplinked to APRS-IS), switches the modem
 * to full duplex (a wire loop never gives CSMA a clear channel), transmits it
 * via the DAC, and waits for the ADC/demodulator/decoder chain to hand the
 * same frame back. The real hook and the configured duplex mode are always
 * restored before returning.
 *
 * @param msg      Buffer that receives a human-readable PASS/FAIL result.
 * @param msg_len  Size of msg.
 * @return true if the packet was sent and correctly decoded back, false
 *         otherwise (msg explains why: not initialized, timeout, mismatch,
 *         or a test already running).
 */
bool aprs_loop_test_run(char *msg, size_t msg_len);

#endif // APRS_SERVICE_H
