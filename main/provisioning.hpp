#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef struct {
    bool ble_advertising;
    bool wifi_connected;
    char wifi_state[24];
    char ssid[33];
    char ip_address[16];
    char ble_name[18];
    char proof_of_possession[16];
} provisioning_status_t;

// Starts Wi-Fi or, when credentials are absent, ESP BLE provisioning.
// The advertised name is RetroScope_XXXXXX, using the final three MAC bytes.
esp_err_t provisioning_start();

// Copies the current connection and provisioning state for the dashboard.
void provisioning_get_status(provisioning_status_t *status);
