#pragma once

#include "esp_err.h"

// Starts Wi-Fi or, when credentials are absent, ESP BLE provisioning.
// The service name is derived from the device MAC address.
esp_err_t provisioning_start();

