file(
    GLOB_RECURSE
        SOURCES
            *.c
            lwfec/*.c
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS
        .
        lwfec/
    REQUIRES
        driver
        esp_timer
        esp_adc
        esp-dsp
)
