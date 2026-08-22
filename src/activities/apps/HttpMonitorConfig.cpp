#include "HttpMonitorConfig.h"

#include <cstdlib>

namespace HttpMonitorConfig {

namespace {

std::string trim(const std::string& s) {
  const size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  const size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

// Strip a trailing `# comment`. A '#' only starts a comment at the beginning of
// the line or when preceded by whitespace (the usual INI convention) — this
// keeps a literal '#' inside a value (e.g. a URL fragment or a token embedded in
// auth_header) from being silently chopped off.
std::string stripComment(const std::string& s) {
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '#' && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t')) {
      return s.substr(0, i);
    }
  }
  return s;
}

int clampInt(int value, int lo, int hi) {
  if (value < lo) return lo;
  if (value > hi) return hi;
  return value;
}

// Strip exactly one trailing '/' so `.../cmd` and `.../cmd/` behave alike --
// actionUrlFor() always joins with its own '/'.
std::string stripTrailingSlash(const std::string& s) {
  if (!s.empty() && s.back() == '/') return s.substr(0, s.size() - 1);
  return s;
}

// True when `value` contains a CR or LF -- parse() splits lines on '\n', so an
// embedded '\n' can't survive into a single line's value, but an embedded '\r'
// can (a stray CR in the middle of a line, not at the very end where trim()
// would catch it). `url` and `action_url` both go straight into
// http.begin()'s request line; some lenient HTTP parsers treat a lone CR or LF
// as a line terminator, which would let a crafted config smuggle a second
// header. Same guard as auth_header (below): reject the value outright rather
// than strip and hope nothing meaningful was lost.
bool hasCrOrLf(const std::string& value) {
  return value.find('\r') != std::string::npos || value.find('\n') != std::string::npos;
}

// Storage.readFile() (via SDCardManager) caps a file read at 50 KB — parse()'s
// own per-line splitting doesn't otherwise bound how long a single `key = value`
// line can be, so a pathological config could otherwise hand a ~50 KB string to
// a field that's normally a few dozen bytes. Bound each string field explicitly
// rather than relying on downstream truncation (e.g. drawHeader() only truncates
// `title` for *display* — the oversized string would still sit in RAM). 256 chars
// matches the existing URL entry cap in HttpClientActivity/KeyboardEntryActivity.
constexpr size_t MAX_URL_LEN = 256;
constexpr size_t MAX_AUTH_HEADER_LEN = 512;

std::string clampLen(const std::string& s, size_t maxLen) { return s.substr(0, maxLen); }

}  // namespace

bool parse(const char* text, Config& out, std::string& errorOut) {
  out = Config{};  // reset to defaults
  errorOut.clear();

  const std::string content = (text != nullptr) ? text : "";

  size_t pos = 0;
  while (pos <= content.size()) {
    const size_t nl = content.find('\n', pos);
    const std::string rawLine = (nl == std::string::npos) ? content.substr(pos) : content.substr(pos, nl - pos);
    pos = (nl == std::string::npos) ? content.size() + 1 : nl + 1;

    const std::string line = trim(stripComment(rawLine));
    if (line.empty()) continue;

    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;  // malformed line, ignore

    const std::string key = trim(line.substr(0, eq));
    const std::string value = trim(line.substr(eq + 1));
    if (key.empty()) continue;

    if (key == "url") {
      // A rejected value leaves out.url empty, so parse() falls through to its
      // existing "missing url" fatal error below -- the correct, already-tested
      // visible outcome, not a new error path.
      if (!hasCrOrLf(value)) out.url = clampLen(value, MAX_URL_LEN);
    } else if (key == "action_url") {
      // A rejected value leaves out.actionUrl empty, so all four buttons stay
      // inert -- same as if action_url were never set.
      if (!hasCrOrLf(value)) out.actionUrl = stripTrailingSlash(clampLen(value, MAX_URL_LEN));
    } else if (key == "interval_sec") {
      out.intervalSec = clampInt(atoi(value.c_str()), MIN_INTERVAL_SEC, MAX_INTERVAL_SEC);
    } else if (key == "timeout_ms") {
      out.timeoutMs = clampInt(atoi(value.c_str()), MIN_TIMEOUT_MS, MAX_TIMEOUT_MS);
    } else if (key == "full_refresh_every") {
      int v = atoi(value.c_str());
      if (v < 0) v = 0;
      out.fullRefreshEvery = v;
    } else if (key == "font_size") {
      out.fontSize = clampInt(atoi(value.c_str()), MIN_FONT_SIZE, MAX_FONT_SIZE);
    } else if (key == "wifi_hold_sec") {
      out.wifiHoldSec = clampInt(atoi(value.c_str()), MIN_WIFI_HOLD_SEC, MAX_WIFI_HOLD_SEC);
    } else if (key == "battery_min_pct") {
      out.batteryMinPct = clampInt(atoi(value.c_str()), MIN_BATTERY_MIN_PCT, MAX_BATTERY_MIN_PCT);
    } else if (key == "auth_header") {
      // Same hasCrOrLf() guard as url/action_url above -- this value goes
      // straight into http.addHeader().
      if (!hasCrOrLf(value)) {
        out.authHeader = clampLen(value, MAX_AUTH_HEADER_LEN);
      }
    }
    // unknown keys are silently ignored
  }

  if (out.url.empty()) {
    errorOut = "Missing url";
    return false;
  }
  return true;
}

}  // namespace HttpMonitorConfig

// loadFromSd() is the only piece that touches Storage/Arduino types — kept out of
// the native test build (which defines NATIVE_TEST) so the pure parse() above can
// be unit tested without hardware.
#ifndef NATIVE_TEST

#include <HalStorage.h>

namespace HttpMonitorConfig {

bool loadFromSd(Config& out, std::string& errorOut) {
  if (!Storage.exists(CONFIG_PATH)) {
    errorOut = "No config file";
    return false;
  }
  const String contents = Storage.readFile(CONFIG_PATH);
  return parse(contents.c_str(), out, errorOut);
}

}  // namespace HttpMonitorConfig

#endif  // NATIVE_TEST
