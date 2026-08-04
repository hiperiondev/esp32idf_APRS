idf_component_register(
    SRCS
        "main.c"
        "app_config.c"
        "storage.c"
        "aprs_service.c"
        "aprs_filter.c"
        "aprs_coord.c"
        "net_state.c"
        "beacon.c"
        "beacon_scheduler.c"
        "bulletins.c"
        "objects_items.c"
        "weather.c"
        "telemetry.c"
        "time_sync.c"
        "cpu_freq.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        json
        mbedtls
        esp_app_format
        app_update
        spi_flash
        esp_timer
        esp_pm
        lwip
        webconfig
        esp32idf_radioamateur_modem
        digirepeater
        igate
        message
        query
        trafficlog
        lastheard
        sensors_local
        weather_telemetry
)

# A discarded persistence result is a silent data-loss bug, not a style issue:
# every save entry point declared APRS_MUST_CHECK (see main/include/must_check.h)
# reports a write that may never have reached flash, and the page or task that
# ignores it goes on to tell the operator the settings are stored. Promote the
# warning to an error for this component's own sources only, so the guarantee
# holds where those call sites live without reaching the IDF or managed
# component trees.
target_compile_options(${COMPONENT_LIB} PRIVATE -Werror=unused-result)
