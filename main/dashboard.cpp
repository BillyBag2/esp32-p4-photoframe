#include "dashboard.hpp"

#include "provisioning.hpp"

namespace {
lv_obj_t *status_table;

void set_row(uint16_t row, const char *label, const char *value)
{
    lv_table_set_cell_value(status_table, row, 0, label);
    lv_table_set_cell_value(status_table, row, 1, value);
}

void update_status(lv_timer_t *)
{
    provisioning_status_t status = {};
    provisioning_get_status(&status);

    set_row(0, "Wi-Fi", status.wifi_state);
    set_row(1, "SSID", status.ssid);
    set_row(2, "BLE", status.ble_advertising ? "Advertising" : "Stopped");
    set_row(3, "BLE name", status.ble_name);
    set_row(4, "Proof of possession", status.proof_of_possession);
}
} // namespace

esp_err_t dashboard_start(const lv_image_dsc_t *badge)
{
    if (badge == nullptr) {
        return ESP_ERR_INVALID_ARG;
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
    lv_obj_set_style_pad_all(panel, 28, 0);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, "RetroScope");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *subtitle = lv_label_create(panel);
    lv_label_set_text(subtitle, "Connection status");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x8EABC0), 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, 0, 44);

    status_table = lv_table_create(panel);
    lv_table_set_column_count(status_table, 2);
    lv_table_set_column_width(status_table, 0, 176);
    lv_table_set_column_width(status_table, 1, 320);
    lv_obj_set_width(status_table, LV_PCT(100));
    lv_table_set_row_count(status_table, 5);
    lv_table_set_cell_value(status_table, 0, 0, "Wi-Fi");
    lv_table_set_cell_value(status_table, 0, 1, "Starting");
    lv_table_set_cell_value(status_table, 1, 0, "SSID");
    lv_table_set_cell_value(status_table, 1, 1, "Not configured");
    lv_table_set_cell_value(status_table, 2, 0, "BLE");
    lv_table_set_cell_value(status_table, 2, 1, "Starting");
    lv_table_set_cell_value(status_table, 3, 0, "BLE name");
    lv_table_set_cell_value(status_table, 3, 1, "Starting");
    lv_table_set_cell_value(status_table, 4, 0, "Proof of ownership");
    lv_table_set_cell_value(status_table, 4, 1, "myRetroScope");
    lv_obj_set_style_text_color(status_table, lv_color_hex(0xF4F8FA), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(status_table, lv_color_hex(0x223744), LV_PART_ITEMS);
    lv_obj_set_style_border_color(status_table, lv_color_hex(0x406070), LV_PART_ITEMS);
    lv_obj_set_style_pad_all(status_table, 10, LV_PART_ITEMS);
    lv_obj_align(status_table, LV_ALIGN_TOP_LEFT, 0, 92);

    update_status(nullptr);
    lv_timer_create(update_status, 500, nullptr);
    return ESP_OK;
}
