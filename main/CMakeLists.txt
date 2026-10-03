idf_component_register(SRCS "main.c" "library.c" "metadata.c" "playlist.c" "audio.c" "ui.c" "welcome.c" "artwork.c" "console.c" "network.c" "wifi_config.c" "../vendor/tomlc17.c" INCLUDE_DIRS "." "../vendor" REQUIRES esp_lcd_qspi_amoled esp_lcd_touch_cst820 lvgl esp_driver_i2s esp_driver_sdmmc fatfs nvs_flash esp_timer esp_driver_gpio esp_driver_i2c esp_driver_spi esp_hw_support esp_wifi esp_netif esp_event esp_http_server json EMBED_TXTFILES "portal.html")

# Prevent by-value scan snapshots/inlining from exhausting the WiFi task stack.
set_source_files_properties(network.c wifi_config.c ../vendor/tomlc17.c PROPERTIES COMPILE_OPTIONS "-Wframe-larger-than=2048;-fstack-usage")
