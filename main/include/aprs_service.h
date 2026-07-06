/*
 * aprs_service.h - wires the digirepeater, igate and message components
 * together: registers the AX.25 RX hook (aprs_msg_callback, called by
 * esp32_IDF_libAPRS for every decoded frame), starts the APRS-IS client task,
 * and runs the periodic message-retry tick. All three components read their
 * settings from g_config; nothing here duplicates configuration state.
 */
#ifndef APRS_SERVICE_H
#define APRS_SERVICE_H

/**
 * @brief Start the APRS application layer: message queue init, IGate APRS-IS
 * client task, and the 1 Hz service tick (message retry). Call once from
 * app_task() after app_config_load()/wifi_init(). Safe no-op-ish if the
 * relevant g_config.*_en flags are false (services just idle).
 */
void aprs_service_start(void);

#endif // APRS_SERVICE_H
