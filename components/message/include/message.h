/**
 * @file message.h
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
 * @brief APRS text messaging (send/ack/retry, optional AES-128-CBC payload
 * encryption). Plain C / ESP-IDF.
 *
 * Configuration comes from g_config (app_config_t, web admin "Message" page).
 */

#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define MSG_QUEUE_SIZE 20
#define MSG_TEXT_MAX   200

// Maximum APRS message TEXT length per the de-facto protocol convention used
// by UI-View/Xastir/APRSIS32 and the wider APRS ecosystem: with a 9-char
// fixed-width addressee field and a "{NN" message-number suffix, capping the
// text itself at 67 chars keeps the whole ":ADDRESSEE:text{id" information
// field inside the classic 256-byte TNC2 packet budget. MSG_TEXT_MAX above is
// the (larger) in-memory storage limit for the RX/TX queue, not the on-air
// protocol limit - use this constant wherever user-entered message text needs
// to be validated/truncated before it is transmitted.
#define APRS_MSG_TEXT_STD_MAX 67

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
 * @brief Returns true if `gpio` is acceptable as the "Message Alarm" pin:
 * an output-capable ESP32 GPIO that is not already used by the audio modem
 * (ADC/DAC/PTT), the RF module GPIOs, or any of the sensors_local
 * peripheral pins configured on the "MOD (GPIO)" page (I2C x2, 1-Wire,
 * UART0/1/2, Modbus DE, pulse counters, power switch, PPP modem, GNSS PPS).
 * gpio == -1 ("disabled") is always accepted.
 */
bool message_alarm_gpio_is_valid(int8_t gpio);

/**
 * @brief (Re)configure the Message Alarm GPIO from g_config.msg_alarm_enable
 * / g_config.msg_alarm_gpio. Releases any previously configured pin first
 * (leaving it as plain input, not driven), then - if enabled and the pin
 * passes message_alarm_gpio_is_valid() - sets up the new pin as an output
 * and drives it low (idle). Safe to call again any time the config changes
 * (e.g. right after a webconfig save), and safe to call with alarm disabled
 * or gpio == -1 (no-op besides releasing the previous pin).
 */
void message_alarm_configure(bool enable, int8_t gpio);

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

/**
 * @brief Dump the in-memory message queue (both RX and TX entries) as a JSON
 * array, oldest first, for the "Snd/Rcv Msg" web admin chat page. Each
 * element is
 * {"time":<unix seconds>,"dir":"rx"|"tx","call":"<other station>","text":"<message text>","status":"rx"|"pending"|"sent"}
 * - "status" is only meaningful for "tx" entries ("pending": still awaiting
 * ack/retries remain, "sent": acked or no-retry) and is always "rx" for
 * received entries. `out` is always NUL-terminated; the return value is the
 * number of bytes written not counting that trailing NUL (same convention as
 * lastheard_dump_json()/trafficlog_dump_json()).
 */
size_t message_dump_json(char *out, size_t out_size);

/**
 * @brief Register the function used to actually transmit a built TNC2 packet.
 * `channels` is a bitmask: bit0 = RF, bit1 = INET (matches RF_CHANNEL /
 * INET_CHANNEL conventions used elsewhere in the firmware).
 */
#define MSG_CHANNEL_RF   (1 << 0)
#define MSG_CHANNEL_INET (1 << 1)
void message_set_tx_handler(void (*handler)(const char *packet, size_t len, uint8_t channels));

#endif // MESSAGE_H
