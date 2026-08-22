#include "HttpMonitorActivity.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HalPowerManager.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_sleep.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "HttpMonitorLayout.h"
#include "HttpMonitorPower.h"
#include "MappedInputManager.h"

// Typed schema names used by renderDashboard()'s per-row dispatch.
using HttpMonitorSchema::RowAlign;
using HttpMonitorSchema::RowType;
using HttpMonitorSchema::SIZE_INHERIT;
#include "WifiCredentialStore.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/RadioManager.h"

namespace {

// The server owns the title outright (monitor.conf no longer has one); this is
// only ever shown before the first successful poll or when a poll has never
// returned a title.
constexpr const char* kDefaultTitle = "HTTP Monitor";

std::string trim(const std::string& s) {
  const size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  const size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

}  // namespace

// ----------------------------------------------------------------
// Auth header
// ----------------------------------------------------------------

// Shared by fetch() and sendAction(): splits config.authHeader on the first
// ':' and adds it verbatim as an HTTP header. A no-op when authHeader is empty
// or has no colon.
void HttpMonitorActivity::applyAuthHeader(HTTPClient& http) {
  if (config.authHeader.empty()) return;
  const size_t colon = config.authHeader.find(':');
  if (colon == std::string::npos) return;
  const std::string headerName = trim(config.authHeader.substr(0, colon));
  const std::string headerValue = trim(config.authHeader.substr(colon + 1));
  if (!headerName.empty()) {
    http.addHeader(headerName.c_str(), headerValue.c_str());
  }
}

// ----------------------------------------------------------------
// Lifecycle
// ----------------------------------------------------------------

void HttpMonitorActivity::onEnter() {
  Activity::onEnter();
  loadConfig();
  requestUpdate();
}

void HttpMonitorActivity::onExit() {
  Activity::onExit();
  // Belt-and-braces: render() already restores Portrait after every frame, but
  // this guarantees no other activity ever inherits an upside-down renderer,
  // even if exit happens mid-fetch before a frame completes.
  renderer.setOrientation(GfxRenderer::Portrait);
}

void HttpMonitorActivity::loadConfig() {
  configError.clear();
  if (HttpMonitorConfig::loadFromSd(config, configError)) {
    state = IDLE;
    pollIntervalMs = static_cast<unsigned long>(config.intervalSec) * 1000UL;
    pollsUntilClean = (config.fullRefreshEvery > 0) ? config.fullRefreshEvery : 1;
  } else {
    state = NO_CONFIG;
  }
}

// ----------------------------------------------------------------
// Loop
// ----------------------------------------------------------------

void HttpMonitorActivity::loop() {
  // Reset every iteration -- see the header comment on sleptThisIteration.
  // Only maybeLightSleep() (at the very bottom of this function) ever sets it
  // true, and only when a slice actually ran, so every early return below
  // (Back, NO_CONFIG, BATTERY_CRITICAL, ...) correctly leaves it false.
  sleptThisIteration = false;

  // Back always wins, so input stays responsive even while a fetch is due.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (state == NO_CONFIG) {
    return;
  }

  if (state == BATTERY_CRITICAL) {
    // Recovery: USB power removes the brownout risk that put us here, and
    // requiring a restart to resume monitoring would be needlessly hostile
    // for an unattended wall panel someone just walked over and plugged in.
    if (gpio.isUsbConnected()) {
      state = hasDashboard ? SHOWING : IDLE;
      lastPollMs = millis();
      requestUpdate();
    }
    return;
  }

  checkBatteryCritical();
  if (state == BATTERY_CRITICAL) return;

  if (state == IDLE) {
    lastPollMs = millis();
    state = FETCHING;
    if (!hasDashboard) requestUpdate(true);
    fetch();
    return;
  }

  // Manual refresh
  if ((state == SHOWING || state == ERROR) && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    lastPollMs = millis();
    forceRedraw = true;
    state = FETCHING;
    if (!hasDashboard) requestUpdate(true);
    fetch();
    return;
  }

  // Button actions: Left/Right/Up/Down send a command to config.actionUrl and
  // show a brief ack, nothing more -- no re-poll, no rendering of the reply, no
  // poll-timer reset. A press is a no-op when action_url isn't configured (the
  // most common case today), matching the previous "deliberately unbound"
  // behavior for servers that don't know about actions.
  if ((state == SHOWING || state == ERROR) && !config.actionUrl.empty()) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      sendAction(ActionSlot::Up);
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      sendAction(ActionSlot::Down);
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      sendAction(ActionSlot::Left);
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      sendAction(ActionSlot::Right);
    }
  }

  // Clear an expired action ack -- a one-shot timer, not a per-frame poll: once
  // it fires, one more update erases the indicator and it stays cleared.
  // Wrap-safe subtraction (millis() - start), matching lastPollMs.
  if (actionAck[0] != '\0' && millis() - actionAckStartMs >= ACTION_ACK_MS) {
    {
      RenderLock lock(*this);
      actionAck[0] = '\0';
    }
    requestUpdate();
  }

  // Clear an expired WiFi hold -- also one-shot, matching the actionAck clear
  // above (and, like it, the write is under a RenderLock -- the render task
  // reads wifiHoldActive without one to pick the header glyph). Also drops
  // WiFi right here rather than waiting for the next fetch() to notice: fetch()
  // only shuts WiFi down after a poll completes, which could be up to a full
  // interval_sec away, and until then the header glyph would fall through to
  // AlwaysOn (WiFi genuinely is still up) even though auto-drop is armed --
  // indistinguishable from a non-auto-drop config at a glance. Dropping it here
  // instead makes the glyph switch straight to Dropped as soon as the hold
  // actually ends. Not gated on `state == SHOWING`: a hold can be armed from
  // ERROR too (action buttons work in ERROR), and the battery/panel-wear
  // reasoning for dropping promptly applies regardless of what's on screen.
  if (wifiHoldActive) {
    const unsigned long holdMs = static_cast<unsigned long>(config.wifiHoldSec) * 1000UL;
    if (millis() - wifiHoldStartMs >= holdMs) {
      {
        RenderLock lock(*this);
        wifiHoldActive = false;
      }
      if (HttpMonitorConfig::autoDropWifi(config)) {
        RADIO.shutdown();
      }
      if (state == SHOWING) requestUpdate();
    }
  }

  // Timed poll
  if (state == SHOWING || state == ERROR) {
    const unsigned long now = millis();
    if (now - lastPollMs >= pollIntervalMs) {
      lastPollMs = now;
      state = FETCHING;
      if (!hasDashboard) requestUpdate(true);
      fetch();
    }
  }

  maybeLightSleep();
}

// Throttled battery-critical check (BATTERY_CHECK_INTERVAL_MS): getBattery
// Percentage() is uncached on X4 (reads the ADC every call; X3 caches for
// BATTERY_POLL_MS internally, but this file can't assume which board it's on),
// so this must not run every loop() iteration. config.batteryMinPct == 0
// disables the guard outright -- same convention as autoDropWifi().
void HttpMonitorActivity::checkBatteryCritical() {
  if (config.batteryMinPct <= 0) return;

  const unsigned long now = millis();
  if (now - lastBatteryCheckMs < BATTERY_CHECK_INTERVAL_MS) return;
  lastBatteryCheckMs = now;

  // USB present, or a reading above the threshold, resets the streak -- only
  // CONSECUTIVE low readings count; see the field comment in the header for
  // why a single sample must not latch the (irreversible without USB) guard.
  if (gpio.isUsbConnected() || powerManager.getBatteryPercentage() > static_cast<uint16_t>(config.batteryMinPct)) {
    batteryCriticalConsecutiveLowReads = 0;
    return;
  }

  if (++batteryCriticalConsecutiveLowReads < BATTERY_CRITICAL_CONSECUTIVE_READS) return;

  RADIO.shutdown();
  {
    // Matches fetch()'s convention: `state` (read by the render task without a
    // lock -- ActivityManager.cpp) is only ever written under a RenderLock.
    RenderLock lock(*this);
    state = BATTERY_CRITICAL;
  }
  // Immediate, not deferred: this terminal screen exists specifically so a
  // brownout doesn't happen mid-refresh, so it must actually get painted
  // before anything else (including main.cpp's own auto-sleep check on the
  // next iteration) has a chance to act on the low battery first.
  requestUpdate(true);
}

