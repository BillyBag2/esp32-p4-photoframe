# ESP32-P4 Photo Frame (M5Stack Tab5)

ESP-IDF firmware foundation for the M5Stack Tab5. It initializes the 5-inch
MIPI-DSI display with LVGL 9, mounts the microSD card, uses the BMI270
accelerometer to rotate the display, and exposes Wi-Fi provisioning over BLE.

## Hardware support

- Official `espressif/m5stack_tab5` BSP for display, touch, SDMMC and BMI270
- LVGL 9.6 (resolved by the component manager)
- ESP32-P4 hardware JPEG driver (`esp_driver_jpeg`)
- SD card mounted at `/sdcard`
- Four-way orientation with a 0.2 g hysteresis region
- ESP32-C6 Wi-Fi/BLE via ESP-Hosted over the Tab5 SDIO wiring
- ESP BLE Provisioning Security 1; service name `PROV_xxxxxx`

The development proof-of-possession string is `tab5-frame`. Replace it with a
per-device secret before production deployment.

## Build

Use an ESP-IDF 6.1 environment (the BSP supports IDF 5.4 or newer):

```text
idf.py set-target esp32p4
idf.py build
idf.py flash monitor
```

The component manager downloads and locks the BSP and libraries declared in
`main/idf_component.yml`. Configuration is split into:

- `sdkconfig.defaults` — common LVGL and provisioning settings
- `sdkconfig.defaults.esp32p4` — P4, PSRAM, hardware/radio and SDIO settings
- `sdkconfig.defaults.m5stack_tab5` — Tab5 BSP settings

If an old `sdkconfig` predates these files, remove it or run
`idf.py fullclean set-target esp32p4` once so ESP-IDF regenerates it.

## ESP32-C6 firmware

The on-board C6 must run an ESP-Hosted slave image compatible with the resolved
ESP-Hosted 2.x component. Older Tab5 factory/demo C6 images using ESP-Hosted 1.x
are not RPC-compatible. Provision with Espressif's ESP BLE Provisioning mobile
app using the service name printed on the serial console.

