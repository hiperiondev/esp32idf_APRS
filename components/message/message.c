/**
 * @file message.c
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
 * @brief APRS text messaging implementation: outgoing message and ACK
 * formatting, incoming message parsing and acknowledgement, retry/timeout
 * handling of the in-memory queue, and optional AES-128-CBC payload
 * encryption.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mbedtls/aes.h"
#include "mbedtls/base64.h"
#include "mbedtls/md5.h"

#include "afsk.h" // afsk_ptt_gpio_is_valid(), MODEM_ADC_GPIO / MODEM_DAC_GPIO
#include "app_config.h"
#include "BMP180.h" // bmp180_gpio_is_reserved(): keep the I2C pins out of the alarm pin
#include "message.h"

static const char *TAG = "message";

#define AES_BLOCK_SIZE 16
#define MSG_ALARM_PULSE_MS 1000

static msg_entry_t s_queue[MSG_QUEUE_SIZE];
static uint16_t s_msgID = 0;
static void (*s_txHandler)(const char *packet, size_t len, uint8_t channels) = NULL;

void message_set_tx_handler(void (*handler)(const char *packet, size_t len, uint8_t channels)) {
    s_txHandler = handler;
}

void message_init(void) {
    memset(s_queue, 0, sizeof(s_queue));
}

// ---------------------------------------------------------------------------
// Message Alarm GPIO: driven to 1 for MSG_ALARM_PULSE_MS whenever a direct
// message addressed to g_config.msg_mycall is received, then back to 0 until
// the next one. Disabled by default (g_config.msg_alarm_enable == false /
// g_config.msg_alarm_gpio == -1).
// ---------------------------------------------------------------------------
static int8_t s_alarmGpio = -1;
static esp_timer_handle_t s_alarmTimer = NULL;

bool message_alarm_gpio_is_valid(int8_t gpio) {
    if (gpio == -1)
        return true; // "disabled" is always accepted

    // Output-capable, not the input-only pads, not the internal flash/PSRAM
    // pads, and not colliding with the audio modem's ADC/DAC - same rules
    // as the PTT pin (see afsk_ptt_gpio_is_valid()).
    if (!afsk_ptt_gpio_is_valid(gpio))
        return false;

    // Not already used by the PTT pin. (rf_tx_gpio/rf_rx_gpio/rf_sql_gpio/
    // rf_pd_gpio/rf_pwr_gpio used to be checked here too, but those were
    // legacy leftovers from a removed "RF Module GPIO" page - nothing applied
    // them to a real pin, so they no longer blocked an otherwise-free GPIO and
    // have since been deleted from app_config_t.)
    if (gpio == g_config.rf_ptt_gpio)
        return false;

    // Not already used by any sensors_local peripheral bus. (These fields are
    // config-struct-only placeholders with no driver behind them yet - see
    // the same note in web_gpio_collect_used() - so they're not checked here;
    // only pins that are genuinely wired to something (PTT above, BMP180 I2C
    // via bmp180_gpio_is_reserved()) can make a GPIO invalid.)
    if (bmp180_gpio_is_reserved(gpio))
        return false;

    return true;
}

static void alarmTimerCb(void *arg) {
    (void)arg;
    if (s_alarmGpio >= 0)
        gpio_set_level((gpio_num_t)s_alarmGpio, 0);
}

void message_alarm_configure(bool enable, int8_t gpio) {
    int8_t new_gpio = (enable && message_alarm_gpio_is_valid(gpio)) ? gpio : -1;

    if (s_alarmTimer)
        esp_timer_stop(s_alarmTimer); // no-op if not running

    // Release the previous pin (disabling, or switching to a different one)
    // so a stale output isn't left driving.
    if (s_alarmGpio >= 0 && s_alarmGpio != new_gpio) {
        gpio_set_level((gpio_num_t)s_alarmGpio, 0);
        gpio_set_direction((gpio_num_t)s_alarmGpio, GPIO_MODE_INPUT);
    }

    s_alarmGpio = new_gpio;

    if (s_alarmGpio < 0)
        return;

    if (!s_alarmTimer) {
        const esp_timer_create_args_t args = {
            .callback = alarmTimerCb,
            .name = "msg_alarm",
        };
        if (esp_timer_create(&args, &s_alarmTimer) != ESP_OK) {
            ESP_LOGW(TAG, "Message Alarm: failed to create timer, disabling");
            s_alarmGpio = -1;
            return;
        }
    }

    gpio_reset_pin((gpio_num_t)s_alarmGpio);
    gpio_set_direction((gpio_num_t)s_alarmGpio, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)s_alarmGpio, 0); // idle low until a message arrives
    ESP_LOGI(TAG, "Message Alarm: GPIO%d enabled", (int)s_alarmGpio);
}

// Pulses the alarm pin high for MSG_ALARM_PULSE_MS. Called each time a direct
// message for g_config.msg_mycall is received. Re-arms the timer on every
// call, so back-to-back messages keep the pin high without flickering, and it
// only drops back to 0 once MSG_ALARM_PULSE_MS elapses with no new message.
static void message_alarm_pulse(void) {
    if (s_alarmGpio < 0 || !s_alarmTimer)
        return;

    gpio_set_level((gpio_num_t)s_alarmGpio, 1);
    esp_timer_stop(s_alarmTimer); // no-op if not currently running
    esp_timer_start_once(s_alarmTimer, (uint64_t)MSG_ALARM_PULSE_MS * 1000ULL);
}

// ---------------------------------------------------------------------------
// Helpers: hex <-> bytes, trim, uppercase, PKCS7
// ---------------------------------------------------------------------------
static size_t hexStringToBytes(const char *hex, uint8_t *out, size_t maxLen) {
    size_t len = strlen(hex);
    size_t n = 0;
    if (len % 2 != 0)
        return 0;
    for (size_t i = 0; i < len && n < maxLen; i += 2) {
        char c1 = hex[i], c2 = hex[i + 1];
        uint8_t hi = (c1 >= '0' && c1 <= '9') ? c1 - '0' : (c1 >= 'A' && c1 <= 'F') ? c1 - 'A' + 10 : (c1 >= 'a' && c1 <= 'f') ? c1 - 'a' + 10 : 0;
        uint8_t lo = (c2 >= '0' && c2 <= '9') ? c2 - '0' : (c2 >= 'A' && c2 <= 'F') ? c2 - 'A' + 10 : (c2 >= 'a' && c2 <= 'f') ? c2 - 'a' + 10 : 0;
        out[n++] = (hi << 4) | lo;
    }
    return n;
}

static void trimUpper(char *s) {
    // trim
    char *start = s;
    while (*start && isspace((unsigned char)*start))
        start++;
    if (start != s)
        memmove(s, start, strlen(start) + 1);
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        s[--len] = 0;
    for (size_t i = 0; i < len; i++)
        s[i] = toupper((unsigned char)s[i]);
}

// Compares two callsigns ignoring any "-SSID" suffix, so a message sent to
// "N0CALL", "N0CALL-0".."N0CALL-15" (or any other/garbled SSID) is treated
// as addressed to station "N0CALL". Case-insensitive; SSID digits themselves
// are not validated, only stripped.
static bool callsignBaseMatch(const char *a, const char *b) {
    size_t na = 0, nb = 0;
    while (a[na] && a[na] != '-')
        na++;
    while (b[nb] && b[nb] != '-')
        nb++;
    if (na == 0 || na != nb)
        return false;
    return strncasecmp(a, b, na) == 0;
}

static void pkcs7Pad(const uint8_t *in, size_t inLen, uint8_t *out, size_t *outLen) {
    size_t pad = AES_BLOCK_SIZE - (inLen % AES_BLOCK_SIZE);
    memcpy(out, in, inLen);
    memset(out + inLen, (uint8_t)pad, pad);
    *outLen = inLen + pad;
}

static bool pkcs7Unpad(uint8_t *buf, size_t bufLen, size_t *outLen) {
    if (bufLen == 0 || bufLen % AES_BLOCK_SIZE != 0)
        return false;
    uint8_t pad = buf[bufLen - 1];
    if (pad == 0 || pad > AES_BLOCK_SIZE)
        return false;
    for (size_t i = 0; i < pad; i++) {
        if (buf[bufLen - 1 - i] != pad)
            return false;
    }
    *outLen = bufLen - pad;
    return true;
}

// IV = MD5("<callsign>_<msgID>"), matching the original firmware's scheme
// (deterministic per-message IV rather than random, so both ends can derive it).
static void deriveIv(const char *callsign, uint16_t msgID, uint8_t iv[16]) {
    char callTrim[16];
    strncpy(callTrim, callsign, sizeof(callTrim) - 1);
    callTrim[sizeof(callTrim) - 1] = 0;
    trimUpper(callTrim);

    char input[32];
    snprintf(input, sizeof(input), "%s_%u", callTrim, (unsigned)msgID);

    mbedtls_md5((const unsigned char *)input, strlen(input), iv);
}

// Returns encrypted length written to `out` (base64 text, NUL-terminated), or 0 on failure.
static size_t aesEncryptBase64WithIV(const char *plain, const uint8_t key[16], uint16_t msgID, const char *myCall, char *out, size_t outMax) {
    size_t inLen = strlen(plain);
    size_t paddedLen = inLen + (AES_BLOCK_SIZE - (inLen % AES_BLOCK_SIZE));
    if (paddedLen > 256)
        return 0;

    uint8_t padded[256];
    pkcs7Pad((const uint8_t *)plain, inLen, padded, &paddedLen);

    uint8_t iv[AES_BLOCK_SIZE];
    deriveIv(myCall, msgID, iv);

    uint8_t cipher[256];
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, key, 128);
    mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, paddedLen, iv, padded, cipher);
    mbedtls_aes_free(&aes);

    size_t olen = 0;
    mbedtls_base64_encode((unsigned char *)out, outMax, &olen, cipher, paddedLen);
    if (olen < outMax)
        out[olen] = 0;
    return olen;
}

// Returns plaintext length written to `out` (NUL-terminated), or 0 on failure.
static size_t aesDecryptBase64WithIV(const char *b64, const uint8_t key[16], const char *fromCall, uint16_t msgID, char *out, size_t outMax) {
    size_t b64Len = strlen(b64);
    uint8_t decoded[256];
    size_t decLen = 0;
    if (mbedtls_base64_decode(decoded, sizeof(decoded), &decLen, (const unsigned char *)b64, b64Len) != 0)
        return 0;
    if (decLen == 0 || decLen % AES_BLOCK_SIZE != 0)
        return 0;

    uint8_t iv[AES_BLOCK_SIZE];
    deriveIv(fromCall, msgID, iv);

    uint8_t plainPadded[256];
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, key, 128);
    mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, decLen, iv, decoded, plainPadded);
    mbedtls_aes_free(&aes);

    size_t outLen = 0;
    if (!pkcs7Unpad(plainPadded, decLen, &outLen))
        return 0;
    if (outLen >= outMax)
        outLen = outMax - 1;
    memcpy(out, plainPadded, outLen);
    out[outLen] = 0;
    return outLen;
}

// ---------------------------------------------------------------------------
// Queue management
// ---------------------------------------------------------------------------
int pkgMsg_Find(const char *call, uint16_t msgID, bool rxtx) {
    // Exact match only. s_queue[i].callsign is always NUL-terminated (see
    // pkgMsgUpdate(), which memset()s the field before strncpy()), so a
    // plain strcmp() is safe here. This used to be strstr(), which does a
    // *substring* search: e.g. a stored "N0CALL" would match an incoming
    // "N0CALL-9" and vice versa, and two unrelated callsigns could
    // coincidentally overlap. That let an ack/reply from the wrong station
    // mark another station's queued message as acknowledged, or let a new
    // outgoing message overwrite an unrelated in-flight queue slot.
    for (int i = 0; i < MSG_QUEUE_SIZE; i++) {
        if (s_queue[i].used && s_queue[i].msgID == msgID && s_queue[i].rxtx == rxtx && strcmp(s_queue[i].callsign, call) == 0)
            return i;
    }
    return -1;
}

static int pkgMsgOldestSlot(void) {
    int ret = 0;
    time_t minimum = time(NULL) + 86400;
    for (int i = 0; i < MSG_QUEUE_SIZE; i++) {
        if (!s_queue[i].used)
            return i;
        if (s_queue[i].time < minimum) {
            minimum = s_queue[i].time;
            ret = i;
        }
    }
    return ret;
}

msg_entry_t getMsgList(int idx) {
    msg_entry_t ret;
    memset(&ret, 0, sizeof(ret));
    if (idx >= 0 && idx < MSG_QUEUE_SIZE)
        ret = s_queue[idx];
    return ret;
}

static int pkgMsgUpdate(const char *call, const char *text, uint16_t msgID, int8_t ack, bool rxtx) {
    if (!call[0] || !text[0])
        return -1;

    int i = pkgMsg_Find(call, msgID, rxtx);
    if (i < 0)
        i = pkgMsgOldestSlot();

    s_queue[i].used = true;
    s_queue[i].time = time(NULL);
    s_queue[i].msgID = msgID;
    s_queue[i].ack = ack;
    s_queue[i].rxtx = rxtx;
    memset(s_queue[i].callsign, 0, sizeof(s_queue[i].callsign));
    strncpy(s_queue[i].callsign, call, sizeof(s_queue[i].callsign) - 1);
    strncpy(s_queue[i].text, text, sizeof(s_queue[i].text) - 1);
    s_queue[i].text[sizeof(s_queue[i].text) - 1] = 0;
    return i;
}

// Escapes ", \ and control chars for safe embedding in a JSON string literal.
// Mirrors the same small helper duplicated in lastheard.c/trafficlog.c.
static size_t json_escape(const char *src, char *dst, size_t dst_size) {
    size_t di = 0;
    for (const char *p = src; *p && di + 2 < dst_size; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            dst[di++] = '\\';
            dst[di++] = (char)c;
        } else if (c < 0x20) {
            continue;
        } else {
            dst[di++] = (char)c;
        }
    }
    dst[di] = 0;
    return di;
}

size_t message_dump_json(char *out, size_t out_size) {
    if (out == NULL || out_size < 4)
        return 0;

    // Sort by time, oldest first, so the chat page can just append in
    // received order. MSG_QUEUE_SIZE is small (20), so a plain selection
    // sort over an index array is more than fast enough here and avoids
    // touching s_queue's own layout.
    int order[MSG_QUEUE_SIZE];
    int n_used = 0;
    for (int i = 0; i < MSG_QUEUE_SIZE; i++) {
        if (s_queue[i].used)
            order[n_used++] = i;
    }
    for (int a = 0; a < n_used - 1; a++) {
        int best = a;
        for (int b = a + 1; b < n_used; b++) {
            if (s_queue[order[b]].time < s_queue[order[best]].time)
                best = b;
        }
        if (best != a) {
            int tmp = order[a];
            order[a] = order[best];
            order[best] = tmp;
        }
    }

    size_t pos = 0;
    out[pos++] = '[';
    bool first = true;

    for (int k = 0; k < n_used && pos + 4 < out_size; k++) {
        msg_entry_t *e = &s_queue[order[k]];

        char call_esc[sizeof(e->callsign) * 2];
        char text_esc[MSG_TEXT_MAX * 2];
        json_escape(e->callsign, call_esc, sizeof(call_esc));
        json_escape(e->text, text_esc, sizeof(text_esc));

        const char *status = e->rxtx ? "rx" : (e->ack > 0 ? "pending" : "sent");

        int len = snprintf(out + pos, out_size - pos, "%s{\"time\":%lld,\"dir\":\"%s\",\"call\":\"%s\",\"text\":\"%s\",\"status\":\"%s\"}", first ? "" : ",",
                            (long long)e->time, e->rxtx ? "rx" : "tx", call_esc, text_esc, status);
        if (len < 0)
            break;
        if (pos + (size_t)len + 2 >= out_size)
            break; // would overflow on the closing ']' - stop, keep what we have
        pos += (size_t)len;
        first = false;
    }

    out[pos++] = ']';
    out[pos] = 0;
    return pos;
}

// g_config.msg_path is a BITMASK over g_config.path[0..3] (TR_F_PATH_BITMASK
// on the Message webconfig page) - see beacon.c's buildPathSuffix() for the
// full rationale. Kept in sync with that implementation.
static void buildPathSuffix(char *out, size_t outMax) {
    out[0] = 0;

    // Snapshot the path bitmask and the four presets under the config lock, so
    // a concurrent web save can't tear a preset string mid-read.
    uint8_t msgPath;
    char pathPreset[4][72];
    app_config_lock();
    msgPath = g_config.msg_path;
    memcpy(pathPreset, g_config.path, sizeof(pathPreset));
    app_config_unlock();

    if (msgPath == 0 || outMax == 0)
        return;

    size_t used = 0;
    for (int bit = 0; bit < 4; bit++) {
        if (!(msgPath & (1 << bit)))
            continue;
        if (!pathPreset[bit][0])
            continue;

        int n = snprintf(out + used, outMax - used, ",%s", pathPreset[bit]);
        if (n < 0)
            break;
        if ((size_t)n >= outMax - used) {
            used = outMax - 1;
            break;
        }
        used += (size_t)n;
    }
}

// Builds and sends one packet per enabled channel from a shared "info"
// field (":ADDRESSEE:text{id" or ":ADDRESSEE:ackNNN"), rather than one
// packet reused verbatim on both channels.
//
// The RF leg gets the operator-configured digipeater path
// (g_config.msg_path, via buildPathSuffix()). The APRS-IS leg does NOT:
// WIDEn-N aliases are RF-only and meaningless (and misleading to other
// IS clients/servers) once a packet is injected straight into APRS-IS.
// Per APRS-IS convention, locally-originated traffic sent to the IS
// network carries a "TCPIP*" q-construct tag in its path instead of an
// RF unproto path - see aprsc/javAPRSSrvr behavior and the APRS-IS
// server spec. Previously both legs sent the exact same RF-path packet,
// so every outgoing/retried message and ack showed up on APRS-IS still
// wearing e.g. "WIDE1-1,WIDE2-1".
static void txPacket(const char *myCall, const char *info) {
    if (!s_txHandler) {
        ESP_LOGW(TAG, "No TX handler registered, dropping: %s", info);
        return;
    }
    if (g_config.msg_rf) {
        char path[80];
        buildPathSuffix(path, sizeof(path));
        char packet[400];
        int len = snprintf(packet, sizeof(packet), "%s>APE32L%s:%s", myCall, path, info);
        if (len > 0)
            s_txHandler(packet, (size_t)len, MSG_CHANNEL_RF);
    }
    if (g_config.msg_inet) {
        char packet[400];
        int len = snprintf(packet, sizeof(packet), "%s>APE32L,TCPIP*:%s", myCall, info);
        if (len > 0)
            s_txHandler(packet, (size_t)len, MSG_CHANNEL_INET);
    }
}

void sendAPRSMessage(const char *toCall, const char *text, bool encrypt) {
    if (!toCall[0] || !text[0])
        return;
    ++s_msgID;

    char myCallUp[10];
    app_config_lock();
    strncpy(myCallUp, g_config.msg_mycall, sizeof(myCallUp) - 1);
    app_config_unlock();
    myCallUp[sizeof(myCallUp) - 1] = 0;
    trimUpper(myCallUp);

    char toCallUp[10];
    strncpy(toCallUp, toCall, sizeof(toCallUp) - 1);
    toCallUp[sizeof(toCallUp) - 1] = 0;
    trimUpper(toCallUp);

    char toCallFixed[10];
    memset(toCallFixed, ' ', 9);
    toCallFixed[9] = 0;
    memcpy(toCallFixed, toCallUp, strlen(toCallUp) > 9 ? 9 : strlen(toCallUp));

    char payload[300];
    if (encrypt) {
        uint8_t key[16] = { 0 };
        hexStringToBytes(g_config.msg_key, key, sizeof(key));
        if (aesEncryptBase64WithIV(text, key, s_msgID, myCallUp, payload, sizeof(payload)) == 0) {
            strncpy(payload, text, sizeof(payload) - 1); // fall back unencrypted on failure
            payload[sizeof(payload) - 1] = 0;
        }
    } else {
        strncpy(payload, text, sizeof(payload) - 1);
        payload[sizeof(payload) - 1] = 0;
    }

    char info[320];
    snprintf(info, sizeof(info), ":%s:%s{%u", toCallFixed, payload, (unsigned)s_msgID);

    txPacket(myCallUp, info);
    ESP_LOGD(TAG, "Send APRS message to %s msgID %u: %s", toCall, (unsigned)s_msgID, info);

    int8_t ackVal = (g_config.msg_retry == 0) ? -2 : (int8_t)g_config.msg_retry;
    pkgMsgUpdate(toCall, text, s_msgID, ackVal, false);
}

void sendAPRSAck(const char *toCall, const char *msgNo) {
    char toCallFixed[10];
    memset(toCallFixed, ' ', 9);
    toCallFixed[9] = 0;
    size_t n = strlen(toCall);
    memcpy(toCallFixed, toCall, n > 9 ? 9 : n);

    char myCall[10];
    app_config_lock();
    memcpy(myCall, g_config.msg_mycall, sizeof(myCall));
    app_config_unlock();

    char info[160];
    snprintf(info, sizeof(info), ":%s:ack%s", toCallFixed, msgNo);
    txPacket(myCall, info);
    ESP_LOGD(TAG, "Send APRS ACK to %s msgNo %s", toCall, msgNo);
}

void sendAPRSMessageRetry(void) {
    time_t now = time(NULL);

    // Snapshot the own-call and (if used) the encryption key once, so a web
    // save can't rewrite them mid-loop while frames are being built/signed.
    char myCall[10];
    char msgKey[sizeof(g_config.msg_key)];
    app_config_lock();
    memcpy(myCall, g_config.msg_mycall, sizeof(myCall));
    memcpy(msgKey, g_config.msg_key, sizeof(msgKey));
    app_config_unlock();

    for (int i = 0; i < MSG_QUEUE_SIZE; i++) {
        if (!s_queue[i].used || s_queue[i].ack <= 0)
            continue;
        if ((now - s_queue[i].time) <= g_config.msg_interval)
            continue;

        if (--s_queue[i].ack > 0)
            s_queue[i].time = now + g_config.msg_interval;

        char toCallFixed[10];
        memset(toCallFixed, ' ', 9);
        toCallFixed[9] = 0;
        size_t n = strlen(s_queue[i].callsign);
        memcpy(toCallFixed, s_queue[i].callsign, n > 9 ? 9 : n);

        char payload[300];
        if (g_config.msg_encrypt) {
            uint8_t key[16] = { 0 };
            hexStringToBytes(msgKey, key, sizeof(key));
            if (aesEncryptBase64WithIV(s_queue[i].text, key, s_queue[i].msgID, myCall, payload, sizeof(payload)) == 0) {
                strncpy(payload, s_queue[i].text, sizeof(payload) - 1);
                payload[sizeof(payload) - 1] = 0;
            }
        } else {
            strncpy(payload, s_queue[i].text, sizeof(payload) - 1);
            payload[sizeof(payload) - 1] = 0;
        }

        char info[320];
        snprintf(info, sizeof(info), ":%s:%s{%u", toCallFixed, payload, (unsigned)s_queue[i].msgID);
        txPacket(myCall, info);
        ESP_LOGD(TAG, "Retry APRS message[%d] to %s msgID %u ack left %d", i, s_queue[i].callsign, (unsigned)s_queue[i].msgID, s_queue[i].ack);
    }
}

// ---------------------------------------------------------------------------
// Incoming
// ---------------------------------------------------------------------------
void handleIncomingAPRS(const char *line) {
    if (!g_config.msg_enable)
        return;

    const char *msgMarker = strstr(line, "::");
    if (msgMarker == NULL || msgMarker == line)
        return;

    char fromCall[16] = { 0 };
    const char *gt = strchr(line, '>');
    if (gt) {
        size_t n = (size_t)(gt - line);
        if (n >= sizeof(fromCall))
            n = sizeof(fromCall) - 1;
        memcpy(fromCall, line, n);
    }

    const char *payload = msgMarker + 2;
    if (strlen(payload) < 10)
        return;

    char toCall[12] = { 0 };
    memcpy(toCall, payload, 9);
    trimUpper(toCall);

    if (strcasecmp(fromCall, toCall) == 0)
        return; // message to self, ignore

    const char *colon = strchr(payload + 9, ':');
    if (colon == NULL)
        return;

    char message[300] = { 0 };
    strncpy(message, colon + 1, sizeof(message) - 1);
    // trim trailing whitespace/CR
    size_t mlen = strlen(message);
    while (mlen > 0 && isspace((unsigned char)message[mlen - 1]))
        message[--mlen] = 0;

    char msgNo[12] = { 0 };
    char *brace = strchr(message, '{');
    if (brace && brace[1]) {
        strncpy(msgNo, brace + 1, sizeof(msgNo) - 1);
        *brace = 0;
        mlen = strlen(message);
        while (mlen > 0 && isspace((unsigned char)message[mlen - 1]))
            message[--mlen] = 0;
    }

    bool isAck = (strncmp(message, "ack", 3) == 0);
    if (isAck)
        strncpy(msgNo, message + 3, sizeof(msgNo) - 1);

    ESP_LOGD(TAG, "Message from %s to %s: %s", fromCall, toCall, message);

    // Accept the message if the addressee's base callsign matches ours,
    // regardless of SSID on either side (message to "N0CALL", "N0CALL-7",
    // etc. is accepted as long as it's configured mycall is "N0CALL", with
    // or without its own SSID).
    if (!callsignBaseMatch(toCall, g_config.msg_mycall) || msgNo[0] == 0)
        return;

    if (isAck) {
        int i = pkgMsg_Find(fromCall, (uint16_t)atoi(msgNo), false);
        if (i >= 0)
            s_queue[i].ack = -2; // acked
        return;
    }

    char decrypted[300];
    if (g_config.msg_encrypt) {
        uint8_t key[16] = { 0 };
        hexStringToBytes(g_config.msg_key, key, sizeof(key));
        if (aesDecryptBase64WithIV(message, key, fromCall, (uint16_t)atoi(msgNo), decrypted, sizeof(decrypted)) == 0)
            return;
    } else {
        strncpy(decrypted, message, sizeof(decrypted) - 1);
        decrypted[sizeof(decrypted) - 1] = 0;
    }

    size_t dlen = strlen(decrypted);
    while (dlen > 0 && isspace((unsigned char)decrypted[dlen - 1]))
        decrypted[--dlen] = 0;
    if (dlen == 0)
        return;

    pkgMsgUpdate(fromCall, decrypted, (uint16_t)atoi(msgNo), -1, true);
    sendAPRSAck(fromCall, msgNo);
    message_alarm_pulse();
}
