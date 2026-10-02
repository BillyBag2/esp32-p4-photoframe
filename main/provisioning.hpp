#pragma once

#include "esp_err.h"

// Starts Wi-Fi or, when credentials are absent, ESP BLE provisioning.
// The advertised name is RetroScope_XXXXXX, using the final three MAC bytes.
esp_err_t provisioning_start();
