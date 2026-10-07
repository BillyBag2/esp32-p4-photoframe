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
- JC8012 uses a JD9365 MIPI-DSI display HAL; its storage, touch, sensor, and
  C6 power-control integration are not implemented yet
- ESP32-P4-Panel-ETH-2RO remains on an unsupported-hardware HAL stub
- LVGL 9.6 (resolved by the component manager)
- ESP32-P4 hardware JPEG driver (`esp_driver_jpeg`)
- SD card mounted at `/sdcard`
- Four-way orientation with a 0.2 g hysteresis region
- ESP32-C6 Wi-Fi/BLE via the shared ESP-Hosted provisioning path; SDIO pins and
  reset wiring are target defaults
- ESP BLE Provisioning Security 1; service name `RetroScope_XXXXXX`

## Hardware differences

| Feature | Tab5 | JC8012P4A1C_I_W_Y |
| --- | --- | --- |
| Flash | 16 MB SPI flash | 16 MB SPI NOR flash (configured QIO, 80 MHz) |
| RAM / PSRAM | 32 MB Octal PSRAM | 32 MB Octal PSRAM (configured HEX mode, 200 MHz) |
| Display | MIPI/RGB display via Tab5 BSP, 1280 × 720 | 10.1-inch JD9365 MIPI-DSI, 800 × 1280 native; configured for landscape in the app |
| Display reset | BSP-controlled | GPIO27 |
| Backlight | BSP-controlled | GPIO23, LEDC PWM at 5 kHz (currently full brightness) |
| DSI PHY power | BSP-controlled | ESP32-P4 LDO channel 3 at 2.5 V |
| SD card | External, supported by BSP | TF/microSD connector present; firmware mount not implemented |
| Touch | Supported by BSP | GSL3680 touch hardware; firmware integration not implemented |
| Touch I²C / controls | BSP-controlled | SDA GPIO7, SCL GPIO8, interrupt GPIO21, reset GPIO22 |
| RTC | Available through Tab5 BSP | RX8025T with RTC backup battery; firmware integration not implemented |
| RTC I²C | BSP-controlled | SDA GPIO7, SCL GPIO8 (shared bus) |
| SD card pins | BSP-controlled | CLK GPIO43, CMD GPIO44, D0–D3 GPIO39–42; firmware mount not implemented |
| IMU | BMI270 | Not confirmed / not integrated |
| ESP32-C6 | Yes, managed by BSP | Onboard C6 via ESP-Hosted SDIO: CLK GPIO18, CMD GPIO19, D0 GPIO14, D1 GPIO15, D2 GPIO16, D3 GPIO17; active-high reset GPIO54 |

