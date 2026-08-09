cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# ---------------------------------------------------------------------------
# esp32idf_radioamateur_modem board wiring.
#
# Every pin (ADC, DAC, and PTT), the ADC attenuation and both sample rates
# are compile-time constants in
# components/esp32idf_radioamateur_modem/include/esp32idf_radioamateur_modem_config.h,
# which is guarded with #ifndef precisely so the build system can override them
# (see the file header). That is what this block is: the project's board
# definition. None of these pins have a g_config counterpart - nothing reads
# them at runtime.
#
# PTT's GPIO is an internal radiomodem feature and, like the ADC/DAC pins, is
# fixed at compile time here rather than being selectable from the web admin's
# Radio page - it is intentionally NOT stored in g_config and has no
# <select> on that page. PTT's active level (MODEM_PTT_ACTIVE_HIGH) has no
# runtime or web-admin counterpart either; it is active high on this board.
#
# These MUST be set before project(), while components are still being
# configured - after it they are too late to reach the component's sources.
#
# Constraints enforced by the component at compile time:
#   MODEM_DAC_GPIO : 25 (DAC1) or 26 (DAC2) only - the ESP32 DAC is not
#                    routable through the GPIO matrix.
#   MODEM_ADC_GPIO : 32..39 only - ADC2 is unusable while WiFi is up, and this
#                    firmware always has WiFi up.
#   MODEM_PTT_GPIO : -1 (disabled) or any output-capable GPIO not equal to
#                    MODEM_ADC_GPIO/MODEM_DAC_GPIO (0-19, 21-23, 25-27, 32, 33
#                    minus whichever of those the ADC/DAC pins above claim).
# ---------------------------------------------------------------------------
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_ADC_GPIO=33" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_DAC_GPIO=25" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_PTT_GPIO=26" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_PTT_ACTIVE_HIGH=1" APPEND)
# Status LEDs are optional; -1 disables them.
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_LED_TX_GPIO=-1" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_LED_RX_GPIO=-1" APPEND)

project(esp32idf_APRS)
