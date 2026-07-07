/*
 * aprs_service.h - wires the digirepeater, igate and message components
 * together: registers the AX.25 RX hook (aprs_msg_callback, called by
 * esp32_IDF_libAPRS for every decoded frame), starts the APRS-IS client task,
 * and runs the periodic message-retry tick. All three components read their
 * settings from g_config; nothing here duplicates configuration state.
 */
#ifndef APRS_SERVICE_H
#define APRS_SERVICE_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Start the APRS application layer: message queue init, IGate APRS-IS
 * client task, and the 1 Hz service tick (message retry). Call once from
 * app_task() after app_config_load()/wifi_init(). Safe no-op-ish if the
 * relevant g_config.*_en flags are false (services just idle).
 */
void aprs_service_start(void);

/**
 * @brief Tell aprs_service.c that APRS_init() has actually been called (the
 * audio ADC/DAC AFSK modem hardware is up), so aprs_loop_test_run() can tell
 * "disabled in config" apart from "enabled but not yet applied - reboot
 * needed" whenever the webconfig checkbox is toggled without a reboot.
 * Call once from main.c, right after a successful APRS_init().
 */
void aprs_service_notify_modem_ready(void);

/**
 * @brief Audio ADC/DAC AFSK modem self-test ("LOOP TEST" button on the
 * Radio/Modem webconfig page). Requires the ADC and DAC GPIOs to be wired
 * together (a physical audio loopback) so whatever the modem transmits is
 * immediately re-received on the same board.
 *
 * Builds a small APRS packet with a random one-time token, temporarily
 * takes over the AX.25 RX hook (so the test frame is never treated as real
 * RF traffic / digipeated / uplinked to APRS-IS), transmits it via the DAC,
 * and waits for the ADC/demodulator/decoder chain to hand the same frame
 * back. The original RX hook is always restored before returning.
 *
 * @param msg      Buffer that receives a human-readable PASS/FAIL result.
 * @param msg_len  Size of msg.
 * @return true if the packet was sent and correctly decoded back, false
 *         otherwise (msg explains why: not initialized, timeout, mismatch,
 *         or a test already running).
 */
bool aprs_loop_test_run(char *msg, size_t msg_len);

#endif // APRS_SERVICE_H
