# X3 Support

biscuit runs on the **Xteink X3** as well as the X4. This page is the technical
reference for the X3 work — what's supported, how detection works, and the
flashing model. For the user-facing "how do I flash it" steps, see
[x3-initial-flash-checklist.md](x3-initial-flash-checklist.md).

## Hardware differences (X4 vs X3)

| | X4 | X3 |
|---|---|---|
| SoC | ESP32-C3 | ESP32-C3 |
| Display | 4.26" 800×480 SSD1677 | 3.68" 792×528 SSD1677 (same IC) |
| Display SPI pins | SCLK 8, MOSI 10, CS 21, DC 4, RST 5, BUSY 6 | identical |
| Battery | ADC on GPIO0 | BQ27220 fuel gauge, I²C 0x55 |
| Extra I²C | — | DS3231 RTC (0x68), QMI8658 IMU (0x6B) |
| I²C pins | — | SDA 20, SCL 0 |
| USB | data + power | charging only / factory-locked on many units |
| Power button | GPIO3 | GPIO3 |

## What is implemented

- **Device auto-detection** — `lib/hal/HalGPIO.cpp` fingerprints the I²C bus for
  the X3 peripherals (BQ27220 + DS3231 + QMI8658), scores two passes, caches the
  result in NVS, and supports a manual override. `gpio.deviceIsX3()` is the
  single source of truth everywhere else.
- **Display** — `HalDisplay::begin()` calls `einkDisplay.setDisplayX3()` when
  `deviceIsX3()`, and the geometry getters are runtime (`getDisplayWidth()`,
  `getDisplayHeight()`, `getDisplayWidthBytes()`, `getBufferSize()`) so the
  792×528 panel is driven correctly. Row-stride code (`DirectPixelWriter.h`,
  `ScreenshotUtil.cpp`) uses runtime geometry rather than the static X4
  constants.
- **Battery / USB** — `HalPowerManager` reads the BQ27220 fuel gauge over I²C on
  X3; USB-plug detection reads the fuel-gauge current register.
- **SD firmware update** — `src/network/SdFirmwareUpdater.{h,cpp}` streams an
  SD-card image into the OTA slot via the ESP-IDF `esp_ota_*` API (lazy erase),
  validates the ESP image magic, and switches the boot partition on success.
  - Settings → **Update from SD card** reads `firmware.bin`.
  - Boot-time recovery reads `force_update.bin` before the UI starts.

## Flashing model

| Path | File | Trigger | Who handles it |
|---|---|---|---|
| First install (stock → biscuit) | `update.bin` | hold left button + power | OEM bootloader |
| Update (biscuit → biscuit) | `firmware.bin` | Settings menu | biscuit (`SdFirmwareUpdater`) |
| Recovery (broken biscuit) | `force_update.bin` | power on | biscuit (boot-time check) |

The OEM bootloader handles the **first** flash and expects the app image named
`update.bin`; biscuit's own updater handles every flash after that. See the
flashing guide for step-by-step instructions.

## Caveats

- **Unverified until built.** The X3 code is written but has not been compiled or
  run on hardware; the build happens on the owner's machine.
- **Flash size.** The SD updater reuses the existing OTA slots (app0/app1,
  0x640000 = 6.5 MB each) already used by the WiFi OTA. The app image must fit
  that slot; if it doesn't, use the `slim` / `OMIT_FONTS` build.
- **App-level recovery only.** `force_update.bin` recovery requires biscuit to
  reach the point in `setup()` after SD init. A truly dead image must use the OEM
  bootloader `update.bin` path instead.
- **Buttons / deep-sleep.** Assumed identical to X4 (shared ADC ladder, GPIO3
  power). Still to be verified on hardware.
