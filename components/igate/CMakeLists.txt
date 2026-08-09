# main is listed as PRIV_REQUIRES, not REQUIRES: igate.c calls into main's own
# .c files (app_config.c, aprs_service.c, aprs_filter.c, net_state.c) rather
# than just using its headers, so the dependency is real, but it is only used
# in this component's implementation and never re-exported through igate.h, so
# it does not belong on the public REQUIRES list. main in turn lists igate in
# its own REQUIRES, which makes this a documented circular component
# dependency (see the ESP-IDF build-system guide's "Component Requirements"
# section): CMake resolves it by repeating both component libraries on the
# linker command line, and idf_component_register's own REQUIRES/
# PRIV_REQUIRES machinery is what makes "../../main/include" resolvable here
# in the first place.
idf_component_register(
    SRCS
        "igate.c"
    INCLUDE_DIRS
        "include"
        "../../main/include"
    REQUIRES
        esp32idf_radioamateur_modem
        lwip
        esp_netif
        trafficlog
        lastheard
    PRIV_REQUIRES
        main
)
