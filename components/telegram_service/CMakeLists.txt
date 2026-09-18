# Component registration for telegram_service, the high level bot service
# built on top of the esp_telegram_bot transport.
# The cJSON parser is not part of ESP-IDF: it is pulled from the ESP
# Component Registry as "espressif/cjson" through idf_component.yml, which
# adds it to this component's private requirements automatically.

idf_component_register(
    SRCS "telegram_service.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_telegram_bot
    PRIV_REQUIRES log esp_timer esp_system esp_wifi
)
