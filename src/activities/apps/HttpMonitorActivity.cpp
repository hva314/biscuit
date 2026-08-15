#include "HttpMonitorActivity.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstring>

#include "HttpMonitorLayout.h"
#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/RadioManager.h"

namespace {

std::string trim(const std::string& s) {
  const size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  const size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

// Null-terminated copy into a fixed buffer, truncating (not overflowing) if needed.
void copyBounded(char* dst, size_t dstSize, const char* src) {
  strncpy(dst, src, dstSize - 1);
  dst[dstSize - 1] = '\0';
}

}  // namespace

// ----------------------------------------------------------------
// Lifecycle
// ----------------------------------------------------------------

void HttpMonitorActivity::onEnter() {
  Activity::onEnter();
  loadConfig();
  requestUpdate();
}

void HttpMonitorActivity::onExit() { Activity::onExit(); }

void HttpMonitorActivity::loadConfig() {
  configError.clear();
  if (HttpMonitorConfig::loadFromSd(config, configError)) {
    state = IDLE;
    pollIntervalMs = static_cast<unsigned long>(config.intervalSec) * 1000UL;
    framesUntilClean = (config.fullRefreshEvery > 0) ? config.fullRefreshEvery : 1;
  } else {
    state = NO_CONFIG;
  }
}

// ----------------------------------------------------------------
// Loop
// ----------------------------------------------------------------

void HttpMonitorActivity::loop() {
  // Back always wins, so input stays responsive even while a fetch is due.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (state == NO_CONFIG) {
    return;
  }

  if (state == IDLE) {
    lastPollMs = millis();
    state = FETCHING;
    requestUpdate(true);
    fetch();
    return;
  }

  // Manual refresh
  if ((state == SHOWING || state == ERROR) &&
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    lastPollMs = millis();
    state = FETCHING;
    requestUpdate(true);
    fetch();
    return;
  }

  // Scroll (only meaningful once a dashboard is showing)
  if (state == SHOWING) {
    buttonNavigator.onNext([this] {
      scrollOffset++;
      requestUpdate();
    });
    buttonNavigator.onPrevious([this] {
      if (scrollOffset > 0) scrollOffset--;
      requestUpdate();
    });
  }

  // Timed poll
  if (state == SHOWING || state == ERROR) {
    const unsigned long now = millis();
    if (now - lastPollMs >= pollIntervalMs) {
      lastPollMs = now;
      state = FETCHING;
      requestUpdate(true);
      fetch();
    }
  }
}

// ----------------------------------------------------------------
// Fetch
// ----------------------------------------------------------------

