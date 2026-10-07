#include "hardware/hal/platform_hal.hpp"
#include "hardware/jc8012/jd9365_panel_init.hpp"
#include <atomic>

#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_jd9365.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace hardware {
namespace {
constexpr char TAG[] = "jc8012";
constexpr gpio_num_t kLcdResetGpio = GPIO_NUM_27;
constexpr gpio_num_t kBacklightGpio = GPIO_NUM_23;
constexpr uint32_t kPanelWidth = 800;
constexpr uint32_t kPanelHeight = 1280;
esp_ldo_channel_handle_t phy_ldo;
esp_lcd_dsi_bus_handle_t dsi_bus;
esp_lcd_panel_io_handle_t dbi_io;
esp_lcd_panel_handle_t panel;
lv_display_t *lvgl_display;
uint16_t *frame_buffer;
lv_display_flush_cb_t port_flush;
unsigned traced_flushes;
unsigned traced_refreshes;
std::atomic<uint32_t> completed_frames{0};
static_assert(std::atomic<uint32_t>::is_always_lock_free);

bool IRAM_ATTR count_scanout_frames(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t *, void *)
{
    completed_frames.fetch_add(1, std::memory_order_relaxed);
    return false;
}

bool notify_flush_ready(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t *, void *context)
{
    lv_display_flush_ready(static_cast<lv_display_t *>(context));
    return false;
}

void trace_display_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    const bool trace = lv_display_get_rotation(display) == LV_DISPLAY_ROTATION_90 && traced_flushes < 3;
    if (trace) {
        ++traced_flushes;
        ESP_LOGI(TAG, "LVGL flush %u: area=(%d,%d)-(%d,%d), first pixel=0x%04X",
                 traced_flushes, static_cast<int>(area->x1), static_cast<int>(area->y1),
                 static_cast<int>(area->x2), static_cast<int>(area->y2),
                 static_cast<unsigned>(*reinterpret_cast<uint16_t *>(pixels)));
    }
    port_flush(display, area, pixels);
    if (trace) {
        ESP_LOGI(TAG, "LVGL flush returned; framebuffer first pixel=0x%04X",
                 static_cast<unsigned>(frame_buffer[0]));
    }
}

void trace_refresh(lv_event_t *event)
{
    auto *display = static_cast<lv_display_t *>(lv_event_get_user_data(event));
    if (lv_display_get_rotation(display) == LV_DISPLAY_ROTATION_90 && traced_refreshes < 2) {
        ++traced_refreshes;
        ESP_LOGI(TAG, "LVGL landscape refresh completed (%ldx%ld); DMA frames=%lu",
                 static_cast<long>(lv_display_get_horizontal_resolution(display)),
                 static_cast<long>(lv_display_get_vertical_resolution(display)),
                 static_cast<unsigned long>(completed_frames.load(std::memory_order_relaxed)));
    }
}

esp_err_t configure_backlight()
{
    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.timer_num = LEDC_TIMER_0;
    timer.freq_hz = 5000;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
    ledc_channel_config_t channel = {};
    channel.gpio_num = kBacklightGpio;
    channel.speed_mode = LEDC_LOW_SPEED_MODE;
    channel.channel = LEDC_CHANNEL_0;
    channel.timer_sel = LEDC_TIMER_0;
    channel.duty = 1023;
    channel.hpoint = 0;
    channel.sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD;
    return ledc_channel_config(&channel);
}
}

