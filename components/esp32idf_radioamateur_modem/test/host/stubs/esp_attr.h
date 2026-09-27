/**
 * @file esp_attr.h
 * @brief Host stand-in for the ESP-IDF section-placement attributes.
 *
 * On the host there is no IRAM, so every placement attribute used by the modem
 * sources expands to nothing and the functions they decorate compile as
 * ordinary code.
 */

#pragma once

/** @brief IRAM placement attribute; empty on the host. */
#define IRAM_ATTR
/** @brief DRAM placement attribute; empty on the host. */
#define DRAM_ATTR
/** @brief RTC slow-memory placement attribute; empty on the host. */
#define RTC_DATA_ATTR
