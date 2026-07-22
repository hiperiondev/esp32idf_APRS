# sensors_local component

idf_component_register(
    SRCS
        "sensors_local.c"
        "drivers/example/sensor_local_telemetry_example.c"
        "drivers/example/sensor_local_weather_example.c"
        "drivers/bmp180/bmp180.c"
    INCLUDE_DIRS
        "include"
        "drivers"
        "drivers/bmp180"
    REQUIRES
        weather_telemetry
    PRIV_REQUIRES
        bmp180
    WHOLE_ARCHIVE
)

# WHOLE_ARCHIVE above is essential: each driver object is only referenced by
# its own constructor, so without it the linker's --gc-sections would drop the
# unreferenced driver objects from the final image and their auto-registration
# would never run (leaving the registry empty). WHOLE_ARCHIVE forces every
# object of this component into the binary so all drivers register at start.
#
# "drivers/bmp180" is on INCLUDE_DIRS so both this component and any consumer
# (the webconfig GPIO pickers) can #include "BMP180.h" for the reserved-pin
# helper. PRIV_REQUIRES bmp180 pulls in the esp-idf-lib BMP180 driver (managed
# component, declared in idf_component.yml); it is private because nothing
# outside this component uses the BMP180 driver API directly.
