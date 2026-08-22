// test/test_http_monitor_config/test_http_monitor_config.cpp
// Tests HttpMonitorConfig::parse — pure logic, no Arduino/HAL dependency.
// Run: pio test -e native -f test_http_monitor_config
//
// This pulls in the real implementation (HttpMonitorConfig.cpp) directly so the
// tests exercise the exact code that ships on-device. HttpMonitorConfig.cpp guards
// its Storage-dependent loadFromSd() behind `#ifndef NATIVE_TEST`, so including it
// here (with -DNATIVE_TEST set by the native env) never pulls in HalStorage.h.

#include <unity.h>

#include <string>

#include "../../src/activities/apps/HttpMonitorConfig.cpp"
#include "../../src/activities/apps/HttpMonitorConfig.h"

using HttpMonitorConfig::Config;

// ---- defaults ----

void test_defaults_when_keys_absent() {
  Config cfg;
  std::string err;
  bool ok = HttpMonitorConfig::parse("url = http://192.168.1.10:8080/status.json\n", cfg, err);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(30, cfg.intervalSec);
  TEST_ASSERT_EQUAL(5000, cfg.timeoutMs);
  TEST_ASSERT_EQUAL(20, cfg.fullRefreshEvery);
  TEST_ASSERT_EQUAL_STRING("", cfg.authHeader.c_str());
}

// ---- key = value spacing variants ----

void test_key_value_with_spaces() {
  Config cfg;
  std::string err;
  TEST_ASSERT_TRUE(HttpMonitorConfig::parse("url = http://a\n", cfg, err));
  TEST_ASSERT_EQUAL_STRING("http://a", cfg.url.c_str());
}

void test_key_value_no_spaces() {
  Config cfg;
  std::string err;
  TEST_ASSERT_TRUE(HttpMonitorConfig::parse("url=http://b\n", cfg, err));
  TEST_ASSERT_EQUAL_STRING("http://b", cfg.url.c_str());
}

// ---- comments ----

void test_comment_lines_ignored() {
  Config cfg;
  std::string err;
  TEST_ASSERT_TRUE(HttpMonitorConfig::parse("# a comment\nurl = http://c\n# another comment\n", cfg, err));
  TEST_ASSERT_EQUAL_STRING("http://c", cfg.url.c_str());
}

void test_inline_trailing_comment_ignored() {
  Config cfg;
  std::string err;
  TEST_ASSERT_TRUE(HttpMonitorConfig::parse("url = http://d\ninterval_sec = 45 # every 45s\n", cfg, err));
  TEST_ASSERT_EQUAL(45, cfg.intervalSec);
}

// A '#' only starts a comment at line-start or after whitespace — a '#' embedded
// directly in a value (no preceding space) must be preserved verbatim, since a
// credential or URL fragment containing '#' must not be silently truncated.
void test_hash_inside_value_without_preceding_space_preserved() {
  Config cfg;
  std::string err;
  TEST_ASSERT_TRUE(HttpMonitorConfig::parse("url=http://x\nauth_header = Authorization: Bearer abc#123\n", cfg, err));
  TEST_ASSERT_EQUAL_STRING("Authorization: Bearer abc#123", cfg.authHeader.c_str());
}

void test_hash_in_url_fragment_preserved() {
  Config cfg;
  std::string err;
  TEST_ASSERT_TRUE(HttpMonitorConfig::parse("url = http://host/status.json#frag\n", cfg, err));
  TEST_ASSERT_EQUAL_STRING("http://host/status.json#frag", cfg.url.c_str());
}

void test_trailing_comment_after_whitespace_still_stripped() {
  Config cfg;
  std::string err;
  TEST_ASSERT_TRUE(HttpMonitorConfig::parse(
      "url=http://y\nauth_header = Authorization: Bearer abc123   # do not send in logs\n", cfg, err));
  TEST_ASSERT_EQUAL_STRING("Authorization: Bearer abc123", cfg.authHeader.c_str());
}

void test_full_line_comment_still_ignored() {
  Config cfg;
  std::string err;
  TEST_ASSERT_TRUE(HttpMonitorConfig::parse("# this whole line is a comment\nurl = http://z\n", cfg, err));
  TEST_ASSERT_EQUAL_STRING("http://z", cfg.url.c_str());
}

