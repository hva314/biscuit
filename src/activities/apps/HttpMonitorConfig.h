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

// How long WiFi is kept associated after a directional-button press, when
// auto-drop (see autoDropWifi() below) has powered it down between polls. A
// second press before the hold expires re-arms it -- see HttpMonitorActivity.
constexpr int MIN_WIFI_HOLD_SEC = 5;
constexpr int MAX_WIFI_HOLD_SEC = 600;
constexpr int DEFAULT_WIFI_HOLD_SEC = 30;

// Below this poll interval, WiFi auto-drop stays off -- see autoDropWifi().
// Reconnecting costs a multi-second blocking handshake (reconnectWifiHeadless()),
// which would dominate a fast poll cadence and turn a snappy dashboard into one
// that stalls on every single refresh.
constexpr int AUTO_DROP_MIN_INTERVAL_SEC = 300;

// Battery-critical shutoff threshold. 0 disables the guard entirely -- same
// "0 disables" convention as autoDropWifi() above. Clamped to a sane 0..50
// range: above 50% "critical" would be a nonsensical, alarmist default for
// most batteries, and it's a config-file typo away from a device that
// refuses to run the dashboard at all.
constexpr int MIN_BATTERY_MIN_PCT = 0;
constexpr int MAX_BATTERY_MIN_PCT = 50;
constexpr int DEFAULT_BATTERY_MIN_PCT = 5;

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
  int wifiHoldSec = DEFAULT_WIFI_HOLD_SEC;            // seconds WiFi stays up after a button press
  int batteryMinPct = DEFAULT_BATTERY_MIN_PCT;        // 0 disables the battery-critical guard
  std::string authHeader;            // optional, sent verbatim as an HTTP header, e.g. "Authorization: Bearer abc123"
  int fontSize = DEFAULT_FONT_SIZE;  // fallback dashboard font size, used only when the server omits `fontSize`
  // Base URL for the four button actions, e.g. "http://192.168.0.24:8777/cmd" ->
  // GET .../cmd/up|down|left|right. Empty (the default) means all four buttons
  // stay inert -- see actionUrlFor().
  std::string actionUrl;
};

// Slot order matches HttpMonitorActivity::ActionSlot {Up,Down,Left,Right} --
// NOT MappedInputManager::Button, whose enum order differs (Back, Confirm,
// Left, Right, Up, Down, ...). Indexing this array with a Button value would
// silently pick the wrong slot (or an inert one). Kept here (not in the
// activity) so the URL math is natively testable without pulling in
// Arduino/HAL types.
constexpr const char* ACTION_SLOT_NAMES[4] = {"up", "down", "left", "right"};

// Builds the full command URL for a button slot (0=up, 1=down, 2=left, 3=right).
// Returns "" when actionUrl is empty -- the caller uses that as "inert button".
inline std::string actionUrlFor(const Config& config, int slot) {
  if (config.actionUrl.empty()) return "";
  if (slot < 0 || slot >= 4) return "";
  return config.actionUrl + "/" + ACTION_SLOT_NAMES[slot];
}

// Whether HttpMonitorActivity should power WiFi off between polls and only
// bring it up for the duration of a poll/button press. Pure function of the
// poll interval: below AUTO_DROP_MIN_INTERVAL_SEC (300s), the multi-second
// blocking reconnect (reconnectWifiHeadless()) would dominate the poll cadence,
// so auto-drop stays off and WiFi is left associated, same as before this
// feature existed. Strictly greater-than -- 300s itself keeps WiFi always on.
inline bool autoDropWifi(const Config& config) { return config.intervalSec > AUTO_DROP_MIN_INTERVAL_SEC; }

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
