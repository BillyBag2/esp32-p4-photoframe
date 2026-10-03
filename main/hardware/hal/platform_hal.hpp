#pragma once

#include "esp_err.h"
#include "lvgl.h"

namespace hardware {

using accelerometer_callback_t = void (*)(float x, float y, float z, void *context);

const char *target_name();

esp_err_t display_init(lv_display_t **display);
bool display_lock(uint32_t timeout_ms);
void display_unlock();
esp_err_t display_set_rotation(lv_display_t *display, lv_disp_rotation_t rotation);

esp_err_t accelerometer_start(accelerometer_callback_t callback, void *context);
esp_err_t sdcard_mount();
const char *sdcard_mount_point();
esp_err_t c6_radio_enable();

} // namespace hardware
