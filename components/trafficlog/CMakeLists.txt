# main is listed as PRIV_REQUIRES, not REQUIRES: trafficlog.c uses main's
# json_escape.h internally to format its own JSON output, but never
# re-exports it through trafficlog.h, so the dependency does not belong on
# the public REQUIRES list. main in turn lists trafficlog in its own
# REQUIRES, which makes this a documented circular component dependency (see
# the ESP-IDF build-system guide's "Component Requirements" section): CMake
# resolves it by repeating both component libraries on the linker command
# line, and idf_component_register's own REQUIRES/PRIV_REQUIRES machinery is
# what makes "../../main/include" resolvable here in the first place.
idf_component_register(
    SRCS
        "trafficlog.c"
    INCLUDE_DIRS
        "include"
        "../../main/include"
    REQUIRES
        esp_timer
        freertos
    PRIV_REQUIRES
        main
)