// ---- header injection: auth_header must not smuggle a second HTTP header ----

// parse() only splits on '\n', so an embedded '\r' NOT at the very end of the
// line survives trim() (which only trims leading/trailing whitespace) and would
// otherwise reach http.addHeader() verbatim — some lenient HTTP parsers treat a
// lone CR as a line terminator, which would let this smuggle a second header.
void test_auth_header_with_embedded_cr_rejected() {
  Config cfg;
  std::string err;
  const std::string valueWithEmbeddedCr = "Bearer abc\rX-Evil: 1";
  const bool ok =
      HttpMonitorConfig::parse(("url=http://a\nauth_header = " + valueWithEmbeddedCr + "\n").c_str(), cfg, err);
  TEST_ASSERT_TRUE(ok);  // url is still valid — only auth_header is rejected, not the whole file
  TEST_ASSERT_EQUAL_STRING("", cfg.authHeader.c_str());
}

void test_auth_header_without_embedded_cr_still_accepted() {
  Config cfg;
  std::string err;
  TEST_ASSERT_TRUE(HttpMonitorConfig::parse("url=http://a\nauth_header = Authorization: Bearer abc123\n", cfg, err));
  TEST_ASSERT_EQUAL_STRING("Authorization: Bearer abc123", cfg.authHeader.c_str());
}

// ---- length bounds (Storage.readFile() caps at 50 KB; a pathological single
// long line shouldn't hand an unbounded string to a normally-short field) ----

void test_url_length_is_bounded() {
  Config cfg;
  std::string err;
  const std::string longUrl = "http://x/" + std::string(1000, 'a');
  TEST_ASSERT_TRUE(HttpMonitorConfig::parse(("url=" + longUrl + "\n").c_str(), cfg, err));
  TEST_ASSERT_TRUE(cfg.url.size() <= 256);
}

void test_auth_header_length_is_bounded() {
  Config cfg;
  std::string err;
  const std::string longAuth = std::string(1000, 'c');
  TEST_ASSERT_TRUE(HttpMonitorConfig::parse(("url=http://a\nauth_header=" + longAuth + "\n").c_str(), cfg, err));
  TEST_ASSERT_TRUE(cfg.authHeader.size() <= 512);
}

// ---- blank lines ----

void test_blank_lines_ignored() {
  Config cfg;
  std::string err;
  TEST_ASSERT_TRUE(HttpMonitorConfig::parse("\n\nurl = http://e\n\n\n", cfg, err));
  TEST_ASSERT_EQUAL_STRING("http://e", cfg.url.c_str());
}

// ---- unknown keys ----

void test_unknown_keys_ignored() {
  Config cfg;
  std::string err;
  TEST_ASSERT_TRUE(HttpMonitorConfig::parse("url = http://f\nbanana = yes\n", cfg, err));
  TEST_ASSERT_EQUAL_STRING("http://f", cfg.url.c_str());
}

// ---- numeric clamps ----

// The liveness dial (and its dial_tick_sec key) is gone -- an SD card left over
// from before this change still has the key in monitor.conf, and it must parse
// like any other unknown key: silently ignored, not a fatal error.
void test_dial_tick_sec_key_still_parses_as_unknown() {
  Config cfg;
  std::string err;
  const bool ok = HttpMonitorConfig::parse("url=http://g\ndial_tick_sec=5\n", cfg, err);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_STRING("http://g", cfg.url.c_str());
}

void test_interval_sec_clamped_low() {
  Config cfg;
  std::string err;
  HttpMonitorConfig::parse("url=http://g\ninterval_sec=1\n", cfg, err);
  TEST_ASSERT_EQUAL(5, cfg.intervalSec);
}

void test_interval_sec_clamped_high() {
  Config cfg;
  std::string err;
  HttpMonitorConfig::parse("url=http://g\ninterval_sec=999999\n", cfg, err);
  TEST_ASSERT_EQUAL(3600, cfg.intervalSec);
}