// ----------------------------------------------------------------
// R1: sliced light sleep
// ----------------------------------------------------------------

// Enters a single SLICE_MS light-sleep slice when it's genuinely safe to.
//
// Render-in-progress guard (the hazard: esp_light_sleep_start() mid-SPI-
// transaction or mid-e-ink-update could leave the panel in a bad state).
// RenderLock::peek() is read as the LAST thing before the sleep calls, with
// nothing else executed in between -- same pattern EpubReaderActivity.cpp
// already uses to skip an action while a render is busy. On this single-core
// ESP32-C3 target, loop() and the render task can never truly run
// simultaneously; the render task can only get CPU time via a preemptive
// context switch (a FreeRTOS tick interrupt, since both run at priority 1).
// For light sleep to straddle a render, the render task would have to be
// notified-but-not-yet-scheduled at the exact moment peek() is read (which
// peek() can't see -- it only detects a render that has already taken the
// RenderLock), AND a tick interrupt would have to land in the handful of
// instructions between that read and esp_light_sleep_start(). That window is
// several orders of magnitude smaller than a FreeRTOS tick period, so the
// residual risk is the same class -- and same order of magnitude -- as the
// one EpubReaderActivity already accepts with the identical pattern. A
// blocking RenderLock acquire here would close even that sliver, but at the
// cost of stalling loop() (and therefore input handling) for the length of
// whatever render is in progress, which given e-ink refreshes can take
// seconds -- a strictly worse trade for a wall-mounted dashboard than the
// tiny residual race.
//
// millis() across light sleep: every timer in this file (lastPollMs,
// wifiHoldStartMs, actionAckStartMs, lastBatteryCheckMs) depends on millis()
// (backed by esp_timer_get_time(), see esp32-hal-misc.c) continuing to advance
// correctly across esp_light_sleep_start(). ESP-IDF's esp_timer is documented
// to resynchronize against the RTC on wake specifically so this holds, but
// that isn't something this native-only session can verify on real hardware.
// TREAT AS AN ON-DEVICE VERIFICATION ITEM: confirm poll cadence and the WiFi
// hold duration stay honest over several intervals. If it ever turns out NOT
// to hold, every timer here already reads `millis() - start >= duration`
// rather than an absolute deadline, which is the cheap half of a fix; the
// other half (measuring actual elapsed sleep time via esp_timer_get_time()
// before/after each slice and folding the delta back into the *Ms fields)
// is deliberately NOT added here -- it would be unverifiable complexity
// against a problem not yet confirmed to exist.
//
// Button-sampling arithmetic (why SLICE_MS is 40): let P be the effective
// main-loop sampling period while idle. main.cpp calls activityManager.loop()
// and checks skipLoopDelay() immediately after in the SAME iteration --
// skipLoopDelay() here returns sleptThisIteration (set below, only when a
// slice actually ran), so on every iteration that sleeps, main.cpp takes its
// yield() branch instead of stacking its own delay(10)/delay(50) on top of
// the slice. That branch also forces full clock for the remainder of the
// iteration (see skipLoopDelay()'s doc comment for why that's cheap and
// intentional), so P is just the sleep itself plus whatever small amount of
// full-clock work the rest of loop() and main.cpp's own per-iteration
// housekeeping do before the next slice starts -- call that overhead ~5ms
// (an estimate; not something this native-only session can measure). So
// P ~= SLICE_MS + 5ms = 45ms, down from ~90ms when main.cpp's 50ms idle delay
// was still stacking on top of every slice.
//
// InputManager::update() only commits a press once TWO consecutive samples,
// >DEBOUNCE_DELAY (5ms) apart, see the same held state -- a state change
// alone just resets the debounce clock without committing anything. Given
// sampling instants spaced P apart at an arbitrary phase relative to the
// physical press, the worst case (button makes contact just after a sample)
// needs the press held across not one but two full periods to guarantee a
// commit regardless of phase: d_worst ~= 2P.
//   - Original SLICE_MS=200, stacked with main.cpp's 50ms delay: P~=250ms,
//     d_worst~=500ms -- short taps, Back included, produced no event at all.
//   - SLICE_MS=40, still stacked with the 50ms delay: P~=90ms, d_worst~=180ms
//     -- still above a ~100ms illustrative tap in the strict worst case.
//   - SLICE_MS=40, with the 50ms delay replaced by skipLoopDelay()'s yield():
//     P~=45ms, d_worst~=90ms -- comfortably under a ~100ms tap, with margin.
// This is strictly better on both axes, not a responsiveness/power tradeoff:
// the 50ms idle delay was 10MHz-active time (dropping it removes awake time
// AND shrinks the sampling period), whereas the slice itself is genuine light
// sleep (~1mA-class) either way.
void HttpMonitorActivity::maybeLightSleep() {
  const unsigned long now = millis();
  const unsigned long holdMs = static_cast<unsigned long>(config.wifiHoldSec) * 1000UL;

  HttpMonitorPower::SleepGuardInput in{};
  in.showingDashboard = (state == SHOWING);
  in.actionAckPending = (actionAck[0] != '\0');
  in.wifiAssociated = (WiFi.getMode() != WIFI_MODE_NULL);
  in.wifiHoldActive = wifiHoldActive;
  in.wifiHoldRemainingMs = wifiHoldActive ? HttpMonitorPower::remainingMs(now - wifiHoldStartMs, holdMs) : 0UL;
  in.pollRemainingMs = HttpMonitorPower::remainingMs(now - lastPollMs, pollIntervalMs);
  in.sliceMs = SLICE_MS;
  // renderBusy MUST be the last field read, immediately before the guard
  // check below -- see the render-in-progress reasoning above.
  in.renderBusy = RenderLock::peek();

  if (!HttpMonitorPower::canLightSleep(in)) return;

  esp_sleep_enable_timer_wakeup(SLICE_US);
  esp_light_sleep_start();
  // The timer wakeup source persists once armed -- esp_sleep_enable_timer_
  // wakeup() is a one-way "arm", not a per-call configuration, and nothing
  // else in this tree ever calls esp_sleep_disable_wakeup_source(). Left
  // armed, EVERY later esp_deep_sleep_start() (main.cpp's enterDeepSleep())
  // would also wake on this same SLICE_MS-class RTC timer. On battery that's
  // masked by the battery-latch MOSFET cutting power outright, but on USB the
  // MCU stays powered through deep sleep, so it would wake and reboot
  // ~SLICE_MS after every single deep sleep -- a permanent reboot loop on exactly the
  // USB-powered wall panel this feature targets. Disarm it the moment this
  // slice is done; see also HalPowerManager::startDeepSleep()'s defensive
  // disable for the same source.
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);

  // A slice actually ran -- skipLoopDelay() reads this back in the SAME
  // main.cpp iteration to skip main.cpp's own redundant idle delay. See the
  // arithmetic above and skipLoopDelay()'s doc comment for why this must stay
  // conditional on having actually slept.
  sleptThisIteration = true;
}

// ----------------------------------------------------------------
// Fetch
// ----------------------------------------------------------------

