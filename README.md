# ESP32-P4 Photo Frame

ESP-IDF firmware for ESP32-P4 photo frame boards. Shared application code
provides the LVGL dashboard, photo badge, storage reporting, orientation logic,
and Wi-Fi/BLE provisioning. A small hardware abstraction layer keeps board
drivers and pin choices out of the application.

## Hardware support

- M5Stack Tab5 HAL uses the official `espressif/m5stack_tab5` BSP for display,
  touch, SDMMC, and its I2C bus
- BMI270 sensor-hub adapter lives under `main/hardware/common` and accepts an
  I2C bus supplied by a target; other boards with the same IMU can reuse it
- ESP32-P4-Panel-ETH-2RO and JC8012P4A1C_I_W_Y have compile-only HAL stubs until
  their display, storage, sensor, and radio wiring is defined
- LVGL 9.6 (resolved by the component manager)
- ESP32-P4 hardware JPEG driver (`esp_driver_jpeg`)
- SD card mounted at `/sdcard`
- Four-way orientation with a 0.2 g hysteresis region
- ESP32-C6 Wi-Fi/BLE via the shared ESP-Hosted provisioning path; SDIO pins and
  reset wiring are target defaults
- ESP BLE Provisioning Security 1; service name `RetroScope_XXXXXX`

## Target architecture

The firmware selects hardware through `RETROSCOPE_TARGET`. Shared dashboard,
storage reporting, orientation logic, and ESP-Hosted C6 Wi-Fi/BLE provisioning
stay in the application layer. Board display, SD card, I2C bus, accelerometer,
and C6 power control belong to a target HAL. The BMI270 sensor-hub adapter is
shared under `main/hardware/common` and can be reused by targets that provide
an I2C bus. The two wish-list targets currently have compile-only stubs pending
confirmed pin, display, memory, and peripheral details.

The development proof-of-possession string is `myRetroScope`. Replace it with a
per-device secret before production deployment.

## Build

Use an ESP-IDF 6.1 environment. The default target is `tab5`:

```text
idf.py -B build-tab5 -D RETROSCOPE_TARGET=tab5 set-target esp32p4 build
```

Flash and monitor that build with `idf.py -B build-tab5 flash monitor`.

Select either future target with a separate build directory so each target has
an independent generated sdkconfig:

```text
idf.py -B build-panel -D RETROSCOPE_TARGET=esp32_p4_panel_eth_2ro set-target esp32p4 build
idf.py -B build-jc8012 -D RETROSCOPE_TARGET=jc8012p4a1c_i_w_y set-target esp32p4 build
```

Those targets currently compile against unsupported-hardware HAL stubs; their
sdkconfig defaults leave board wiring and memory details for confirmation.
Configuration is split into common application defaults, common P4/C6 defaults,
and per-target defaults. The component manager resolves libraries declared in
`main/idf_component.yml`.

## ESP32-C6 firmware

The on-board C6 must run an ESP-Hosted slave image compatible with the resolved
ESP-Hosted 2.x component. Older Tab5 factory/demo C6 images using ESP-Hosted 1.x
are not RPC-compatible. Provision with Espressif's ESP BLE Provisioning mobile
app using the `RetroScope_XXXXXX` service name printed on the serial console.

## Google photo picker

Keys can be tested using `photo_picker_test\photo_picker.py`

Create a Google OAuth 2.0 Client ID. Choose a TV style device.

See: [Google Cloud Console](https://console.cloud.google.com/apis/credentials?project=linux-share)

Create a Google Cloud project, enable the Google Photos Library API, and create an OAuth 2.0 client ID . Download the credentials file and place it in the `photo_picker_test\token.json` file.

## TODO

- [ ] Add Google token. Compile time?
- [ ] Initialize Google Photos API.
- [ ] BLE provisioning uses per-device proof-of-possession string. Generate and
    store in flash.
- [ ] Add Google photo picker to the ESP32-P4 firmware.
- [ ] Display on screen the provisioning name and the proof-of-possession string.
- [ ] Display on screen network state. IP address and Wi-Fi signal strength.
- [ ] Add a QR code for the provisioning URL?
- [ ] Add a QR code for the Google photo picker URL.
- [ ] Add a QR code for Apple and Google provisioning apps.
- [ ] Add a working directory to the sd card at `/sdcard/photos`.
- [ ] Publish manual to GitHub Pages.

## Status info

- [x] SSID
- [ ] IP address
- [ ] Wi-Fi signal strength (RSSI)
- [x] BLE provisioning name
- [x] BLE proof-of-possession string
- [ ] Accelerometer orientation (normal or inverted, landscape or portrait)
- [ ] SD card mount status 
- [ ] SD card free space (MB)
- [ ] SD card used space (MB)
- [ ] SD card total space (MB)
- [ ] CPU temperature (°C)
- [ ] CPU frequency (MHz)
- [ ] CPU usage (%)
- [ ] Memory usage (%)
- [ ] Free heap (Kbytes)
- [ ] Free PSRAM (kbytes)
- [ ] Free internal storage (bytes)
- [ ] Battery voltage (V)
- [ ] Battery charge percentage
- [ ] Battery charging status (charging, discharging, external power)
- [ ] Battery charging current (+/-mA)

### Status items needing additional managed components or hardware

- Battery voltage, charge percentage, charging state, and charging current need a compatible battery monitor/charger interface and corresponding driver support.

## Done

[x] ESP BlueTooth Provisioning.
[x] Join Wi-Fi network.
