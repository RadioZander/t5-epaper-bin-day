idf_component_register(SRCS "main.c" "battery.c" "bin_calendar.c" "bin_fetch.c" "button.c" "config.c" "epd.c"
                            "gfx.c" "portal.c" "settings.c" "wifi.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES "portal.html"
                    REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_client esp_http_server mbedtls
                             esp_app_format esp_adc dns_server
                             esp_driver_spi esp_driver_gpio esp_timer)