void HttpMonitorActivity::fetch() {
  // Restore full CPU clock for this whole blocking window -- WiFi (steady-state
  // or reconnectWifiHeadless() below) and the HTTP GET further down both need
  // well above LOW_POWER_FREQ (ESP32 WiFi's RF/MAC layer needs ~80MHz+). R3's
  // allowPowerSaving() lets main.cpp downclock while genuinely idle between
  // polls, and once auto-drop has taken WiFi to WIFI_MODE_NULL, HalPowerManager's
  // WiFi guard (HalPowerManager.cpp) no longer blocks that downclock -- so by the
  // time a poll fires, the CPU can genuinely be sitting at 10MHz. A direct call,
  // not HalPowerManager::Lock: the render task already holds a Lock for the
  // duration of every render (ActivityManager.cpp), and this multi-second fetch()
  // can plausibly overlap one -- Lock is one-holder-at-a-time (HalPowerManager.cpp),
  // so a second acquirer would log "Lock already held, ignore" and get
  // valid=false. setPowerSaving(false) is idempotent and cannot contend; nothing
  // re-downclocks until main.cpp's loop-bottom, which only runs after
  // activityManager.loop() -- and therefore this whole function -- has returned.
  powerManager.setPowerSaving(false);

  if (WiFi.status() != WL_CONNECTED) {
    if (!HttpMonitorConfig::autoDropWifi(config)) {
      // Human-present path (interval_sec <= AUTO_DROP_MIN_INTERVAL_SEC): keep
      // today's exact interactive-picker behavior unchanged.
      RADIO.ensureWifi();
      startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                             [this](const ActivityResult& result) {
                               if (result.isCancelled || WiFi.status() != WL_CONNECTED) {
                                 {
                                   RenderLock lock(*this);
                                   fetchError = "WiFi not connected";
                                   httpStatusCode = 0;
                                   state = ERROR;
                                 }
                                 requestUpdate();
                               } else {
                                 fetch();  // retry now that WiFi is up
                               }
                             });
      return;
    }

    // Auto-drop path: WiFi is expected to be down between polls -- reconnect
    // headlessly and bounded. MUST NOT push WifiSelectionActivity here: an
    // unattended wall panel that pushes an interactive picker sits there until
    // someone walks over, and since that activity doesn't override
    // preventAutoSleep(), the device would then auto-sleep to a full power-off
    // needing a physical button press. A failed reconnect falls through to
    // ERROR and the existing "Timed poll" branch in loop() retries next interval.
    if (!reconnectWifiHeadless(WIFI_RECONNECT_TIMEOUT_MS, /*showAck=*/false)) {
      {
        RenderLock lock(*this);
        fetchError = "WiFi reconnect failed";
        httpStatusCode = 0;
        state = ERROR;
      }
      requestUpdate();
      RADIO.shutdown();  // nothing to hold onto; harmless if already down
      return;
    }
    // Fall through: WiFi is now up, continue to the GET below.
  }

  HTTPClient http;
  http.begin(config.url.c_str());
  http.setTimeout(config.timeoutMs);
  // Bug fix: setTimeout() only bounds the socket read (_tcpTimeout); the TCP
  // connect itself defaults to HTTPCLIENT_DEFAULT_TCP_TIMEOUT (5000ms)
  // regardless of timeout_ms unless set explicitly. Without this an
  // unreachable host blocked ~5s even with a much smaller configured
  // timeout_ms, contradicting docs/http-monitor.md's description of the key.
  http.setConnectTimeout(config.timeoutMs);
  applyAuthHeader(http);

  // Everything below is computed into LOCAL state only — `render()` runs on its
  // own FreeRTOS task and reads `dashboard`/`state`/`fetchError`/`httpStatusCode`
  // without a lock (ActivityManager.cpp:56: "the loop() method must be
  // responsible for acquir[ing one] if needed"). Committing all of it under a
  // single RenderLock at the end (not held across this blocking HTTP call) is
  // what makes the commit atomic from render()'s point of view.
  const int code = http.GET();
  bool ok = false;
  std::string errMsg;
  HttpMonitorSchema::Dashboard next;

  if (code == HTTP_CODE_OK) {
    const int64_t reportedLength = http.getSize();
    if (reportedLength <= 0 || reportedLength > MAX_BODY_BYTES) {
      errMsg = "Response too large or unknown length";
    } else {
      // Whitelist filter — every key HttpMonitorSchema::apply() reads must be
      // let through here, or the filtered doc silently drops it. `bar` is a
      // whole-subtree include (filter["bar"] = true): it keeps BOTH the legacy
      // integer form and the new object form. A nested object filter
      // (["bar"]["value"] = true, ...) would strip the object's inner keys AND
      // — worse — a nested filter on a primitive produces no value at all, so
      // legacy `"bar": 88` rows would lose their bar. The subtree include is the
      // only shape that preserves both forms.
      JsonDocument filter;
      filter["title"] = true;
      filter["updated"] = true;
      filter["fontSize"] = true;
      filter["rotation"] = true;
      filter["sections"][0]["heading"] = true;
      filter["sections"][0]["rows"][0]["type"] = true;
      filter["sections"][0]["rows"][0]["label"] = true;
      filter["sections"][0]["rows"][0]["value"] = true;
      filter["sections"][0]["rows"][0]["text"] = true;
      filter["sections"][0]["rows"][0]["glyphs"] = true;
      filter["sections"][0]["rows"][0]["bar"] = true;  // whole subtree: int + object forms
      filter["sections"][0]["rows"][0]["alert"] = true;
      filter["sections"][0]["rows"][0]["bold"] = true;
      filter["sections"][0]["rows"][0]["size"] = true;
      filter["sections"][0]["rows"][0]["align"] = true;
      filter["sections"][0]["rows"][0]["height"] = true;
      filter["sections"][0]["rows"][0]["inset"] = true;
      filter["sections"][0]["rows"][0]["lineWidth"] = true;
      filter["alerts"][0] = true;

      JsonDocument doc;
      const DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
      if (err) {
        errMsg = std::string("JSON parse failed: ") + err.c_str();
        LOG_ERR("HTTPMON", "JSON parse failed: %s", err.c_str());
      } else {
        // ---- apply response, enforcing every cap before it lands in `next` ----
        // The typed schema walk (type/align/bold/size, bar int-or-object, text,
        // glyphs, spacer/divider params, and all the caps) lives in the pure
        // header HttpMonitorSchema.h so the exact parse path is native-testable.
        HttpMonitorSchema::apply(doc, next);
        ok = true;
      }
    }
  } else {
    errMsg = (code > 0) ? ("HTTP " + std::to_string(code)) : "Connection failed";
    LOG_ERR("HTTPMON", "Fetch failed: %d", code);
  }

  http.end();

  // Only touch the panel if what the user would see actually differs from what is
  // already on it. A dashboard whose values are stable used to repaint — and so
  // flash — on every single poll; now a static dashboard leaves the panel idle.
  //
  // The comparison must be against what is DISPLAYED, not against `state`: at this
  // point `state` is always FETCHING (fetch() is only ever called after setting
  // it), and a background poll deliberately leaves the previous dashboard on
  // screen. `displayedState` tracks what was last actually pushed, which is what
  // makes the error -> same-dashboard transition repaint instead of being skipped
  // as "unchanged".
  bool contentChanged = false;
  {
    RenderLock lock(*this);
    if (ok) {
      contentChanged = (displayedState != SHOWING) || (dashboard != next);
      dashboard = next;
      state = SHOWING;
      hasDashboard = true;
    } else {
      contentChanged = (displayedState != ERROR) || (fetchError != errMsg) || (httpStatusCode != code);
      fetchError = errMsg;
      state = ERROR;
    }
    httpStatusCode = code;
    if (forceRedraw) {
      contentChanged = true;
      forceRedraw = false;
    }

    // Advance the clean-refresh cadence only on polls that actually redraw, not
    // on every completed poll. Ghosting is caused by updates, so a dashboard that
    // has been sitting still has nothing to clean — counting idle polls here would
    // fire a full-screen flash on a panel that had not been touched since the last
    // one. A persistent ERROR screen still counts, because it does redraw when the
    // status or message changes.
    if (contentChanged) {
      displayedState = state;
      if (config.fullRefreshEvery > 0) {
        if (--pollsUntilClean <= 0) {
          cleanRefreshDue = true;
          pollsUntilClean = config.fullRefreshEvery;
        }
      }
    }
  }

  if (contentChanged) requestUpdate();

  // R2 auto-drop: WiFi is only ever kept up between polls for two reasons --
  // a short-poll config (autoDropWifi() false) or an active post-press hold.
  // Neither applies here, so drop it. This also clears the WIFI_MODE_NULL gate
  // in HalPowerManager::setPowerSaving() that otherwise blocks downclocking --
  // R3's allowPowerSaving() override only pays off once WiFi actually goes down.
  if (HttpMonitorConfig::autoDropWifi(config) && !wifiHoldActive) {
    RADIO.shutdown();
  }
}

// ----------------------------------------------------------------
// Headless WiFi reconnect
// ----------------------------------------------------------------

