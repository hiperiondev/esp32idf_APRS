idf_component_register(
    SRCS
        "main.c"
        "app_config.c"
        "storage.c"
        "json_store.c"
        "aprs_service.c"
        "aprs_filter.c"
        "aprs_coord.c"
        "aprs_dao.c"
        "net_state.c"
        "beacon.c"
        "beacon_scheduler.c"
        "bulletins.c"
        "objects_items.c"
        "weather.c"
        "gps.c"
        "telegram_app.c"
        "telemetry.c"
        "time_sync.c"
        "cpu_freq.c"
        "heap_monitor.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        cjson
        esp_app_format
        app_update
        spi_flash
        esp_timer
        esp_pm
        esp_driver_uart
        lwip
        webconfig
        telegram_service
        esp_telegram_bot
        esp32idf_radioamateur_modem
        digirepeater
        igate
        message
        query
        winlink
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
