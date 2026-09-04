# main is listed as PRIV_REQUIRES, not REQUIRES: mice.c uses main's
# aprs_minutes.h internally for the degrees/minutes quantisation shared with
# the uncompressed position field (main/aprs_coord.c) and the "!DAO!"
# extension (main/aprs_dao.c), but this component's own public header never
# re-exports it, so the dependency does not belong on the public REQUIRES
# list. main in turn lists weather_telemetry in its own REQUIRES, which makes
# this a documented circular component dependency (see the ESP-IDF
# build-system guide's "Component Requirements" section): CMake resolves it
# by repeating both component libraries on the linker command line.
# PRIV_REQUIRES main is sufficient on its own to make main's public
# INCLUDE_DIRS visible here, so this component does not add
# "../../main/include" to its own INCLUDE_DIRS.
idf_component_register(
    SRCS
        "mice.c"
    INCLUDE_DIRS
        "include"
    PRIV_REQUIRES
        main
)
