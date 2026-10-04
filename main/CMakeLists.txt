set(RETROSCOPE_TARGET "tab5" CACHE STRING "Hardware target (tab5, esp32_p4_panel_eth_2ro, jc8012p4a1c_i_w_y)")
set_property(CACHE RETROSCOPE_TARGET PROPERTY STRINGS tab5 esp32_p4_panel_eth_2ro jc8012p4a1c_i_w_y)

set(RETROSCOPE_TARGET_REQUIRES)
if(RETROSCOPE_TARGET STREQUAL "tab5")
    set(RETROSCOPE_TARGET_SOURCE "tab5/platform_hal.cpp")
    list(APPEND RETROSCOPE_TARGET_REQUIRES m5stack_tab5)
elseif(RETROSCOPE_TARGET STREQUAL "esp32_p4_panel_eth_2ro" OR
       RETROSCOPE_TARGET STREQUAL "jc8012p4a1c_i_w_y")
    set(RETROSCOPE_TARGET_SOURCE "hardware/stub/unsupported_platform.cpp")
else()
    message(FATAL_ERROR "Unknown RETROSCOPE_TARGET='${RETROSCOPE_TARGET}'")
endif()

idf_component_register(
    SRCS "dashboard.cpp" "retro_badge_asset.cpp" "screen_assets.cpp" "main.cpp" "provisioning.cpp"
         "hardware/common/bmi270_imu.cpp" "${RETROSCOPE_TARGET_SOURCE}"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_event esp_netif esp_wifi esp_driver_jpeg esp_driver_tsens esp_driver_i2c
            esp_hw_support fatfs wear_levelling bmi270 sensor_hub ${RETROSCOPE_TARGET_REQUIRES}
)

if(NOT RETROSCOPE_TARGET STREQUAL "tab5")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RETROSCOPE_TARGET_NAME=\"${RETROSCOPE_TARGET}\")
endif()
