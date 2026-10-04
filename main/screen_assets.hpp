#pragma once

#include <cstddef>
#include <cstdint>

const uint8_t *screen_load_media_jpeg_data();
size_t screen_load_media_jpeg_size();
const uint8_t *screen_settings_jpeg_data();
size_t screen_settings_jpeg_size();
uint32_t screen_artwork_dimension();

const uint8_t *screen_test_card_jpeg_data();
size_t screen_test_card_jpeg_size();
uint32_t screen_test_card_width();
uint32_t screen_test_card_height();
