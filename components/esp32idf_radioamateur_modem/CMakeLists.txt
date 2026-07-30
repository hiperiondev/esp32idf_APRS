idf_component_register(
    SRCS
        "src/afsk.c"
        "src/modem.c"
        "src/ax25.c"
        "src/crc_ccit.c"
        "src/fx25.c"
        "src/esp32idf_radioamateur_modem.c"
        "lwfec/rs.c"
        "lwfec/gf.c"
    INCLUDE_DIRS
        .
        "include"
        "lwfec"
    REQUIRES
        esp_adc
        esp_driver_dac
        esp_driver_gptimer
        esp_driver_gpio
        esp_timer
    PRIV_REQUIRES
        esp_hw_support
        # hal: dac_ll_update_output_value(), a static inline used by the DAC
        # sample ISR so it does not have to call into flash. See dac_write_isr()
        # in afsk.c. It is a low-level header, so if a future IDF moves it this
        # is the line that breaks, and the #else branch there is the fallback.
        hal
)

# Enable FX.25
target_compile_definitions(${COMPONENT_LIB} PUBLIC -DENABLE_FX25)
