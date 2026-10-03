#include "hardware/hal/platform_hal.hpp"

namespace hardware {

const char *target_name() { return RETROSCOPE_TARGET_NAME; }

esp_err_t display_init(lv_display_t **display)
{
    if (display != nullptr) {
        *display = nullptr;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

bool display_lock(uint32_t) { return false; }
void display_unlock() {}
esp_err_t display_set_rotation(lv_display_t *, lv_disp_rotation_t) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t accelerometer_start(accelerometer_callback_t, void *) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sdcard_mount() { return ESP_ERR_NOT_SUPPORTED; }
const char *sdcard_mount_point() { return "/sdcard"; }
esp_err_t c6_radio_enable() { return ESP_ERR_NOT_SUPPORTED; }

} // namespace hardware
