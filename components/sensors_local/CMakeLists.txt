idf_component_register(
    SRCS
        "sensors_local.c"
        "drivers/sensor_local_example.c"
    INCLUDE_DIRS
        "include"
        "drivers"
    REQUIRES
        weather_telemetry
)

# Enable sensor local example
target_compile_definitions(${COMPONENT_LIB} PUBLIC -DCONFIG_SENSORS_LOCAL_EXAMPLE_DRIVER5)