void test_timeout_ms_clamped_low() {
  Config cfg;
  std::string err;
  HttpMonitorConfig::parse("url=http://g\ntimeout_ms=1\n", cfg, err);
  TEST_ASSERT_EQUAL(1000, cfg.timeoutMs);
}

void test_timeout_ms_clamped_high() {
  Config cfg;
  std::string err;
  HttpMonitorConfig::parse("url=http://g\ntimeout_ms=999999\n", cfg, err);
  TEST_ASSERT_EQUAL(30000, cfg.timeoutMs);
}

// ---- missing url ----

void test_missing_url_is_fatal() {
  Config cfg;
  std::string err;
  bool ok = HttpMonitorConfig::parse("interval_sec = 10\n", cfg, err);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_TRUE(err.find("url") != std::string::npos);
}

void test_empty_text_is_fatal_missing_url() {
  Config cfg;
  std::string err;
  bool ok = HttpMonitorConfig::parse("", cfg, err);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_TRUE(err.find("url") != std::string::npos);
}

// ---- whitespace trimming ----

void test_whitespace_trimmed_key_and_value() {
  Config cfg;
  std::string err;
  TEST_ASSERT_TRUE(HttpMonitorConfig::parse("   url    =    http://h   \n", cfg, err));
  TEST_ASSERT_EQUAL_STRING("http://h", cfg.url.c_str());
}

// ---- action_url ----

void test_action_url_absent_defaults_empty() {
  Config cfg;
  std::string err;
  TEST_ASSERT_TRUE(HttpMonitorConfig::parse("url=http://a\n", cfg, err));
  TEST_ASSERT_EQUAL_STRING("", cfg.actionUrl.c_str());
}

void test_action_url_parsed() {
  Config cfg;
  std::string err;
  TEST_ASSERT_TRUE(HttpMonitorConfig::parse("url=http://a\naction_url = http://192.168.0.24:8777/cmd\n", cfg, err));
  TEST_ASSERT_EQUAL_STRING("http://192.168.0.24:8777/cmd", cfg.actionUrl.c_str());
}

void test_action_url_trailing_slash_stripped() {
  Config cfg;
  std::string err;
  TEST_ASSERT_TRUE(HttpMonitorConfig::parse("url=http://a\naction_url = http://192.168.0.24:8777/cmd/\n", cfg, err));
  TEST_ASSERT_EQUAL_STRING("http://192.168.0.24:8777/cmd", cfg.actionUrl.c_str());
}

// ---- header injection: action_url must not smuggle a second HTTP header ----
// Mirrors auth_header's guard exactly (test_auth_header_with_embedded_cr_rejected):
// http.begin(url) puts action_url straight into the request line, so an embedded
// '\r' surviving trim() (which only trims leading/trailing whitespace) could let
// a crafted monitor.conf smuggle an extra header into every action request.
void test_action_url_with_embedded_cr_rejected() {
  Config cfg;
  std::string err;
  const std::string valueWithEmbeddedCr = "http://h/cmd\rX-Evil: 1";
  const bool ok =
      HttpMonitorConfig::parse(("url=http://a\naction_url = " + valueWithEmbeddedCr + "\n").c_str(), cfg, err);
  TEST_ASSERT_TRUE(ok);  // url is still valid -- only action_url is rejected, not the whole file
  TEST_ASSERT_EQUAL_STRING("", cfg.actionUrl.c_str());
  // Rejected action_url means all four buttons stay inert, same as if it were
  // never set.
  TEST_ASSERT_EQUAL_STRING("", HttpMonitorConfig::actionUrlFor(cfg, 0).c_str());
}

// ---- header injection: url must not smuggle a second HTTP header either ----
// Identical hole to action_url (same http.begin(url) sink in fetch()), so the
// same guard applies: reject the value outright rather than strip. A rejected
// url leaves it empty, so parse() fails with the existing "missing url" error --
// the correct, already-tested visible outcome, not a new error path.
void test_url_with_embedded_cr_fails_to_parse() {
  Config cfg;
  std::string err;
  const std::string urlWithEmbeddedCr = "http://a\rX-Evil: 1";
  const bool ok = HttpMonitorConfig::parse(("url = " + urlWithEmbeddedCr + "\n").c_str(), cfg, err);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_TRUE(err.find("url") != std::string::npos);
}

