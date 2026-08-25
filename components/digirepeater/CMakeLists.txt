# main is listed as PRIV_REQUIRES, not REQUIRES: digirepeater.c uses main's
# app_config.h internally to read g_config, but never re-exports it through
# digirepeater.h, so the dependency does not belong on the public REQUIRES
# list. main in turn lists digirepeater in its own REQUIRES, which makes
# this a documented circular component dependency (see the ESP-IDF
# build-system guide's "Component Requirements" section): CMake resolves it
# by repeating both component libraries on the linker command line.
# PRIV_REQUIRES main is sufficient on its own to make main's public
# INCLUDE_DIRS visible here, so this component does not add "../../main/include"
# to its own INCLUDE_DIRS.
idf_component_register(
    SRCS
        "digirepeater.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp32idf_radioamateur_modem
        igate
    PRIV_REQUIRES
        main
)
