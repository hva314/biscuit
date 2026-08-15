# X3 Flashing Guide

## What this is (in plain words)

The Xteink X3's USB port is **locked at the factory** on many units (especially
AliExpress ones) — it charges the battery but won't let a computer talk to it. So
the normal "flash over a USB cable" way of installing firmware doesn't work.

We found the way in: the X3 has a **built-in bootloader** (in its original
software) that can install a new firmware straight from the MicroSD card, with no
computer and no USB cable. This guide tells you exactly how.

There are three procedures here — use the one that matches your situation:

| You want to... | File on the SD card | How you start it |
|---|---|---|
| Install biscuit for the **first time** (device still has its original software) | `update.bin` | Hold **left button + power** together |
| Update biscuit to a newer version | `firmware.bin` | Settings → **Update from SD card** |
| Recover a biscuit that won't boot | `force_update.bin` | Just turn it on — it flashes by itself |

> The file name is **exact**. Put the file at the **top level** of the card — not
> inside any folder. `update.bin` is not the same as `firmware.bin`.

---

## Before you start

- The **X3** device (its original software can still be on it — that's fine).
- A **MicroSD card**, 8–32 GB, formatted **FAT32** (most cards already are).
- The correct file (`update.bin`, `firmware.bin`, or `force_update.bin` — see above).
- A way to put that file on the card: a computer with an SD slot, or a small USB
  card reader.

---

## First-time install (device still has the original software)

This is the step that gets biscuit onto the X3 for the very first time.

1. On your computer, copy the firmware file onto the SD card and name it
   **`update.bin`**. It must be at the **top level** of the card, and the name
   must be exactly `update.bin` — not `firmware.bin`, not `update (1).bin`.
2. Eject the card properly, then put it into the X3.
3. Plug the X3 into USB power (a phone charger is fine).
4. **Hold the left-side button and the power button at the same time**, and keep
   holding both.
5. Keep holding until the **bootloader screen** appears. It finds `update.bin` by
   itself and flashes it — you don't need to press anything else.
6. Wait for it to finish and reboot. biscuit is now installed.

> **If it just boots normally instead of flashing:** the file is probably not at
> the top level of the card, or the name isn't exactly `update.bin`. Check both
> and try again.
>
> **Heads-up:** this installs biscuit, but it does **not** unlock the USB port.
> That's fine — from now on you update using the SD card (below), never USB.

---

## Updating biscuit (already running)

Once biscuit is on the X3, update it like this:

1. Copy the new firmware to the SD card as **`firmware.bin`** (top level).
2. Put the card in the X3 and turn it on.
3. Open **Settings → Update from SD card**.
4. Confirm. A progress bar runs, then the device reboots into the new version.

---

## Recovering a biscuit that won't boot

If a bad update leaves the device stuck on a blank or broken screen:

1. Copy the good firmware to the SD card as **`force_update.bin`** (top level).
2. Put the card in the X3 and turn it on.
3. biscuit notices the file before anything else starts, re-flashes
   automatically, then reboots. No button presses needed.

---

## Where this came from (what we researched)

We checked how three other open-source firmwares for this device handle flashing,
so biscuit follows the same conventions:

| Firmware | Flashing method |
|---|---|
| **Papyrix** | USB flasher; SD `firmware.bin` (menu) + `force_update.bin` (boot) |
| **Pixelpaper** | OTA over USB via a website |
| **CrossPoint** (biscuit's parent) | Web flasher + `esptool` + an unlocker tool for locked units |

The `update.bin` + button-hold bootloader method is documented in Xteink's own
flashing guide and works on USB-locked X3 units — which is why it's our
first-flash path.
