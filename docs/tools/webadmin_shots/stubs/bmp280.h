#pragma once
#include "esp_err.h"
#include "i2cdev.h"
#include "driver/gpio.h"
#include <stdint.h>
#define BMP280_CHIP_ID 0x58
#define BME280_CHIP_ID 0x60
#define BMP280_I2C_ADDRESS_0 0x76
#define BMP280_I2C_ADDRESS_1 0x77
typedef int BMP280_Filter; typedef int BMP280_Oversampling; typedef int BMP280_StandbyTime;
#define BMP280_MODE_NORMAL 3
typedef struct { int mode, filter, oversampling_pressure, oversampling_temperature, oversampling_humidity, standby; } bmp280_params_t;
typedef struct { uint8_t id; int dummy; } bmp280_t;
esp_err_t bmp280_init_desc(bmp280_t *d, uint8_t addr, int port, gpio_num_t sda, gpio_num_t scl);
esp_err_t bmp280_free_desc(bmp280_t *d);
esp_err_t bmp280_init_default_params(bmp280_params_t *p);
esp_err_t bmp280_init(bmp280_t *d, bmp280_params_t *p);
esp_err_t bmp280_read_float(bmp280_t *d, float *t, float *p, float *h);
