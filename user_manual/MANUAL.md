# RetroScope user manual

## Supported Hardware

| Hardware               | Notes     |
|------------------------|-----------|
| M5Stack Tab5           | WIP       |
| ESP32-P4-Panel-ETH-2RO | Wish list |
| JC8012P4A1C_I_W_Y      | Wish list |

The firmware selects hardware through `RETROSCOPE_TARGET`. Shared dashboard,
storage reporting, orientation logic, and ESP-Hosted C6 Wi-Fi/BLE provisioning
stay in the application layer. Board display, SD card, I2C bus, accelerometer,
and C6 power control belong to a target HAL. The BMI270 sensor-hub adapter is
shared under `main/hardware/common` and can be reused by targets that provide
an I2C bus. The two wish-list targets currently have compile-only stubs pending
confirmed pin, display, memory, and peripheral details.

## SDCard preparation

Use up to 32GB SDCard formatted as FAT32. The required directory structure will be created automatically. Other files on the SDCard will be ignored. The following directories will be created:

- RetroScope
  - Downloads
  - FramePhotos
  - Thumbnails

RetroScope is designed to have the SD Card permanently inserted. For some hardware the card is not removable.

Add `token.json` to the `RetroScope` directory. This file is created by the Google photo picker and contains the OAuth2 access token for the Google Photos API. Use a developer token with a limited number of named users.

## Connecting to your Wi-Fi

Download the latest ESP32 provisioning app from the Google Play Store or Apple
App Store. Open the app and follow the instructions to connect your device to a
Wi-Fi network.

Download Esp32 provisioning app from...

- [Google Play Store](https://play.google.com/store/apps/details?id=com.espressif.provble)
- [Apple App Store](https://apps.apple.com/us/app/esp-ble-provisioning/id1473590141)

The device appears as `RetroScope_XXXXXX` in the app. The final six hexadecimal
characters are derived from the device MAC address.

At the time of writing the proof-of-possession string is `myRetroScope`.