### Tab5

 [M5Stack Tab5](https://docs.m5stack.com/en/core/Tab5) 

### JC8012P4A1C_I_W_Y

- [Github](https://github.com/guitionofficial/P4-series/tree/master/JC8012P4A1C_I_W_Y/JC8012P4A1C_I_W_Y)

#### Updating C6 firmware on a JC8012P4A1C_I_W_Y

Try using...
[espHome Installer](https://jtenniswood.github.io/espcontrol/getting-started/c6-recovery?device=guition-esp32-p4-jc8012p4a1-v3&utm_source=chatgpt.com)


## Target architecture

The firmware selects hardware through `RETROSCOPE_TARGET`. Shared dashboard,
storage reporting, orientation logic, and ESP-Hosted C6 Wi-Fi/BLE provisioning
stay in the application layer. Board display, SD card, I2C bus, accelerometer,
and C6 power control belong to a target HAL. The BMI270 sensor-hub adapter is
shared under `main/hardware/common` and can be reused by targets that provide
an I2C bus. The JC8012 display HAL is implemented; its remaining board
peripherals and the Panel ETH target still need integration and verification.

The development proof-of-possession string is `myRetroScope`. Replace it with a
per-device secret before production deployment.

## Build

Use an ESP-IDF 6.1 environment. The default target is `tab5`:

The managed Tab5 BSP dependency follows `RETROSCOPE_TARGET` automatically;
it does not depend on a setting in an existing `sdkconfig` file.

```text
idf.py -B build-tab5 -D RETROSCOPE_TARGET=tab5 set-target esp32p4 build
```

Flash and monitor that build with `idf.py -B build-tab5 flash monitor`.

Select either additional target with a separate build directory so each target has
an independent generated sdkconfig:

```text
idf.py -B build-panel -D RETROSCOPE_TARGET=esp32_p4_panel_eth_2ro set-target esp32p4 build
idf.py -B build-jc8012 -D RETROSCOPE_TARGET=jc8012p4a1c_i_w_y set-target esp32p4 build
```

The JC8012 target uses the newer JD9365 panel initialization and 800x1280
timings used by [EspControl's V3 configuration](https://github.com/jtenniswood/espcontrol/blob/main/devices/guition-esp32-p4-jc8012p4a1-v3/device/device.yaml):
70 MHz DPI clock and 840 Mbps DSI lanes. V3 identifies production ESP32-P4
v3.x silicon and uses the same newer-panel settings as V2. The original V1
panel needs different settings. Startup diagnostics show a blue framebuffer
for one second before LVGL draws the dashboard. The hardware colour-bar
generator is bypassed to test framebuffer scanout directly. Completed DMA
frames, the first landscape LVGL flushes, and refresh completions are logged
to help locate a blank dashboard. Other panel revisions may need
different initialization and timings. Panel ETH still compiles against an
unsupported-hardware HAL stub. Other unimplemented peripherals remain
unavailable on JC8012.
Configuration is split into common application defaults, common P4/C6 defaults,
and per-target defaults. The component manager resolves libraries declared in
`main/idf_component.yml`.

## ESP32-C6 firmware

The on-board C6 must run an ESP-Hosted slave image matching the version of the
resolved `espressif/esp_hosted` host component in `dependencies.lock`. Sharing
the same major version is insufficient: the JC8012 factory image reporting
2.3.2 rejects the Bluetooth controller initialization RPC from the 2.12.x host
with `ESP_ERR_NOT_SUPPORTED`. Update the C6 slave firmware before using BLE
provisioning with that host. Startup logs report the C6 firmware version and
any unsupported RPC; the display remains running if radio startup fails.
Older Tab5 factory/demo C6 images using ESP-Hosted 1.x are also not
RPC-compatible. Provision with Espressif's ESP BLE Provisioning mobile
app using the `RetroScope_XXXXXX` service name printed on the serial console.

## Google photo picker

Keys can be tested using `photo_picker_test\photo_picker.py`

Create a Google OAuth 2.0 Client ID. Choose a TV style device.

See: [Google Cloud Console](https://console.cloud.google.com/apis/credentials?project=linux-share)

Create a Google Cloud project, enable the Google Photos Library API, and create an OAuth 2.0 client ID . Download the credentials file and place it in the `photo_picker_test\token.json` file.

## TODO

- [ ] BLE provisioning uses per-device proof-of-possession string. Generate and
    store in flash.
- [x] Display on screen the provisioning name and the proof-of-possession string.
- [ ] Display on screen network state. IP address and Wi-Fi signal strength.
- [ ] Add a QR code for the provisioning URL?
- [ ] Add a QR code for the Google photo picker URL.
- [ ] Add a QR code for Apple and Google provisioning apps.
- [ ] Add a working directory to the sd card at `/sdcard/photos`.
- [ ] Publish manual to GitHub Pages.
- [ ] Add a photo frame screen to the dashboard.
- [ ] Website for photoframe picker.

## Controls

- [ ] SDCard appears as a storage device.
- [ ] Screen brightness.
- [ ] Back to BLE provisioning mode.

## Status info

- [x] SSID
- [x] IP address
- [x] Wi-Fi signal strength (RSSI)
- [x] BLE provisioning name
- [x] BLE proof-of-possession string
- [x] Accelerometer orientation (normal or inverted, landscape or portrait)
- [ ] SD card mount status
- [ ] SD card free space (MB)
- [ ] SD card used space (MB)
- [ ] SD card total space (MB)
- [x] CPU temperature (°C)
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
- [ ] Mac address of the ESP32-P4
- [ ] Mac address of the ESP32-C6
- [ ] Hardware version of the ESP32-P4

### Status items needing additional managed components or hardware

- Battery voltage, charge percentage, charging state, and charging current need a compatible battery monitor/charger interface and corresponding driver support.

## Done

[x] ESP BlueTooth Provisioning.
[x] Join Wi-Fi network.
