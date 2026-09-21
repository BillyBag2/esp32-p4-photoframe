#include <cmath>

#include "bsp/m5stack_tab5.h"
#include "esp_check.h"
#include "esp_hosted.h"
#include "esp_hosted_misc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "provisioning.hpp"

namespace {
constexpr char TAG[] = "photoframe";
lv_display_t *display;
lv_disp_rotation_t current_rotation = LV_DISPLAY_ROTATION_0;

void accelerometer_event(void *, sensor_event_base_t, int32_t event_id, void *event_data)
{
    if (event_id != SENSOR_ACCE_DATA_READY || event_data == nullptr || display == nullptr) {
        return;
    }

    const auto *group = static_cast<const sensor_data_group_t *>(event_data);
    if (group->number == 0) {
        return;
    }
    const axis3_t &a = group->sensor_data[0].acce;

    constexpr float hysteresis_g = 0.20F;
    if (std::fabs(std::fabs(a.x) - std::fabs(a.y)) < hysteresis_g) {
        return;
    }

    lv_disp_rotation_t next;
    if (std::fabs(a.x) > std::fabs(a.y)) {
        next = a.x > 0 ? LV_DISPLAY_ROTATION_90 : LV_DISPLAY_ROTATION_270;
    } else {
        next = a.y > 0 ? LV_DISPLAY_ROTATION_0 : LV_DISPLAY_ROTATION_180;
    }

    if (next != current_rotation && bsp_display_lock(100)) {
        bsp_display_rotate(display, next);
        current_rotation = next;
        bsp_display_unlock();
    }
}

esp_err_t start_orientation_sensor()
{
    bsp_sensor_config_t cfg = {
        .type = IMU_ID,
        .mode = MODE_POLLING,
        .period = 250,
    };
    sensor_handle_t sensor = nullptr;
    ESP_RETURN_ON_ERROR(bsp_sensor_init(&cfg, &sensor), TAG, "BMI270 initialization");
    ESP_RETURN_ON_ERROR(iot_sensor_handler_register(sensor, accelerometer_event, nullptr), TAG,
                        "BMI270 event handler");
    return iot_sensor_start(sensor);
}
} // namespace

extern "C" void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Allow the board power rails and I2C peripherals to settle after reset.
    ESP_LOGI(TAG, "Waiting for Tab5 peripherals to settle");
    vTaskDelay(pdMS_TO_TICKS(500));

    // ST712x combines LCD and touch. Release the LCD reset before the BSP
    // probes the touch controller to identify newer Tab5 board revisions.
    ESP_ERROR_CHECK(bsp_feature_enable(BSP_FEATURE_LCD, true));
    vTaskDelay(pdMS_TO_TICKS(20));

    display = bsp_display_start();
    ESP_ERROR_CHECK(display == nullptr ? ESP_FAIL : ESP_OK);
    ESP_ERROR_CHECK(bsp_display_backlight_on());

    err = bsp_sdcard_mount();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No SD card mounted: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "SD card mounted at %s", BSP_SD_MOUNT_POINT);
    }

    ESP_ERROR_CHECK(start_orientation_sensor());

    // ESP32-P4 has no radio. Power the Tab5's ESP32-C6 and bring up its
    // ESP-Hosted SDIO transport before Wi-Fi/BLE provisioning.
    ESP_ERROR_CHECK(bsp_feature_enable(BSP_FEATURE_WIFI, true));
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_ERROR_CHECK(static_cast<esp_err_t>(esp_hosted_init()));
    err = static_cast<esp_err_t>(esp_hosted_connect_to_slave());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP-Hosted C6 SDIO connection failed: %s", esp_err_to_name(err));
        return;
    }

    // ESP-Hosted 2.x can return success from connect_to_slave even when the
    // transport driver failed. Verify an RPC before starting Bluetooth.
    esp_hosted_coprocessor_fwver_t c6_version = {};
    err = esp_hosted_get_coprocessor_fwversion(&c6_version);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "C6 is not responding over SDIO: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "C6 ESP-Hosted firmware: %lu.%lu.%lu",
             static_cast<unsigned long>(c6_version.major1),
             static_cast<unsigned long>(c6_version.minor1),
             static_cast<unsigned long>(c6_version.patch1));
    if (c6_version.major1 < 2) {
        ESP_LOGE(TAG, "C6 firmware is incompatible with ESP-Hosted 2.x; flash a matching 2.x slave image");
        return;
    }
    ESP_ERROR_CHECK(esp_hosted_bt_controller_init());
    ESP_ERROR_CHECK(esp_hosted_bt_controller_enable());
    ESP_ERROR_CHECK(provisioning_start());

    ESP_LOGI(TAG, "Tab5 photo frame hardware initialized");
}