void HttpMonitorActivity::fetch() {
  if (WiFi.status() != WL_CONNECTED) {
    RADIO.ensureWifi();
    startActivityForResult(
        std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
        [this](const ActivityResult& result) {
          if (result.isCancelled || WiFi.status() != WL_CONNECTED) {
            {
              RenderLock lock(*this);
              fetchError = "WiFi not connected";
              httpStatusCode = 0;
              state = ERROR;
            }
            requestUpdate();
          } else {
            fetch();  // retry now that WiFi is up
          }
        });
    return;
  }

  HTTPClient http;
  http.begin(config.url.c_str());
  http.setTimeout(config.timeoutMs);

  if (!config.authHeader.empty()) {
    const size_t colon = config.authHeader.find(':');
    if (colon != std::string::npos) {
      const std::string headerName = trim(config.authHeader.substr(0, colon));
      const std::string headerValue = trim(config.authHeader.substr(colon + 1));
      if (!headerName.empty()) {
        http.addHeader(headerName.c_str(), headerValue.c_str());
      }
    }
  }

  // Everything below is computed into LOCAL state only — `render()` runs on its
  // own FreeRTOS task and reads `dashboard`/`state`/`fetchError`/`httpStatusCode`
  // without a lock (ActivityManager.cpp:56: "the loop() method must be
  // responsible for acquir[ing one] if needed"). Committing all of it under a
  // single RenderLock at the end (not held across this blocking HTTP call) is
  // what makes the commit atomic from render()'s point of view.
  const int code = http.GET();
  bool ok = false;
  std::string errMsg;
  Dashboard next;

  if (code == HTTP_CODE_OK) {
    const int64_t reportedLength = http.getSize();
    if (reportedLength <= 0 || reportedLength > MAX_BODY_BYTES) {
      errMsg = "Response too large or unknown length";
    } else {
      JsonDocument filter;
      filter["title"] = true;
      filter["updated"] = true;
      filter["sections"][0]["heading"] = true;
      filter["sections"][0]["rows"][0]["label"] = true;
      filter["sections"][0]["rows"][0]["value"] = true;
      filter["sections"][0]["rows"][0]["bar"] = true;
      filter["sections"][0]["rows"][0]["alert"] = true;
      filter["alerts"][0] = true;

      JsonDocument doc;
      const DeserializationError err =
          deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
      if (err) {
        errMsg = std::string("JSON parse failed: ") + err.c_str();
        LOG_ERR("HTTPMON", "JSON parse failed: %s", err.c_str());
      } else {
        // ---- apply response, enforcing every cap before it lands in `next` ----
        const char* title = doc["title"] | config.title.c_str();
        copyBounded(next.title, sizeof(next.title), title);

        const char* updated = doc["updated"] | "";
        copyBounded(next.updated, sizeof(next.updated), updated);

        if (doc["alerts"].is<JsonArray>()) {
          for (JsonVariant a : doc["alerts"].as<JsonArray>()) {
            if (static_cast<int>(next.alerts.size()) >= MAX_ALERTS) break;
            if (!a.is<const char*>()) continue;
            std::array<char, 48> text{};
            copyBounded(text.data(), text.size(), a.as<const char*>());
            next.alerts.push_back(text);
          }
        }

        if (doc["sections"].is<JsonArray>()) {
          for (JsonObject secObj : doc["sections"].as<JsonArray>()) {
            if (static_cast<int>(next.sections.size()) >= MAX_SECTIONS) break;
            Section section;
            const char* heading = secObj["heading"] | "";
            copyBounded(section.heading, sizeof(section.heading), heading);

            if (secObj["rows"].is<JsonArray>()) {
              for (JsonObject rowObj : secObj["rows"].as<JsonArray>()) {
                if (static_cast<int>(section.rows.size()) >= MAX_ROWS_PER_SECTION) break;
                Row row;
                const char* label = rowObj["label"] | "";
                const char* value = rowObj["value"] | "";
                copyBounded(row.label, sizeof(row.label), label);
                copyBounded(row.value, sizeof(row.value), value);
                // Bar is 0..100 per the documented contract; anything else
                // (negative, >100, or the wrong type) is treated as absent
                // rather than trusted verbatim.
                const int barRaw = rowObj["bar"] | -1;
                row.bar = (barRaw >= 0 && barRaw <= 100) ? barRaw : -1;
                row.alert = rowObj["alert"] | false;
                section.rows.push_back(row);
              }
            }
            next.sections.push_back(section);
          }
        }

        ok = true;
      }
    }
  } else {
    errMsg = (code > 0) ? ("HTTP " + std::to_string(code)) : "Connection failed";
    LOG_ERR("HTTPMON", "Fetch failed: %d", code);
  }

  http.end();

  {
    RenderLock lock(*this);
    httpStatusCode = code;
    if (ok) {
      dashboard = next;
      state = SHOWING;
      scrollOffset = 0;
    } else {
      fetchError = errMsg;
      state = ERROR;
    }
  }
  requestUpdate();
}

// ----------------------------------------------------------------
// Render
// ----------------------------------------------------------------

void HttpMonitorActivity::render(RenderLock&&) {
  renderer.clearScreen();

  switch (state) {
    case NO_CONFIG:
      renderNoConfig();
      break;
    case IDLE:
    case FETCHING:
      renderFetching();
      break;
    case SHOWING:
      renderDashboard();
      break;
    case ERROR:
      renderError();
      break;
  }

  if (config.fullRefreshEvery > 0) {
    // ReaderUtils::displayWithRefreshCycle() re-seeds its counter from
    // SETTINGS.getRefreshFrequency() (the GLOBAL setting) whenever it fires a
    // full refresh (ReaderUtils.h:55) — it has no way to know about our
    // per-config full_refresh_every. Detect that reset (it only happens when
    // the counter was <= 1 going in) and re-seed with our own value right
    // after, so the config setting governs every cycle, not just the first.
    const bool willReset = framesUntilClean <= 1;
    ReaderUtils::displayWithRefreshCycle(renderer, framesUntilClean);
    if (willReset) framesUntilClean = config.fullRefreshEvery;
  } else {
    renderer.displayBuffer();
  }
}

