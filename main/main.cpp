#include <cstdlib>

#include "bsp/m5stack_tab5.h"
#include "dashboard.hpp"
#include "driver/jpeg_decode.h"
#include "esp_check.h"
#include "esp_hosted.h"
#include "esp_hosted_misc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "provisioning.hpp"
#include "retro_badge_asset.hpp"

namespace {
constexpr char TAG[] = "photoframe";
lv_display_t *display;

esp_err_t decode_badge(lv_image_dsc_t *badge)
{
    constexpr uint32_t kBadgeDimension = 720;
    constexpr uint32_t kBytesPerPixel = 2;

    jpeg_decode_picture_info_t info = {};
    const uint8_t *jpeg = retro_badge_jpeg_data();
    const size_t jpeg_size = retro_badge_jpeg_size();
    ESP_RETURN_ON_ERROR(jpeg_decoder_get_info(jpeg, jpeg_size, &info), TAG, "read badge JPEG header");
    if (info.width != kBadgeDimension || info.height != kBadgeDimension) {
        ESP_LOGE(TAG, "Badge must be 720 x 720; received %lu x %lu",
                 static_cast<unsigned long>(info.width), static_cast<unsigned long>(info.height));
        return ESP_ERR_INVALID_SIZE;
    }

    jpeg_decode_memory_alloc_cfg_t memory_config = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };
    size_t buffer_size = 0;
    uint8_t *pixels = static_cast<uint8_t *>(jpeg_alloc_decoder_mem(
        kBadgeDimension * kBadgeDimension * kBytesPerPixel, &memory_config, &buffer_size));
    if (pixels == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    jpeg_decoder_handle_t decoder = nullptr;
    jpeg_decode_engine_cfg_t engine_config = {};
    engine_config.timeout_ms = 80;
    esp_err_t err = jpeg_new_decoder_engine(&engine_config, &decoder);
    if (err == ESP_OK) {
        jpeg_decode_cfg_t decode_config = {};
        decode_config.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
        decode_config.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR;
        uint32_t decoded_size = 0;
        err = jpeg_decoder_process(decoder, &decode_config, jpeg, jpeg_size, pixels, buffer_size,
                                   &decoded_size);
        jpeg_del_decoder_engine(decoder);
        if (err == ESP_OK && decoded_size != kBadgeDimension * kBadgeDimension * kBytesPerPixel) {
            err = ESP_ERR_INVALID_SIZE;
        }
        if (err == ESP_OK) {
            *badge = {};
            badge->header.magic = LV_IMAGE_HEADER_MAGIC;
            badge->header.cf = LV_COLOR_FORMAT_RGB565;
            badge->header.w = kBadgeDimension;
            badge->header.h = kBadgeDimension;
            badge->header.stride = kBadgeDimension * kBytesPerPixel;
            badge->data_size = decoded_size;
            badge->data = pixels;
            return ESP_OK;
        }
    }

    std::free(pixels);
    return err;
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

    // The 720 px square badge and status panel use the Tab5's 1280 x 720
    // landscape canvas. Keep this startup dashboard in that orientation.
    lv_image_dsc_t badge = {};
    ESP_ERROR_CHECK(decode_badge(&badge));
    if (!bsp_display_lock(1000)) {
        ESP_LOGE(TAG, "Could not lock LVGL display for dashboard setup");
        std::free(const_cast<uint8_t *>(badge.data));
        return;
    }
    // Rotation 90 maps LVGL's 1280 x 720 landscape canvas onto the panel's
    // native 720 x 1280 portrait scanout with the expected left-to-right order.
    bsp_display_rotate(display, LV_DISPLAY_ROTATION_270);
    err = dashboard_start(&badge);
    bsp_display_unlock();
    ESP_ERROR_CHECK(err);

    err = bsp_sdcard_mount();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No SD card mounted: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "SD card mounted at %s", BSP_SD_MOUNT_POINT);
    }

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
