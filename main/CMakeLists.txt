# Read the same board selection in normal configuration and early expansion.
idf_build_get_property(RETROSCOPE_TARGET RETROSCOPE_TARGET)
if(NOT RETROSCOPE_TARGET)
    message(FATAL_ERROR "RETROSCOPE_TARGET build property is not set")
endif()

set(RETROSCOPE_TARGET_REQUIRES)
set(RETROSCOPE_SQUARE_ASSET_SOURCE)
set(RETROSCOPE_BADGE_ASSET_SOURCE)
set(RETROSCOPE_TEST_CARD_ASSET_SOURCE)
if(RETROSCOPE_TARGET STREQUAL "tab5")
    set(RETROSCOPE_TARGET_SOURCE "tab5/platform_hal.cpp")
    set(RETROSCOPE_SQUARE_ASSET_SOURCE "screen_assets_720x720.cpp")
    set(RETROSCOPE_BADGE_ASSET_SOURCE "retro_badge_asset.cpp")
    set(RETROSCOPE_TEST_CARD_ASSET_SOURCE "screen_test_card_1280x720.cpp")
    list(APPEND RETROSCOPE_TARGET_REQUIRES m5stack_tab5)
elseif(RETROSCOPE_TARGET STREQUAL "esp32_p4_panel_eth_2ro" OR
       RETROSCOPE_TARGET STREQUAL "jc8012p4a1c_i_w_y")
    if(RETROSCOPE_TARGET STREQUAL "jc8012p4a1c_i_w_y")
        set(RETROSCOPE_TARGET_SOURCE "jc8012_platform_hal.cpp")
        set(RETROSCOPE_SQUARE_ASSET_SOURCE "screen_assets_800x800.cpp")
        set(RETROSCOPE_BADGE_ASSET_SOURCE "retro_badge_asset_800x800.cpp")
        set(RETROSCOPE_TEST_CARD_ASSET_SOURCE "screen_test_card_1280x800.cpp")
        list(APPEND RETROSCOPE_TARGET_REQUIRES esp_lcd esp_lcd_jd9365 esp_driver_ledc esp_lvgl_port)
    else()
        set(RETROSCOPE_TARGET_SOURCE "hardware/stub/unsupported_platform.cpp")
        # The Panel ETH target is not characterized yet; use the Tab5-sized set
        # until its display dimensions are known.
        set(RETROSCOPE_SQUARE_ASSET_SOURCE "screen_assets_720x720.cpp")
        set(RETROSCOPE_BADGE_ASSET_SOURCE "retro_badge_asset.cpp")
        set(RETROSCOPE_TEST_CARD_ASSET_SOURCE "screen_test_card_1280x720.cpp")
    endif()
else()
    message(FATAL_ERROR "Unknown RETROSCOPE_TARGET='${RETROSCOPE_TARGET}'")
endif()

idf_component_register(
    SRCS "dashboard.cpp" "${RETROSCOPE_BADGE_ASSET_SOURCE}"
         "${RETROSCOPE_SQUARE_ASSET_SOURCE}" "${RETROSCOPE_TEST_CARD_ASSET_SOURCE}"
         "main.cpp" "provisioning.cpp"
         "hardware/common/bmi270_imu.cpp" "${RETROSCOPE_TARGET_SOURCE}"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_event esp_netif esp_wifi esp_driver_jpeg esp_driver_tsens esp_driver_i2c
            esp_hw_support fatfs wear_levelling bmi270 sensor_hub ${RETROSCOPE_TARGET_REQUIRES}
)

if(NOT RETROSCOPE_TARGET STREQUAL "tab5")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RETROSCOPE_TARGET_NAME=\"${RETROSCOPE_TARGET}\")
endif()
