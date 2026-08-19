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

// Storage.readFile() (via SDCardManager) caps a file read at 50 KB — parse()'s
// own per-line splitting doesn't otherwise bound how long a single `key = value`
// line can be, so a pathological config could otherwise hand a ~50 KB string to
// a field that's normally a few dozen bytes. Bound each string field explicitly
// rather than relying on downstream truncation (e.g. drawHeader() only truncates
// `title` for *display* — the oversized string would still sit in RAM). 256 chars
// matches the existing URL entry cap in HttpClientActivity/KeyboardEntryActivity.
constexpr size_t MAX_URL_LEN = 256;
constexpr size_t MAX_TITLE_LEN = 64;
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
      out.url = clampLen(value, MAX_URL_LEN);
    } else if (key == "interval_sec") {
      out.intervalSec = clampInt(atoi(value.c_str()), MIN_INTERVAL_SEC, MAX_INTERVAL_SEC);
    } else if (key == "timeout_ms") {
      out.timeoutMs = clampInt(atoi(value.c_str()), MIN_TIMEOUT_MS, MAX_TIMEOUT_MS);
    } else if (key == "full_refresh_every") {
      int v = atoi(value.c_str());
      if (v < 0) v = 0;
      out.fullRefreshEvery = v;
    } else if (key == "dial_tick_sec") {
      out.dialTickSec = clampInt(atoi(value.c_str()), MIN_DIAL_TICK_SEC, MAX_DIAL_TICK_SEC);
    } else if (key == "font_size") {
      out.fontSize = clampInt(atoi(value.c_str()), MIN_FONT_SIZE, MAX_FONT_SIZE);
    } else if (key == "title") {
      if (!value.empty()) out.title = clampLen(value, MAX_TITLE_LEN);
    } else if (key == "auth_header") {
      // parse() splits lines on '\n', so an embedded '\n' can't survive into a
      // single line's value — but an embedded '\r' can (e.g. a stray CR in the
      // middle of a line, not at the very end where trim() would catch it), and
      // this value goes straight into http.addHeader(). A lone CR or LF is
      // enough for some lenient HTTP parsers to treat it as a line terminator,
      // which would let a crafted config smuggle a second header. Reject the
      // value outright (leave authHeader at its empty default) rather than
      // trying to strip and hope nothing meaningful was lost.
      if (value.find('\r') == std::string::npos && value.find('\n') == std::string::npos) {
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