void test_action_url_for_empty_when_action_url_absent() {
  Config cfg;
  std::string err;
  HttpMonitorConfig::parse("url=http://a\n", cfg, err);
  TEST_ASSERT_EQUAL_STRING("", HttpMonitorConfig::actionUrlFor(cfg, 0).c_str());
  TEST_ASSERT_EQUAL_STRING("", HttpMonitorConfig::actionUrlFor(cfg, 1).c_str());
  TEST_ASSERT_EQUAL_STRING("", HttpMonitorConfig::actionUrlFor(cfg, 2).c_str());
  TEST_ASSERT_EQUAL_STRING("", HttpMonitorConfig::actionUrlFor(cfg, 3).c_str());
}

void test_action_url_for_builds_slot_urls() {
  Config cfg;
  std::string err;
  HttpMonitorConfig::parse("url=http://a\naction_url = http://h/cmd\n", cfg, err);
  TEST_ASSERT_EQUAL_STRING("http://h/cmd/up", HttpMonitorConfig::actionUrlFor(cfg, 0).c_str());
  TEST_ASSERT_EQUAL_STRING("http://h/cmd/down", HttpMonitorConfig::actionUrlFor(cfg, 1).c_str());
  TEST_ASSERT_EQUAL_STRING("http://h/cmd/left", HttpMonitorConfig::actionUrlFor(cfg, 2).c_str());
  TEST_ASSERT_EQUAL_STRING("http://h/cmd/right", HttpMonitorConfig::actionUrlFor(cfg, 3).c_str());
}

// ---- auth_header ----

void test_auth_header_parsed() {
  Config cfg;
  std::string err;
  TEST_ASSERT_TRUE(HttpMonitorConfig::parse("url=http://i\nauth_header = Authorization: Bearer abc123\n", cfg, err));
  TEST_ASSERT_EQUAL_STRING("Authorization: Bearer abc123", cfg.authHeader.c_str());
}

// `title` is no longer a device-side key (the server owns the title outright),
// but an SD card left over from before this change still has one — it must be
// silently ignored like any other unknown key, not treated as a parse error.
void test_title_key_still_parses_as_unknown() {
  Config cfg;
  std::string err;
  const bool ok = HttpMonitorConfig::parse("url=http://i\ntitle = prod-1\n", cfg, err);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_STRING("http://i", cfg.url.c_str());
}

// ---- full_refresh_every ----

void test_full_refresh_every_zero_disables() {
  Config cfg;
  std::string err;
  TEST_ASSERT_TRUE(HttpMonitorConfig::parse("url=http://j\nfull_refresh_every=0\n", cfg, err));
  TEST_ASSERT_EQUAL(0, cfg.fullRefreshEvery);
}

void test_full_refresh_every_default() {
  Config cfg;
  std::string err;
  TEST_ASSERT_TRUE(HttpMonitorConfig::parse("url=http://k\n", cfg, err));
  TEST_ASSERT_EQUAL(20, cfg.fullRefreshEvery);
}

// ---- font_size ----

void test_font_size_default_when_absent() {
  Config cfg;
  std::string err;
  TEST_ASSERT_TRUE(HttpMonitorConfig::parse("url=http://k\n", cfg, err));
  TEST_ASSERT_EQUAL(2, cfg.fontSize);
}

void test_font_size_clamped_low() {
  Config cfg;
  std::string err;
  HttpMonitorConfig::parse("url=http://g\nfont_size=-5\n", cfg, err);
  TEST_ASSERT_EQUAL(0, cfg.fontSize);
}

void test_font_size_clamped_high() {
  Config cfg;
  std::string err;
  HttpMonitorConfig::parse("url=http://g\nfont_size=99\n", cfg, err);
  TEST_ASSERT_EQUAL(3, cfg.fontSize);
}

void test_font_size_valid_value_parsed() {
  Config cfg;
  std::string err;
  HttpMonitorConfig::parse("url=http://g\nfont_size=1\n", cfg, err);
  TEST_ASSERT_EQUAL(1, cfg.fontSize);
}

// ---- wifi_hold_sec ----

