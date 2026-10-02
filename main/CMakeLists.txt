idf_component_register(
    SRCS "retro_badge_asset.cpp" "dashboard.cpp" "main.cpp" "provisioning.cpp"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_event esp_netif esp_wifi esp_driver_jpeg
)
