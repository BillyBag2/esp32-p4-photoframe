#pragma once

#include "esp_err.h"
#include "lvgl.h"

// Creates the landscape startup dashboard. The 720 px badge stays flush left
// while a live provisioning table fills the remaining right-hand area.
esp_err_t dashboard_start(const lv_image_dsc_t *badge);