void HttpMonitorActivity::renderNoConfig() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "HTTP Monitor");

  const int lineH = renderer.getLineHeight(UI_10_FONT_ID) + 6;
  const int smallLineH = renderer.getLineHeight(SMALL_FONT_ID) + 4;
  const int maxTextWidth = pageWidth - 2 * metrics.contentSidePadding;
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 3;

  renderer.drawCenteredText(UI_10_FONT_ID, y, "No config found", true, EpdFontFamily::BOLD);
  y += lineH + 10;

  renderer.drawCenteredText(SMALL_FONT_ID, y, "Create this file on the SD card:");
  y += smallLineH;
  renderer.drawCenteredText(SMALL_FONT_ID, y, HttpMonitorConfig::CONFIG_PATH);
  y += smallLineH + 12;

  renderer.drawCenteredText(SMALL_FONT_ID, y, "Required key:");
  y += smallLineH;
  renderer.drawCenteredText(SMALL_FONT_ID, y, "url = http://host:port/status.json");
  y += smallLineH + 12;

  if (!configError.empty()) {
    const auto err = renderer.truncatedText(SMALL_FONT_ID, configError.c_str(), maxTextWidth);
    renderer.drawCenteredText(SMALL_FONT_ID, y, err.c_str());
  }

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void HttpMonitorActivity::renderFetching() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, config.title.c_str());
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Fetching...");
}

