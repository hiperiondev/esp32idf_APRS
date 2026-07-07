#ifndef AFSK_H
#define AFSK_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_attr.h"
#include "fir_filter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "AX25.h"
#include "FIFO.h"
#include "HDLC.h"
#include "board_hal.h"

#define SIN_LEN 512
static const uint8_t sin_table[] = {
    128, 129, 131, 132, 134, 135, 137, 138, 140, 142, 143, 145, 146, 148, 149, 151, 152, 154, 155, 157, 158, 160, 162, 163, 165, 166,
    167, 169, 170, 172, 173, 175, 176, 178, 179, 181, 182, 183, 185, 186, 188, 189, 190, 192, 193, 194, 196, 197, 198, 200, 201, 202,
    203, 205, 206, 207, 208, 210, 211, 212, 213, 214, 215, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231,
    232, 233, 234, 234, 235, 236, 237, 238, 238, 239, 240, 241, 241, 242, 243, 243, 244, 245, 245, 246, 246, 247, 248, 248, 249, 249,
    250, 250, 250, 251, 251, 252, 252, 252, 253, 253, 253, 253, 254, 254, 254, 254, 254, 255, 255, 255, 255, 255, 255, 255,
};

static inline uint8_t sinSample(uint16_t i) {
    uint16_t newI = i % (SIN_LEN / 2);
    newI = (newI >= (SIN_LEN / 4)) ? (SIN_LEN / 2 - newI - 1) : newI;
    uint8_t sine = sin_table[newI];
    return (i >= (SIN_LEN / 2)) ? (255 - sine) : sine;
}

#define SWITCH_TONE(inc)          (((inc) == MARK_INC) ? SPACE_INC : MARK_INC)
#define BITS_DIFFER(bits1, bits2) (((bits1) ^ (bits2)) & 0x01)
#define DUAL_XOR(bits1, bits2)    ((((bits1) ^ (bits2)) & 0x03) == 0x03)
#define SIGNAL_TRANSITIONED(bits) DUAL_XOR((bits), (bits) >> 2)
#define TRANSITION_FOUND(bits)    BITS_DIFFER((bits), (bits) >> 1)

#define CPU_FREQ F_CPU

#define CONFIG_AFSK_RX_BUFLEN      1
#define CONFIG_AFSK_TX_BUFLEN      700
#define CONFIG_AFSK_RXTIMEOUT      0
#define CONFIG_AFSK_PREAMBLE_LEN   350UL
#define CONFIG_AFSK_TRAILER_LEN    50UL
#define CONFIG_AFSK_DAC_SAMPLERATE 38400

#define SPK_PIN    ADC1_CHANNEL_5 // Real ADC1 input, GPIO33 (adc_pins[0] on ESP32)
#define MIC_PIN    25             // Real DAC1 (DAC_CHAN_0) output, GPIO25
#define LED_RX_PIN 2
#define LED_TX_PIN 4

#ifdef I2S_INTERNAL
#include "driver/dac.h"
#include "driver/i2s.h"

#define SAMPLE_RATE  SAMPLERATE
#define PIN_I2S_BCLK 26
#define PIN_I2S_LRC  27
#define PIN_I2S_DIN  35
#define PIN_I2S_DOUT 25
typedef enum { LEFTCHANNEL = 0, RIGHTCHANNEL = 1 } SampleIndex;
/// @parameter MODE : I2S_MODE_RX or I2S_MODE_TX
/// @parameter BPS : I2S_BITS_PER_SAMPLE_16BIT or I2S_BITS_PER_SAMPLE_32BIT
void I2S_Init(i2s_mode_t MODE, i2s_bits_per_sample_t BPS);
#else

#endif

#define DIV_ROUND(dividend, divisor) (((dividend) + (divisor) / 2) / (divisor))

#define AFSK_DAC_IRQ_START()                                                                                                                                   \
    do {                                                                                                                                                       \
        extern volatile bool hw_afsk_dac_isr;                                                                                                                  \
        hw_afsk_dac_isr = true;                                                                                                                                \
        digitalWrite(LED_TX_PIN, HIGH);                                                                                                                        \
    } while (0)
