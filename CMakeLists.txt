cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# ---------------------------------------------------------------------------
# esp32idf_radioamateur_modem board wiring.
#
# The component this replaced (esp32_IDF_libAPRS) took its ADC/DAC/PTT pins at
# RUNTIME, in the aprs_modem_config_t handed to APRS_init() from g_config. The
# new component does not: every pin, the ADC attenuation and both sample rates
# are compile-time constants in
# components/esp32idf_radioamateur_modem/include/esp32idf_radioamateur_modem_config.h,
# which is guarded with #ifndef precisely so the build system can override them
# (see the file header). That is what this block is: the project's board
# definition. The g_config fields that used to carry those pins (adc_gpio,
# dac_gpio, adc_sel_gpio, dac_sel_gpio, adc_atten, rf_sql_*, rf_pwr_*, rf_pd_*,
# rf_tx_gpio, rf_rx_gpio) have been deleted - nothing read them.
#
# PTT is the one pin still selectable at runtime, on the web admin's Radio page:
# aprs_service_build_modem_config() maps g_config.rf_ptt_gpio / .rf_ptt_active
# into modem_config_t.ptt_gpio / .ptt_active_high on every boot and on every
# Save. To keep ONE source of truth for the board wiring, app_config.c derives
# the factory default of those two fields from MODEM_PTT_GPIO /
# MODEM_PTT_ACTIVE_HIGH below - so changing the pin here moves both the
# component default and the config default together, and a config.json written
# by an operator still overrides it at runtime as intended.
#
# These MUST be set before project(), while components are still being
# configured - after it they are too late to reach the component's sources.
#
# Values below match the pins the previous build was actually wired for (the
# old LOOP TEST text hard-coded "GPIO33 (ADC in)" and "GPIO25 (DAC out)"), and
# the PTT pin/polarity carried over from app_config.c's previous factory
# defaults (GPIO26, active low).
#
# Constraints enforced by the component at compile time:
#   MODEM_DAC_GPIO : 25 (DAC1) or 26 (DAC2) only - the ESP32 DAC is not
#                    routable through the GPIO matrix.
#   MODEM_ADC_GPIO : 32..39 only - ADC2 is unusable while WiFi is up, and this
#                    firmware always has WiFi up.
# ---------------------------------------------------------------------------
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_ADC_GPIO=33" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_DAC_GPIO=25" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_PTT_GPIO=26" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_PTT_ACTIVE_HIGH=0" APPEND)
# Status LEDs are optional; -1 disables them (the old component drove none).
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_LED_TX_GPIO=-1" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_LED_RX_GPIO=-1" APPEND)

project(esp32idf_APRS)
