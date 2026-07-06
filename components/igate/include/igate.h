/*
 * igate.h - APRS-IS Internet Gateway component (RF->INET and INET->RF), C /
 * ESP-IDF (LWIP sockets). Configuration comes entirely from g_config
 * (app_config_t, see main/include/app_config.h / web admin "IGate" page).
 */
#ifndef IGATE_H
#define IGATE_H

#include <stdbool.h>
#include <stdint.h>

#include "AX25.h"

#define DUP_PACKET_CACHE_SIZE 10
#define DUP_PACKET_TIMEOUT_MS 30000 // 30 s

typedef struct {
    uint32_t rxCount;   // frames considered for gatewaying
    uint32_t txCount;   // frames actually sent to APRS-IS
    uint32_t dropCount; // frames dropped (filters, NOGATE, RFONLY, etc.)
    uint32_t dupCount;  // duplicate frames suppressed
} igate_stats_t;

/**
 * @brief Start the IGate service task (APRS-IS TCP client with auto-reconnect,
 * login, RX line pump). No-op if g_config.igate_en is false. Safe to call once
 * from app startup; re-reads g_config each reconnect so web-admin changes take
 * effect after the next reconnect cycle.
 */
void igate_start(void);

/**
 * @brief Stop the IGate service task and close the APRS-IS connection.
 */
void igate_stop(void);

/**
 * @brief Feed one RF-decoded AX.25 frame to the gateway (RF -> INET direction).
 * Applies the RFONLY/TCPIP/qA/NOGATE/satellite-gate filters, builds the TNC2
 * text line with qAR/qAO path, de-duplicates, and forwards it to APRS-IS if
 * connected.
 * @return 1 if forwarded, 0 if dropped/duplicate/not connected.
 */
int igateProcess(AX25Msg *packet);

/**
 * @brief Duplicate-packet check only (exposed for reuse by other components,
 * e.g. digipeater wanting to avoid re-announcing the same frame).
 */
bool isDuplicatePacket(AX25Msg *packet);
void clearExpiredDuplicates(void);

igate_stats_t igate_get_stats(void);
void igate_reset_stats(void);

/**
 * @brief True while the APRS-IS TCP socket is currently open (logged in and
 * pumping the RX line reader). Used by the web dashboard's "Network Status"
 * panel (APRS-IS pill), mirroring aprsClient.connected() on the reference
 * ESP32APRS_Audio dashboard.
 */
bool igate_is_connected(void);

/**
 * @brief Register the handler invoked for every raw TNC2 text line received
 * from APRS-IS (INET -> RF direction), e.g. to hand it to the message
 * component and/or re-transmit it on RF via APRS_sendTNC2Pkt(). Only called
 * when g_config.inet2rf is true.
 */
void igate_set_inet2rf_handler(void (*handler)(const char *line));

/**
 * @brief Send a raw already-built TNC2 text line to APRS-IS (used by the
 * message component to gate outbound APRS messages/acks over INET). No-op
 * (returns false) if not currently connected.
 */
bool igate_send_raw(const char *line, size_t len);

#endif // IGATE_H