#define AFSK_DAC_IRQ_STOP()                                                                                                                                    \
    do {                                                                                                                                                       \
        extern volatile bool hw_afsk_dac_isr;                                                                                                                  \
        hw_afsk_dac_isr = false;                                                                                                                               \
        digitalWrite(LED_TX_PIN, LOW);                                                                                                                         \
    } while (0)

// Here's some macros for controlling the RX/TX LEDs
#define LED_RX_ON()  digitalWrite(LED_PIN, HIGH);
#define LED_RX_OFF() digitalWrite(LED_PIN, LOW);

#define DECODE_DELAY   4.458981479161393e-4 // sample delay
#define DELAY_DIVIDEND 325
#define DELAY_DIVISOR  728866
#define DELAYED_N      ((DELAY_DIVIDEND * SAMPLERATE + DELAY_DIVISOR / 2) / DELAY_DIVISOR)

#define FIR_BPF_N (8 * 4 - 1)
#define FIR_LPF_N (8 * 4 - 1) // must be multiple of 4 minus 1

#define AX25_FLAG     0x7e
#define DATA_LEN      500
#define FX25_DATA_LEN 255

#define TCB_QUEUE_LENGTH    (1024 * 2)
#define TCB_QUEUE_ITEM_SIZE sizeof(uint16_t)

#ifdef FX25_ENABLE
typedef struct FX25TAG fx25tag_t;
#endif

typedef struct TCB { // TNC Control Block
    TaskHandle_t task;

    uint8_t port; // port NO. 0 - 5
    uint16_t pkts;

    uint8_t state;
    uint8_t flag;
    uint8_t kiss_type; // indicate port number in upper nibble
    int data_cnt;
    uint8_t data_byte;
    uint8_t data_bit_cnt;

    // decode bit
    uint8_t pval;
    uint8_t nrzi;
    int adjust;

    // audio signal processing
    uint16_t avg;
    int avg_sum;

#define TCB_AVG_N 125 // 23

    uint16_t avg_buf[TCB_AVG_N];
    uint8_t avg_idx;
    int cdt_lvl;
    uint8_t cdt;
    int8_t cdt_led_pin;
    uint8_t cdt_led_on;
    SemaphoreHandle_t cdt_sem;

    // FSK decode
    uint8_t bit;

#ifdef FX25TNCR2 // only rev.2 has STA LED
    uint8_t sta_led_pin;
#endif

    // kiss parameter
    uint8_t SlotTime;      // 10ms uint
    uint8_t TXDELAY;       // 10ms uint
    uint8_t persistence_P; // P = p * 256 - 1
    uint8_t fullDuplex;

#ifdef ENABLE_TCM3105
    int pll_adj;
    uint32_t prev_ts;
    uint8_t enable_tcm3105; // enable TCM3105 for the port if true
    uint8_t cdt_off_timer;
#endif
} tcb_t;

extern int offset;

void AFSK_init(int8_t adc_pin, int8_t dac_pin, int8_t ptt_pin, int8_t sql_pin, int8_t pwr_pin, int8_t led_tx_pin, int8_t led_rx_pin, int8_t led_strip_pin,
               bool ptt_act, bool sql_act, bool pwr_act);
void AFSK_deinit();
void AFSK_Poll(bool SA818, bool RFPower);
void AFSK_TimerEnable(bool sts);
void DAC_TimerEnable(bool sts);
void afskSetHPF(bool val);
void afskSetBPF(bool val);
void afskSetADCAtten(uint8_t val);
void afskSetPTT(int8_t val, bool act);
void afskSetPWR(int8_t val, bool act);
void afskSetSQL(int8_t val, bool act);
bool getTransmit();
void setTransmit(bool val);
bool getReceive();
void afskSetModem(uint8_t val, bool bpf, uint16_t timeSlot, uint16_t preamble, uint8_t fx25Mode);
void setPtt(bool state);
void IRAM_ATTR LED_Status2(uint8_t red, uint8_t green, uint8_t blue);

#endif
