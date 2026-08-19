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
  TEST_ASSERT_EQUAL_STRING("HTTP Monitor", cfg.title.c_str());
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

void test_title_length_is_bounded() {
  Config cfg;
  std::string err;
  const std::string longTitle = std::string(1000, 'b');
  TEST_ASSERT_TRUE(HttpMonitorConfig::parse(("url=http://a\ntitle=" + longTitle + "\n").c_str(), cfg, err));
  TEST_ASSERT_TRUE(cfg.title.size() <= 64);
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

void test_dial_tick_sec_defaults_to_5() {
  Config cfg;
  std::string err;
  HttpMonitorConfig::parse("url=http://g\n", cfg, err);
  TEST_ASSERT_EQUAL(HttpMonitorConfig::DEFAULT_DIAL_TICK_SEC, cfg.dialTickSec);
}

void test_dial_tick_sec_zero_disables_the_dial() {
  // 0 is a meaningful value, not a "missing" one: it switches the liveness dial
  // off entirely, which is what lets a static dashboard leave the e-ink panel
  // completely idle between polls. It must survive the clamp.
  Config cfg;
  std::string err;
  HttpMonitorConfig::parse("url=http://g\ndial_tick_sec=0\n", cfg, err);
  TEST_ASSERT_EQUAL(0, cfg.dialTickSec);
}

void test_dial_tick_sec_clamped_low() {
  Config cfg;
  std::string err;
  HttpMonitorConfig::parse("url=http://g\ndial_tick_sec=-9\n", cfg, err);
  TEST_ASSERT_EQUAL(0, cfg.dialTickSec);
}

void test_dial_tick_sec_clamped_high() {
  Config cfg;
  std::string err;
  HttpMonitorConfig::parse("url=http://g\ndial_tick_sec=99999\n", cfg, err);
  TEST_ASSERT_EQUAL(60, cfg.dialTickSec);
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

// ---- title / auth_header ----

void test_title_and_auth_header_parsed() {
  Config cfg;
  std::string err;
  TEST_ASSERT_TRUE(
      HttpMonitorConfig::parse("url=http://i\ntitle = prod-1\nauth_header = Authorization: Bearer abc123\n", cfg, err));
  TEST_ASSERT_EQUAL_STRING("prod-1", cfg.title.c_str());
  TEST_ASSERT_EQUAL_STRING("Authorization: Bearer abc123", cfg.authHeader.c_str());
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
  RUN_TEST(test_title_length_is_bounded);
  RUN_TEST(test_auth_header_length_is_bounded);
  RUN_TEST(test_blank_lines_ignored);
  RUN_TEST(test_unknown_keys_ignored);
  RUN_TEST(test_dial_tick_sec_defaults_to_5);
  RUN_TEST(test_dial_tick_sec_zero_disables_the_dial);
  RUN_TEST(test_dial_tick_sec_clamped_low);
  RUN_TEST(test_dial_tick_sec_clamped_high);
  RUN_TEST(test_interval_sec_clamped_low);
  RUN_TEST(test_interval_sec_clamped_high);
  RUN_TEST(test_timeout_ms_clamped_low);
  RUN_TEST(test_timeout_ms_clamped_high);
  RUN_TEST(test_missing_url_is_fatal);
  RUN_TEST(test_empty_text_is_fatal_missing_url);
  RUN_TEST(test_whitespace_trimmed_key_and_value);
  RUN_TEST(test_title_and_auth_header_parsed);
  RUN_TEST(test_full_refresh_every_zero_disables);
  RUN_TEST(test_full_refresh_every_default);
  RUN_TEST(test_font_size_default_when_absent);
  RUN_TEST(test_font_size_clamped_low);
  RUN_TEST(test_font_size_clamped_high);
  RUN_TEST(test_font_size_valid_value_parsed);
  return UNITY_END();
}
