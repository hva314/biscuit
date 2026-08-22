#pragma once

// Pure sliced-light-sleep guard logic for HttpMonitorActivity's R1 (light
// sleep between polls). No Arduino/HAL dependency -- safe to unit test
// natively. The actual esp_sleep_enable_timer_wakeup()/esp_light_sleep_start()
// calls, and every input's real-time source (millis(), RenderLock::peek()),
// live in HttpMonitorActivity.cpp, which is not natively testable -- pulling
// the DECISION out into pure arithmetic is what makes it testable at all.
namespace HttpMonitorPower {

// Wrap-safe "how long until this deadline fires", given how long it has
// already been running (elapsedMs) and its full duration (totalMs). Plain
// unsigned subtraction (totalMs - elapsedMs) would wrap to a huge positive
// number once the deadline has passed; clamping to 0 instead matches every
// other timer in HttpMonitorActivity's wrap-safe `millis() - start >=
// duration` pattern.
inline unsigned long remainingMs(unsigned long elapsedMs, unsigned long totalMs) {
  return (elapsedMs >= totalMs) ? 0UL : (totalMs - elapsedMs);
}

// Every input the sleep decision depends on, computed by the caller so this
// function itself needs no Arduino/HAL access. `renderBusy` is the only field
// backed by a live check (RenderLock::peek()) rather than activity state --
// see HttpMonitorActivity::maybeLightSleep() for why it must be the LAST
// thing read before the actual sleep call.
struct SleepGuardInput {
  bool showingDashboard;              // state == SHOWING -- never sleep mid-fetch,
                                      // mid-reconnect, or on the error/no-config/
                                      // battery-critical screens
  bool actionAckPending;              // actionAck[0] != '\0' -- must clear on its
                                      // own 1500ms schedule, not be delayed by sleep
  bool renderBusy;                    // RenderLock::peek() -- a render is in
                                      // progress or about to be; never straddle it
  bool wifiAssociated;                // WiFi.getMode() != WIFI_MODE_NULL -- manual
                                      // esp_light_sleep_start() powers the modem
                                      // outside any DTIM alignment (this tree never
                                      // configures esp_pm/esp_wifi_set_ps for this
                                      // path), so sleeping while associated risks a
                                      // missed-beacon deauth. Only ever false once
                                      // auto-drop has actually taken WiFi down.
  bool wifiHoldActive;                // wifiHoldActive
  unsigned long wifiHoldRemainingMs;  // remainingMs(...) against wifi_hold_sec;
                                      // meaningless (and ignored) when !wifiHoldActive
  unsigned long pollRemainingMs;      // remainingMs(...) against pollIntervalMs
  unsigned long sliceMs;              // the light-sleep slice length
};

// Whether HttpMonitorActivity::loop() may enter a light-sleep slice this
// iteration. A poll or an active WiFi hold expiring must never be overshot by
// a slice -- both are vetoed once less than one full slice remains, not only
// once they've already fired, so the device wakes up to handle them on time
// rather than up to a whole slice late.
inline bool canLightSleep(const SleepGuardInput& in) {
  if (!in.showingDashboard) return false;
  if (in.actionAckPending) return false;
  if (in.renderBusy) return false;
  if (in.wifiAssociated) return false;
  if (in.wifiHoldActive && in.wifiHoldRemainingMs < in.sliceMs) return false;
  if (in.pollRemainingMs < in.sliceMs) return false;
  return true;
}

}  // namespace HttpMonitorPower
