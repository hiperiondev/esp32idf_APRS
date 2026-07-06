/*
 * message.h - APRS text messaging (send/ack/retry, optional AES-128-CBC
 * payload encryption). Plain C / ESP-IDF. Configuration comes from g_config
 * (app_config_t, web admin "Message" page).
 */
#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define MSG_QUEUE_SIZE 20
#define MSG_TEXT_MAX   200

typedef struct {
    time_t time;
    int8_t ack; // >0: retries remaining, -1: RX pending, -2: acked/no-retry
    bool rxtx;  // true = RX, false = TX
    uint16_t msgID;
    char callsign[11];
    char text[MSG_TEXT_MAX];
    bool used;
} msg_entry_t;

/**
 * @brief Initialize the in-memory message queue. Call once at startup.
 */
void message_init(void);

/**
 * @brief Send an APRS text message to `toCall`, optionally AES-encrypting the
 * payload with g_config.msg_key. Transmits on RF and/or INET per
 * g_config.msg_rf / g_config.msg_inet via the TX-queue callback registered
 * with message_set_tx_handler().
 */
void sendAPRSMessage(const char *toCall, const char *text, bool encrypt);

/**
 * @brief Send an APRS message ACK ("ackNN") to `toCall`.
 */
void sendAPRSAck(const char *toCall, const char *msgNo);

/**
 * @brief Retry any pending outbound messages whose ack window has elapsed.
 * Call periodically (e.g. once per second) from the application task.
 */
void sendAPRSMessageRetry(void);

/**
 * @brief Parse one incoming TNC2 text line (from RF or APRS-IS) and, if it is
 * an APRS message addressed to g_config.msg_mycall, decrypt/store it and send
 * an ack. ACK lines update the outbound queue's retry state instead.
 */
void handleIncomingAPRS(const char *line);

int pkgMsg_Find(const char *call, uint16_t msgID, bool rxtx);
msg_entry_t getMsgList(int idx);

/**
 * @brief Register the function used to actually transmit a built TNC2 packet.
 * `channels` is a bitmask: bit0 = RF, bit1 = INET (matches RF_CHANNEL /
 * INET_CHANNEL conventions used elsewhere in the firmware).
 */
#define MSG_CHANNEL_RF   (1 << 0)
#define MSG_CHANNEL_INET (1 << 1)
void message_set_tx_handler(void (*handler)(const char *packet, size_t len, uint8_t channels));

#endif // MESSAGE_H
