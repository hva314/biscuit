#pragma once

#include <array>
#include <string>
#include <vector>

#include "HttpMonitorConfig.h"
#include "HttpMonitorSchema.h"
#include "activities/Activity.h"

class HTTPClient;

// A live server dashboard. Polls a JSON status endpoint on a timer and renders a
// server-driven schema (sections -> rows). See docs/http-monitor.md and
// docs/http-monitor-server-api.md for the config file and server contract.
class HttpMonitorActivity final : public Activity {
 public:
  explicit HttpMonitorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("HttpMonitor", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == SHOWING || state == FETCHING; }
  bool skipLoopDelay() override { return false; }

 private:
  enum State { NO_CONFIG, IDLE, FETCHING, SHOWING, ERROR };
  // Order matches HttpMonitorConfig::ACTION_SLOT_NAMES ("up","down","left","right").
  enum class ActionSlot { Up, Down, Left, Right };

  // Hard caps on untrusted response data (sections/rows/alerts/label/value/
  // text/glyphs) live in HttpMonitorSchema.h and are enforced as the filtered
  // document is walked, before anything lands in `dashboard`. The 8 KB body cap
  // below is NOT a hard bound on memory used while parsing: it only rejects
  // responses whose server-declared Content-Length exceeds the limit (fetch()
  // checks http.getSize() before parsing) or is absent/unknown. A server that
  // lies about Content-Length can still make ArduinoJson read an unbounded
  // amount from getStream() before the schema caps apply — deserializeJson()
  // degrades to a NoMemory DeserializationError rather than crashing
  // (ArduinoJson v7), so this doesn't corrupt anything, but it means the 8 KB
  // figure is a cap on trusted-length responses, not a hard ceiling.
  static constexpr long MAX_BODY_BYTES = 8192;

  State state = NO_CONFIG;
  HttpMonitorConfig::Config config;
  HttpMonitorSchema::Dashboard dashboard;
  std::string configError;
  std::string fetchError;
  int httpStatusCode = 0;
  unsigned long lastPollMs = 0;
  unsigned long pollIntervalMs = 30000;
  int pollsUntilClean = 1;
  bool hasDashboard = false;
  bool cleanRefreshDue = false;
  // The state whose content is currently on the panel. fetch() compares against
  // this (not `state`, which is always FETCHING at commit time) to decide whether
  // a poll needs to touch the e-ink at all.
  State displayedState = NO_CONFIG;
  // Set when the user presses Confirm. A manual refresh always repaints, even if
  // the server returns byte-identical data: the press is an explicit request for
  // a redraw, and silently skipping it makes the button look broken. Automatic
  // polls have no such obligation — that is the whole point of the skip.
  bool forceRedraw = false;
  // Liveness dial: the dashboard header's far-right 24x24 corner is a ring +
  // second-hand. loop() advances spinnerFrame every config.dialTickSec seconds
  // while the dashboard is showing; renderDashboard() paints the ring and hand
  // from it.
  //
  // Every tick costs a whole-panel e-ink update, so the tick period is a direct
  // multiplier on panel wear -- this used to be hardcoded at 1Hz, which is
  // ~86k updates/day for a decorative second hand. dial_tick_sec = 0 disables
  // the dial and, with unchanged server data, leaves the panel completely idle
  // between polls.
  int spinnerFrame = 0;
  unsigned long lastSpinnerUpdate = 0;

  // Button-action ack: a small, brief "sent" indicator, and nothing more -- no
  // re-poll, no rendering of the reply, no poll-timer reset. Cleared by loop()
  // once ACTION_ACK_MS has elapsed, which requests one more update to erase it.
  static constexpr unsigned long ACTION_ACK_MS = 1500;
  char actionAck[24] = {};
  // Start time, not an absolute deadline -- compared via wrap-safe subtraction
  // (millis() - actionAckStartMs >= ACTION_ACK_MS), matching every other timer
  // in this file (lastPollMs, lastSpinnerUpdate). An absolute `until` deadline
  // breaks at the ~49-day millis() wrap: if it wrapped to a small number, a
  // plain `millis() >= until` comparison would go true immediately.
  unsigned long actionAckStartMs = 0;

  void loadConfig();
  void fetch();
  void sendAction(ActionSlot slot);
  // Splits config.authHeader on ':' and adds it to `http` -- shared by fetch()
  // and sendAction() so the header logic isn't duplicated.
  void applyAuthHeader(HTTPClient& http);
  // Sets actionAck (RenderLock'd) and requests a repaint -- shared by
  // sendAction()'s "sending"/result steps.
  void setActionAck(const char* text);

  void renderNoConfig();
  void renderFetching();
  void renderError();
  void renderDashboard();
  // Centered SMALL text just above the hint bar, white-filled behind it since
  // this band overlaps the dashboard content band; no-op when actionAck is empty.
  void drawActionAck();

  // The dashboard-wide font size: the server's top-level `fontSize` when it sent
  // one, otherwise monitor.conf's `font_size`. Returns a ladder index (0..3).
  int dashboardFontSizeIndex() const;
  // Maps dashboardFontSizeIndex() to a registered font id, validated against
  // renderer.getFontMap() (a shipping -DOMIT_FONTS build only registers a
  // handful of fonts) — falls back to UI_12_FONT_ID if the candidate is absent.
  int dashboardFontId() const;
  // Maps a row's `size` field (0..3) to a registered font id, validated against
  // renderer.getFontMap() like dashboardFontId(); SIZE_INHERIT selects the
  // dashboard-wide font. Falls back to UI_12 if absent.
  int fontForSize(uint8_t sizeIdx) const;
};
