idf_component_register(
    SRCS
        "web_common.c"
        "web_server.c"
        "pages/page_common.c"
        "pages/page_station.c"
        "pages/page_bulletins.c"
        "pages/page_objects.c"
        "pages/page_storage.c"
        "pages/page_wireless.c"
        "pages/page_system.c"
        "pages/page_about.c"
        "pages/page_igate.c"
        "pages/page_digi.c"
        "pages/page_tracker.c"
        "pages/page_wx.c"
        "pages/page_tlm.c"
        "pages/page_radio.c"
        "pages/page_msg.c"
        "pages/page_query.c"
        "pages/page_msgchat.c"
        "pages/page_symbol.c"
    INCLUDE_DIRS
        "include"
        "translations"
        "../../main/include"
    REQUIRES
        esp_http_server
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
        trafficlog
        lastheard
        igate
        digirepeater
        esp32idf_radioamateur_modem
        sensors_local
        weather_telemetry
        message
)

# A discarded persistence result is a silent data-loss bug, not a style issue:
# every save entry point declared APRS_MUST_CHECK (see main/include/must_check.h)
# reports a write that may never have reached flash, and the page or task that
# ignores it goes on to tell the operator the settings are stored. Promote the
# warning to an error for this component's own sources only, so the guarantee
# holds where those call sites live without reaching the IDF or managed
# component trees.
target_compile_options(${COMPONENT_LIB} PRIVATE -Werror=unused-result)
