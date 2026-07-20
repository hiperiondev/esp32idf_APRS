# sensors_local component

idf_component_register(
    SRCS
        "sensors_local.c"
        "drivers/example/sensor_local_telemetry_example.c"
        "drivers/example/sensor_local_weather_example.c"
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
