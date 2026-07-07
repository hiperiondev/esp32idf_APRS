#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "AFSK.h"
#include "AX25.h"
#include "LibAPRSesp.h"
#include "board_hal.h"

static const char *TAG_LIBAPRS = "LibAPRS";

AX25Ctx AX25;
extern void aprs_msg_callback(struct AX25Msg *msg);

#define countof(a) sizeof(a) / sizeof(a[0])

unsigned long custom_preamble = 350UL;
unsigned long custom_tail = 50UL;

AX25Call src;
AX25Call dst;
AX25Call path1;
AX25Call path2;

char CALL[7] = "NOCALL";
int CALL_SSID = 0;
char DST[7] = "APE32I";
int DST_SSID = 0;
char PATH1[7] = "WIDE1";
int PATH1_SSID = 1;
char PATH2[7] = "WIDE2";
int PATH2_SSID = 2;

AX25Call path[8];

// Location packet assembly fields
char latitude[9];
char longtitude[10];
char symbolTable = '/';
char symbol = 'n';

uint8_t power = 10;
uint8_t height = 10;
uint8_t gain = 10;
uint8_t directivity = 10;
/////////////////////////

// Message packet assembly fields
char message_recip[7];
int message_recip_ssid = -1;

int message_seq = 0;
char lastMessage[67];
size_t lastMessageLen;
bool message_autoAck = false;
/////////////////////////

// _hook (declared non-static in AX25.c) is the callback ax25_decode_default()
// invokes for every decoded RX frame. Nothing in this codebase ever assigned
// it, so RX frames were silently dropped even once the modem itself was
// running - wire it up here alongside the rest of modem bring-up.
extern ax25_callback_t _hook;

void APRS_init(const aprs_modem_config_t *cfg) {
    // Bring up GPIO (PTT/SQL/PWR/ADC/DAC), the sigma-delta channel and both
    // hardware timers. AFSK_init() calls AFSK_hw_init() internally.
    AFSK_init(cfg->adc_pin, cfg->dac_pin, cfg->ptt_pin, cfg->sql_pin, cfg->pwr_pin, LED_TX_PIN, LED_RX_PIN, -1, cfg->ptt_active, cfg->sql_active,
              cfg->pwr_active);

    // Apply the webconfig "Radio"/"Mod" page settings that AFSK_init() alone
    // doesn't cover.
    afskSetADCAtten(cfg->adc_atten);
    afskSetSQL(cfg->sql_pin, cfg->sql_active);
    afskSetPTT(cfg->ptt_pin, cfg->ptt_active);
    afskSetPWR(cfg->pwr_pin, cfg->pwr_active);

    // Sets up mark/space frequencies, PLL constants and buffer sizes, then
    // calls ModemInit()/Ax25Init() internally.
    afskSetModem(cfg->modem_type, cfg->bpf, cfg->tx_timeslot, cfg->preamble, cfg->fx25_mode);

    _hook = aprs_msg_callback;
}

void APRS_poll(void) {
    // ax25_poll(&AX25);
}

void APRS_setCallsign(char *call, int ssid) {
    memset(CALL, 0, 7);
    int i = 0;
    while (i < 6 && call[i] != 0) {
        CALL[i] = call[i];
        i++;
    }
    CALL_SSID = ssid;
}

void APRS_setDestination(char *call, int ssid) {
    memset(DST, 0, 7);
    int i = 0;
    while (i < 6 && call[i] != 0) {
        DST[i] = call[i];
        i++;
    }
    DST_SSID = ssid;
}

void APRS_setPath1(char *call, int ssid) {
    memset(PATH1, 0, 7);
    int i = 0;
    while (i < 6 && call[i] != 0) {
        PATH1[i] = call[i];
        i++;
    }
    PATH1_SSID = ssid;
}

void APRS_setPath2(char *call, int ssid) {
    memset(PATH2, 0, 7);
    int i = 0;
    while (i < 6 && call[i] != 0) {
        PATH2[i] = call[i];
        i++;
    }
    PATH2_SSID = ssid;
}

void APRS_setMessageDestination(char *call, int ssid) {
    memset(message_recip, 0, 7);
    int i = 0;
    while (i < 6 && call[i] != 0) {
        message_recip[i] = call[i];
        i++;
    }
    message_recip_ssid = ssid;
}

void APRS_setPreamble(unsigned long pre) {
    custom_preamble = pre;
}

void APRS_setTail(unsigned long tail) {
    custom_tail = tail;
}

void APRS_useAlternateSymbolTable(bool use) {
    if (use) {
        symbolTable = '\\';
    } else {
        symbolTable = '/';
    }
}

void APRS_setSymbol(char sym) {
    symbol = sym;
}

void APRS_setLat(char *lat) {
    memset(latitude, 0, 9);
    int i = 0;
    while (i < 8 && lat[i] != 0) {
        latitude[i] = lat[i];
        i++;
    }
}

void APRS_setLon(char *lon) {
    memset(longtitude, 0, 10);
    int i = 0;
    while (i < 9 && lon[i] != 0) {
        longtitude[i] = lon[i];
        i++;
    }
}

void APRS_setPower(int s) {
    if (s >= 0 && s < 10) {
        power = s;
    }
}

void APRS_setHeight(int s) {
    if (s >= 0 && s < 10) {
        height = s;
    }
}

void APRS_setGain(int s) {
    if (s >= 0 && s < 10) {
        gain = s;
    }
}

void APRS_setDirectivity(int s) {
    if (s >= 0 && s < 10) {
        directivity = s;
    }
}

