/*
 * board_hal.c - pure ESP-IDF implementation, see board_hal.h
 */

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"

#include "board_hal.h"

/*
 * adc_oneshot_read_isr() is implemented (as a regular, non-static, exported
 * symbol) inside the esp_adc component specifically to allow an ISR-safe
 * single-shot read - it skips adc_oneshot_read()'s mutex/lock and does the
 * clock-tree/calibration-code setup with portENTER_CRITICAL_SAFE() instead of
 * portENTER_CRITICAL(). As of this IDF version it just isn't declared in the
 * public esp_adc/adc_oneshot.h header, so we forward-declare it ourselves
 * rather than reimplementing its register sequence by hand. Requires
 * CONFIG_ADC_ONESHOT_CTRL_FUNC_IN_IRAM=y (see sdkconfig) so its code is
 * actually placed in IRAM.
 */
extern esp_err_t adc_oneshot_read_isr(adc_oneshot_unit_handle_t handle, adc_channel_t chan, int *out_raw);

/* ------------------------------------------------------------------ */
/* GPIO                                                                  */
/* ------------------------------------------------------------------ */

void pinMode(int pin, int mode) {
    gpio_config_t io_conf = { 0 };
    io_conf.pin_bit_mask = (1ULL << pin);
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;

    switch (mode) {
        case OUTPUT:
            io_conf.mode = GPIO_MODE_OUTPUT;
            break;
        case OUTPUT_OPEN_DRAIN:
            io_conf.mode = GPIO_MODE_OUTPUT_OD;
            break;
        case INPUT_PULLUP:
            io_conf.mode = GPIO_MODE_INPUT;
            io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
            break;
        case INPUT_PULLDOWN:
            io_conf.mode = GPIO_MODE_INPUT;
            io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
            break;
        case INPUT:
        default:
            io_conf.mode = GPIO_MODE_INPUT;
            break;
    }

    gpio_config(&io_conf);
}

void digitalWrite(int pin, int level) {
    gpio_set_level((gpio_num_t)pin, level ? 1 : 0);
}

int digitalRead(int pin) {
    return gpio_get_level((gpio_num_t)pin);
}

/* ------------------------------------------------------------------ */
/* Timing                                                                */
/* ------------------------------------------------------------------ */

unsigned long millis(void) {
    return (unsigned long)(esp_timer_get_time() / 1000ULL);
}

unsigned long micros(void) {
    return (unsigned long)esp_timer_get_time();
}

void delay(unsigned long ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void delayMicroseconds(uint32_t us) {
    esp_rom_delay_us(us);
}

long board_random(long min, long max) {
    if (max <= min)
        return min;
    return min + (long)(esp_random() % (uint32_t)(max - min));
}

/* ------------------------------------------------------------------ */
/* Legacy-style ADC1 one-shot helpers                                    */
/* ------------------------------------------------------------------ */

static adc_oneshot_unit_handle_t s_adc1_handle = NULL;
static adc_cali_handle_t s_adc1_cali_handle = NULL;
static adc_atten_t s_adc1_atten = ADC_ATTEN_DB_0;
static adc_bitwidth_t s_adc1_bitwidth = ADC_BITWIDTH_12;

/* Cached channel + linear raw->mV fit for analogReadMilliVoltsISR(), refreshed
 * whenever analogSetPinAttenuation() (re)configures the channel/calibration.
 * Computed once outside ISR context so the ISR itself only does a
 * multiply-add - no driver locking, argument validation, or calibration-curve
 * lookups. */
static adc_channel_t s_adc1_isr_channel = (adc_channel_t)-1;
static bool s_adc1_isr_channel_valid = false;
static float s_adc1_mv_slope = 1.0f;
static float s_adc1_mv_offset = 0.0f;

static void adc1_ensure_unit(void) {
    if (s_adc1_handle != NULL)
        return;

    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    adc_oneshot_new_unit(&init_cfg, &s_adc1_handle);
}

void analogReadResolution(int bits) {
    switch (bits) {
        case 9:
            s_adc1_bitwidth = ADC_BITWIDTH_9;
            break;
        case 10:
            s_adc1_bitwidth = ADC_BITWIDTH_10;
            break;
        case 11:
            s_adc1_bitwidth = ADC_BITWIDTH_11;
            break;
        case 12:
        default:
            s_adc1_bitwidth = ADC_BITWIDTH_12;
            break;
    }
}

static bool pin_to_adc1_channel(int pin, adc_channel_t *channel) {
    adc_unit_t unit;
    if (adc_oneshot_io_to_channel(pin, &unit, channel) != ESP_OK)
        return false;
    return unit == ADC_UNIT_1;
}

void analogSetPinAttenuation(int pin, int attenuation) {
    adc1_ensure_unit();
    s_adc1_atten = (adc_atten_t)attenuation;

    adc_channel_t channel;
    if (!pin_to_adc1_channel(pin, &channel))
        return;

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = s_adc1_bitwidth,
        .atten = s_adc1_atten,
    };
    adc_oneshot_config_channel(s_adc1_handle, channel, &chan_cfg);

    if (s_adc1_cali_handle != NULL) {
#if CONFIG_IDF_TARGET_ESP32
        adc_cali_delete_scheme_line_fitting(s_adc1_cali_handle);
#else
        adc_cali_delete_scheme_curve_fitting(s_adc1_cali_handle);
#endif
        s_adc1_cali_handle = NULL;
    }

#if CONFIG_IDF_TARGET_ESP32
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = s_adc1_atten,
        .bitwidth = s_adc1_bitwidth,
    };
    adc_cali_create_scheme_line_fitting(&cali_cfg, &s_adc1_cali_handle);
