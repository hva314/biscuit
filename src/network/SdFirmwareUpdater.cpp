#include "SdFirmwareUpdater.h"

#include <Logging.h>

namespace {
constexpr uint8_t ESP_IMAGE_MAGIC = 0xE9;  // first byte of every ESP32 app image header
}

SdFirmwareUpdater::~SdFirmwareUpdater() { abort(); }

SdFirmwareUpdater::Error SdFirmwareUpdater::begin(const char* path) {
  abort();  // reset any previous state

  if (!Storage.exists(path)) {
    return Error::NoFile;
  }
  if (!Storage.openFileForRead("FW", path, file_)) {
    return Error::FileOpenError;
  }

  totalSize_ = file_.size();
  if (totalSize_ < MIN_IMAGE_SIZE) {
    abort();
    return Error::TooSmall;
  }

  // Verify the ESP image magic before erasing anything.
  if (file_.read(buffer_, 1) != 1 || buffer_[0] != ESP_IMAGE_MAGIC) {
    abort();
    return Error::BadMagic;
  }
  if (!file_.seekSet(0)) {
    abort();
    return Error::FileReadError;
  }

  partition_ = esp_ota_get_next_update_partition(nullptr);
  if (partition_ == nullptr) {
    // No second OTA slot in the partition table actually on this device. Note
    // that on the X3 the table is the stock one -- biscuit's partitions.csv is
    // only written over USB, which many X3 units do not expose.
    LOG_ERR("FW", "no OTA partition available for update");
    abort();
    return Error::NoPartition;
  }
  if (totalSize_ > partition_->size) {
    LOG_ERR("FW", "image %u B exceeds OTA partition %u B", static_cast<unsigned>(totalSize_),
            static_cast<unsigned>(partition_->size));
    abort();
    return Error::TooLarge;
  }

  // OTA_SIZE_UNKNOWN avoids a blocking full-partition erase up front; the flash
  // is erased lazily as esp_ota_write() streams. Size vs. partition is checked above.
  const esp_err_t err = esp_ota_begin(partition_, OTA_SIZE_UNKNOWN, &handle_);
  if (err != ESP_OK) {
    LOG_ERR("FW", "esp_ota_begin failed: %s", esp_err_to_name(err));
    abort();
    return Error::FlashError;
  }

  active_ = true;
  processedSize_ = 0;
  return Error::OK;
}

SdFirmwareUpdater::PumpResult SdFirmwareUpdater::pump(Error* outError) {
  if (!active_) {
    if (outError) *outError = Error::FlashError;
    return PumpResult::Error;
  }

  if (processedSize_ >= totalSize_) {
    return PumpResult::Done;
  }

  const size_t remaining = totalSize_ - processedSize_;
  const size_t want = remaining < CHUNK_SIZE ? remaining : CHUNK_SIZE;
  const int n = file_.read(buffer_, want);
  if (n < 0) {
    if (outError) *outError = Error::FileReadError;
    return PumpResult::Error;
  }
  if (n == 0) {
    // Premature EOF: file is shorter than its advertised size.
    if (outError) *outError = Error::FileReadError;
    return PumpResult::Error;
  }

  const esp_err_t err = esp_ota_write(handle_, buffer_, static_cast<size_t>(n));
  if (err != ESP_OK) {
    LOG_ERR("FW", "esp_ota_write failed: %s", esp_err_to_name(err));
    if (outError) *outError = Error::FlashError;
    return PumpResult::Error;
  }

  processedSize_ += static_cast<size_t>(n);
  return processedSize_ >= totalSize_ ? PumpResult::Done : PumpResult::InProgress;
}

SdFirmwareUpdater::Error SdFirmwareUpdater::commit() {
  if (!active_) {
    return Error::FlashError;
  }

  const esp_err_t endErr = esp_ota_end(handle_);
  handle_ = 0;
  active_ = false;
  if (endErr != ESP_OK) {
    LOG_ERR("FW", "esp_ota_end failed: %s", esp_err_to_name(endErr));
    return Error::FlashError;
  }

  const esp_err_t setErr = esp_ota_set_boot_partition(partition_);
  if (setErr != ESP_OK) {
    LOG_ERR("FW", "esp_ota_set_boot_partition failed: %s", esp_err_to_name(setErr));
    return Error::FlashError;
  }

  file_.close();
  return Error::OK;
}

void SdFirmwareUpdater::abort() {
  if (active_) {
    esp_ota_abort(handle_);
    active_ = false;
    handle_ = 0;
  }
  if (file_.isOpen()) {
    file_.close();
  }
  partition_ = nullptr;
  processedSize_ = 0;
  totalSize_ = 0;
}
