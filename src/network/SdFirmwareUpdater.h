#pragma once

#include <HalStorage.h>
#include <esp_ota_ops.h>

#include <cstddef>
#include <cstdint>

// Flashes a firmware image from the SD card into the OTA partition.
// SD-sourced counterpart to OtaUpdater (which downloads over HTTPS). Needed
// because the X3 has no USB data connection, so SD is the only update path.
//
// Pump-based so the activity loop() stays responsive and can render progress
// between chunks (as opposed to blocking for the whole flash).
class SdFirmwareUpdater {
 public:
  enum class Error : uint8_t {
    OK = 0,
    NoFile,        // path does not exist on the SD card
    FileOpenError, // could not open the file
    TooSmall,      // image smaller than the minimum valid app image
    TooLarge,      // image larger than the OTA partition
    BadMagic,      // not an ESP32 image (missing 0xE9 header byte)
    NoPartition,   // no OTA partition available
    FileReadError, // SD read failed mid-flash
    FlashError,    // esp_ota_* call failed
  };

  enum class PumpResult : uint8_t { InProgress, Done, Error };

  SdFirmwareUpdater() = default;
  ~SdFirmwareUpdater();

  SdFirmwareUpdater(const SdFirmwareUpdater&) = delete;
  SdFirmwareUpdater& operator=(const SdFirmwareUpdater&) = delete;

  // Open + validate the image and begin the OTA transaction. No data flashed yet.
  Error begin(const char* path);
  // Flash up to one chunk. Returns InProgress until all bytes are written;
  // on failure sets *outError.
  PumpResult pump(Error* outError = nullptr);
  // Finalize a fully-flashed image: verify, set boot partition, close. Call only
  // after pump() returns Done.
  Error commit();

  // Abort a partially-flashed image and release resources.
  void abort();

  size_t getProcessedSize() const { return processedSize_; }
  size_t getTotalSize() const { return totalSize_; }

 private:
  static constexpr size_t CHUNK_SIZE = 4096;
  static constexpr size_t MIN_IMAGE_SIZE = 64 * 1024;  // below this can't be a valid app image

  HalFile file_;
  const esp_partition_t* partition_ = nullptr;
  esp_ota_handle_t handle_ = 0;
  uint8_t buffer_[CHUNK_SIZE];
  size_t totalSize_ = 0;
  size_t processedSize_ = 0;
  bool active_ = false;
};
