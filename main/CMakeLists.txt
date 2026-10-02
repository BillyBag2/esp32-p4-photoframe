idf_component_register(
    SRCS "dashboard.cpp" "retro_badge_asset.cpp" "main.cpp" "provisioning.cpp"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_event esp_netif esp_wifi esp_driver_jpeg esp_driver_tsens esp_hw_support fatfs wear_levelling
)
