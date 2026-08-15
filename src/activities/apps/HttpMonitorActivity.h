#pragma once

#include <array>
#include <string>
#include <vector>

#include "HttpMonitorConfig.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

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

  // Hard caps on untrusted response data. Sections/rows/alerts/label/value are
  // enforced as the response is walked, before anything lands in `dashboard`.
  // The 8 KB body cap is NOT a hard bound on memory used while parsing: it only
  // rejects responses whose server-declared Content-Length exceeds the limit
  // (fetch() checks http.getSize() before parsing) or is absent/unknown. A
  // server that lies about Content-Length can still make ArduinoJson read an
  // unbounded amount from getStream() before the per-array caps below apply —
  // deserializeJson() degrades to a NoMemory DeserializationError rather than
  // crashing (ArduinoJson v7), so this doesn't corrupt anything, but it means
  // the 8 KB figure is a cap on trusted-length responses, not a hard ceiling.
  static constexpr int MAX_SECTIONS = 6;
  static constexpr int MAX_ROWS_PER_SECTION = 12;
  static constexpr int MAX_ALERTS = 4;
  static constexpr int MAX_LABEL_LEN = 24;
  static constexpr int MAX_VALUE_LEN = 16;
  static constexpr long MAX_BODY_BYTES = 8192;

  struct Row {
    char label[MAX_LABEL_LEN + 1] = {};
    char value[MAX_VALUE_LEN + 1] = {};
    int bar = -1;  // -1 = no bar (text-only row), else 0..100
    bool alert = false;
  };

  struct Section {
    char heading[MAX_LABEL_LEN + 1] = {};
    std::vector<Row> rows;
  };

  struct Dashboard {
    char title[32] = {};
    char updated[32] = {};
    std::vector<Section> sections;
    std::vector<std::array<char, 48>> alerts;
  };

  State state = NO_CONFIG;
  HttpMonitorConfig::Config config;
  Dashboard dashboard;
  std::string configError;
  std::string fetchError;
  int httpStatusCode = 0;
  unsigned long lastPollMs = 0;
  unsigned long pollIntervalMs = 30000;
  int scrollOffset = 0;
  int framesUntilClean = 1;
  ButtonNavigator buttonNavigator;

  void loadConfig();
  void fetch();

  void renderNoConfig();
  void renderFetching();
  void renderError();
  void renderDashboard();
};