void HttpMonitorActivity::renderError() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, config.title.c_str());

  const int lineH = renderer.getLineHeight(UI_10_FONT_ID) + 6;
  const int smallLineH = renderer.getLineHeight(SMALL_FONT_ID) + 2;
  const int maxTextWidth = pageWidth - 2 * metrics.contentSidePadding;
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 3;

  char statusBuf[32];
  if (httpStatusCode > 0) {
    snprintf(statusBuf, sizeof(statusBuf), "HTTP %d", httpStatusCode);
  } else {
    snprintf(statusBuf, sizeof(statusBuf), "Connection failed");
  }
  renderer.drawCenteredText(UI_10_FONT_ID, y, statusBuf, true, EpdFontFamily::BOLD);
  y += lineH + 6;

  // maxLines must be > 0 — GfxRenderer::wrappedText() returns an empty vector
  // otherwise (GfxRenderer.cpp: `if (!text || maxWidth <= 0 || maxLines <= 0)
  // return lines;`), which would silently drop every error detail.
  static constexpr int MAX_ERROR_LINES = 3;
  const auto errLines = renderer.wrappedText(SMALL_FONT_ID, fetchError.c_str(), maxTextWidth, MAX_ERROR_LINES);
  for (const auto& line : errLines) {
    renderer.drawCenteredText(SMALL_FONT_ID, y, line.c_str());
    y += smallLineH;
  }

  const auto labels = mappedInput.mapLabels("Back", "Retry", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void HttpMonitorActivity::renderDashboard() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const char* title = dashboard.title[0] != '\0' ? dashboard.title : config.title.c_str();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title);

  // Rows use UI_12 — this is a wall/desk dashboard read from across a room, not a
  // handheld screen. Section headings share the font but are drawn BOLD (rows stay
  // REGULAR) so they read as visually dominant despite the shared size.
  const int lineH = renderer.getLineHeight(UI_12_FONT_ID) + 6;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int contentWidth = pageWidth - 2 * metrics.contentSidePadding;
  const int unreservedContentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  // Count total scrollable lines: one per non-empty heading, one per row, plus an
  // "Alerts" heading and one line per alert if any were sent.
  int headingCount = 0;
  int rowCount = 0;
  for (const auto& section : dashboard.sections) {
    if (section.heading[0] != '\0') headingCount++;
    rowCount += static_cast<int>(section.rows.size());
  }
  const int totalLines =
      HttpMonitorLayout::computeTotalLines(headingCount, rowCount, static_cast<int>(dashboard.alerts.size()));

  // The scroll indicator needs its own row so it can never sit on top of a row's
  // right-aligned value (that overlap is exactly what "%d/%d" stamped over the
  // first row's value looked like before this fix). Only reserve that row when
  // scrolling is actually needed — the common case is a dashboard that fits.
  auto unreservedScroll =
      HttpMonitorLayout::computeScrollMetrics(totalLines, unreservedContentTop, contentBottom, lineH);
  const bool needsScrollIndicator = unreservedScroll.maxScroll > 0;
  const int smallLineH = renderer.getLineHeight(SMALL_FONT_ID) + 4;
  const int contentTop = needsScrollIndicator ? (unreservedContentTop + smallLineH) : unreservedContentTop;

  const auto scrollMetrics = HttpMonitorLayout::computeScrollMetrics(totalLines, contentTop, contentBottom, lineH);
  const int visibleLines = scrollMetrics.visibleLines;
  const int maxScroll = scrollMetrics.maxScroll;
  scrollOffset = HttpMonitorLayout::clampScrollOffset(scrollOffset, maxScroll);

  static constexpr int BAR_GAP = 10;

  int lineIdx = 0;
  int y = contentTop;

  auto drawLine = [&](bool isHeading, const char* label, const char* value, int bar, bool alert) {
    // Label and value are measured/truncated to their own capped columns FIRST —
    // this guarantees, by construction, that whatever's left for the bar cannot
    // overlap either of them, regardless of how long `value` is (up to the 16-char
    // cap).
    std::string labelStr;
    std::string valueStr;
    int labelWidth = 0;
    int valueWidth = 0;
    int barX = 0;
    int barSpan = 0;

    if (!isHeading) {
      // Text has priority over the bar — the bar is decoration, the value is the
      // data. Column budgets and the bar's remaining span are the same pure
      // arithmetic the preview harness uses (HttpMonitorLayout.h) — no duplicated
      // formulas to drift out of sync.
      const auto cols = HttpMonitorLayout::computeRowColumns(contentWidth, bar >= 0);

      labelStr = renderer.truncatedText(UI_12_FONT_ID, label, cols.labelColMax);
      labelWidth = renderer.getTextWidth(UI_12_FONT_ID, labelStr.c_str());

      valueWidth = renderer.getTextWidth(UI_12_FONT_ID, value);
      valueStr =
          (valueWidth > cols.valueColMax) ? renderer.truncatedText(UI_12_FONT_ID, value, cols.valueColMax) : value;
      valueWidth = renderer.getTextWidth(UI_12_FONT_ID, valueStr.c_str());

      if (bar >= 0) {
        const auto barSpanResult = HttpMonitorLayout::computeBarSpan(metrics.contentSidePadding, pageWidth,
                                                                      labelWidth, valueWidth, cols.barColMax, BAR_GAP);
        barX = barSpanResult.barX;
        barSpan = barSpanResult.barSpan;
      }
    }

    const bool visible = (lineIdx >= scrollOffset) && (y + lineH <= contentBottom);
    if (visible) {
      if (isHeading) {
        // Headings are drawn directly (not via GUI.drawSubHeader, which only ever
        // draws REGULAR) so BOLD keeps them visually dominant over UI_12 rows.
        renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, y, label, true, EpdFontFamily::BOLD);
      } else {
        renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, y, labelStr.c_str(), true,
                          alert ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
        renderer.drawText(UI_12_FONT_ID, pageWidth - metrics.contentSidePadding - valueWidth, y,
                          valueStr.c_str());

        if (bar >= 0 && barSpan > 0) {
          // Drawn directly rather than via GUI.drawProgressBar(), which paints an
          // unwanted percentage label BELOW its rect (BaseTheme.cpp) — 9px into the
          // next row, on top of that row's own label/value — logs on every call,
          // and computes its fill width from an unclamped percent. `bar` is already
          // clamped to 0..100 by fetch(), so the fill math here is safe outright.
          const int barH = lineH - 12;
          const int barY = y + (lineH - barH) / 2;
          renderer.drawRect(barX, barY, barSpan, barH);
          const int fillWidth = (barSpan > 4) ? (barSpan - 4) * bar / 100 : 0;
          if (fillWidth > 0) {
            renderer.fillRect(barX + 2, barY + 2, fillWidth, barH - 4, true);
          }
        }
      }
      y += lineH;
    }
    lineIdx++;
  };

  for (const auto& section : dashboard.sections) {
    if (section.heading[0] != '\0') {
      drawLine(true, section.heading, nullptr, -1, false);
    }
    for (const auto& row : section.rows) {
      drawLine(false, row.label, row.value, row.bar, row.alert);
    }
  }

  if (!dashboard.alerts.empty()) {
    drawLine(true, "Alerts", nullptr, -1, false);
    for (const auto& alertText : dashboard.alerts) {
      drawLine(false, alertText.data(), "", -1, true);
    }
  }

  if (dashboard.sections.empty() && dashboard.alerts.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, (contentTop + contentBottom) / 2, "No data");
  }

  if (needsScrollIndicator) {
    char scrollBuf[16];
    const int pageSize = (visibleLines > 0) ? visibleLines : 1;
    const int totalPages = HttpMonitorLayout::computeTotalPages(totalLines, pageSize);
    snprintf(scrollBuf, sizeof(scrollBuf), "%d/%d", scrollOffset / pageSize + 1, totalPages);
    const int w = renderer.getTextWidth(SMALL_FONT_ID, scrollBuf);
    // Drawn in the row reserved above unreservedContentTop — never on top of a
    // row's own content.
    renderer.drawText(SMALL_FONT_ID, pageWidth - metrics.contentSidePadding - w, unreservedContentTop, scrollBuf);
  }

  const auto labels = mappedInput.mapLabels("Back", "Refresh", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
