#pragma once

#include <array>
#include <string>
#include <vector>

#include "HttpMonitorConfig.h"
#include "HttpMonitorSchema.h"
#include "activities/Activity.h"

class HTTPClient;

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
  // Deliberately excludes BATTERY_CRITICAL: once the battery guard trips, the
  // device must auto-sleep on its own normal timeout rather than being held
  // awake by this activity -- a brownout during an e-ink update can leave the
  // panel partially driven.
  bool preventAutoSleep() override { return state == SHOWING || state == FETCHING; }
  // Resident (preventAutoSleep()==true) but opts out of forced full clock:
  // this is what lets the device downclock / auto-drop WiFi between polls
  // instead of pinning full CPU speed forever just to keep the dashboard on
  // screen.
  bool allowPowerSaving() override { return true; }
  // True only for the one loop() iteration that actually completed a light-
  // sleep slice (see sleptThisIteration below) -- NOT unconditionally. This is
  // what lets main.cpp replace its own end-of-loop delay(10)/delay(50) with a
  // near-instant yield() for that iteration, which is strictly better than
  // stacking both: main.cpp's own idle delay is redundant on top of a slice
  // that already spent 40ms genuinely asleep, and unconditionally skipping it
  // would instead spin the loop hot burning power whenever canLightSleep()
  // vetoes (fetch in progress, render busy, a deadline imminent). See
  // maybeLightSleep() for the full reasoning and the resulting arithmetic.
  bool skipLoopDelay() override { return sleptThisIteration; }

 private:
  // BATTERY_CRITICAL is terminal until USB is (re)connected: no polling, no
  // WiFi, and preventAutoSleep() deliberately excludes it (see below) so the
  // device auto-sleeps cleanly instead of running the battery to a brownout
  // mid-panel-refresh.
  enum State { NO_CONFIG, IDLE, FETCHING, SHOWING, ERROR, BATTERY_CRITICAL };
  // Order matches HttpMonitorConfig::ACTION_SLOT_NAMES ("up","down","left","right").
  enum class ActionSlot { Up, Down, Left, Right };
  // The far-right 24x24 header corner (vacated by the removed liveness dial).
  // AlwaysOn when auto-drop is off (HttpMonitorConfig::autoDropWifi() false);
  // Dropped when auto-drop is on and WiFi is currently down between polls;
  // Held while wifiHoldActive (WiFi kept up after a directional button press).
  enum class WifiIndicator { AlwaysOn, Dropped, Held };

  // Hard caps on untrusted response data (sections/rows/alerts/label/value/
  // text/glyphs) live in HttpMonitorSchema.h and are enforced as the filtered
  // document is walked, before anything lands in `dashboard`. The 8 KB body cap
  // below is NOT a hard bound on memory used while parsing: it only rejects
  // responses whose server-declared Content-Length exceeds the limit (fetch()
  // checks http.getSize() before parsing) or is absent/unknown. A server that
  // lies about Content-Length can still make ArduinoJson read an unbounded
  // amount from getStream() before the schema caps apply — deserializeJson()
  // degrades to a NoMemory DeserializationError rather than crashing
  // (ArduinoJson v7), so this doesn't corrupt anything, but it means the 8 KB
  // figure is a cap on trusted-length responses, not a hard ceiling.
  static constexpr long MAX_BODY_BYTES = 8192;

  State state = NO_CONFIG;
  HttpMonitorConfig::Config config;
  HttpMonitorSchema::Dashboard dashboard;
  std::string configError;
  std::string fetchError;
  int httpStatusCode = 0;
  unsigned long lastPollMs = 0;
  unsigned long pollIntervalMs = 30000;
  int pollsUntilClean = 1;
  bool hasDashboard = false;
  bool cleanRefreshDue = false;
  // The state whose content is currently on the panel. fetch() compares against
  // this (not `state`, which is always FETCHING at commit time) to decide whether
  // a poll needs to touch the e-ink at all.
  State displayedState = NO_CONFIG;
  // Set when the user presses Confirm. A manual refresh always repaints, even if
  // the server returns byte-identical data: the press is an explicit request for
  // a redraw, and silently skipping it makes the button look broken. Automatic
  // polls have no such obligation — that is the whole point of the skip.
  bool forceRedraw = false;

  // Button-action ack: a small, brief "sent" indicator, and nothing more -- no
  // re-poll, no rendering of the reply, no poll-timer reset. Cleared by loop()
  // once ACTION_ACK_MS has elapsed, which requests one more update to erase it.
  static constexpr unsigned long ACTION_ACK_MS = 1500;
  char actionAck[24] = {};
  // Start time, not an absolute deadline -- compared via wrap-safe subtraction
  // (millis() - actionAckStartMs >= ACTION_ACK_MS), matching every other timer
  // in this file (lastPollMs). An absolute `until` deadline
  // breaks at the ~49-day millis() wrap: if it wrapped to a small number, a
  // plain `millis() >= until` comparison would go true immediately.
  unsigned long actionAckStartMs = 0;

  // Auto-drop WiFi lifecycle -- only meaningful when HttpMonitorConfig::
  // autoDropWifi(config) is true (config.intervalSec > AUTO_DROP_MIN_INTERVAL_SEC).
  // A start time, not an absolute deadline -- wrap-safe subtraction (millis() -
  // wifiHoldStartMs >= holdMs), matching every other timer in this file
  // (actionAckStartMs, lastPollMs). Armed by a directional button press while
  // auto-drop is active (a further press re-arms/extends it); cleared by loop()
  // once wifi_hold_sec has elapsed, at which point the NEXT fetch() that
  // completes will RADIO.shutdown() again.
  bool wifiHoldActive = false;
  unsigned long wifiHoldStartMs = 0;
  // Bounded timeout for the blocking headless reconnect -- same order of
  // magnitude as the existing blocking fetch()/sendAction() HTTP calls.
  static constexpr unsigned long WIFI_RECONNECT_TIMEOUT_MS = 8000;

  // R1: sliced light sleep. The nav buttons are an ADC resistor ladder
  // (InputManager.cpp), not digital GPIOs, so there is no edge to wake a GPIO
  // interrupt on -- the only way to notice a press is to wake up and poll the
  // ADC. See maybeLightSleep() for the render-in-progress guard reasoning.
  //
  // 40ms, not the originally-planned 200ms: InputManager::update() commits a
  // press/release only on a LATER sample that still sees the same pin state
  // (a state change alone just resets its debounce clock) -- so detecting a
  // tap needs the button held across (at least) two samples, and the sample
  // period is however long main.cpp's loop takes, which this sleep call
  // dominates. At 200ms that pushed the worst-case detectable tap length past
  // 400ms -- long enough that ordinary taps, including Back, produced no
  // event at all. At 40ms, two samples fit inside ~80ms, comfortably under a
  // normal ~100ms+ tap; see maybeLightSleep()'s comment for the full
  // worst-case arithmetic. ~1-2ms of wake overhead per 40ms slice still keeps
  // the device asleep ~95-97% of idle wall time.
  static constexpr uint64_t SLICE_US = 40000;
  static constexpr unsigned long SLICE_MS = 40;
  // Set by maybeLightSleep() only when a slice actually ran this loop()
  // iteration (never unconditionally); read back by skipLoopDelay() in the
  // SAME iteration, since main.cpp calls activityManager.loop() and then
  // checks skipLoopDelay() immediately after. Reset at the top of loop() so a
  // later iteration that doesn't sleep (or returns early) doesn't inherit a
  // stale true.
  bool sleptThisIteration = false;

  // R10: battery-critical guard. getBatteryPercentage() is uncached on X4
  // (reads the ADC every call) -- throttled the same way as everything else
  // in this file that shouldn't run on every loop() iteration.
  unsigned long lastBatteryCheckMs = 0;
  static constexpr unsigned long BATTERY_CHECK_INTERVAL_MS = 5000;
  // A single unaveraged ADC sample can sag momentarily (a WiFi TX burst or an
  // e-ink refresh drawing current) and read at/below the threshold without the
  // battery actually being critical -- latching on one such sample would be
  // irreversible (the terminal state only recovers via USB). Require this many
  // CONSECUTIVE throttled checks (BATTERY_CHECK_INTERVAL_MS apart, so ~15s
  // total) to all read at/below batteryMinPct before actually latching.
  int batteryCriticalConsecutiveLowReads = 0;
  static constexpr int BATTERY_CRITICAL_CONSECUTIVE_READS = 3;

  void loadConfig();
  void fetch();
  void sendAction(ActionSlot slot);
  // Splits config.authHeader on ':' and adds it to `http` -- shared by fetch()
  // and sendAction() so the header logic isn't duplicated.
  void applyAuthHeader(HTTPClient& http);
  // Sets actionAck (RenderLock'd) and requests a repaint -- shared by
  // sendAction()'s "sending"/result steps.
  void setActionAck(const char* text);
  // Headless (no UI) WiFi reconnect for the auto-drop path: connects to the
  // last-known network via WifiCredentialStore, reproducing (not calling --
  // it's private/non-static) the WiFi.mode/hostname/begin() body of
  // WifiSelectionActivity::attemptConnection(). Blocking, bounded by timeoutMs.
  // Never pushes the interactive picker on failure -- see fetch()'s auto-drop
  // branch for why an unattended panel must not do that.
  //
  // showAck controls whether this paints an actionAck status ("connecting" /
  // "no saved network" / "connect failed"): true only from sendAction()'s
  // user-initiated reconnect, false from fetch()'s background poll. A
  // background reconnect ack would never actually be visible during the wait
  // (setActionAck()'s requestUpdate() is deferred until loop() returns, and
  // this whole call happens inside loop()'s call to fetch()) -- it would only
  // land coalesced with the finished poll's own repaint, forcing one anyway
  // even when fetch()'s contentChanged said nothing changed. That defeats the
  // "unchanged data leaves the panel idle" property auto-drop configs depend
  // on for their panel-wear budget.
  bool reconnectWifiHeadless(unsigned long timeoutMs, bool showAck);
  // Enters a single 200ms light-sleep slice when HttpMonitorPower::
  // canLightSleep() says it's safe -- see its doc comment and the definition
  // in HttpMonitorActivity.cpp for the render-in-progress race analysis.
  void maybeLightSleep();
  // Throttled (BATTERY_CHECK_INTERVAL_MS) battery-critical check: transitions
  // into BATTERY_CRITICAL when the battery is at/below config.batteryMinPct
  // and USB is not connected. A no-op when config.batteryMinPct == 0.
  void checkBatteryCritical();

  void renderNoConfig();
  void renderFetching();
  void renderError();
  void renderDashboard();
  // Terminal "Battery critical -- monitoring stopped" screen. Visual precedent:
  // SleepActivity::renderStatusSleepScreen() (boot_sleep/SleepActivity.cpp).
  void renderBatteryCritical();
  // Centered SMALL text just above the hint bar, white-filled behind it since
  // this band overlaps the dashboard content band; no-op when actionAck is empty.
  void drawActionAck();
  // Draws the wifiState glyph (filled/outline dot + two signal arcs, plus a
  // diagonal slash or underscore bar) in the x,y..+24,+24 header corner.
  // Vector primitives only -- see HttpMonitorLayout.h / renderDashboard() for
  // why this isn't a bitmap asset.
  void drawWifiIndicator(int x, int y, WifiIndicator wifiState);

  // The dashboard-wide font size: the server's top-level `fontSize` when it sent
  // one, otherwise monitor.conf's `font_size`. Returns a ladder index (0..3).
  int dashboardFontSizeIndex() const;
  // Maps dashboardFontSizeIndex() to a registered font id, validated against
  // renderer.getFontMap() (a shipping -DOMIT_FONTS build only registers a
  // handful of fonts) — falls back to UI_12_FONT_ID if the candidate is absent.
  int dashboardFontId() const;
  // Maps a row's `size` field (0..3) to a registered font id, validated against
  // renderer.getFontMap() like dashboardFontId(); SIZE_INHERIT selects the
  // dashboard-wide font. Falls back to UI_12 if absent.
  int fontForSize(uint8_t sizeIdx) const;
};
