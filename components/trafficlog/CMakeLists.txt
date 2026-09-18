# main is listed as PRIV_REQUIRES, not REQUIRES: trafficlog.c uses main's
# json_escape.h internally to format its own JSON output, but never
# re-exports it through trafficlog.h, so the dependency does not belong on
# the public REQUIRES list. main in turn lists trafficlog in its own
# REQUIRES, which makes this a documented circular component dependency (see
# the ESP-IDF build-system guide's "Component Requirements" section): CMake
# resolves it by repeating both component libraries on the linker command
# line. PRIV_REQUIRES main is sufficient on its own to make main's public
# INCLUDE_DIRS visible here, so this component does not add "../../main/include"
# to its own INCLUDE_DIRS.
idf_component_register(
    SRCS
        "trafficlog.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_timer
        freertos
    PRIV_REQUIRES
        main
)
