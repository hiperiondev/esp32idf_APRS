#ifndef LIBAPRS_ESP_H_
#define LIBAPRS_ESP_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "AFSK.h"
#include "AX25.h"
#include "CRC-CCIT.h"
#include "FIFO.h"
#include "HDLC.h"

/**
 * @brief Hardware/modem settings APRS_init() needs to bring up the AFSK/AX.25
 * modem. Populated by the caller (main.c) from g_config so this component
 * doesn't need a build dependency on app_config.h/main.
 */
typedef struct {
    int8_t adc_pin;  // ADC input GPIO (g_config.adc_gpio)
    int8_t dac_pin;  // DAC/sigma-delta output GPIO (g_config.dac_gpio)
    int8_t ptt_pin;  // PTT GPIO (g_config.rf_ptt_gpio), -1 if unused
    int8_t sql_pin;  // Squelch input GPIO (g_config.rf_sql_gpio), -1 if unused
    int8_t pwr_pin;  // RF power-switch GPIO (g_config.rf_pwr_gpio), -1 if unused
    bool ptt_active; // PTT active level (g_config.rf_ptt_active)
    bool sql_active; // Squelch active level (g_config.rf_sql_active)
    bool pwr_active; // Power-switch active level (g_config.rf_pwr_active)

    uint8_t adc_atten;   // 0-4 attenuation index (g_config.adc_atten)
    uint8_t modem_type;  // AFSK modulation select: 0=MODEM_300, 1=MODEM_1200, 2=MODEM_1200_V23, 3=MODEM_9600
                          // (g_config.afsk_modem_type - the "Audio / AFSK" -> "Modulation" webconfig field, used
                          // for both RX and TX on the audio ADC/DAC modem; NOT g_config.modem_type, which is
                          // the separate optional RF module's modem mode)
    bool bpf;            // Bandpass/flat-audio-in flag (g_config.audio_lpf)
    uint16_t tx_timeslot; // ms (g_config.tx_timeslot)
    uint16_t preamble;    // ms (g_config.preamble)
    uint8_t fx25_mode;    // 0=off,1=RX,2=RX+TX (g_config.fx25_mode)
} aprs_modem_config_t;

void APRS_init(const aprs_modem_config_t *cfg);
void APRS_poll(void);

void APRS_setCallsign(char *call, int ssid);
void APRS_setDestination(char *call, int ssid);
void APRS_setMessageDestination(char *call, int ssid);
void APRS_setPath1(char *call, int ssid);
void APRS_setPath2(char *call, int ssid);

void APRS_setPreamble(unsigned long pre);
void APRS_setTail(unsigned long tail);
void APRS_useAlternateSymbolTable(bool use);
void APRS_setSymbol(char sym);

void APRS_setLat(char *lat);
void APRS_setLon(char *lon);
void APRS_setPower(int s);
void APRS_setHeight(int s);
void APRS_setGain(int s);
void APRS_setDirectivity(int s);

void APRS_sendPkt(void *_buffer, size_t length);
void APRS_sendLoc(void *_buffer, size_t length);
void APRS_sendMsg(void *_buffer, size_t length);
void APRS_msgRetry();

void APRS_printSettings();
void APRS_sendTNC2Pkt(const uint8_t *raw, size_t length);

int freeMemory();

// Diagnostic helpers (see definitions in AFSK.c): min/max raw ADC code seen
// by the sampling ISR since the last reset. Used by the "LOOP TEST" to report
// whether the ADC is seeing any signal swing at all.
void AFSK_resetAdcDiag(void);
void AFSK_getAdcDiag(int16_t *out_min, int16_t *out_max);
// Converts a raw ADC code (as returned by AFSK_getAdcDiag) to calibrated
// millivolts using the same calibration scheme adc_continue_init() set up,
// falling back to a rough estimate if no calibration eFuses were burned.
// Lets callers (e.g. the LOOP TEST failure messages) report an actual
// voltage instead of an ambiguous raw code, which is the difference between
// "the pin reads ~3.3V, definitely a HW short/miswire" and "the pin reads
// some code that only looks wrong because of a units/scale bug".
int AFSK_adcRawToMv(int16_t raw);

#endif /* LIBAPRS_ESP_H_ */
