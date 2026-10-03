#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "hardware/hal/platform_hal.hpp"

namespace hardware::common {

esp_err_t bmi270_start(i2c_master_bus_handle_t bus, uint8_t address,
                       accelerometer_callback_t callback, void *context);

} // namespace hardware::common
