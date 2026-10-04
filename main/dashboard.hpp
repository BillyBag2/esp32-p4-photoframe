#pragma once

#include "esp_err.h"
#include "lvgl.h"

// Creates the landscape dashboard and two artwork screens. Vertical swipes
// cycle through them without animation.
esp_err_t dashboard_start(const lv_image_dsc_t *badge,
                          const lv_image_dsc_t *load_media,
                          const lv_image_dsc_t *settings,
                          const char *sd_mount_point);

// Updates the displayed orientation description without changing display rotation.
void dashboard_set_orientation(const char *orientation);
