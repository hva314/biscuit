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

| Path | File | Trigger | Who handles it | Available |
|---|---|---|---|---|
| First install (stock → biscuit) | `update.bin` | hold left button + power | OEM bootloader | **Once only** — see below |
| Update (biscuit → biscuit) | `firmware.bin` | Settings menu | biscuit (`SdFirmwareUpdater`) | Always, size permitting |
| Recovery (broken biscuit) | `force_update.bin` | power on | biscuit (boot-time check) | Only if `setup()` reaches SD init |

> **The OEM bootloader is a one-way door.** It lives in Xteink's *stock
> firmware*, not in a persistent bootloader. Installing biscuit overwrites it,
> and the left-button + power gesture stops doing anything from then on — the
> device just boots biscuit normally. Once biscuit is installed, **Settings →
> Update from SD card is the only way to change the firmware** on a unit with no
> USB data lines. Plan accordingly: an image too large for the OTA slot cannot be
> installed at all.

`force_update.bin` is not an escape hatch from that. It calls the same
`SdFirmwareUpdater` (`src/main.cpp`), so it fails identically on an oversized
image — and it **deletes the file** on failure to avoid a boot loop, so it is a
silent one-shot.

## Caveats

- **Build + smoke test.** Compiled with PlatformIO (isolated venv, Python 3.12)
  and flashed onto the X3 via the OEM `update.bin` bootloader; a walkaround
  confirmed core functions (display, navigation, apps) work. Settings →
  **Update from SD card** has since been exercised on hardware too — it works,
  subject to the size limit below.
- **Flash size — the OTA slot is smaller than this repo's partition table says.**
  `partitions.csv` declares app0/app1 at 0x640000 (6,553,600 B), but **that table
  is never written to an X3**. It reaches the flash only via USB `esptool`, and
  many X3 units expose no USB data lines at all (some ship with a 2-pin charge
  connector). The device therefore keeps Xteink's **stock** partition table
  forever, and `esp_ota_get_next_update_partition()` returns a slot whose size we
  do not control and cannot read without USB.

  Measured on a real X3 via Settings → Update from SD card:

  | Image | Size | Result |
  |---|---|---|
  | `slim` + HTTP Monitor | 6,501,280 B | **rejected** |
  | `slim` + HTTP Monitor, `-DOMIT_FONTS` | 3,729,888 B | **accepted** |

  So the real ceiling on that unit is somewhere in **[3,729,888, 6,501,280)** —
  it has not been narrowed further. Do not assume 6.25 MB is available. Note the
  6,484,992 B `slim` image that unit ran previously arrived through the OEM
  bootloader, a different mechanism, and says nothing about the OTA slot size.

  Budget conservatively, keep `slim`, and if an update fails, suspect size first.
- **App-level recovery only.** `force_update.bin` recovery requires biscuit to
  reach the point in `setup()` after SD init. A truly dead image must use the OEM
  bootloader `update.bin` path instead.
- **Deep-sleep / battery / button ADC.** Spot-checked in a walkaround but not
  exhaustively verified (fuel-gauge calibration, deep-sleep wake, per-button ADC
  mapping). Assumed identical to X4 topology.
