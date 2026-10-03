#include "hardware/hal/platform_hal.hpp"

#include "bmi270.h"
#include "bsp/m5stack_tab5.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hardware/common/bmi270_imu.hpp"

namespace hardware {

const char *target_name() { return "M5Stack Tab5"; }

esp_err_t display_init(lv_display_t **display)
{
    if (display == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    *display = nullptr;

    // Let board rails settle, then release the combined LCD/touch controller
    // before the BSP probes it to identify Tab5 revisions.
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_RETURN_ON_ERROR(bsp_feature_enable(BSP_FEATURE_LCD, true), "tab5", "LCD enable");
    vTaskDelay(pdMS_TO_TICKS(20));

    *display = bsp_display_start();
    if (*display == nullptr) {
        return ESP_FAIL;
    }
    return bsp_display_backlight_on();
}

bool display_lock(uint32_t timeout_ms) { return bsp_display_lock(timeout_ms); }
void display_unlock() { bsp_display_unlock(); }

esp_err_t display_set_rotation(lv_display_t *display, lv_disp_rotation_t rotation)
{
    if (display == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    bsp_display_rotate(display, rotation);
    return ESP_OK;
}

esp_err_t accelerometer_start(accelerometer_callback_t callback, void *context)
{
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), "tab5", "I2C initialization");
    return common::bmi270_start(bsp_i2c_get_handle(), BMI270_I2C_ADDRESS_L, callback, context);
}

esp_err_t sdcard_mount() { return bsp_sdcard_mount(); }
const char *sdcard_mount_point() { return BSP_SD_MOUNT_POINT; }

esp_err_t c6_radio_enable()
{
    ESP_RETURN_ON_ERROR(bsp_feature_enable(BSP_FEATURE_WIFI, true), "tab5", "C6 radio enable");
    vTaskDelay(pdMS_TO_TICKS(200));
    return ESP_OK;
}

} // namespace hardware