void test_wifi_hold_sec_default_is_30() {
  Config cfg;
  std::string err;
  TEST_ASSERT_TRUE(HttpMonitorConfig::parse("url=http://k\n", cfg, err));
  TEST_ASSERT_EQUAL(HttpMonitorConfig::DEFAULT_WIFI_HOLD_SEC, cfg.wifiHoldSec);
  TEST_ASSERT_EQUAL(30, cfg.wifiHoldSec);
}

void test_wifi_hold_sec_parsed() {
  Config cfg;
  std::string err;
  HttpMonitorConfig::parse("url=http://k\nwifi_hold_sec=90\n", cfg, err);
  TEST_ASSERT_EQUAL(90, cfg.wifiHoldSec);
}

void test_wifi_hold_sec_clamped_low() {
  Config cfg;
  std::string err;
  HttpMonitorConfig::parse("url=http://k\nwifi_hold_sec=0\n", cfg, err);
  TEST_ASSERT_EQUAL(HttpMonitorConfig::MIN_WIFI_HOLD_SEC, cfg.wifiHoldSec);
}

void test_wifi_hold_sec_clamped_high() {
  Config cfg;
  std::string err;
  HttpMonitorConfig::parse("url=http://k\nwifi_hold_sec=999999\n", cfg, err);
  TEST_ASSERT_EQUAL(HttpMonitorConfig::MAX_WIFI_HOLD_SEC, cfg.wifiHoldSec);
}

// ---- autoDropWifi() -- the precise 300s boundary matters: this predicate is
// what decides whether the device ever powers WiFi off between polls, so an
// off-by-one here silently changes battery behavior for every interval near
// the boundary. ----

void test_auto_drop_wifi_false_at_300_boundary() {
  Config cfg;
  std::string err;
  HttpMonitorConfig::parse("url=http://k\ninterval_sec=300\n", cfg, err);
  TEST_ASSERT_FALSE(HttpMonitorConfig::autoDropWifi(cfg));
}

void test_auto_drop_wifi_true_just_above_300_boundary() {
  Config cfg;
  std::string err;
  HttpMonitorConfig::parse("url=http://k\ninterval_sec=301\n", cfg, err);
  TEST_ASSERT_TRUE(HttpMonitorConfig::autoDropWifi(cfg));
}

void test_auto_drop_wifi_false_for_default_interval() {
  Config cfg;
  std::string err;
  HttpMonitorConfig::parse("url=http://k\n", cfg, err);
  TEST_ASSERT_FALSE(HttpMonitorConfig::autoDropWifi(cfg));
}

void test_auto_drop_wifi_true_for_large_interval() {
  Config cfg;
  std::string err;
  HttpMonitorConfig::parse("url=http://k\ninterval_sec=3600\n", cfg, err);
  TEST_ASSERT_TRUE(HttpMonitorConfig::autoDropWifi(cfg));
}

// ---- battery_min_pct ----

void test_battery_min_pct_default_is_5() {
  Config cfg;
  std::string err;
  TEST_ASSERT_TRUE(HttpMonitorConfig::parse("url=http://k\n", cfg, err));
  TEST_ASSERT_EQUAL(HttpMonitorConfig::DEFAULT_BATTERY_MIN_PCT, cfg.batteryMinPct);
  TEST_ASSERT_EQUAL(5, cfg.batteryMinPct);
}

void test_battery_min_pct_parsed() {
  Config cfg;
  std::string err;
  HttpMonitorConfig::parse("url=http://k\nbattery_min_pct=10\n", cfg, err);
  TEST_ASSERT_EQUAL(10, cfg.batteryMinPct);
}

// 0 is a meaningful value, not a "missing" one: it disables the battery-critical
// guard entirely, the same way dial_tick_sec=0 used to disable the liveness
// dial. It must survive the clamp intact.
void test_battery_min_pct_zero_disables_and_survives_clamp() {
  Config cfg;
  std::string err;
  HttpMonitorConfig::parse("url=http://k\nbattery_min_pct=0\n", cfg, err);
  TEST_ASSERT_EQUAL(0, cfg.batteryMinPct);
}

