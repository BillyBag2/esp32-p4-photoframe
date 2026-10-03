#include "dashboard.hpp"

#include <cstdio>

#include "driver/temperature_sensor.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_private/esp_clk.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "provisioning.hpp"

namespace {
constexpr char TAG[] = "dashboard";
constexpr uint16_t kRowCount = 20;
constexpr char kInternalMountPoint[] = "/internal";

enum status_row_t : uint16_t {
    kWifiRow,
    kSsidRow,
    kIpRow,
    kRssiRow,
    kBleRow,
    kBleNameRow,
    kPopRow,
    kOrientationRow,
    kUptimeRow,
    kSdStatusRow,
    kSdFreeRow,
    kSdUsedRow,
    kSdTotalRow,
    kCpuTemperatureRow,
    kCpuFrequencyRow,
    kCpuUsageRow,
    kMemoryUsageRow,
    kFreeHeapRow,
    kFreePsramRow,
    kInternalFreeRow,
};

lv_obj_t *status_table;
temperature_sensor_handle_t temperature_sensor;
portMUX_TYPE orientation_lock = portMUX_INITIALIZER_UNLOCKED;
char current_orientation[32] = "Waiting for sensor";
char sd_mount_point[32] = "/sdcard";

void set_row(status_row_t row, const char *label, const char *value)
{
    lv_table_set_cell_value(status_table, row, 0, label);
    lv_table_set_cell_value(status_table, row, 1, value);
}

void set_row_fmt(status_row_t row, const char *label, const char *format, unsigned long long value)
{
    char text[40];
    std::snprintf(text, sizeof(text), format, value);
    set_row(row, label, text);
}

void update_status(lv_timer_t *)
{
    provisioning_status_t provisioning = {};
    provisioning_get_status(&provisioning);

    set_row(kWifiRow, "Wi-Fi", provisioning.wifi_state);
    set_row(kSsidRow, "SSID", provisioning.ssid);
    const char *ip_status = provisioning.ip_address[0] != '\0'
                                ? provisioning.ip_address
                                : (provisioning.wifi_connected ? "Address unavailable" : "Not connected");
    set_row(kIpRow, "IP address", ip_status);

    wifi_ap_record_t ap_info = {};
    if (provisioning.wifi_connected && esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        char rssi[24];
        std::snprintf(rssi, sizeof(rssi), "%d dBm", ap_info.rssi);
        set_row(kRssiRow, "Wi-Fi signal", rssi);
    } else {
        set_row(kRssiRow, "Wi-Fi signal", "Unavailable");
    }

    set_row(kBleRow, "BLE", provisioning.ble_advertising ? "Advertising" : "Stopped");
    set_row(kBleNameRow, "BLE name", provisioning.ble_name);
    set_row(kPopRow, "Proof of ownership", provisioning.proof_of_possession);

    char orientation[sizeof(current_orientation)];
    portENTER_CRITICAL(&orientation_lock);
    std::snprintf(orientation, sizeof(orientation), "%s", current_orientation);
    portEXIT_CRITICAL(&orientation_lock);
    set_row(kOrientationRow, "Device orientation", orientation);
    set_row_fmt(kUptimeRow, "Uptime", "%llu seconds",
                static_cast<unsigned long long>(esp_timer_get_time() / 1000000));

    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    if (esp_vfs_fat_info(sd_mount_point, &total_bytes, &free_bytes) == ESP_OK) {
        set_row(kSdStatusRow, "SD card", "Mounted");
        set_row_fmt(kSdFreeRow, "SD free", "%llu MB", free_bytes / (1024 * 1024));
        set_row_fmt(kSdUsedRow, "SD used", "%llu MB", (total_bytes - free_bytes) / (1024 * 1024));
        set_row_fmt(kSdTotalRow, "SD total", "%llu MB", total_bytes / (1024 * 1024));
    } else {
        set_row(kSdStatusRow, "SD card", "Not mounted");
        set_row(kSdFreeRow, "SD free", "Unavailable");
        set_row(kSdUsedRow, "SD used", "Unavailable");
        set_row(kSdTotalRow, "SD total", "Unavailable");
    }

    float celsius = 0.0F;
    if (temperature_sensor != nullptr && temperature_sensor_get_celsius(temperature_sensor, &celsius) == ESP_OK) {
        char temperature[24];
        std::snprintf(temperature, sizeof(temperature), "%.1f C", static_cast<double>(celsius));
        set_row(kCpuTemperatureRow, "CPU temperature", temperature);
    } else {
        set_row(kCpuTemperatureRow, "CPU temperature", "Unavailable");
    }

    set_row_fmt(kCpuFrequencyRow, "CPU frequency", "%llu MHz",
                static_cast<unsigned long long>(esp_clk_cpu_freq() / 1000000));
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS && !CONFIG_FREERTOS_SMP
    const unsigned idle_percent_core0 = static_cast<unsigned>(ulTaskGetIdleRunTimePercentForCore(0));
    const unsigned idle_percent_core1 = static_cast<unsigned>(ulTaskGetIdleRunTimePercentForCore(1));
    char cpu_usage[24];
    std::snprintf(cpu_usage, sizeof(cpu_usage), "%u%% average", 100 - (idle_percent_core0 + idle_percent_core1) / 2);
    set_row(kCpuUsageRow, "CPU usage", cpu_usage);
#else
    set_row(kCpuUsageRow, "CPU usage", "Unavailable");
#endif

    const size_t internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (internal_total != 0) {
        char memory_usage[24];
        const unsigned used_percent = static_cast<unsigned>((internal_total - internal_free) * 100 / internal_total);
        std::snprintf(memory_usage, sizeof(memory_usage), "%u%% internal heap", used_percent);
        set_row(kMemoryUsageRow, "Memory usage", memory_usage);
    } else {
        set_row(kMemoryUsageRow, "Memory usage", "Unavailable");
    }
    set_row_fmt(kFreeHeapRow, "Free heap", "%llu KB",
                static_cast<unsigned long long>(esp_get_free_heap_size() / 1024));
    set_row_fmt(kFreePsramRow, "Free PSRAM", "%llu KB",
                static_cast<unsigned long long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) / 1024));

    uint64_t internal_total_bytes = 0;
    uint64_t internal_free_bytes = 0;
    if (esp_vfs_fat_info(kInternalMountPoint, &internal_total_bytes, &internal_free_bytes) == ESP_OK) {
        set_row_fmt(kInternalFreeRow, "Internal storage free", "%llu bytes", internal_free_bytes);
    } else {
        set_row(kInternalFreeRow, "Internal storage free", "Unavailable");
    }
}
} // namespace

