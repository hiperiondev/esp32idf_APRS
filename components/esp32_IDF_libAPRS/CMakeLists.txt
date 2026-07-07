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

# FX.25 (forward-error-corrected AX.25) support must always be compiled in.
# Whether it is actually used on the air is a strictly runtime decision,
# controlled by Ax25Config.fx25 / Ax25Config.fx25Tx (set from the "FX.25"
# checkbox on the RADIO/MODEM webconfig page via Ax25Init()/afskSetModem()).
# PUBLIC so any translation unit including AX25.h/fx25.h (in this component
# or in code that depends on it) sees consistent struct layouts/prototypes.
target_compile_definitions(${COMPONENT_LIB} PUBLIC ENABLE_FX25)
