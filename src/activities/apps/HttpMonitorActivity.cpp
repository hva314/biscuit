#include "HttpMonitorActivity.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HalStorage.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "HttpMonitorLayout.h"
#include "MappedInputManager.h"

// Typed schema names used by renderDashboard()'s per-row dispatch.
using HttpMonitorSchema::RowAlign;
using HttpMonitorSchema::RowType;
using HttpMonitorSchema::SIZE_INHERIT;
#include "activities/network/WifiSelectionActivity.h"
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

    // font_size in monitor.conf is the initial value; the sidecar (written by
    // Up/Down at runtime) overrides it so the last live choice survives a reboot
    // without rewriting (and clobbering comments in) monitor.conf.
    fontSizeIndex = config.fontSize;
    if (Storage.exists(FONT_STATE_PATH)) {
      const String s = Storage.readFile(FONT_STATE_PATH);
      // Require a real digit — atoi("") and atoi("garbage") both return 0, which
      // would otherwise silently pin the font to the smallest size and discard
      // the monitor.conf font_size default on a corrupt/empty sidecar.
      if (s.length() > 0 && s[0] >= ('0' + HttpMonitorConfig::MIN_FONT_SIZE) &&
          s[0] <= ('0' + HttpMonitorConfig::MAX_FONT_SIZE)) {
        fontSizeIndex = s[0] - '0';
      }
    }
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

  // Liveness dial: advance the header hand once per second while the dashboard
  // is on screen (SHOWING only). The dial is drawn in renderDashboard(), so
  // advancing it in FETCHING/ERROR would repaint a framebuffer whose visible
  // content does not change — a full clearScreen+redraw every second with no
  // visible effect (battery drain, panel wear, ghosting). The mutation runs
  // under the RenderLock because the render task reads spinnerFrame, then a
  // single redraw is requested. E-ink partial refresh leaves a faint ghost of
  // the previous hand, so dialCleanCountdown forces a clean HALF_REFRESH every
  // 60 ticks (~1 minute wall-clock).
  if (state == SHOWING) {
    const unsigned long now = millis();
    if (now - lastSpinnerUpdate >= 1000UL) {
      lastSpinnerUpdate = now;
      {
        RenderLock lock(*this);
        spinnerFrame = (spinnerFrame + 1) % 12;
        if (--dialCleanCountdown <= 0) {
          dialCleanCountdown = 60;
          cleanRefreshDue = true;
        }
      }
      requestUpdate();
    }
  }

  if (state == IDLE) {
    lastPollMs = millis();
    state = FETCHING;
    if (!hasDashboard) requestUpdate(true);
    fetch();
    return;
  }

  // Manual refresh
  if ((state == SHOWING || state == ERROR) &&
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    lastPollMs = millis();
    state = FETCHING;
    if (!hasDashboard) requestUpdate(true);
    fetch();
    return;
  }

  // Scroll (Left/Right, only meaningful once a dashboard is showing) and font
  // size (Up/Down). These must stay on disjoint button sets — Up/Down are
  // ButtonNavigator's default next/previous buttons too, so onNext()/onPrevious()
  // would double-bind them to both scrolling and font size.
  if (state == SHOWING) {
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] {
      if (scrollOffset > 0) {
        RenderLock lock(*this);
        scrollOffset--;
      }
      requestUpdate();
    });
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] {
      {
        RenderLock lock(*this);
        scrollOffset++;
      }
      requestUpdate();
    });
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this] { adjustFontSize(+1); });
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this] { adjustFontSize(-1); });
  }

  // Timed poll
  if (state == SHOWING || state == ERROR) {
    const unsigned long now = millis();
    if (now - lastPollMs >= pollIntervalMs) {
      lastPollMs = now;
      state = FETCHING;
      if (!hasDashboard) requestUpdate(true);
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
  HttpMonitorSchema::Dashboard next;

  if (code == HTTP_CODE_OK) {
    const int64_t reportedLength = http.getSize();
    if (reportedLength <= 0 || reportedLength > MAX_BODY_BYTES) {
      errMsg = "Response too large or unknown length";
    } else {
      // Whitelist filter — every key HttpMonitorSchema::apply() reads must be
      // let through here, or the filtered doc silently drops it. `bar` is a
      // whole-subtree include (filter["bar"] = true): it keeps BOTH the legacy
      // integer form and the new object form. A nested object filter
      // (["bar"]["value"] = true, ...) would strip the object's inner keys AND
      // — worse — a nested filter on a primitive produces no value at all, so
      // legacy `"bar": 88` rows would lose their bar. The subtree include is the
      // only shape that preserves both forms.
      JsonDocument filter;
      filter["title"] = true;
      filter["updated"] = true;
      filter["sections"][0]["heading"] = true;
      filter["sections"][0]["rows"][0]["type"] = true;
      filter["sections"][0]["rows"][0]["label"] = true;
      filter["sections"][0]["rows"][0]["value"] = true;
      filter["sections"][0]["rows"][0]["text"] = true;
      filter["sections"][0]["rows"][0]["glyphs"] = true;
      filter["sections"][0]["rows"][0]["bar"] = true;  // whole subtree: int + object forms
      filter["sections"][0]["rows"][0]["alert"] = true;
      filter["sections"][0]["rows"][0]["bold"] = true;
      filter["sections"][0]["rows"][0]["size"] = true;
      filter["sections"][0]["rows"][0]["align"] = true;
      filter["sections"][0]["rows"][0]["height"] = true;
      filter["sections"][0]["rows"][0]["inset"] = true;
      filter["sections"][0]["rows"][0]["lineWidth"] = true;
      filter["alerts"][0] = true;

      JsonDocument doc;
      const DeserializationError err =
          deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
      if (err) {
        errMsg = std::string("JSON parse failed: ") + err.c_str();
        LOG_ERR("HTTPMON", "JSON parse failed: %s", err.c_str());
      } else {
        // ---- apply response, enforcing every cap before it lands in `next` ----
        // The typed schema walk (type/align/bold/size, bar int-or-object, text,
        // glyphs, spacer/divider params, and all the caps) lives in the pure
        // header HttpMonitorSchema.h so the exact parse path is native-testable.
        HttpMonitorSchema::apply(doc, next, config.title.c_str());
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
      hasDashboard = true;
    } else {
      fetchError = errMsg;
      state = ERROR;
    }
    // Advance the clean-refresh cadence on every completed poll, not just a
    // successful one — a persistent ERROR screen (server down) polls just as
    // often as SHOWING does, and without this the ghosting-clear cadence never
    // fires until a fetch finally succeeds, letting ghosting accumulate behind
    // the error text indefinitely.
    if (config.fullRefreshEvery > 0) {
      if (--framesUntilClean <= 0) {
        cleanRefreshDue = true;
        framesUntilClean = config.fullRefreshEvery;
      }
    }
  }
  requestUpdate();
}

// ----------------------------------------------------------------
// Font size
// ----------------------------------------------------------------

// Ladder index -> candidate font id. Only fonts guaranteed present in the
// shipping (`slim` + `-DOMIT_FONTS`) build are used — see main.cpp's
// setupDisplayAndFonts(), which registers just BOOKERLY_14, UI_10, UI_12 and
// SMALL under OMIT_FONTS. Index 2 (UI_12) is the default and matches the size
// the dashboard has always rendered at.
int HttpMonitorActivity::dashboardFontId() const {
  static constexpr int kFontLadder[] = {SMALL_FONT_ID, UI_10_FONT_ID, UI_12_FONT_ID, BOOKERLY_14_FONT_ID};
  static constexpr int kLadderSize = static_cast<int>(sizeof(kFontLadder) / sizeof(kFontLadder[0]));

  const int idx =
      (fontSizeIndex >= 0 && fontSizeIndex < kLadderSize) ? fontSizeIndex : HttpMonitorConfig::DEFAULT_FONT_SIZE;
  const int candidate = kFontLadder[idx];
  // A missing font renders blank (GfxRenderer returns width/lineheight 0 for an
  // unregistered id), so fall back to UI_12 — always present — rather than
  // trust the ladder blindly.
  const auto& fontMap = renderer.getFontMap();
  if (fontMap.find(candidate) != fontMap.end()) return candidate;
  return UI_12_FONT_ID;
}

// Per-row font override: a `size` field on the row picks a ladder entry directly;
// SIZE_INHERIT (the common case) defers to the dashboard-wide font the user
// scaled with Up/Down. Same validation as dashboardFontId() — unregistered
// candidates (a -DOMIT_FONTS build) fall back to UI_12.
int HttpMonitorActivity::fontForSize(uint8_t sizeIdx) const {
  if (sizeIdx != SIZE_INHERIT) {
    static constexpr int kFontLadder[] = {SMALL_FONT_ID, UI_10_FONT_ID, UI_12_FONT_ID, BOOKERLY_14_FONT_ID};
    static constexpr int kLadderSize = static_cast<int>(sizeof(kFontLadder) / sizeof(kFontLadder[0]));
    if (sizeIdx < kLadderSize) {
      const auto& fontMap = renderer.getFontMap();
      if (fontMap.find(kFontLadder[sizeIdx]) != fontMap.end()) return kFontLadder[sizeIdx];
    }
  }
  return dashboardFontId();
}

void HttpMonitorActivity::adjustFontSize(int delta) {
  const int next = fontSizeIndex + delta;
  if (next < HttpMonitorConfig::MIN_FONT_SIZE || next > HttpMonitorConfig::MAX_FONT_SIZE || next == fontSizeIndex) {
    return;
  }
  {
    RenderLock lock(*this);
    fontSizeIndex = next;
  }
  saveFontSize();
  requestUpdate();
}

void HttpMonitorActivity::saveFontSize() { Storage.writeFile(FONT_STATE_PATH, String(fontSizeIndex)); }

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

  if (config.fullRefreshEvery > 0 && cleanRefreshDue) {
    cleanRefreshDue = false;
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
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

  // ---- Header: the dashboard header is a status bar, rendered explicitly rather
  // than via GUI.drawHeader() (which centers the title and paints the battery on
  // the far right). Title sits LEFT (UI_12 BOLD), the `updated` timestamp is
  // right-aligned in SMALL just left of the far-right 24x24 corner reserved for
  // the liveness dial (drawn from loop(), step 6). The battery is dropped for the
  // monitor header — the dial + timestamp own that zone.
  const int headerY = metrics.topPadding;
  const int headerH = metrics.headerHeight;
  const char* title = dashboard.title[0] != '\0' ? dashboard.title : config.title.c_str();

  const char* updated = dashboard.updated;
  const int updatedW = (updated[0] != '\0') ? renderer.getTextWidth(SMALL_FONT_ID, updated) : 0;
  constexpr int DIAL_SIZE = 24;
  const int dialX = pageWidth - metrics.contentSidePadding - DIAL_SIZE;  // 436 on X4
  const int updatedX = dialX - 8 - updatedW;  // timestamp right edge, 8px clear of the dial
  const int titleZone = pageWidth - 2 * metrics.contentSidePadding - updatedW - DIAL_SIZE - 2 * 8;

  const int titleLineH = renderer.getLineHeight(UI_12_FONT_ID);
  const int titleY = headerY + (headerH - titleLineH) / 2;
  const std::string titleStr = renderer.truncatedText(UI_12_FONT_ID, title, titleZone, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, titleY, titleStr.c_str(), true, EpdFontFamily::BOLD);
  if (updated[0] != '\0') {
    const int updatedY = headerY + (headerH - renderer.getLineHeight(SMALL_FONT_ID)) / 2;
    renderer.drawText(SMALL_FONT_ID, updatedX, updatedY, updated);
  }

  // Liveness dial in the reserved 24x24 corner: a static ring (radius 10, 1px)
  // plus a 2px hand pointing at one of 12 precomputed 30° positions. loop()
  // advances spinnerFrame 1Hz; render() clears the framebuffer first, so the
  // hand is repainted (no in-frame ghost) and dialCleanCountdown keeps the
  // physical display clean via a periodic HALF_REFRESH. drawArc() paints a
  // single quadrant annulus, so four calls (one per sign pair) form the ring.
  {
    static constexpr int DIAL_RADIUS = 10;
    // 12 second-hand positions, 30° apart, precomputed so render needs no trig:
    // index 0 = 12 o'clock, then clockwise. |dx,dy| ≈ 8 in every direction.
    static constexpr struct {
      int dx;
      int dy;
    } kHandSteps[12] = {
        {0, -8}, {4, -7}, {7, -4}, {8, 0}, {7, 4}, {4, 7},
        {0, 8},  {-4, 7}, {-7, 4}, {-8, 0}, {-7, -4}, {-4, -7},
    };
    const int ccx = dialX + DIAL_SIZE / 2;
    const int ccy = headerY + headerH / 2;
    for (int q = 0; q < 4; ++q) {
      const int xDir = (q & 1) ? 1 : -1;
      const int yDir = (q & 2) ? 1 : -1;
      renderer.drawArc(DIAL_RADIUS, ccx, ccy, xDir, yDir, 1, true);
    }
    const int handIdx = spinnerFrame % 12;
    renderer.drawLine(ccx, ccy, ccx + kHandSteps[handIdx].dx, ccy + kHandSteps[handIdx].dy, 2, true);
  }

  // ---- Content band ----
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int contentWidth = pageWidth - 2 * metrics.contentSidePadding;
  const int unreservedContentTop = headerY + headerH + metrics.verticalSpacing;
  static constexpr int BAR_GAP = 10;
  const int smallLineH = renderer.getLineHeight(SMALL_FONT_ID) + 4;

  const int rowFontGlobal = dashboardFontId();
  const int rowLineHGlobal = renderer.getLineHeight(rowFontGlobal) + 6;

  // ---- Build the per-entry scroll list. Each entry is one heading, one row, or
  // one alert line; heights vary (per-row font size, wrapped text, spacers,
  // dividers, glyph bands), so the scroll model is a prefix sum over
  // entryHeights[] (HttpMonitorLayout::entryY / computeScrollMetrics), not the
  // old uniform-pitch line model.
  static constexpr int MAX_ENTRIES = HttpMonitorSchema::MAX_SECTIONS *
                                         (1 + HttpMonitorSchema::MAX_ROWS_PER_SECTION) +
                                     1 + HttpMonitorSchema::MAX_ALERTS;
  struct RenderEntry {
    const HttpMonitorSchema::Row* row = nullptr;  // valid for typed rows
    const char* text = nullptr;                   // heading or alert text
    bool isHeading = false;
  };
  RenderEntry entries[MAX_ENTRIES];
  int entryHeights[MAX_ENTRIES];
  int entryCount = 0;

  auto addEntry = [&](const HttpMonitorSchema::Row* row, const char* text, bool isHeading, int height) {
    if (entryCount >= MAX_ENTRIES) return;
    entries[entryCount] = RenderEntry{row, text, isHeading};
    entryHeights[entryCount] = height;
    ++entryCount;
  };

  for (const auto& section : dashboard.sections) {
    if (section.heading[0] != '\0') {
      addEntry(nullptr, section.heading, /*isHeading=*/true, rowLineHGlobal);
    }
    for (const auto& row : section.rows) {
      const int rowFont = fontForSize(row.sizeIdx);
      const int rowLineH = renderer.getLineHeight(rowFont) + 6;
      int h = rowLineH;
      switch (row.type) {
        case RowType::SPACER:
          h = HttpMonitorLayout::spacerEntryHeight(row.spacerHeight);
          break;
        case RowType::DIVIDER:
          h = HttpMonitorLayout::dividerEntryHeight();
          break;
        case RowType::GLYPHS:
          h = HttpMonitorLayout::glyphsEntryHeight();
          break;
        case RowType::TEXT: {
          // Alert text rows draw a 6x6 marker + indent (drawTextRow), so they
          // wrap at a narrower width; the height must match what is drawn.
          const int markerPad = row.alert() ? 10 : 0;
          const int lineCount =
              static_cast<int>(renderer.wrappedText(rowFont, row.text, contentWidth - markerPad, 2).size());
          h = HttpMonitorLayout::textEntryHeight(rowLineH, lineCount);
          break;
        }
        default:  // KV and BAR
          h = HttpMonitorLayout::kvEntryHeight(rowLineH);
          break;
      }
      addEntry(&row, nullptr, false, h);
    }
  }
  if (!dashboard.alerts.empty()) {
    addEntry(nullptr, "Alerts", true, rowLineHGlobal);
    for (const auto& alertText : dashboard.alerts) {
      addEntry(nullptr, alertText.data(), false, rowLineHGlobal);
    }
  }

  // ---- Scroll metrics (per-entry). The scroll indicator reserves its own SMALL
  // row at the top so it can never sit on top of a row's right-aligned value.
  auto unreservedScroll =
      HttpMonitorLayout::computeScrollMetrics(entryHeights, entryCount, unreservedContentTop, contentBottom);
  const bool needsScrollIndicator = unreservedScroll.maxScroll > 0;
  const int contentTop = needsScrollIndicator ? (unreservedContentTop + smallLineH) : unreservedContentTop;

  const auto scrollMetrics =
      HttpMonitorLayout::computeScrollMetrics(entryHeights, entryCount, contentTop, contentBottom);
  const int visibleEntries = scrollMetrics.visibleEntries;
  const int maxScroll = scrollMetrics.maxScroll;
  scrollOffset = HttpMonitorLayout::clampScrollOffset(scrollOffset, maxScroll);

  // ---- Drawing helpers ----
  // The bar. Drawn directly (not GUI.drawProgressBar(), which paints an unwanted
  // percentage label below its rect and computes fill from an unclamped percent).
  // bar.value is clamped to 0..100 by the schema walk, so the fill math is safe
  // outright. Flush fill: fillWidth spans the whole outline, so value==100 fills
  // it edge-to-edge. Segmented bars split the span into cells with 2px gaps.
  auto drawBarShape = [&](int barX, int y, int barSpan, int rowLineH, const HttpMonitorSchema::BarSpec& bar) {
    int barH = rowLineH - 12;
    if (barH < 2) barH = 2;  // keep the bar visible even at the smallest font size
    const int barY = y + (rowLineH - barH) / 2;
    if (bar.segments > 1 && barSpan > 8) {
      const int totalGap = (bar.segments - 1) * 2;
      const int cellW = (barSpan - totalGap) / bar.segments;
      if (cellW >= 2) {
        const int filledCells = (bar.value * bar.segments) / 100;
        for (int s = 0; s < bar.segments; ++s) {
          const int cx = barX + s * (cellW + 2);
          renderer.drawRect(cx, barY, cellW, barH);
          if (s < filledCells) renderer.fillRect(cx, barY, cellW, barH, true);
        }
        return;
      }
    }
    renderer.drawRect(barX, barY, barSpan, barH);
    const int fillWidth = barSpan * bar.value / 100;
    if (fillWidth > 0) renderer.fillRect(barX, barY, fillWidth, barH, true);
  };

  // Glyph cell shapes for `glyphs` rows. Reserved chars map to the geometric
  // shapes below; anything else renders as a SMALL text glyph centered in the
  // 16x16 cell.
  auto drawGlyph = [&](int cx, int cy, char ch) {
    constexpr int CELL = 16;
    switch (ch) {
      case '#':  // filled square
        renderer.fillRect(cx, cy, CELL, CELL, true);
        break;
      case 'o':  // hollow square
        renderer.drawRect(cx, cy, CELL, CELL);
        break;
      case '.': {  // filled disk (16px, radius 8) — four quadrant arcs
        for (int q = 0; q < 4; ++q) {
          const int xd = (q & 1) ? 1 : -1;
          const int yd = (q & 2) ? 1 : -1;
          renderer.drawArc(8, cx + 8, cy + 8, xd, yd, 8, true);
        }
        break;
      }
      case '+':
        renderer.drawLine(cx + 8, cy, cx + 8, cy + CELL - 1, 2, true);
        renderer.drawLine(cx, cy + 8, cx + CELL - 1, cy + 8, 2, true);
        break;
      case 'x':
        renderer.drawLine(cx, cy, cx + CELL - 1, cy + CELL - 1, 2, true);
        renderer.drawLine(cx + CELL - 1, cy, cx, cy + CELL - 1, 2, true);
        break;
      case '!': {  // filled up-triangle
        const int px[] = {cx + 8, cx, cx + CELL - 1};
        const int py[] = {cy, cy + CELL - 1, cy + CELL - 1};
        renderer.fillPolygon(px, py, 3, true);
        break;
      }
      case '^':  // up-triangle outline
        renderer.drawLine(cx + 8, cy, cx, cy + CELL - 1, 2, true);
        renderer.drawLine(cx, cy + CELL - 1, cx + CELL - 1, cy + CELL - 1, 2, true);
        renderer.drawLine(cx + CELL - 1, cy + CELL - 1, cx + 8, cy, 2, true);
        break;
      case 'v':  // down-triangle outline
        renderer.drawLine(cx + 8, cy + CELL - 1, cx, cy, 2, true);
        renderer.drawLine(cx, cy, cx + CELL - 1, cy, 2, true);
        renderer.drawLine(cx + CELL - 1, cy, cx + 8, cy + CELL - 1, 2, true);
        break;
      case ' ':
        break;
      default: {
        char buf[2] = {ch, '\0'};
        const int w = renderer.getTextWidth(SMALL_FONT_ID, buf);
        renderer.drawText(SMALL_FONT_ID, cx + (CELL - w) / 2, cy, buf);
        break;
      }
    }
  };

  auto drawAlertMarker = [&](int x, int y, int lineH) { renderer.fillRect(x, y + (lineH - 6) / 2, 6, 6, true); };

  // KV / BAR row: label left, value right-aligned, the bar spanning whatever's
  // left between them (24px floor -> render as a bar-less kv row below it).
  auto drawKv = [&](const HttpMonitorSchema::Row& row, int y) {
    const int rowFont = fontForSize(row.sizeIdx);
    const int rowLineH = renderer.getLineHeight(rowFont) + 6;
    const bool hasBar = row.bar.value >= 0;
    const auto cols = HttpMonitorLayout::computeRowColumns(contentWidth, hasBar);
    const int markerPad = row.alert() ? 10 : 0;
    const EpdFontFamily::Style style =
        (row.bold() || row.alert()) ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;

    const std::string labelStr = renderer.truncatedText(rowFont, row.label, cols.labelColMax - markerPad, style);
    const int labelWidth = renderer.getTextWidth(rowFont, labelStr.c_str(), style);
    int valueWidth = renderer.getTextWidth(rowFont, row.value);
    std::string valueStr =
        (valueWidth > cols.valueColMax) ? renderer.truncatedText(rowFont, row.value, cols.valueColMax) : row.value;
    valueWidth = renderer.getTextWidth(rowFont, valueStr.c_str());

    if (hasBar) {
      // Width %/align of the bar object map onto the full label->value span
      // (placeFractionalBar); legacy int bars are fullWidth + LEFT.
      const auto barResult = HttpMonitorLayout::placeFractionalBar(
          metrics.contentSidePadding, pageWidth, markerPad + labelWidth, valueWidth, BAR_GAP, row.bar.widthPct,
          static_cast<HttpMonitorLayout::BarAlign>(row.bar.align));
      if (barResult.barSpan >= HttpMonitorLayout::MIN_BAR_SPAN) {
        const int labelX = metrics.contentSidePadding + markerPad;
        if (row.alert()) drawAlertMarker(metrics.contentSidePadding, y, rowLineH);
        renderer.drawText(rowFont, labelX, y, labelStr.c_str(), true, style);
        renderer.drawText(rowFont, pageWidth - metrics.contentSidePadding - valueWidth, y, valueStr.c_str());
        drawBarShape(barResult.barX, y, barResult.barSpan, rowLineH, row.bar);
        return;
      }
      // barSpan below the 24px floor — fall through and render as a bar-less kv row.
    }

    // Bar-less kv row, block placed per `align`.
    const int blockGap = 16;
    const int blockWidth = markerPad + labelWidth + blockGap + valueWidth;
    int labelX;
    switch (row.align) {
      case RowAlign::RIGHT:
        labelX = pageWidth - metrics.contentSidePadding - valueWidth - blockGap - labelWidth;
        break;
      case RowAlign::CENTER:
        labelX = metrics.contentSidePadding + (contentWidth - blockWidth) / 2 + markerPad;
        break;
      default:
        labelX = metrics.contentSidePadding + markerPad;
        break;
    }
    const int valueX = labelX + labelWidth + blockGap;
    if (row.alert()) drawAlertMarker(labelX - markerPad, y, rowLineH);
    renderer.drawText(rowFont, labelX, y, labelStr.c_str(), true, style);
    renderer.drawText(rowFont, valueX, y, valueStr.c_str());
  };

  // Text row: wraps to at most 2 lines, honors align/bold/size. Alert text rows
  // get the same treatment as kv rows — bolded, plus the 6x6 alert marker at the
  // row's left edge with the text indented so it never sits under the marker.
  // The wrap width shrinks by markerPad (matching the height pass) so the entry
  // height matches what is actually drawn.
  auto drawTextRow = [&](const HttpMonitorSchema::Row& row, int y) {
    const int rowFont = fontForSize(row.sizeIdx);
    const int rowLineH = renderer.getLineHeight(rowFont) + 6;
    const bool alert = row.alert();
    const int markerPad = alert ? 10 : 0;
    const EpdFontFamily::Style style =
        (row.bold() || alert) ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const auto lines = renderer.wrappedText(rowFont, row.text, contentWidth - markerPad, 2);
    int ty = y;
    for (const auto& line : lines) {
      const int tw = renderer.getTextWidth(rowFont, line.c_str());
      int tx;
      switch (row.align) {
        case RowAlign::RIGHT:
          tx = pageWidth - metrics.contentSidePadding - tw;
          break;
        case RowAlign::CENTER:
          tx = metrics.contentSidePadding + markerPad + (contentWidth - markerPad - tw) / 2;
          break;
        default:
          tx = metrics.contentSidePadding + markerPad;
          break;
      }
      renderer.drawText(rowFont, tx, ty, line.c_str(), true, style);
      ty += rowLineH;
    }
    if (alert) drawAlertMarker(metrics.contentSidePadding, y, rowLineH);
  };

  // Divider: 12px band, 1-2px rule centered on it, optional centered SMALL caption
  // (the rule splits into two segments leaving an 8px clear gutter around it).
  auto drawDivider = [&](const HttpMonitorSchema::Row& row, int y) {
    const int inset = std::min(row.dividerInset, contentWidth / 2);
    const int left = metrics.contentSidePadding + inset;
    const int right = pageWidth - metrics.contentSidePadding - inset;
    const int ruleY = y + 6;  // center of the band
    if (row.label[0] != '\0') {
      const int labelW = renderer.getTextWidth(SMALL_FONT_ID, row.label);
      const int lx = metrics.contentSidePadding + (contentWidth - labelW) / 2;
      renderer.drawText(SMALL_FONT_ID, lx, y + (12 - renderer.getLineHeight(SMALL_FONT_ID)) / 2, row.label);
      static constexpr int LABEL_GAP = 8;
      if (lx - LABEL_GAP > left) {
        renderer.drawLine(left, ruleY, lx - LABEL_GAP, ruleY, row.dividerLineWidth, true);
      }
      if (right > lx + labelW + LABEL_GAP) {
        renderer.drawLine(lx + labelW + LABEL_GAP, ruleY, right, ruleY, row.dividerLineWidth, true);
      }
    } else if (right > left) {
      renderer.drawLine(left, ruleY, right, ruleY, row.dividerLineWidth, true);
    }
  };

  // Glyphs: 20px band, optional SMALL label, then 16x16 cells at 4px gaps. The
  // run is clamped to the band (maxGlyphsFor) so a max-length glyphs string
  // (32 cells = 636px) never overflows the ~440px content band — every
  // out-of-band pixel would log a GFX "!! Outside range" and draw clipped. The
  // clamp accounts for the label so the whole block stays in-band.
  auto drawGlyphs = [&](const HttpMonitorSchema::Row& row, int y) {
    const int rawCount = static_cast<int>(strlen(row.glyphs));
    const int labelW = (row.label[0] != '\0') ? renderer.getTextWidth(SMALL_FONT_ID, row.label) : 0;
    const int glyphsAvailable = contentWidth - ((row.label[0] != '\0') ? (labelW + 8) : 0);
    const int glyphCount = std::min(rawCount, HttpMonitorLayout::maxGlyphsFor(glyphsAvailable));
    const int glyphsW = glyphCount * 20 - 4;  // 16px cells + 4px gaps
    int gx;
    if (row.label[0] != '\0') {
      const int blockW = labelW + 8 + glyphsW;
      switch (row.align) {
        case RowAlign::RIGHT:
          gx = pageWidth - metrics.contentSidePadding - blockW;
          break;
        case RowAlign::CENTER:
          gx = metrics.contentSidePadding + (contentWidth - blockW) / 2;
          break;
        default:
          gx = metrics.contentSidePadding;
          break;
      }
      renderer.drawText(SMALL_FONT_ID, gx, y + (20 - renderer.getLineHeight(SMALL_FONT_ID)) / 2, row.label);
      gx += labelW + 8;
    } else {
      switch (row.align) {
        case RowAlign::RIGHT:
          gx = pageWidth - metrics.contentSidePadding - glyphsW;
          break;
        case RowAlign::CENTER:
          gx = metrics.contentSidePadding + (contentWidth - glyphsW) / 2;
          break;
        default:
          gx = metrics.contentSidePadding;
          break;
      }
    }
    const int gy = y + (20 - 16) / 2;
    for (int i = 0; i < glyphCount; ++i) {
      drawGlyph(gx, gy, row.glyphs[i]);
      gx += 20;
    }
  };

  auto dispatchRow = [&](const HttpMonitorSchema::Row& row, int y) {
    switch (row.type) {
      case RowType::SPACER:
        break;  // blank band
      case RowType::DIVIDER:
        drawDivider(row, y);
        break;
      case RowType::GLYPHS:
        drawGlyphs(row, y);
        break;
      case RowType::TEXT:
        drawTextRow(row, y);
        break;
      default:  // KV and BAR
        drawKv(row, y);
        break;
    }
  };

  // ---- Draw the visible entries ----
  for (int k = 0; k < visibleEntries; ++k) {
    const int i = scrollOffset + k;
    if (i >= entryCount) break;  // scrollOffset+visibleEntries can pass the end
                                 // when maxScroll = entryCount-1 (single tall
                                 // trailing row) — never index past the list.
    const int y = HttpMonitorLayout::entryY(entryHeights, scrollOffset, i, contentTop);
    const RenderEntry& e = entries[i];
    // Safety net: never paint a row past the content band. Only reachable with
    // mixed-height lists where a scrolled-to tall row overflows — visibleEntries
    // (the leading-entry count) can't know that a later row is taller.
    if (y + entryHeights[i] > contentBottom) break;
    if (e.row != nullptr) {
      dispatchRow(*e.row, y);
    } else if (e.isHeading) {
      // Headings are drawn directly (not via GUI.drawSubHeader, which only ever
      // draws REGULAR) so BOLD keeps them visually dominant over the row font.
      renderer.drawText(rowFontGlobal, metrics.contentSidePadding, y, e.text, true, EpdFontFamily::BOLD);
    } else {
      // Alert line: BOLD label + 6x6 marker prefix, no value/bar.
      const std::string alertStr =
          renderer.truncatedText(rowFontGlobal, e.text, contentWidth / 2 - 10, EpdFontFamily::BOLD);
      drawAlertMarker(metrics.contentSidePadding, y, rowLineHGlobal);
      renderer.drawText(rowFontGlobal, metrics.contentSidePadding + 10, y, alertStr.c_str(), true,
                        EpdFontFamily::BOLD);
    }
  }

  if (dashboard.sections.empty() && dashboard.alerts.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, (contentTop + contentBottom) / 2, "No data");
  }

  if (needsScrollIndicator) {
    char scrollBuf[16];
    const int pageSize = (visibleEntries > 0) ? visibleEntries : 1;
    const int totalPages = HttpMonitorLayout::computeTotalPages(entryCount, pageSize);
    snprintf(scrollBuf, sizeof(scrollBuf), "%d/%d", scrollOffset / pageSize + 1, totalPages);
    const int w = renderer.getTextWidth(SMALL_FONT_ID, scrollBuf);
    // Drawn in the row reserved above unreservedContentTop — never on top of a
    // row's own content.
    renderer.drawText(SMALL_FONT_ID, pageWidth - metrics.contentSidePadding - w, unreservedContentTop, scrollBuf);
  }

  // mapLabels() only labels the four FRONT buttons (Back/Confirm/Left/Right) —
  // the side Up/Down buttons (now font size) aren't covered here; that mapping
  // is documented in docs/http-monitor.md instead.
  const auto labels = mappedInput.mapLabels("Back", "Refresh", "<", ">");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