// Reconnects to the last-known network without any UI -- for the auto-drop
// path only. Reproduces (does not call -- attemptConnection() is private and
// non-static) the WiFi.mode(WIFI_STA)/hostname/WiFi.begin() body of
// WifiSelectionActivity::attemptConnection(). Blocking, bounded by timeoutMs,
// the same pattern fetch()/sendAction() already use for their HTTP calls --
// this app has nothing else to do while it waits. When showAck is true (only
// from sendAction()'s user-initiated path -- see the header comment for why
// fetch()'s background poll passes false), paints a "connecting" indicator via
// setActionAck() BEFORE the blocking wait so it stays visible on the render
// task exactly as sendAction() already documents.
bool HttpMonitorActivity::reconnectWifiHeadless(unsigned long timeoutMs, bool showAck) {
  RADIO.ensureWifi();
  if (WiFi.status() == WL_CONNECTED) return true;

  if (showAck) setActionAck("wifi: connecting");

  std::string ssid;
  std::string password;
  {
    // WIFI_STORE.loadFromFile() touches SD over shared SPI -- must be under a
    // RenderLock, matching WifiSelectionActivity::onEnter()'s identical load.
    RenderLock lock(*this);
    WIFI_STORE.loadFromFile();
    ssid = WIFI_STORE.getLastConnectedSsid();
    if (!ssid.empty()) {
      if (const auto* cred = WIFI_STORE.findCredential(ssid)) {
        password = cred->password;
      }
    }
  }
  if (ssid.empty()) {
    if (showAck) setActionAck("wifi: no saved network");
    return false;
  }

  WiFi.mode(WIFI_STA);
  // Same hostname WifiSelectionActivity::attemptConnection() sets, so routers
  // show "CrossPoint-Reader-AABBCCDDEEFF" regardless of which path connected.
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  const String hostname = "CrossPoint-Reader-" + mac;
  WiFi.setHostname(hostname.c_str());
  if (!password.empty()) {
    WiFi.begin(ssid.c_str(), password.c_str());
  } else {
    WiFi.begin(ssid.c_str());
  }

  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(100);
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (showAck) setActionAck("wifi: connect failed");
    return false;
  }

  RenderLock lock(*this);
  WIFI_STORE.setLastConnectedSsid(ssid);
  return true;
}

// ----------------------------------------------------------------
// Button actions
// ----------------------------------------------------------------

// Sets actionAck under a RenderLock and requests a repaint -- shared by the
// "sending" / result / (loop()'s) "cleared" steps of a button press so each
// one is a single, consistent commit-then-paint.
void HttpMonitorActivity::setActionAck(const char* text) {
  {
    RenderLock lock(*this);
    HttpMonitorSchema::copyBounded(actionAck, sizeof(actionAck), text);
    actionAckStartMs = millis();
  }
  requestUpdate();
}

// A press sends one GET and shows a small "sent"/"failed" indicator -- no
// re-poll, no rendering of the reply, no poll-timer reset. Blocking, same as
// fetch(): this app has nothing else to do while it waits. Three panel
// updates per press ("sending" -> result -> loop()'s later clear) is the
// accepted cost of a deliberate user action -- commit 2b4c62a's refresh
// budget targeted idle/automatic polling, not this.
void HttpMonitorActivity::sendAction(ActionSlot slot) {
  const int slotIdx = static_cast<int>(slot);
  const std::string url = HttpMonitorConfig::actionUrlFor(config, slotIdx);
  if (url.empty()) return;  // no action_url configured -- inert

  // Same full-clock restore as fetch() -- see its comment. Covers
  // reconnectWifiHeadless() below (including its internal delay(100) wait loop)
  // and the HTTP GET further down; placed after the inert-button early return
  // so a no-op press touches nothing.
  powerManager.setPowerSaving(false);

  const char* slotName = HttpMonitorConfig::ACTION_SLOT_NAMES[slotIdx];
  char ack[24];

  // InputManager::update() derives button edges by comparing instantaneous pin
  // state to the last-seen state: a press+release entirely inside a blocking
  // call (like the GET below) produces no event at all -- including Back. So a
  // multi-second block here would make the device look frozen with no way out.
  // Non-auto-drop config: skip the request outright when WiFi is down (same as
  // before this feature existed) -- fetch()'s timed poll already owns WiFi
  // recovery (WifiSelectionActivity) for that config, which would be worse to
  // hijack the screen with on a single button press. Auto-drop config: WiFi
  // being down between polls is the expected steady state, so a press headlessly
  // reconnects (bounded, same blocking category as the GET immediately below,
  // and showAck=true since this IS the user-initiated path) rather than
  // silently doing nothing.
  if (WiFi.status() != WL_CONNECTED) {
    if (!HttpMonitorConfig::autoDropWifi(config)) {
      snprintf(ack, sizeof(ack), "%s: no wifi", slotName);
      setActionAck(ack);
      return;
    }
    if (!reconnectWifiHeadless(WIFI_RECONNECT_TIMEOUT_MS, /*showAck=*/true)) {
      snprintf(ack, sizeof(ack), "%s: no wifi", slotName);
      setActionAck(ack);
      // Don't leave the radio half-up (reconnectWifiHeadless() already called
      // RADIO.ensureWifi()) blocking HalPowerManager's downclock gate until
      // the next successful poll -- same cleanup fetch()'s own failure branch
      // already does.
      RADIO.shutdown();
      return;
    }
  }

  // render() runs on its own FreeRTOS task, so this paints WHILE loop() is
  // blocked on the GET below -- it's what makes the freeze visible as "sending"
  // rather than the device looking dead for up to timeout_ms.
  snprintf(ack, sizeof(ack), "%s: sending", slotName);
  setActionAck(ack);

  HTTPClient http;
  http.begin(url.c_str());
  http.setTimeout(config.timeoutMs);
  // setTimeout() only bounds the socket read (HTTPClient's _tcpTimeout); the
  // TCP connect itself defaults to HTTPCLIENT_DEFAULT_TCP_TIMEOUT (5000ms)
  // regardless of timeout_ms unless set explicitly here -- without this an
  // unreachable host blocks 5s even with timeout_ms = 1000.
  http.setConnectTimeout(config.timeoutMs);
  applyAuthHeader(http);
  const int code = http.GET();
  http.end();

  if (code >= 200 && code < 300) {
    snprintf(ack, sizeof(ack), "%s: sent", slotName);
  } else if (code > 0) {
    snprintf(ack, sizeof(ack), "%s: HTTP %d", slotName, code);
  } else {
    snprintf(ack, sizeof(ack), "%s: failed", slotName);
  }
  setActionAck(ack);

  // Arm/re-arm the post-press hold: a directional press is deliberate user
  // interaction, so it keeps WiFi up through wifi_hold_sec rather than letting
  // fetch() drop it again on the very next poll. A further press before the
  // hold expires re-arms it -- this runs on every send, not just the branch
  // above that had to reconnect first.
  if (HttpMonitorConfig::autoDropWifi(config)) {
    RenderLock lock(*this);
    wifiHoldActive = true;
    wifiHoldStartMs = millis();
  }
}

// ----------------------------------------------------------------
// Font size
// ----------------------------------------------------------------

// Ladder index -> candidate font id. Only fonts guaranteed present in the
// shipping (`slim` + `-DOMIT_FONTS`) build are used — see main.cpp's
// setupDisplayAndFonts(), which registers just BOOKERLY_14, UI_10, UI_12 and
// SMALL under OMIT_FONTS. Index 2 (UI_12) is the default and matches the size
// the dashboard has always rendered at.
int HttpMonitorActivity::dashboardFontSizeIndex() const {
  // The server owns the font size. `fontSize` is already clamped to 0..3 by the
  // schema walk, and is SIZE_INHERIT when the server didn't send one — in which
  // case monitor.conf's font_size (clamped by the config parser) is the fallback,
  // so servers that don't know about the field keep working unchanged.
  if (dashboard.fontSize != SIZE_INHERIT) return static_cast<int>(dashboard.fontSize);
  return config.fontSize;
}