const char *target_name() { return "Guition JC8012P4A1C_I_W_Y"; }
esp_err_t display_init(lv_display_t **display)
{
    if (display == nullptr) return ESP_ERR_INVALID_ARG;
    *display = nullptr;
    esp_ldo_channel_config_t ldo_config = {};
    ldo_config.chan_id = 3;
    ldo_config.voltage_mv = 2500;
    ldo_config.voltage_stable_delay_us = 0;
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_config, &phy_ldo), TAG, "DSI PHY LDO");
    vTaskDelay(pdMS_TO_TICKS(20));
    esp_lcd_dsi_bus_config_t bus_config = {};
    bus_config.bus_id = 0;
    bus_config.num_data_lanes = 2;
    bus_config.phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT;
    bus_config.lane_bit_rate_mbps = 840;
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_config, &dsi_bus), TAG, "DSI bus");
    const esp_lcd_dbi_io_config_t dbi_config = JD9365_PANEL_IO_DBI_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_config, &dbi_io), TAG, "DBI IO");

    // V2/new-panel timings, also used on the V3 production-silicon board.
    esp_lcd_dpi_panel_config_t dpi_config = {};
    dpi_config.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpi_config.dpi_clock_freq_mhz = 70;
    dpi_config.virtual_channel = 0;
    dpi_config.in_color_format = LCD_COLOR_FMT_RGB565;
    dpi_config.out_color_format = LCD_COLOR_FMT_RGB565;
    dpi_config.num_fbs = 1;
    dpi_config.video_timing.h_size = kPanelWidth;
    dpi_config.video_timing.v_size = kPanelHeight;
    dpi_config.video_timing.hsync_back_porch = 20;
    dpi_config.video_timing.hsync_pulse_width = 20;
    dpi_config.video_timing.hsync_front_porch = 40;
    dpi_config.video_timing.vsync_back_porch = 10;
    dpi_config.video_timing.vsync_pulse_width = 4;
    dpi_config.video_timing.vsync_front_porch = 30;
    jd9365_vendor_config_t vendor_config = {};
    vendor_config.init_cmds = jc8012::kPanelInitCommands;
    vendor_config.init_cmds_size = sizeof(jc8012::kPanelInitCommands) /
                                  sizeof(jc8012::kPanelInitCommands[0]);
    vendor_config.mipi_config.dsi_bus = dsi_bus;
    vendor_config.mipi_config.dpi_config = &dpi_config;
    vendor_config.mipi_config.lane_num = 2;
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;
    panel_config.reset_gpio_num = kLcdResetGpio;
    panel_config.vendor_config = &vendor_config;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_jd9365(dbi_io, &panel_config, &panel), TAG, "JD9365");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG, "panel on");
    ESP_RETURN_ON_ERROR(configure_backlight(), TAG, "backlight GPIO23 PWM");
    // Start framebuffer scanout directly. Avoid switching the running stream
    // through the hardware pattern generator while diagnosing DMA output.
    esp_lcd_dpi_panel_event_callbacks_t scanout_callbacks = {};
    scanout_callbacks.on_frame_buf_complete = count_scanout_frames;
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_register_event_callbacks(panel, &scanout_callbacks, nullptr),
                        TAG, "register scanout counter");
    ESP_LOGI(TAG, "JC8012 V2/V3 panel initialized (70 MHz DPI, 840 Mbps DSI); "
                 "starting direct framebuffer output");
    // Verify framebuffer scanout separately from the hardware pattern generator.
    void *frame_buffer_memory = nullptr;
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(panel, 1, &frame_buffer_memory),
                        TAG, "get DPI framebuffer");
    frame_buffer = static_cast<uint16_t *>(frame_buffer_memory);
    for (uint32_t i = 0; i < kPanelWidth * kPanelHeight; ++i) {
        frame_buffer[i] = 0x001F; // RGB565 blue
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(panel, 0, 0, kPanelWidth, kPanelHeight, frame_buffer),
                        TAG, "framebuffer scanout test");
    ESP_LOGI(TAG, "Showing blue framebuffer for one second before LVGL");
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "Blue framebuffer test completed; DMA frames=%lu",
             static_cast<unsigned long>(completed_frames.load(std::memory_order_relaxed)));
    lvgl_port_cfg_t lvgl_config = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_config), TAG, "LVGL init");
    lvgl_port_display_cfg_t display_config = {};
    display_config.io_handle = dbi_io;
    display_config.panel_handle = panel;
    display_config.buffer_size = kPanelWidth * 80;
    display_config.double_buffer = true;
    display_config.hres = kPanelWidth;
    display_config.vres = kPanelHeight;
    display_config.rotation.swap_xy = false;
    display_config.rotation.mirror_x = false;
    display_config.rotation.mirror_y = false;
    display_config.color_format = LV_COLOR_FORMAT_RGB565;
    // LVGL_PORT's generic display path hooks panel-I/O callbacks, but MIPI DSI
    // transfers complete through the DPI panel callbacks. Use its DSI path.
    display_config.flags.buff_dma = false;
    display_config.flags.buff_spiram = true;
    display_config.flags.sw_rotate = true;
    display_config.flags.swap_bytes = false;
    lvgl_port_display_dsi_cfg_t dsi_display_config = {};
    lvgl_display = lvgl_port_add_disp_dsi(&display_config, &dsi_display_config);
    if (lvgl_display == nullptr) return ESP_FAIL;
    lvgl_port_lock(0);
    // Callback registration replaces the full callback set. Preserve LVGL's
    // flush completion notification alongside the DMA scanout counter.
    scanout_callbacks.on_color_trans_done = notify_flush_ready;
    const esp_err_t callback_error = esp_lcd_dpi_panel_register_event_callbacks(
        panel, &scanout_callbacks, lvgl_display);
    if (callback_error != ESP_OK) {
        lvgl_port_unlock();
        return callback_error;
    }
    port_flush = lv_display_get_flush_cb(lvgl_display);
    lv_display_set_flush_cb(lvgl_display, trace_display_flush);
    lv_display_add_event_cb(lvgl_display, trace_refresh, LV_EVENT_REFR_READY, lvgl_display);
    lvgl_port_unlock();
    *display = lvgl_display;
    ESP_LOGI(TAG, "JD9365 active at %lux%lu; PWM backlight on GPIO23",
             static_cast<unsigned long>(kPanelWidth), static_cast<unsigned long>(kPanelHeight));
    return ESP_OK;
}
bool display_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void display_unlock() { lvgl_port_unlock(); }
esp_err_t display_set_rotation(lv_display_t *display, lv_disp_rotation_t rotation)
{
    if (display == nullptr) return ESP_ERR_INVALID_ARG;
    lv_display_set_rotation(display, rotation);
    return ESP_OK;
}
esp_err_t accelerometer_start(accelerometer_callback_t, void *) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sdcard_mount() { return ESP_ERR_NOT_SUPPORTED; }
const char *sdcard_mount_point() { return "/sdcard"; }
// The JC8012 has no separate P4-controlled Wi-Fi power-enable rail. The
// ESP-Hosted SDIO driver owns the C6 reset line (GPIO54) and releases the
// coprocessor when esp_hosted_init() runs.
esp_err_t c6_radio_enable() { return ESP_OK; }
} // namespace hardware
