// test/test_http_monitor_power/test_http_monitor_power.cpp
// Tests HttpMonitorPower.h -- the pure sliced-light-sleep guard logic shared
// (conceptually; there's only one caller) with HttpMonitorActivity::loop().
// Header-only, no Arduino/HAL dependency -- the actual esp_sleep_enable_timer_
// wakeup()/esp_light_sleep_start() calls live in HttpMonitorActivity.cpp, which
// this suite cannot reach (needs real hardware / on-device verification).
// Run: pio test -e native -f test_http_monitor_power

#include <unity.h>

#include "../../src/activities/apps/HttpMonitorPower.h"

using namespace HttpMonitorPower;

// ---- remainingMs: wrap-safe "how long until this deadline fires" ----

void test_remaining_ms_not_yet_elapsed() {
  TEST_ASSERT_EQUAL(400, remainingMs(/*elapsedMs=*/100, /*totalMs=*/500));
}

void test_remaining_ms_zero_at_exact_boundary() {
  TEST_ASSERT_EQUAL(0, remainingMs(/*elapsedMs=*/500, /*totalMs=*/500));
}

// The deadline has already passed -- plain unsigned subtraction (totalMs -
// elapsedMs) would wrap to a huge positive number here; must clamp to 0
// instead, matching every other timer in this file's wrap-safe pattern.
void test_remaining_ms_clamps_to_zero_past_deadline() {
  TEST_ASSERT_EQUAL(0, remainingMs(/*elapsedMs=*/600, /*totalMs=*/500));
}

void test_remaining_ms_zero_elapsed_returns_full_duration() {
  TEST_ASSERT_EQUAL(500, remainingMs(/*elapsedMs=*/0, /*totalMs=*/500));
}

// ---- canLightSleep: every guard must independently veto a sleep ----

static SleepGuardInput baseInput() {
  SleepGuardInput in{};
  in.showingDashboard = true;
  in.actionAckPending = false;
  in.renderBusy = false;
  in.wifiAssociated = false;  // S4: WiFi must be down (auto-drop) to sleep
  in.wifiHoldActive = false;
  in.wifiHoldRemainingMs = 0;
  in.pollRemainingMs = 10000;  // comfortably more than a slice
  in.sliceMs = 200;
  return in;
}

void test_can_sleep_when_all_guards_clear() {
  const auto in = baseInput();
  TEST_ASSERT_TRUE(canLightSleep(in));
}

void test_cannot_sleep_when_not_showing_dashboard() {
  auto in = baseInput();
  in.showingDashboard = false;
  TEST_ASSERT_FALSE(canLightSleep(in));
}

void test_cannot_sleep_with_action_ack_pending() {
  auto in = baseInput();
  in.actionAckPending = true;
  TEST_ASSERT_FALSE(canLightSleep(in));
}

void test_cannot_sleep_while_render_busy() {
  auto in = baseInput();
  in.renderBusy = true;
  TEST_ASSERT_FALSE(canLightSleep(in));
}

// S4: never light-sleep while WiFi is associated. Manual esp_light_sleep_
// start() powers the modem outside any DTIM alignment (this tree never
// configures esp_pm/esp_wifi_set_ps for this path) -- missed beacons risk an
// AP deauth. Light sleep is only safe once auto-drop has actually taken WiFi
// down between polls.
void test_cannot_sleep_while_wifi_associated() {
  auto in = baseInput();
  in.wifiAssociated = true;
  TEST_ASSERT_FALSE(canLightSleep(in));
}

void test_can_sleep_when_wifi_not_associated_and_all_else_clear() {
  auto in = baseInput();
  in.wifiAssociated = false;
  TEST_ASSERT_TRUE(canLightSleep(in));
}

// A poll due within the next slice must not be overshot by sleeping past it.
void test_cannot_sleep_when_poll_due_within_slice() {
  auto in = baseInput();
  in.pollRemainingMs = 150;  // < sliceMs (200)
  TEST_ASSERT_FALSE(canLightSleep(in));
}

void test_can_sleep_when_poll_remaining_exactly_one_slice() {
  auto in = baseInput();
  in.pollRemainingMs = 200;  // == sliceMs: a full slice still fits before it fires
  TEST_ASSERT_TRUE(canLightSleep(in));
}

// A WiFi hold that's about to expire within the next slice must not be
// overshot either -- same reasoning as the poll deadline.
void test_cannot_sleep_when_wifi_hold_about_to_expire() {
  auto in = baseInput();
  in.wifiHoldActive = true;
  in.wifiHoldRemainingMs = 50;  // < sliceMs
  TEST_ASSERT_FALSE(canLightSleep(in));
}

// An active hold with plenty of time left doesn't block sleep on its own.
void test_can_sleep_with_wifi_hold_active_and_far_from_expiry() {
  auto in = baseInput();
  in.wifiHoldActive = true;
  in.wifiHoldRemainingMs = 20000;
  TEST_ASSERT_TRUE(canLightSleep(in));
}

// wifiHoldRemainingMs is meaningless (and must be ignored) when the hold isn't
// active at all -- a stale/garbage value must not veto sleep.
void test_wifi_hold_remaining_ignored_when_hold_not_active() {
  auto in = baseInput();
  in.wifiHoldActive = false;
  in.wifiHoldRemainingMs = 0;  // would veto if (incorrectly) checked unconditionally
  TEST_ASSERT_TRUE(canLightSleep(in));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_remaining_ms_not_yet_elapsed);
  RUN_TEST(test_remaining_ms_zero_at_exact_boundary);
  RUN_TEST(test_remaining_ms_clamps_to_zero_past_deadline);
  RUN_TEST(test_remaining_ms_zero_elapsed_returns_full_duration);
  RUN_TEST(test_can_sleep_when_all_guards_clear);
  RUN_TEST(test_cannot_sleep_when_not_showing_dashboard);
  RUN_TEST(test_cannot_sleep_with_action_ack_pending);
  RUN_TEST(test_cannot_sleep_while_render_busy);
  RUN_TEST(test_cannot_sleep_while_wifi_associated);
  RUN_TEST(test_can_sleep_when_wifi_not_associated_and_all_else_clear);
  RUN_TEST(test_cannot_sleep_when_poll_due_within_slice);
  RUN_TEST(test_can_sleep_when_poll_remaining_exactly_one_slice);
  RUN_TEST(test_cannot_sleep_when_wifi_hold_about_to_expire);
  RUN_TEST(test_can_sleep_with_wifi_hold_active_and_far_from_expiry);
  RUN_TEST(test_wifi_hold_remaining_ignored_when_hold_not_active);
  return UNITY_END();
}
