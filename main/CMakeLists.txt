idf_component_register(
    SRCS "main.cpp" "chameleon_state.cpp" "chameleon_forest.cpp" "chameleon_games.cpp" "chameleon_alarm.cpp" "chameleon_ui.cpp" "chameleon_hardware.cpp" "chameleon_power.cpp" "chameleon_display.cpp" "chameleon_touch.cpp" "chameleon_mood_color.cpp" "chameleon_assets.c" "chameleon_font.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_timer esp_system esp_pm lvgl waveshare__esp32_s3_touch_amoled_1_75c
             esp_wifi esp_event esp_netif esp_http_client esp_http_server mbedtls
             espressif__cjson lwip driver esp_codec_dev)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
