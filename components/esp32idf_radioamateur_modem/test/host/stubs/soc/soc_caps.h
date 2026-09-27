/**
 * @file soc_caps.h
 * @brief Host stand-in for the ESP32 SoC capability macros.
 *
 * Carries the ADC digital-controller sizes of the classic ESP32, the only SoC
 * values the modem configuration header derives anything from.
 */

#pragma once

/** @brief Bytes per ADC DMA conversion result on the ESP32. */
#define SOC_ADC_DIGI_RESULT_BYTES 2
/** @brief Bytes the ADC DMA writes per conversion on the ESP32. */
#define SOC_ADC_DIGI_DATA_BYTES_PER_CONV 4
