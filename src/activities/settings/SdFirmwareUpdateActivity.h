#pragma once

#include "activities/Activity.h"
#include "network/SdFirmwareUpdater.h"

// Flashes /firmware.bin from the SD card into the OTA partition. This is the
// primary update path for the X3, which has no USB data connection.
class SdFirmwareUpdateActivity final : public Activity {
  enum class State { Confirm, Updating, NoFile, Failed, Finished };

  State state = State::Confirm;
  SdFirmwareUpdater updater;
  SdFirmwareUpdater::Error lastError = SdFirmwareUpdater::Error::OK;
  int lastRenderedPercent = -1;

 public:
  explicit SdFirmwareUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("SdFirmwareUpdate", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == State::Updating; }
  bool skipLoopDelay() override { return state == State::Updating; }
};