int HttpMonitorActivity::dashboardFontId() const {
  static constexpr int kFontLadder[] = {SMALL_FONT_ID, UI_10_FONT_ID, UI_12_FONT_ID, BOOKERLY_14_FONT_ID};
  static constexpr int kLadderSize = static_cast<int>(sizeof(kFontLadder) / sizeof(kFontLadder[0]));

  const int requested = dashboardFontSizeIndex();
  const int idx = (requested >= 0 && requested < kLadderSize) ? requested : HttpMonitorConfig::DEFAULT_FONT_SIZE;
  const int candidate = kFontLadder[idx];
  // A missing font renders blank (GfxRenderer returns width/lineheight 0 for an
  // unregistered id), so fall back to UI_12 — always present — rather than
  // trust the ladder blindly.
  const auto& fontMap = renderer.getFontMap();
  if (fontMap.find(candidate) != fontMap.end()) return candidate;
  return UI_12_FONT_ID;
}

// Per-row font override: a `size` field on the row picks a ladder entry directly;
// SIZE_INHERIT (the common case) defers to the dashboard-wide font. Same
// validation as dashboardFontId() — unregistered candidates (a -DOMIT_FONTS
// build) fall back to UI_12.
int HttpMonitorActivity::fontForSize(uint8_t sizeIdx) const {
  if (sizeIdx != SIZE_INHERIT) {
    static constexpr int kFontLadder[] = {SMALL_FONT_ID, UI_10_FONT_ID, UI_12_FONT_ID, BOOKERLY_14_FONT_ID};
    static constexpr int kLadderSize = static_cast<int>(sizeof(kFontLadder) / sizeof(kFontLadder[0]));
    if (sizeIdx < kLadderSize) {
      const auto& fontMap = renderer.getFontMap();
      if (fontMap.find(kFontLadder[sizeIdx]) != fontMap.end()) return kFontLadder[sizeIdx];
    }
  }
  return dashboardFontId();
}

// ----------------------------------------------------------------
// Render
// ----------------------------------------------------------------

void HttpMonitorActivity::render(RenderLock&&) {
  // Server-driven orientation: `dashboard.reversed` flips the screen 180deg for
  // a device mounted upside down. Restored to Portrait right after this frame's
  // displayBuffer() (and again in onExit()) so no other activity inherits an
  // upside-down renderer.
  renderer.setOrientation(dashboard.reversed ? GfxRenderer::PortraitInverted : GfxRenderer::Portrait);

  renderer.clearScreen();

  switch (state) {
    case NO_CONFIG:
      renderNoConfig();
      break;
    case IDLE:
    case FETCHING:
      renderFetching();
      break;
    case SHOWING:
      renderDashboard();
      break;
    case ERROR:
      renderError();
      break;
    case BATTERY_CRITICAL:
      renderBatteryCritical();
      break;
  }

  // The periodic de-ghost pass must be a FULL_REFRESH, not a HALF_REFRESH. The X3
  // branch of EInkDisplay::displayBuffer() computes `fastMode = (mode !=
  // FULL_REFRESH)`, so a HALF_REFRESH there takes the fast *differential* path —
  // identical to a normal update — and the documented "deeper clean refresh" never
  // actually happened on X3. FULL_REFRESH is the only mode that clears ghosting on
  // both panels. It is also the visible flash, which is why it is gated behind the
  // full_refresh_every cadence and only counts polls that redrew.
  if (config.fullRefreshEvery > 0 && cleanRefreshDue) {
    cleanRefreshDue = false;
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
  } else {
    renderer.displayBuffer();
  }

  renderer.setOrientation(GfxRenderer::Portrait);
}

// Centered SMALL text just above the hint bar; no-op when actionAck is empty.
void HttpMonitorActivity::drawActionAck() {
  if (actionAck[0] == '\0') return;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int lineH = renderer.getLineHeight(SMALL_FONT_ID);
  const int y = pageHeight - metrics.buttonHintsHeight - lineH - 4;
  // This band sits inside the content band (contentBottom = pageHeight -
  // buttonHintsHeight - verticalSpacing, and verticalSpacing on some themes is
  // smaller than lineH+4), so it can land on top of the dashboard's last drawn
  // row. Clear it to white first -- same "fillRect(..., false)" pattern themes
  // use to blank a band before drawing over it -- rather than shrinking the
  // content band and relayouting the whole dashboard for a 1.5s indicator.
  const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, actionAck);
  const int padX = 6;
  const int rectX = std::max(0, (pageWidth - textWidth) / 2 - padX);
  const int rectW = std::min(pageWidth - rectX, textWidth + 2 * padX);
  renderer.fillRect(rectX, y - 2, rectW, lineH + 4, false);
  renderer.drawCenteredText(SMALL_FONT_ID, y, actionAck);
}

// Vector wifi glyph in the x,y..+24,+24 header corner: a dot (filled for
// AlwaysOn/Held, outline for Dropped) plus two signal arcs above it, following
// the drawArc-quadrant precedent of drawRoundedRect() / BaseTheme::
// drawBatteryIcon(). No bitmap asset: wifi.h is 32x32 (wrong size) and a third
// static-array copy would cost more flash than this costs cycles. Dropped adds
// a diagonal slash through the whole glyph; Held adds a short underscore bar
// beneath the dot.
void HttpMonitorActivity::drawWifiIndicator(int x, int y, WifiIndicator wifiState) {
  constexpr int SIZE = 24;
  constexpr int DOT_RADIUS = 2;
  constexpr int ARC1_RADIUS = 7;
  constexpr int ARC2_RADIUS = 11;
  constexpr int ARC_STROKE = 2;
  const int cx = x + SIZE / 2;
  const int cy = y + SIZE - 6;

  if (wifiState == WifiIndicator::Dropped) {
    for (int q = 0; q < 4; ++q) {
      const int xDir = (q & 1) ? 1 : -1;
      const int yDir = (q & 2) ? 1 : -1;
      renderer.drawArc(DOT_RADIUS, cx, cy, xDir, yDir, 1, true);  // outline dot
    }
  } else {
    for (int q = 0; q < 4; ++q) {
      const int xDir = (q & 1) ? 1 : -1;
      const int yDir = (q & 2) ? 1 : -1;
      renderer.drawArc(DOT_RADIUS, cx, cy, xDir, yDir, DOT_RADIUS, true);  // filled dot
    }
  }

  // Two signal arcs, upper quadrants only (yDir=-1), one per side.
  renderer.drawArc(ARC1_RADIUS, cx, cy, -1, -1, ARC_STROKE, true);
  renderer.drawArc(ARC1_RADIUS, cx, cy, 1, -1, ARC_STROKE, true);
  renderer.drawArc(ARC2_RADIUS, cx, cy, -1, -1, ARC_STROKE, true);
  renderer.drawArc(ARC2_RADIUS, cx, cy, 1, -1, ARC_STROKE, true);

  if (wifiState == WifiIndicator::Dropped) {
    renderer.drawLine(x + 1, y + 1, x + SIZE - 2, y + SIZE - 2, ARC_STROKE, true);
  } else if (wifiState == WifiIndicator::Held) {
    renderer.drawLine(cx - 6, y + SIZE - 1, cx + 6, y + SIZE - 1, ARC_STROKE, true);
  }
}

void HttpMonitorActivity::renderNoConfig() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, kDefaultTitle);

  const int lineH = renderer.getLineHeight(UI_10_FONT_ID) + 6;
  const int smallLineH = renderer.getLineHeight(SMALL_FONT_ID) + 4;
  const int maxTextWidth = pageWidth - 2 * metrics.contentSidePadding;
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 3;

  renderer.drawCenteredText(UI_10_FONT_ID, y, "No config found", true, EpdFontFamily::BOLD);
  y += lineH + 10;

  renderer.drawCenteredText(SMALL_FONT_ID, y, "Create this file on the SD card:");
  y += smallLineH;
  renderer.drawCenteredText(SMALL_FONT_ID, y, HttpMonitorConfig::CONFIG_PATH);
  y += smallLineH + 12;

  renderer.drawCenteredText(SMALL_FONT_ID, y, "Required key:");
  y += smallLineH;
  renderer.drawCenteredText(SMALL_FONT_ID, y, "url = http://host:port/status.json");
  y += smallLineH + 12;

  if (!configError.empty()) {
    const auto err = renderer.truncatedText(SMALL_FONT_ID, configError.c_str(), maxTextWidth);
    renderer.drawCenteredText(SMALL_FONT_ID, y, err.c_str());
  }

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void HttpMonitorActivity::renderFetching() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const char* title = dashboard.title[0] != '\0' ? dashboard.title : kDefaultTitle;
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Fetching...");
}

