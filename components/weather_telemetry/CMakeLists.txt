# "../../main/include" is on the include path so mice.c can use
# main/include/aprs_minutes.h, the single degrees/minutes quantisation shared
# with the uncompressed position field (main/aprs_coord.c) and the "!DAO!"
# extension (main/aprs_dao.c). That header defines its function inline and
# has no .c file of its own, so this is a header-only dependency: the
# component still needs no REQUIRES entry and takes on no link-time
# dependency on main, which in turn lists weather_telemetry in its own
# REQUIRES.
idf_component_register(
    SRCS
        "mice.c"
    INCLUDE_DIRS
        "include"
        "../../main/include"
    REQUIRES
)