void APRS_printSettings() {
    printf("LibAPRS Settings:\n");
    printf("Callsign:     %s-%d\n", CALL, CALL_SSID);
    printf("Destination:  %s-%d\n", DST, DST_SSID);
    printf("Path1:        %s-%d\n", PATH1, PATH1_SSID);
    printf("Path2:        %s-%d\n", PATH2, PATH2_SSID);
    printf("Message dst:  ");
    if (message_recip[0] == 0) {
        printf("N/A\n");
    } else {
        printf("%s-%d\n", message_recip, message_recip_ssid);
    }
    printf("TX Preamble:  %lu\n", custom_preamble);
    printf("TX Tail:      %lu\n", custom_tail);
    printf("Symbol table: %s\n", (symbolTable == '/') ? "Normal" : "Alternate");
    printf("Symbol:       %c\n", symbol);
    printf("Power:        ");
    if (power < 10)
        printf("%d\n", power);
    else
        printf("N/A\n");
    printf("Height:       ");
    if (height < 10)
        printf("%d\n", height);
    else
        printf("N/A\n");
    printf("Gain:         ");
    if (gain < 10)
        printf("%d\n", gain);
    else
        printf("N/A\n");
    printf("Directivity:  ");
    if (directivity < 10)
        printf("%d\n", directivity);
    else
        printf("N/A\n");
    printf("Latitude:     ");
    if (latitude[0] != 0)
        printf("%s\n", latitude);
    else
        printf("N/A\n");
    printf("Longtitude:   ");
    if (longtitude[0] != 0)
        printf("%s\n", longtitude);
    else
        printf("N/A\n");
}

void APRS_sendPkt(void *_buffer, size_t length) {

    uint8_t *buffer = (uint8_t *)_buffer;

    memcpy(dst.call, DST, 6);
    dst.ssid = DST_SSID;

    memcpy(src.call, CALL, 6);
    src.ssid = CALL_SSID;

    memcpy(path1.call, PATH1, 6);
    path1.ssid = PATH1_SSID;

    memcpy(path2.call, PATH2, 6);
    path2.ssid = PATH2_SSID;

    path[0] = dst;
    path[1] = src;
    path[2] = path1;
    path[3] = path2;

    (void)buffer;
    // ax25_sendVia(&AX25, path, countof(path), buffer, length);
}

void APRS_sendTNC2Pkt(const uint8_t *raw, size_t length) {
    uint8_t data[300];
    int size = 0;
    ax25frame frame;
    ax25_encode(&frame, (char *)raw, length);
    size = hdlcFrame(data, 300, &AX25, &frame);
    ESP_LOGD(TAG_LIBAPRS, "TX HDLC Fram size=%d", size);
    void *handle = NULL;
    if (size > 0) {
        if (NULL != (handle = Ax25WriteTxFrame(data, size))) {
            // Ax25TransmitCheck();
        }
    }
    (void)handle;
}

// Dynamic RAM usage of this function is 30 bytes
void APRS_sendLoc(void *_buffer, size_t length) {
    size_t payloadLength = 20 + length;
    bool usePHG = false;
    if (power < 10 && height < 10 && gain < 10 && directivity < 9) {
        usePHG = true;
        payloadLength += 7;
    }
    uint8_t *packet = (uint8_t *)malloc(payloadLength);
    uint8_t *ptr = packet;
    packet[0] = '=';
    packet[9] = symbolTable;
    packet[19] = symbol;
    ptr++;
    memcpy(ptr, latitude, 8);
    ptr += 9;
    memcpy(ptr, longtitude, 9);
    ptr += 10;
    if (usePHG) {
        packet[20] = 'P';
        packet[21] = 'H';
        packet[22] = 'G';
        packet[23] = power + 48;
        packet[24] = height + 48;
        packet[25] = gain + 48;
        packet[26] = directivity + 48;
        ptr += 7;
    }
    if (length > 0) {
        uint8_t *buffer = (uint8_t *)_buffer;
        memcpy(ptr, buffer, length);
    }

    APRS_sendPkt(packet, payloadLength);
    free(packet);
}

// Dynamic RAM usage of this function is 18 bytes
void APRS_sendMsg(void *_buffer, size_t length) {
    if (length > 67)
        length = 67;
    size_t payloadLength = 11 + length + 4;

    uint8_t *packet = (uint8_t *)malloc(payloadLength);
    uint8_t *ptr = packet;
    packet[0] = ':';
    int callSize = 6;
    int count = 0;
    while (callSize--) {
        if (message_recip[count] != 0) {
            packet[1 + count] = message_recip[count];
            count++;
        }
    }
    if (message_recip_ssid != -1) {
        packet[1 + count] = '-';
        count++;
        if (message_recip_ssid < 10) {
            packet[1 + count] = message_recip_ssid + 48;
            count++;
        } else {
            packet[1 + count] = 49;
            count++;
            packet[1 + count] = message_recip_ssid - 10 + 48;
            count++;
        }
    }
    while (count < 9) {
        packet[1 + count] = ' ';
        count++;
    }
    packet[1 + count] = ':';
    ptr += 11;
    if (length > 0) {
        uint8_t *buffer = (uint8_t *)_buffer;
        memcpy(ptr, buffer, length);
        memcpy(lastMessage, buffer, length);
        lastMessageLen = length;
    }

    message_seq++;
    if (message_seq > 999)
        message_seq = 0;

    packet[11 + length] = '{';
    int n = message_seq % 10;
    int d = ((message_seq % 100) - n) / 10;
    int h = (message_seq - d - n) / 100;

    packet[12 + length] = h + 48;
    packet[13 + length] = d + 48;
    packet[14 + length] = n + 48;

    APRS_sendPkt(packet, payloadLength);
    free(packet);
}

void APRS_msgRetry() {
    message_seq--;
    APRS_sendMsg(lastMessage, lastMessageLen);
}

int freeMemory() {
    return (int)board_getFreeHeap();
}