#else
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = s_adc1_atten,
        .bitwidth = s_adc1_bitwidth,
    };
    adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_adc1_cali_handle);
#endif

    /* Cache the channel and a linear raw->mV fit for analogReadMilliVoltsISR().
     * Sampled from the just-(re)created calibration handle here, outside ISR
     * context, so the ISR path never has to call into the calibration API. */
    s_adc1_isr_channel_valid = pin_to_adc1_channel(pin, &s_adc1_isr_channel);
    if (s_adc1_cali_handle != NULL) {
        int mv_lo = 0, mv_hi = 0;
        int raw_hi = (1 << (s_adc1_bitwidth == ADC_BITWIDTH_9 ? 9 : (s_adc1_bitwidth == ADC_BITWIDTH_10 ? 10 : (s_adc1_bitwidth == ADC_BITWIDTH_11 ? 11 : 12)))) - 1;
        if (adc_cali_raw_to_voltage(s_adc1_cali_handle, 0, &mv_lo) == ESP_OK && adc_cali_raw_to_voltage(s_adc1_cali_handle, raw_hi, &mv_hi) == ESP_OK) {
            s_adc1_mv_offset = (float)mv_lo;
            s_adc1_mv_slope = (float)(mv_hi - mv_lo) / (float)raw_hi;
        } else {
            s_adc1_mv_offset = 0.0f;
            s_adc1_mv_slope = 1.0f;
        }
    } else {
        s_adc1_mv_offset = 0.0f;
        s_adc1_mv_slope = 1.0f;
    }
}

int analogReadMilliVolts(int pin) {
    adc1_ensure_unit();

    adc_channel_t channel;
    if (!pin_to_adc1_channel(pin, &channel))
        return 0;

    int raw = 0;
    if (adc_oneshot_read(s_adc1_handle, channel, &raw) != ESP_OK)
        return 0;

    if (s_adc1_cali_handle != NULL) {
        int mv = 0;
        if (adc_cali_raw_to_voltage(s_adc1_cali_handle, raw, &mv) == ESP_OK)
            return mv;
    }
    return raw;
}

int IRAM_ATTR analogReadMilliVoltsISR(int pin) {
    /* Channel is cached by analogSetPinAttenuation(); no driver lookups here. */
    if (!s_adc1_isr_channel_valid || s_adc1_handle == NULL)
        return 0;

    int raw = 0;
    if (adc_oneshot_read_isr(s_adc1_handle, s_adc1_isr_channel, &raw) != ESP_OK)
        return 0;

    /* Linear fit computed outside ISR context - no calibration-curve call here. */
    return (int)(s_adc1_mv_offset + s_adc1_mv_slope * (float)raw);
}

/* ------------------------------------------------------------------ */
/* hw_timer_t-style periodic timer on top of ESP-IDF gptimer            */
/* ------------------------------------------------------------------ */

struct hw_timer_s {
    gptimer_handle_t gptimer;
    hw_timer_isr_t isr;
    bool enabled;
};

static bool IRAM_ATTR hw_timer_on_alarm(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data) {
    hw_timer_t *t = (hw_timer_t *)user_data;
    if (t != NULL && t->isr != NULL)
        t->isr();
    return false; /* no higher priority task woken */
}

hw_timer_t *timerBegin(uint32_t resolution_hz) {
    hw_timer_t *t = (hw_timer_t *)calloc(1, sizeof(hw_timer_t));
    if (t == NULL)
        return NULL;

    gptimer_config_t cfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = resolution_hz,
    };
    if (gptimer_new_timer(&cfg, &t->gptimer) != ESP_OK) {
        free(t);
        return NULL;
    }
    return t;
}

void timerAttachInterrupt(hw_timer_t *timer, hw_timer_isr_t fn) {
    if (timer == NULL)
        return;
    timer->isr = fn;
    gptimer_event_callbacks_t cbs = {
        .on_alarm = hw_timer_on_alarm,
    };
    gptimer_register_event_callbacks(timer->gptimer, &cbs, timer);
    gptimer_enable(timer->gptimer);
}

void timerAlarm(hw_timer_t *timer, uint64_t alarm_value, bool autoreload, uint32_t reload_count) {
    (void)reload_count;
    if (timer == NULL)
        return;
    gptimer_alarm_config_t alarm_cfg = {
        .alarm_count = alarm_value,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = autoreload,
    };
    gptimer_set_alarm_action(timer->gptimer, &alarm_cfg);
}

void timerStart(hw_timer_t *timer) {
    if (timer == NULL || timer->enabled)
        return;
    gptimer_set_raw_count(timer->gptimer, 0);
    gptimer_start(timer->gptimer);
    timer->enabled = true;
}

void timerStop(hw_timer_t *timer) {
    if (timer == NULL || !timer->enabled)
        return;
    gptimer_stop(timer->gptimer);
    timer->enabled = false;
}

/* ------------------------------------------------------------------ */
/* Misc                                                                  */
/* ------------------------------------------------------------------ */

uint32_t board_getFreeHeap(void) {
    return (uint32_t)esp_get_free_heap_size();
}
