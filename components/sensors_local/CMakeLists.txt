# sensors_local component

idf_component_register(
    SRCS
        "sensors_local.c"
        "drivers/example/sensor_local_telemetry_example.c"
        "drivers/example/sensor_local_weather_example.c"
        "drivers/bmp180/bmp180.c"
        "drivers/bme280/bme280.c"
    INCLUDE_DIRS
        "include"
        "drivers"
        "drivers/bmp180"
        "drivers/bme280"
        "drivers/example"
    REQUIRES
        weather_telemetry
    PRIV_REQUIRES
        bmp180
        bmp280 # esp-idf-lib managed component, drives the BME280 too
    WHOLE_ARCHIVE
)

# WHOLE_ARCHIVE above is essential: each driver object is only referenced by
# its own constructor, so without it the linker's --gc-sections would drop the
# unreferenced driver objects from the final image and their auto-registration
# would never run (leaving the registry empty). WHOLE_ARCHIVE forces every
# object of this component into the binary so all drivers register at start.
#
# Every driver source is listed unconditionally; each one wraps its whole body
# in its own CONFIG_SENSORS_LOCAL_*_DRIVER guard, so a driver turned off in
# Kconfig compiles to an empty translation unit and registers nothing.
#
# The reserved-pin helper the webconfig GPIO pickers need lives in
# "include/sensors_local_i2c.h", which is already public through "include", so
# it stays available to consumers no matter which drivers are enabled. The
# per-chip "drivers/<chip>" entries are here only so this component's own
# sources find their configuration headers. PRIV_REQUIRES pulls in the
# esp-idf-lib BMP180 and BMP280 drivers (managed components, declared in
# idf_component.yml; the latter is named after the BMP280 and is what drives
# our BME280). They are private because nothing outside this component uses
# those driver APIs directly.