void HttpMonitorActivity::renderError() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  const char* title = dashboard.title[0] != '\0' ? dashboard.title : kDefaultTitle;
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title);

  const int lineH = renderer.getLineHeight(UI_10_FONT_ID) + 6;
  const int smallLineH = renderer.getLineHeight(SMALL_FONT_ID) + 2;
  const int maxTextWidth = pageWidth - 2 * metrics.contentSidePadding;
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 3;

  char statusBuf[32];
  if (httpStatusCode > 0) {
    snprintf(statusBuf, sizeof(statusBuf), "HTTP %d", httpStatusCode);
  } else {
    snprintf(statusBuf, sizeof(statusBuf), "Connection failed");
  }
  renderer.drawCenteredText(UI_10_FONT_ID, y, statusBuf, true, EpdFontFamily::BOLD);
  y += lineH + 6;

  // maxLines must be > 0 — GfxRenderer::wrappedText() returns an empty vector
  // otherwise (GfxRenderer.cpp: `if (!text || maxWidth <= 0 || maxLines <= 0)
  // return lines;`), which would silently drop every error detail.
  static constexpr int MAX_ERROR_LINES = 3;
  const auto errLines = renderer.wrappedText(SMALL_FONT_ID, fetchError.c_str(), maxTextWidth, MAX_ERROR_LINES);
  for (const auto& line : errLines) {
    renderer.drawCenteredText(SMALL_FONT_ID, y, line.c_str());
    y += smallLineH;
  }

  drawActionAck();

  const auto labels = mappedInput.mapLabels("Back", "Retry", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// Terminal screen: no polling, no WiFi, and (per preventAutoSleep()) the
// device will auto-sleep on its own normal timeout rather than staying
// resident here. Visual precedent: SleepActivity::renderStatusSleepScreen()
// (boot_sleep/SleepActivity.cpp) -- centered title + a battery bar/percentage.
void HttpMonitorActivity::renderBatteryCritical() {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - 90, "Battery critical", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 60, "Monitoring stopped");

  const uint16_t battPct = powerManager.getBatteryPercentage();
  constexpr int barW = 200;
  constexpr int barH = 8;
  const int barX = (pageWidth - barW) / 2;
  const int barY = pageHeight / 2 - 10;
  renderer.drawRect(barX, barY, barW, barH, true);
  const int fillW = (barW - 4) * battPct / 100;
  if (fillW > 0) {
    renderer.fillRect(barX + 2, barY + 2, fillW, barH - 4, true);
  }

  char battStr[8];
  snprintf(battStr, sizeof(battStr), "%d%%", battPct);
  renderer.drawCenteredText(SMALL_FONT_ID, barY + 16, battStr);

  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 50, "Connect USB power to resume");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void HttpMonitorActivity::renderDashboard() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // ---- Header: the dashboard header is a status bar, rendered explicitly rather
  // than via GUI.drawHeader() (which centers the title and paints the battery on
  // the far right). One line, left to right: title (UI_12 BOLD, truncated) ...
  // poll interval ... `updated` timestamp (both SMALL) ... the far-right 24x24
  // wifi indicator. The battery is dropped for the monitor header — this zone
  // is server-liveness real estate instead.
  //
  // Positioning is HttpMonitorLayout::computeHeaderLayout() — shared verbatim
  // with test_preview.cpp so the two can't silently drift apart; only the wifi
  // glyph's actual drawing primitives differ between renderers.
  const int headerY = metrics.topPadding;
  const int headerH = metrics.headerHeight;
  const char* title = dashboard.title[0] != '\0' ? dashboard.title : kDefaultTitle;

  const char* updated = dashboard.updated;
  const int updatedW = (updated[0] != '\0') ? renderer.getTextWidth(SMALL_FONT_ID, updated) : 0;

  char intervalBuf[16];
  snprintf(intervalBuf, sizeof(intervalBuf), "%ds", config.intervalSec);
  const int intervalW = renderer.getTextWidth(SMALL_FONT_ID, intervalBuf);

  constexpr int WIFI_ICON_SIZE = 24;
  constexpr int HEADER_GAP = 8;
  const auto headerLayout = HttpMonitorLayout::computeHeaderLayout(pageWidth, metrics.contentSidePadding, intervalW,
                                                                   updatedW, WIFI_ICON_SIZE, HEADER_GAP);

  const int titleLineH = renderer.getLineHeight(UI_12_FONT_ID);
  const int titleY = headerY + (headerH - titleLineH) / 2;
  const std::string titleStr =
      renderer.truncatedText(UI_12_FONT_ID, title, headerLayout.titleZone, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, titleY, titleStr.c_str(), true, EpdFontFamily::BOLD);

  const int smallY = headerY + (headerH - renderer.getLineHeight(SMALL_FONT_ID)) / 2;
  renderer.drawText(SMALL_FONT_ID, headerLayout.intervalX, smallY, intervalBuf);
  if (updated[0] != '\0') {
    renderer.drawText(SMALL_FONT_ID, headerLayout.updatedX, smallY, updated);
  }

  // Wifi indicator, in the reserved 24x24 corner. AlwaysOn when auto-drop is
  // off; otherwise Held while the post-press hold is active, else Dropped
  // whenever WiFi isn't currently connected (the steady state between polls).
  // WiFi.status() is a plain read of driver-internal state, safe to call from
  // this render task the same way fetch()/sendAction() already call it from
  // the loop task.
  WifiIndicator wifiIndicatorState = WifiIndicator::AlwaysOn;
  if (HttpMonitorConfig::autoDropWifi(config)) {
    if (wifiHoldActive) {
      wifiIndicatorState = WifiIndicator::Held;
    } else if (WiFi.status() != WL_CONNECTED) {
      wifiIndicatorState = WifiIndicator::Dropped;
    }
  }
  const int iconY = headerY + (headerH - WIFI_ICON_SIZE) / 2;
  drawWifiIndicator(headerLayout.iconX, iconY, wifiIndicatorState);

  // ---- Content band ----
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int contentWidth = pageWidth - 2 * metrics.contentSidePadding;
  const int contentTop = headerY + headerH + metrics.verticalSpacing;
  static constexpr int BAR_GAP = 10;

  const int rowFontGlobal = dashboardFontId();
  const int rowLineHGlobal = renderer.getLineHeight(rowFontGlobal) + 6;

  // ---- Build the per-entry list. Each entry is one heading, one row, or one
  // alert line; heights vary (per-row font size, wrapped text, spacers, dividers,
  // glyph bands), so placement is a prefix sum over entryHeights[]
  // (HttpMonitorLayout::entryY), not a uniform-pitch line model.
  static constexpr int MAX_ENTRIES = HttpMonitorSchema::MAX_SECTIONS * (1 + HttpMonitorSchema::MAX_ROWS_PER_SECTION) +
                                     1 + HttpMonitorSchema::MAX_ALERTS;
  struct RenderEntry {
    const HttpMonitorSchema::Row* row = nullptr;  // valid for typed rows
    const char* text = nullptr;                   // heading or alert text
    bool isHeading = false;
  };
  RenderEntry entries[MAX_ENTRIES];
  int entryHeights[MAX_ENTRIES];
  int entryCount = 0;

  auto addEntry = [&](const HttpMonitorSchema::Row* row, const char* text, bool isHeading, int height) {
    if (entryCount >= MAX_ENTRIES) return;
    entries[entryCount] = RenderEntry{row, text, isHeading};
    entryHeights[entryCount] = height;
    ++entryCount;
  };

  for (const auto& section : dashboard.sections) {
    if (section.heading[0] != '\0') {
      addEntry(nullptr, section.heading, /*isHeading=*/true, rowLineHGlobal);
    }
    for (const auto& row : section.rows) {
      const int rowFont = fontForSize(row.sizeIdx);
      const int rowLineH = renderer.getLineHeight(rowFont) + 6;
      int h = rowLineH;
      switch (row.type) {
        case RowType::SPACER:
          h = HttpMonitorLayout::spacerEntryHeight(row.spacerHeight);
          break;
        case RowType::DIVIDER:
          h = HttpMonitorLayout::dividerEntryHeight();
          break;
        case RowType::GLYPHS:
          h = HttpMonitorLayout::glyphsEntryHeight();
          break;
        case RowType::TEXT: {
          // Alert text rows draw a 6x6 marker + indent (drawTextRow), so they
          // wrap at a narrower width; the height must match what is drawn.
          const int markerPad = row.alert() ? 10 : 0;
          const int lineCount =
              static_cast<int>(renderer.wrappedText(rowFont, row.text, contentWidth - markerPad, 2).size());
          h = HttpMonitorLayout::textEntryHeight(rowLineH, lineCount);
          break;
        }
        default:  // KV and BAR
          h = HttpMonitorLayout::kvEntryHeight(rowLineH);
          break;
      }
      addEntry(&row, nullptr, false, h);
    }
  }
  if (!dashboard.alerts.empty()) {
    addEntry(nullptr, "Alerts", true, rowLineHGlobal);
    for (const auto& alertText : dashboard.alerts) {
      addEntry(nullptr, alertText.data(), false, rowLineHGlobal);
    }
  }

  // ---- How much of the list fits. The dashboard does not scroll: the server owns
  // the font size and the amount of content, so whatever does not fit is clipped.
  const int visibleEntries = HttpMonitorLayout::visibleEntryCount(entryHeights, entryCount, contentTop, contentBottom);
  if (visibleEntries < entryCount) {
    LOG_INF("HTTPMON", "Content does not fit: %d of %d entries drawn", visibleEntries, entryCount);
  }

  // ---- Drawing helpers ----
  // The bar. Drawn directly (not GUI.drawProgressBar(), which paints an unwanted
  // percentage label below its rect and computes fill from an unclamped percent).
  // bar.value is clamped to 0..100 by the schema walk, so the fill math is safe
  // outright. Flush fill: fillWidth spans the whole outline, so value==100 fills
  // it edge-to-edge. Segmented bars split the span into cells with 2px gaps.
  auto drawBarShape = [&](int barX, int y, int barSpan, int rowLineH, const HttpMonitorSchema::BarSpec& bar) {
    int barH = rowLineH - 12;
    if (barH < 2) barH = 2;  // keep the bar visible even at the smallest font size
    const int barY = y + (rowLineH - barH) / 2;
    if (bar.segments > 1 && barSpan > 8) {
      const int totalGap = (bar.segments - 1) * 2;
      const int cellW = (barSpan - totalGap) / bar.segments;
      if (cellW >= 2) {
        const int filledCells = (bar.value * bar.segments) / 100;
        for (int s = 0; s < bar.segments; ++s) {
          const int cx = barX + s * (cellW + 2);
          renderer.drawRect(cx, barY, cellW, barH);
          if (s < filledCells) renderer.fillRect(cx, barY, cellW, barH, true);
        }
        return;
      }
    }
    renderer.drawRect(barX, barY, barSpan, barH);
    const int fillWidth = barSpan * bar.value / 100;
    if (fillWidth > 0) renderer.fillRect(barX, barY, fillWidth, barH, true);
  };

  // Glyph cell shapes for `glyphs` rows. Reserved chars map to the geometric
  // shapes below; anything else renders as a SMALL text glyph centered in the
  // 16x16 cell.
  auto drawGlyph = [&](int cx, int cy, char ch) {
    constexpr int CELL = 16;
    switch (ch) {
      case '#':  // filled square
        renderer.fillRect(cx, cy, CELL, CELL, true);
        break;
      case 'o':  // hollow square
        renderer.drawRect(cx, cy, CELL, CELL);
        break;
      case '.': {  // filled disk (16px, radius 8) — four quadrant arcs
        for (int q = 0; q < 4; ++q) {
          const int xd = (q & 1) ? 1 : -1;
          const int yd = (q & 2) ? 1 : -1;
          renderer.drawArc(8, cx + 8, cy + 8, xd, yd, 8, true);
        }
        break;
      }
      case '+':
        renderer.drawLine(cx + 8, cy, cx + 8, cy + CELL - 1, 2, true);
        renderer.drawLine(cx, cy + 8, cx + CELL - 1, cy + 8, 2, true);
        break;
      case 'x':
        renderer.drawLine(cx, cy, cx + CELL - 1, cy + CELL - 1, 2, true);
        renderer.drawLine(cx + CELL - 1, cy, cx, cy + CELL - 1, 2, true);
        break;
      case '!': {  // filled up-triangle
        const int px[] = {cx + 8, cx, cx + CELL - 1};
        const int py[] = {cy, cy + CELL - 1, cy + CELL - 1};
        renderer.fillPolygon(px, py, 3, true);
        break;
      }
      case '^':  // up-triangle outline
        renderer.drawLine(cx + 8, cy, cx, cy + CELL - 1, 2, true);
        renderer.drawLine(cx, cy + CELL - 1, cx + CELL - 1, cy + CELL - 1, 2, true);
        renderer.drawLine(cx + CELL - 1, cy + CELL - 1, cx + 8, cy, 2, true);
        break;
      case 'v':  // down-triangle outline
        renderer.drawLine(cx + 8, cy + CELL - 1, cx, cy, 2, true);
        renderer.drawLine(cx, cy, cx + CELL - 1, cy, 2, true);
        renderer.drawLine(cx + CELL - 1, cy, cx + 8, cy + CELL - 1, 2, true);
        break;
      case ' ':
        break;
      default: {
        char buf[2] = {ch, '\0'};
        const int w = renderer.getTextWidth(SMALL_FONT_ID, buf);
        renderer.drawText(SMALL_FONT_ID, cx + (CELL - w) / 2, cy, buf);
        break;
      }
    }
  };

  auto drawAlertMarker = [&](int x, int y, int lineH) { renderer.fillRect(x, y + (lineH - 6) / 2, 6, 6, true); };

  // KV / BAR row: label left, value right-aligned, the bar spanning whatever's
  // left between them (24px floor -> render as a bar-less kv row below it).
  auto drawKv = [&](const HttpMonitorSchema::Row& row, int y) {
    const int rowFont = fontForSize(row.sizeIdx);
    const int rowLineH = renderer.getLineHeight(rowFont) + 6;
    const bool hasBar = row.bar.value >= 0;
    const auto cols = HttpMonitorLayout::computeRowColumns(contentWidth, hasBar);
    const int markerPad = row.alert() ? 10 : 0;
    const EpdFontFamily::Style style = (row.bold() || row.alert()) ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;

    const std::string labelStr = renderer.truncatedText(rowFont, row.label, cols.labelColMax - markerPad, style);
    const int labelWidth = renderer.getTextWidth(rowFont, labelStr.c_str(), style);
    int valueWidth = renderer.getTextWidth(rowFont, row.value);
    std::string valueStr =
        (valueWidth > cols.valueColMax) ? renderer.truncatedText(rowFont, row.value, cols.valueColMax) : row.value;
    valueWidth = renderer.getTextWidth(rowFont, valueStr.c_str());

    if (hasBar) {
      // Width %/align of the bar object map onto the full label->value span
      // (placeFractionalBar); legacy int bars are fullWidth + LEFT.
      const auto barResult = HttpMonitorLayout::placeFractionalBar(
          metrics.contentSidePadding, pageWidth, markerPad + labelWidth, valueWidth, BAR_GAP, row.bar.widthPct,
          static_cast<HttpMonitorLayout::BarAlign>(row.bar.align));
      if (barResult.barSpan >= HttpMonitorLayout::MIN_BAR_SPAN) {
        const int labelX = metrics.contentSidePadding + markerPad;
        if (row.alert()) drawAlertMarker(metrics.contentSidePadding, y, rowLineH);
        renderer.drawText(rowFont, labelX, y, labelStr.c_str(), true, style);
        renderer.drawText(rowFont, pageWidth - metrics.contentSidePadding - valueWidth, y, valueStr.c_str());
        drawBarShape(barResult.barX, y, barResult.barSpan, rowLineH, row.bar);
        return;
      }
      // barSpan below the 24px floor — fall through and render as a bar-less kv row.
    }

    // Bar-less kv row, block placed per `align`.
    const int blockGap = 16;
    const int blockWidth = markerPad + labelWidth + blockGap + valueWidth;
    int labelX;
    switch (row.align) {
      case RowAlign::RIGHT:
        labelX = pageWidth - metrics.contentSidePadding - valueWidth - blockGap - labelWidth;
        break;
      case RowAlign::CENTER:
        labelX = metrics.contentSidePadding + (contentWidth - blockWidth) / 2 + markerPad;
        break;
      default:
        labelX = metrics.contentSidePadding + markerPad;
        break;
    }
    const int valueX = labelX + labelWidth + blockGap;
    if (row.alert()) drawAlertMarker(labelX - markerPad, y, rowLineH);
    renderer.drawText(rowFont, labelX, y, labelStr.c_str(), true, style);
    renderer.drawText(rowFont, valueX, y, valueStr.c_str());
  };

  // Text row: wraps to at most 2 lines, honors align/bold/size. Alert text rows
  // get the same treatment as kv rows — bolded, plus the 6x6 alert marker at the
  // row's left edge with the text indented so it never sits under the marker.
  // The wrap width shrinks by markerPad (matching the height pass) so the entry
  // height matches what is actually drawn.
  auto drawTextRow = [&](const HttpMonitorSchema::Row& row, int y) {
    const int rowFont = fontForSize(row.sizeIdx);
    const int rowLineH = renderer.getLineHeight(rowFont) + 6;
    const bool alert = row.alert();
    const int markerPad = alert ? 10 : 0;
    const EpdFontFamily::Style style = (row.bold() || alert) ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const auto lines = renderer.wrappedText(rowFont, row.text, contentWidth - markerPad, 2);
    int ty = y;
    for (const auto& line : lines) {
      const int tw = renderer.getTextWidth(rowFont, line.c_str());
      int tx;
      switch (row.align) {
        case RowAlign::RIGHT:
          tx = pageWidth - metrics.contentSidePadding - tw;
          break;
        case RowAlign::CENTER:
          tx = metrics.contentSidePadding + markerPad + (contentWidth - markerPad - tw) / 2;
          break;
        default:
          tx = metrics.contentSidePadding + markerPad;
          break;
      }
      renderer.drawText(rowFont, tx, ty, line.c_str(), true, style);
      ty += rowLineH;
    }
    if (alert) drawAlertMarker(metrics.contentSidePadding, y, rowLineH);
  };

  // Divider: 12px band, 1-2px rule centered on it, optional centered SMALL caption
  // (the rule splits into two segments leaving an 8px clear gutter around it).
  auto drawDivider = [&](const HttpMonitorSchema::Row& row, int y) {
    const int inset = std::min(row.dividerInset, contentWidth / 2);
    const int left = metrics.contentSidePadding + inset;
    const int right = pageWidth - metrics.contentSidePadding - inset;
    const int ruleY = y + 6;  // center of the band
    if (row.label[0] != '\0') {
      const int labelW = renderer.getTextWidth(SMALL_FONT_ID, row.label);
      const int lx = metrics.contentSidePadding + (contentWidth - labelW) / 2;
      renderer.drawText(SMALL_FONT_ID, lx, y + (12 - renderer.getLineHeight(SMALL_FONT_ID)) / 2, row.label);
      static constexpr int LABEL_GAP = 8;
      if (lx - LABEL_GAP > left) {
        renderer.drawLine(left, ruleY, lx - LABEL_GAP, ruleY, row.dividerLineWidth, true);
      }
      if (right > lx + labelW + LABEL_GAP) {
        renderer.drawLine(lx + labelW + LABEL_GAP, ruleY, right, ruleY, row.dividerLineWidth, true);
      }
    } else if (right > left) {
      renderer.drawLine(left, ruleY, right, ruleY, row.dividerLineWidth, true);
    }
  };

  // Glyphs: 20px band, optional SMALL label, then 16x16 cells at 4px gaps. The
  // run is clamped to the band (maxGlyphsFor) so a max-length glyphs string
  // (32 cells = 636px) never overflows the ~440px content band — every
  // out-of-band pixel would log a GFX "!! Outside range" and draw clipped. The
  // clamp accounts for the label so the whole block stays in-band.
  auto drawGlyphs = [&](const HttpMonitorSchema::Row& row, int y) {
    const int rawCount = static_cast<int>(strlen(row.glyphs));
    const int labelW = (row.label[0] != '\0') ? renderer.getTextWidth(SMALL_FONT_ID, row.label) : 0;
    const int glyphsAvailable = contentWidth - ((row.label[0] != '\0') ? (labelW + 8) : 0);
    const int glyphCount = std::min(rawCount, HttpMonitorLayout::maxGlyphsFor(glyphsAvailable));
    const int glyphsW = glyphCount * 20 - 4;  // 16px cells + 4px gaps
    int gx;
    if (row.label[0] != '\0') {
      const int blockW = labelW + 8 + glyphsW;
      switch (row.align) {
        case RowAlign::RIGHT:
          gx = pageWidth - metrics.contentSidePadding - blockW;
          break;
        case RowAlign::CENTER:
          gx = metrics.contentSidePadding + (contentWidth - blockW) / 2;
          break;
        default:
          gx = metrics.contentSidePadding;
          break;
      }
      renderer.drawText(SMALL_FONT_ID, gx, y + (20 - renderer.getLineHeight(SMALL_FONT_ID)) / 2, row.label);
      gx += labelW + 8;
    } else {
      switch (row.align) {
        case RowAlign::RIGHT:
          gx = pageWidth - metrics.contentSidePadding - glyphsW;
          break;
        case RowAlign::CENTER:
          gx = metrics.contentSidePadding + (contentWidth - glyphsW) / 2;
          break;
        default:
          gx = metrics.contentSidePadding;
          break;
      }
    }
    const int gy = y + (20 - 16) / 2;
    for (int i = 0; i < glyphCount; ++i) {
      drawGlyph(gx, gy, row.glyphs[i]);
      gx += 20;
    }
  };

  auto dispatchRow = [&](const HttpMonitorSchema::Row& row, int y) {
    switch (row.type) {
      case RowType::SPACER:
        break;  // blank band
      case RowType::DIVIDER:
        drawDivider(row, y);
        break;
      case RowType::GLYPHS:
        drawGlyphs(row, y);
        break;
      case RowType::TEXT:
        drawTextRow(row, y);
        break;
      default:  // KV and BAR
        drawKv(row, y);
        break;
    }
  };

  // ---- Draw the visible entries ----
  for (int i = 0; i < visibleEntries; ++i) {
    const int y = HttpMonitorLayout::entryY(entryHeights, i, contentTop);
    const RenderEntry& e = entries[i];
    if (e.row != nullptr) {
      dispatchRow(*e.row, y);
    } else if (e.isHeading) {
      // Headings are drawn directly (not via GUI.drawSubHeader, which only ever
      // draws REGULAR) so BOLD keeps them visually dominant over the row font.
      renderer.drawText(rowFontGlobal, metrics.contentSidePadding, y, e.text, true, EpdFontFamily::BOLD);
    } else {
      // Alert line: BOLD label + 6x6 marker prefix, no value/bar.
      const std::string alertStr =
          renderer.truncatedText(rowFontGlobal, e.text, contentWidth / 2 - 10, EpdFontFamily::BOLD);
      drawAlertMarker(metrics.contentSidePadding, y, rowLineHGlobal);
      renderer.drawText(rowFontGlobal, metrics.contentSidePadding + 10, y, alertStr.c_str(), true, EpdFontFamily::BOLD);
    }
  }

  if (dashboard.sections.empty() && dashboard.alerts.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, (contentTop + contentBottom) / 2, "No data");
  }

  drawActionAck();

  const auto labels = mappedInput.mapLabels("Back", "Refresh", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
