idf_component_register(SRCS "main.c" "library.c" "library_index.c" "metadata.c" "playlist.c" "audio.c" "ui.c" "fallback.c" "theme.c" "artwork.c" "console.c" "tracer.c" "network.c" "captive_dns.c" "wifi_config.c" "power_policy.c" "managed.c" "sync.c" "sync_progress.c" "lyrics.c" "../vendor/ftp/ftp.c" "../vendor/ftp/async_writer.c" "../vendor/ftp/replace.c" "../vendor/tomlc17.c" INCLUDE_DIRS "." "../vendor" "../vendor/ftp" REQUIRES esp_lcd_qspi_amoled esp_lcd_touch_cst820 lvgl esp_driver_i2s esp_driver_sdmmc fatfs nvs_flash esp_timer esp_driver_gpio esp_driver_i2c esp_driver_spi esp_hw_support esp_wifi esp_netif esp_event esp_http_server esp_http_client mdns mbedtls json esp_driver_usb_serial_jtag EMBED_TXTFILES "portal.html")

# Prevent by-value scan snapshots/inlining from exhausting the WiFi task stack.
set_source_files_properties(network.c wifi_config.c ../vendor/tomlc17.c PROPERTIES COMPILE_OPTIONS "-Wframe-larger-than=2048;-fstack-usage")

set_source_files_properties(sync.c managed.c theme.c lyrics.c captive_dns.c tracer.c PROPERTIES COMPILE_OPTIONS "-Wframe-larger-than=2048;-fstack-usage")
