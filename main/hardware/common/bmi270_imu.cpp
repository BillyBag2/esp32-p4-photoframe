#include "hardware/common/bmi270_imu.hpp"

#include "bmi270.h"
#include "iot_sensor_hub.h"
#include "sensor_type.h"

namespace hardware::common {
namespace {

accelerometer_callback_t s_callback;
void *s_context;

void sensor_event(void *, sensor_event_base_t, int32_t event_id, void *event_data)
{
    if (event_id != SENSOR_ACCE_DATA_READY || event_data == nullptr || s_callback == nullptr) {
        return;
    }

    // sensor_hub delivers one sensor_data_t per event (not a sensor_data_group_t).
    const auto *data = static_cast<const sensor_data_t *>(event_data);
    s_callback(data->acce.x, data->acce.y, data->acce.z, s_context);
}

} // namespace

esp_err_t bmi270_start(i2c_master_bus_handle_t bus, uint8_t address,
                       accelerometer_callback_t callback, void *context)
{
    if (bus == nullptr || callback == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    sensor_config_t config = {};
    config.bus = bus;
    config.addr = address;
    config.type = IMU_ID;
    config.mode = MODE_POLLING;
    config.min_delay = 250;
    sensor_handle_t sensor = nullptr;
    esp_err_t err = iot_sensor_create("sensor_hub_bmi270", &config, &sensor);
    if (err != ESP_OK) {
        return err;
    }

    s_callback = callback;
    s_context = context;
    err = iot_sensor_handler_register(sensor, sensor_event, nullptr);
    if (err != ESP_OK) {
        return err;
    }
    return iot_sensor_start(sensor);
}

} // namespace hardware::common
