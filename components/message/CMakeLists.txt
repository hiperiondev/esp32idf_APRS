# main is listed as PRIV_REQUIRES, not REQUIRES: message.c uses main's
# app_config.h, aprs_path.h, aprs_service.h, json_escape.h and str_append.h
# internally, but never re-exports any of them through message.h, so the
# dependency does not belong on the public REQUIRES list. main in turn
# lists message in its own REQUIRES, which makes this a documented circular
# component dependency (see the ESP-IDF build-system guide's "Component
# Requirements" section): CMake resolves it by repeating both component
# libraries on the linker command line, and idf_component_register's own
# REQUIRES/PRIV_REQUIRES machinery is what makes "../../main/include"
# resolvable here in the first place.
idf_component_register(
    SRCS
        "message.c"
    INCLUDE_DIRS
        "include"
        "../../main/include"
    REQUIRES
        esp_driver_gpio
        esp_timer
        esp32idf_radioamateur_modem
        sensors_local
        query
    PRIV_REQUIRES
        main
)
