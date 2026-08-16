#pragma once

#include <string>

// Config format for /biscuit/monitor.conf — hand-editable `key = value` file.
// See docs/http-monitor.md for the full user-facing description.
namespace HttpMonitorConfig {

constexpr const char* CONFIG_PATH = "/biscuit/monitor.conf";

constexpr int MIN_INTERVAL_SEC = 5;
constexpr int MAX_INTERVAL_SEC = 3600;
constexpr int DEFAULT_INTERVAL_SEC = 30;

constexpr int MIN_TIMEOUT_MS = 1000;
constexpr int MAX_TIMEOUT_MS = 30000;
constexpr int DEFAULT_TIMEOUT_MS = 5000;

constexpr int DEFAULT_FULL_REFRESH_EVERY = 20;

// Index into the dashboard font ladder (see HttpMonitorActivity::dashboardFontId).
// 0 = smallest, 3 = largest; 2 (UI_12) matches the size the dashboard has always
// used, so it's the default that keeps today's look unchanged.
constexpr int MIN_FONT_SIZE = 0;
constexpr int MAX_FONT_SIZE = 3;
constexpr int DEFAULT_FONT_SIZE = 2;

struct Config {
  std::string url;
  int intervalSec = DEFAULT_INTERVAL_SEC;
  int timeoutMs = DEFAULT_TIMEOUT_MS;
  int fullRefreshEvery = DEFAULT_FULL_REFRESH_EVERY;  // 0 disables periodic full refresh
  std::string title = "HTTP Monitor";
  std::string authHeader;  // optional, sent verbatim as an HTTP header, e.g. "Authorization: Bearer abc123"
  int fontSize = DEFAULT_FONT_SIZE;  // initial dashboard font size; overridden at runtime by the sidecar file
};

// Pure parser: parses the full contents of a monitor.conf file (`text`) into `out`.
// Returns true on success. On failure, returns false and sets `errorOut` — the only
// fatal error is a missing `url`. Unknown keys are ignored; every numeric is clamped.
// No I/O, no Arduino/HAL dependency — safe to unit test natively.
bool parse(const char* text, Config& out, std::string& errorOut);

// Loads and parses /biscuit/monitor.conf from the SD card via `Storage`.
// Thin I/O wrapper around parse() — kept out of the pure function so parse()
// stays unit-testable without hardware.
bool loadFromSd(Config& out, std::string& errorOut);

}  // namespace HttpMonitorConfig
