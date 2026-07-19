# sensors_local component
#
# Every .c file dropped into drivers/ is compiled automatically - no need to
# edit this list when adding a new sensor driver. Each driver self-registers
# into the runtime registry via SENSORS_LOCAL_DRIVER_AUTOREGISTER (a C
# constructor that runs before app_main), so "all drivers in drivers/" are
# loaded and made available at start with nothing else to wire up.
file(GLOB SENSORS_LOCAL_DRIVER_SRCS "drivers/*.c")

idf_component_register(
    SRCS
        "sensors_local.c"
        ${SENSORS_LOCAL_DRIVER_SRCS}
    INCLUDE_DIRS
        "include"
        "drivers"
    REQUIRES
        weather_telemetry
    WHOLE_ARCHIVE
)

# WHOLE_ARCHIVE above is essential: each driver object is only referenced by
# its own constructor, so without it the linker's --gc-sections would drop the
# unreferenced driver objects from the final image and their auto-registration
# would never run (leaving the registry empty). WHOLE_ARCHIVE forces every
# object of this component into the binary so all drivers register at start.

# Built-in example drivers (one WEATHER, one TELEMETRY), each returning random
# data on every call. Their defines enable the file bodies. Real drivers added
# to drivers/ need no define - they are compiled and registered unconditionally.
target_compile_definitions(${COMPONENT_LIB} PUBLIC -DCONFIG_SENSORS_LOCAL_WEATHER_EXAMPLE_DRIVER)
target_compile_definitions(${COMPONENT_LIB} PUBLIC -DCONFIG_SENSORS_LOCAL_TELEMETRY_EXAMPLE_DRIVER)
