# Component registration for esp_telegram_bot, the transport layer talking
# to the Telegram Bot HTTPS API.
# The root certificate is not part of the binary: when the ESP-IDF
# certificate bundle is not selected, it is read at run time from the
# LittleFS storage partition mounted by main/storage.c, so nothing has to
# be embedded here.

idf_component_register(
    SRCS "esp_telegram_bot.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client esp-tls
    PRIV_REQUIRES log mbedtls vfs
)
