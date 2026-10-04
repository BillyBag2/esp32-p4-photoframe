#include <cstdlib>
#include <cmath>

#include "dashboard.hpp"
#include "driver/jpeg_decode.h"
#include "esp_check.h"
#include "esp_hosted.h"
#include "esp_hosted_misc.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "hardware/hal/platform_hal.hpp"
#include "nvs_flash.h"
#include "provisioning.hpp"
#include "retro_badge_asset.hpp"
#include "screen_assets.hpp"
#include "wear_levelling.h"

namespace {
constexpr char TAG[] = "photoframe";
lv_display_t *display;
lv_disp_rotation_t display_rotation = LV_DISPLAY_ROTATION_90;
// LVGL keeps a pointer to this descriptor while the image is displayed.
// Give it static lifetime because app_main returns after starting the services.
lv_image_dsc_t badge_image;
lv_image_dsc_t load_media_image;
lv_image_dsc_t settings_image;

void set_display_rotation(lv_disp_rotation_t rotation)
{
    if (display == nullptr || rotation == display_rotation) {
        return;
    }

    if (!hardware::display_lock(1000)) {
        ESP_LOGW(TAG, "Could not lock display to change orientation");
        return;
    }
    const esp_err_t err = hardware::display_set_rotation(display, rotation);
    hardware::display_unlock();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not change display orientation: %s", esp_err_to_name(err));
        return;
    }
    display_rotation = rotation;
}

void accelerometer_update(float x, float y, float, void *)
{
    constexpr float hysteresis_g = 0.20F;
    const float abs_x = std::fabs(x);
    const float abs_y = std::fabs(y);
    if (abs_x + abs_y < hysteresis_g) {
        // Gravity is mostly on the sensor's Z axis, so neither portrait nor
        // landscape can be inferred from X/Y while the device is lying flat.
        dashboard_set_orientation("Flat / transition");
        return;
    }
    if (std::fabs(abs_x - abs_y) < hysteresis_g) {
        dashboard_set_orientation("Changing orientation");
        return;
    }

    if (abs_x > abs_y) {
        if (x > 0) {
            dashboard_set_orientation("Landscape (normal)");
            set_display_rotation(LV_DISPLAY_ROTATION_90);
        } else {
            dashboard_set_orientation("Landscape (inverted)");
            set_display_rotation(LV_DISPLAY_ROTATION_270);
        }
    } else {
        dashboard_set_orientation(y > 0 ? "Portrait (normal)" : "Portrait (inverted)");
    }
}

esp_err_t decode_jpeg_image(const uint8_t *jpeg, size_t jpeg_size, const char *name,
                            lv_image_dsc_t *image)
{
    constexpr uint32_t kBadgeDimension = 720;
    constexpr uint32_t kBytesPerPixel = 2;

    jpeg_decode_picture_info_t info = {};
    ESP_RETURN_ON_ERROR(jpeg_decoder_get_info(jpeg, jpeg_size, &info), TAG,
                        "read %s JPEG header", name);
    if (info.width != kBadgeDimension || info.height != kBadgeDimension) {
        ESP_LOGE(TAG, "%s must be 720 x 720; received %lu x %lu", name,
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
            const auto *decoded_pixels = reinterpret_cast<const uint16_t *>(pixels);
            ESP_LOGI(TAG,
                     "%s RGB565 samples: overall-first=0x%04X, overall-last=0x%04X, "
                     "first-line-first=0x%04X, first-line-last=0x%04X",
                     name,
                     static_cast<unsigned>(decoded_pixels[0]),
                     static_cast<unsigned>(decoded_pixels[kBadgeDimension * kBadgeDimension - 1]),
                     static_cast<unsigned>(decoded_pixels[kBadgeDimension - 1]),
                     static_cast<unsigned>(decoded_pixels[0]));
            *image = {};
            image->header.magic = LV_IMAGE_HEADER_MAGIC;
            image->header.cf = LV_COLOR_FORMAT_RGB565;
            image->header.w = kBadgeDimension;
            image->header.h = kBadgeDimension;
            image->header.stride = kBadgeDimension * kBytesPerPixel;
            image->data_size = decoded_size;
            image->data = pixels;
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

    ESP_LOGI(TAG, "Starting hardware target: %s", hardware::target_name());
    display = nullptr;
    ESP_ERROR_CHECK(hardware::display_init(&display));

    // The target display and status panel use a landscape canvas. Keep this
    // startup dashboard in that orientation.
    badge_image = {};
    load_media_image = {};
    settings_image = {};
    ESP_ERROR_CHECK(decode_jpeg_image(retro_badge_jpeg_data(), retro_badge_jpeg_size(),
                                      "Retro badge", &badge_image));
    ESP_ERROR_CHECK(decode_jpeg_image(load_media_jpeg_data(), load_media_jpeg_size(),
                                      "Load media image", &load_media_image));
    ESP_ERROR_CHECK(decode_jpeg_image(settings_jpeg_data(), settings_jpeg_size(),
                                      "Settings image", &settings_image));
    if (!hardware::display_lock(1000)) {
        ESP_LOGE(TAG, "Could not lock LVGL display for dashboard setup");
        std::free(const_cast<uint8_t *>(badge_image.data));
        std::free(const_cast<uint8_t *>(load_media_image.data));
        std::free(const_cast<uint8_t *>(settings_image.data));
        return;
    }
    // Rotation values: 0=portrait, 90=landscape, 180=portrait inverted,
    // 270=landscape inverted. Sensor reporting remains independent.
    err = hardware::display_set_rotation(display, LV_DISPLAY_ROTATION_90);
    if (err == ESP_OK) {
        err = dashboard_start(&badge_image, &load_media_image, &settings_image,
                              hardware::sdcard_mount_point());
    }
    hardware::display_unlock();
    ESP_ERROR_CHECK(err);

    esp_vfs_fat_mount_config_t internal_mount_config = VFS_FAT_MOUNT_DEFAULT_CONFIG();
    internal_mount_config.max_files = 4;
    wl_handle_t internal_wl_handle = WL_INVALID_HANDLE;
    err = esp_vfs_fat_spiflash_mount_rw_wl("/internal", "storage", &internal_mount_config,
                                          &internal_wl_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Internal FAT storage unavailable: %s", esp_err_to_name(err));
    }

    err = hardware::accelerometer_start(accelerometer_update, nullptr);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Orientation sensor unavailable: %s", esp_err_to_name(err));
        dashboard_set_orientation("Unavailable");
    }

    err = hardware::sdcard_mount();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No SD card mounted: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "SD card mounted at %s", hardware::sdcard_mount_point());
    }

    // All supported targets use a C6-style ESP-Hosted radio coprocessor.
    ESP_ERROR_CHECK(hardware::c6_radio_enable());
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

    ESP_LOGI(TAG, "%s photo frame hardware initialized", hardware::target_name());
}