void dashboard_set_orientation(const char *orientation)
{
    portENTER_CRITICAL(&orientation_lock);
    std::snprintf(current_orientation, sizeof(current_orientation), "%s",
                  orientation == nullptr ? "Unavailable" : orientation);
    portEXIT_CRITICAL(&orientation_lock);
}

esp_err_t dashboard_start(const lv_image_dsc_t *badge, const char *mount_point)
{
    if (badge == nullptr || mount_point == nullptr || mount_point[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    std::snprintf(sd_mount_point, sizeof(sd_mount_point), "%s", mount_point);

    temperature_sensor_config_t temperature_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    if (temperature_sensor_install(&temperature_config, &temperature_sensor) == ESP_OK) {
        if (temperature_sensor_enable(temperature_sensor) != ESP_OK) {
            temperature_sensor_uninstall(temperature_sensor);
            temperature_sensor = nullptr;
        }
    }

    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101820), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *image = lv_image_create(screen);
    lv_image_set_src(image, badge);
    lv_obj_set_size(image, 720, 720);
    lv_obj_set_style_pad_all(image, 0, 0);

    lv_obj_t *panel = lv_obj_create(screen);
    lv_obj_set_height(panel, LV_PCT(100));
    lv_obj_set_flex_grow(panel, 1);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x192834), 0);
    lv_obj_set_style_pad_all(panel, 20, 0);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, "RetroScope status");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    status_table = lv_table_create(panel);
    lv_table_set_column_count(status_table, 2);
    lv_table_set_column_width(status_table, 0, 190);
    lv_table_set_column_width(status_table, 1, 300);
    lv_table_set_row_count(status_table, kRowCount);
    lv_obj_set_width(status_table, LV_PCT(100));
    lv_obj_set_style_text_color(status_table, lv_color_hex(0xF4F8FA), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(status_table, lv_color_hex(0x223744), LV_PART_ITEMS);
    lv_obj_set_style_border_color(status_table, lv_color_hex(0x406070), LV_PART_ITEMS);
    lv_obj_set_style_pad_all(status_table, 3, LV_PART_ITEMS);
    lv_obj_align(status_table, LV_ALIGN_TOP_LEFT, 0, 36);

    constexpr const char *labels[kRowCount] = {
        "Wi-Fi", "SSID", "IP address", "Wi-Fi signal", "BLE", "BLE name",
        "Proof of ownership", "Device orientation", "Uptime",
        "SD card", "SD free", "SD used", "SD total",
        "CPU temperature", "CPU frequency", "CPU usage", "Memory usage", "Free heap", "Free PSRAM",
        "Internal storage free",
    };
    for (uint16_t row = 0; row < kRowCount; ++row) {
        lv_table_set_cell_value(status_table, row, 0, labels[row]);
        lv_table_set_cell_value(status_table, row, 1, "Reading...");
    }

    update_status(nullptr);
    lv_timer_create(update_status, 1000, nullptr);
    return ESP_OK;
}