void test_battery_min_pct_clamped_low() {
  Config cfg;
  std::string err;
  HttpMonitorConfig::parse("url=http://k\nbattery_min_pct=-9\n", cfg, err);
  TEST_ASSERT_EQUAL(HttpMonitorConfig::MIN_BATTERY_MIN_PCT, cfg.batteryMinPct);
}

void test_battery_min_pct_clamped_high() {
  Config cfg;
  std::string err;
  HttpMonitorConfig::parse("url=http://k\nbattery_min_pct=999\n", cfg, err);
  TEST_ASSERT_EQUAL(HttpMonitorConfig::MAX_BATTERY_MIN_PCT, cfg.batteryMinPct);
  TEST_ASSERT_EQUAL(50, cfg.batteryMinPct);
}

// ============================================================
void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_when_keys_absent);
  RUN_TEST(test_key_value_with_spaces);
  RUN_TEST(test_key_value_no_spaces);
  RUN_TEST(test_comment_lines_ignored);
  RUN_TEST(test_inline_trailing_comment_ignored);
  RUN_TEST(test_hash_inside_value_without_preceding_space_preserved);
  RUN_TEST(test_hash_in_url_fragment_preserved);
  RUN_TEST(test_trailing_comment_after_whitespace_still_stripped);
  RUN_TEST(test_full_line_comment_still_ignored);
  RUN_TEST(test_auth_header_with_embedded_cr_rejected);
  RUN_TEST(test_auth_header_without_embedded_cr_still_accepted);
  RUN_TEST(test_url_length_is_bounded);
  RUN_TEST(test_auth_header_length_is_bounded);
  RUN_TEST(test_blank_lines_ignored);
  RUN_TEST(test_unknown_keys_ignored);
  RUN_TEST(test_dial_tick_sec_key_still_parses_as_unknown);
  RUN_TEST(test_interval_sec_clamped_low);
  RUN_TEST(test_interval_sec_clamped_high);
  RUN_TEST(test_timeout_ms_clamped_low);
  RUN_TEST(test_timeout_ms_clamped_high);
  RUN_TEST(test_missing_url_is_fatal);
  RUN_TEST(test_empty_text_is_fatal_missing_url);
  RUN_TEST(test_whitespace_trimmed_key_and_value);
  RUN_TEST(test_auth_header_parsed);
  RUN_TEST(test_title_key_still_parses_as_unknown);
  RUN_TEST(test_action_url_absent_defaults_empty);
  RUN_TEST(test_action_url_parsed);
  RUN_TEST(test_action_url_trailing_slash_stripped);
  RUN_TEST(test_action_url_with_embedded_cr_rejected);
  RUN_TEST(test_url_with_embedded_cr_fails_to_parse);
  RUN_TEST(test_action_url_for_empty_when_action_url_absent);
  RUN_TEST(test_action_url_for_builds_slot_urls);
  RUN_TEST(test_full_refresh_every_zero_disables);
  RUN_TEST(test_full_refresh_every_default);
  RUN_TEST(test_font_size_default_when_absent);
  RUN_TEST(test_font_size_clamped_low);
  RUN_TEST(test_font_size_clamped_high);
  RUN_TEST(test_font_size_valid_value_parsed);
  RUN_TEST(test_wifi_hold_sec_default_is_30);
  RUN_TEST(test_wifi_hold_sec_parsed);
  RUN_TEST(test_wifi_hold_sec_clamped_low);
  RUN_TEST(test_wifi_hold_sec_clamped_high);
  RUN_TEST(test_auto_drop_wifi_false_at_300_boundary);
  RUN_TEST(test_auto_drop_wifi_true_just_above_300_boundary);
  RUN_TEST(test_auto_drop_wifi_false_for_default_interval);
  RUN_TEST(test_auto_drop_wifi_true_for_large_interval);
  RUN_TEST(test_battery_min_pct_default_is_5);
  RUN_TEST(test_battery_min_pct_parsed);
  RUN_TEST(test_battery_min_pct_zero_disables_and_survives_clamp);
  RUN_TEST(test_battery_min_pct_clamped_low);
  RUN_TEST(test_battery_min_pct_clamped_high);
  return UNITY_END();
}
