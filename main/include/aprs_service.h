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

#include "esp32idf_radioamateur_modem.h"

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
 * Replaces the old component's APRS_sendTNC2Pkt(raw, len). The modem's
 * modem_send_tnc2() takes a NUL-terminated string, while every caller here has
 * a pointer+length into a larger buffer, so this does that conversion (and the
 * AX25_FRAME_MAX_SIZE bounds check) once, centrally.
 *
 * This is the RF leg only - it never touches the APRS-IS/IGate socket
 * (see igate_send_raw()), and a discard here (modem not ready, RF TX
 * buffer still busy, or packet too long) never affects it either. Callers
 * that also send the same packet over IGate do so via their own,
 * independent igate_send_raw() call.
 *
 * @param packet TNC2 text, not necessarily NUL-terminated at @p len.
 * @param len    Length, in bytes, of the packet text.
 * @return true if the packet was handed to the modem for transmission,
 *         false if it was discarded (see log for the reason).
 */
bool aprs_service_send_tnc2(const char *packet, size_t len);

/**
 * @brief Application-level counters for the web dashboard's STATISTICS
 * panel, tracked independently of whether the digipeater or IGate features
 * are enabled - unlike digi_get_stats()/igate_get_stats(), whose counters
 * only move while their respective g_config.*_en flag is on, these always
 * reflect real RF/traffic activity.
 */
typedef struct {
    uint32_t radio_rx; /**< Frames decoded off RF (every frame the modem hands up), regardless of what happened to them after. */
    uint32_t radio_tx; /**< Frames successfully transmitted on RF (beacons, digipeats, INET2RF relays, messages, etc). */
    uint32_t rf2inet;  /**< Frames relayed from RF to APRS-IS (IGate actually uplinked them). */
    uint32_t inet2rf;  /**< Lines relayed from APRS-IS to RF (IGate actually transmitted them). */
    uint32_t digi;     /**< Frames digipeated (path rewritten and re-transmitted). Only ever moves while digi_en is on - there is nothing to digipeat with it off. */
    uint32_t drop;     /**< Frames received but discarded at the RX/service level (e.g. placeholder/invalid source callsign) - tracked regardless of digi_en/igate_en, unlike digi_get_stats()/igate_get_stats()'s own drop counters. */
    uint32_t err;      /**< Frames the modem handed up that failed to decode as a valid APRS (UI, no-layer-3) AX.25 frame - tracked regardless of digi_en/igate_en. */
} aprs_service_stats_t;

/**
 * @brief Snapshot of the current dashboard statistics counters.
 * @return Current counter values. Safe to call from any task.
 */
aprs_service_stats_t aprs_service_get_stats(void);

/**
 * @brief Build the modem component's runtime configuration from g_config.
 *
 * The single mapping point between the web admin's Radio / Modem page and
 * modem_config_t, shared by the boot-time modem_init() in main.c, the live
 * re-apply on Save (aprs_service_apply_modem_config()) and the LOOP TEST.
 *
 * Note that the audio pins, ADC attenuation and sample rates are NOT part of
 * this: the modem component takes them as compile-time constants (see the
 * idf_build_set_property() block in the top-level CMakeLists.txt), unlike the
 * esp32_IDF_libAPRS component this replaced, which took them at runtime in
 * aprs_modem_config_t. Software squelch, RX volume and the AGC ceiling have no
 * equivalent at all in the new component; the g_config fields that used to
 * carry them (sql_level / volume / agc_max_gain) have been removed rather than
 * left in flash as settings that change nothing. PTT is the one pin still
 * taken at runtime, from g_config.rf_ptt_gpio / .rf_ptt_active.
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
