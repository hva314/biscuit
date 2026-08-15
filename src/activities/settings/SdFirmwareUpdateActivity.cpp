#include "SdFirmwareUpdateActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr char kFirmwarePath[] = "/firmware.bin";
}

void SdFirmwareUpdateActivity::onEnter() {
  Activity::onEnter();

  // Check up front so the user isn't prompted to confirm a file that isn't there.
  if (!Storage.exists(kFirmwarePath)) {
    state = State::NoFile;
  }
  requestUpdate();
}

void SdFirmwareUpdateActivity::onExit() {
  updater.abort();  // safety net in case the activity is popped mid-flash
  Activity::onExit();
}

void SdFirmwareUpdateActivity::loop() {
  switch (state) {
    case State::Confirm:
      if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        const auto err = updater.begin(kFirmwarePath);
        if (err != SdFirmwareUpdater::Error::OK) {
          lastError = err;
          state = State::Failed;
        } else {
          lastRenderedPercent = -1;
          state = State::Updating;
        }
        requestUpdate();
      } else if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        finish();
      }
      break;

    case State::Updating: {
      SdFirmwareUpdater::Error err = SdFirmwareUpdater::Error::OK;
      const auto res = updater.pump(&err);

      if (res == SdFirmwareUpdater::PumpResult::InProgress) {
        // Throttle renders so the e-ink screen isn't refreshed every chunk.
        const size_t total = updater.getTotalSize();
        const int percent = total > 0 ? static_cast<int>(updater.getProcessedSize() * 100 / total) : 0;
        if (percent - lastRenderedPercent >= 5) {
          lastRenderedPercent = percent;
          requestUpdate();
        }
      } else if (res == SdFirmwareUpdater::PumpResult::Done) {
        const auto commitErr = updater.commit();
        if (commitErr == SdFirmwareUpdater::Error::OK) {
          state = State::Finished;
          requestUpdateAndWait();
          ESP.restart();
        } else {
          lastError = commitErr;
          state = State::Failed;
          requestUpdate();
        }
      } else {
        lastError = err;
        state = State::Failed;
        requestUpdate();
      }
      break;
    }

    case State::NoFile:
    case State::Failed:
      if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        finish();
      }
      break;

    case State::Finished:
      break;
  }
}

void SdFirmwareUpdateActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SD_FIRMWARE_UPDATE));

  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - height) / 2;

  switch (state) {
    case State::Confirm: {
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_SD_UPDATE_PROMPT), true, EpdFontFamily::BOLD);
      const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_CONFIRM), "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }

    case State::Updating: {
      const size_t total = updater.getTotalSize();
      const int percent = total > 0 ? static_cast<int>(updater.getProcessedSize() * 100 / total) : 0;

      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATING));

      int y = top + height + metrics.verticalSpacing;
      GUI.drawProgressBar(renderer,
                          Rect{metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding * 2,
                               metrics.progressBarHeight},
                          percent, 100);

      y += metrics.progressBarHeight + metrics.verticalSpacing;
      renderer.drawCenteredText(UI_10_FONT_ID, y, (std::to_string(percent) + "%").c_str());
      break;
    }

    case State::NoFile: {
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_SD_NO_FIRMWARE), true, EpdFontFamily::BOLD);
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }

    case State::Failed: {
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_FAILED), true, EpdFontFamily::BOLD);
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }

    case State::Finished:
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_COMPLETE), true, EpdFontFamily::BOLD);
      break;
  }

  renderer.displayBuffer();
}
