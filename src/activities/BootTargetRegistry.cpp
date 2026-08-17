#include "BootTargetRegistry.h"

#include <array>

#ifndef NATIVE_TEST
#include "activities/apps/BatteryMonitorActivity.h"
#include "activities/apps/CalculatorActivity.h"
#include "activities/apps/ClockActivity.h"
#include "activities/apps/DeviceInfoActivity.h"
#include "activities/apps/DiceRollerActivity.h"
#include "activities/apps/HttpMonitorActivity.h"
#include "activities/apps/SdFileBrowserActivity.h"
#include "activities/reader/ReaderActivity.h"
#endif  // NATIVE_TEST

namespace {

// Pure metadata table — no HAL/Arduino dependency, safe to compile natively (see
// BootTargets::hasFactory() below for why this is split from the factories).
struct MetaEntry {
  StrId label;
};

constexpr std::array<MetaEntry, 9> kMeta = {{
    {StrId::STR_BOOT_APP_MENU},
    {StrId::STR_CLOCK},
    {StrId::STR_HTTP_MONITOR},
    {StrId::STR_BATTERY_MONITOR},
    {StrId::STR_FILE_BROWSER},
    {StrId::STR_CALCULATOR},
    {StrId::STR_READER},
    {StrId::STR_DEVICE_INFO},
    {StrId::STR_DICE_ROLLER},
}};

}  // namespace

size_t BootTargets::count() { return kMeta.size(); }

StrId BootTargets::label(size_t index) { return index < kMeta.size() ? kMeta[index].label : kMeta[0].label; }

std::vector<StrId> BootTargets::labels() {
  std::vector<StrId> v;
  v.reserve(kMeta.size());
  for (const auto& e : kMeta) v.push_back(e.label);
  return v;
}

#ifndef NATIVE_TEST

namespace {

std::unique_ptr<Activity> makeClock(GfxRenderer& r, MappedInputManager& m) {
  return std::make_unique<ClockActivity>(r, m);
}
std::unique_ptr<Activity> makeHttpMonitor(GfxRenderer& r, MappedInputManager& m) {
  return std::make_unique<HttpMonitorActivity>(r, m);
}
std::unique_ptr<Activity> makeBatteryMonitor(GfxRenderer& r, MappedInputManager& m) {
  return std::make_unique<BatteryMonitorActivity>(r, m);
}
std::unique_ptr<Activity> makeFileBrowser(GfxRenderer& r, MappedInputManager& m) {
  return std::make_unique<SdFileBrowserActivity>(r, m);
}
std::unique_ptr<Activity> makeCalculator(GfxRenderer& r, MappedInputManager& m) {
  return std::make_unique<CalculatorActivity>(r, m);
}
std::unique_ptr<Activity> makeReader(GfxRenderer& r, MappedInputManager& m) {
  // Empty path -> opens the reader library.
  return std::make_unique<ReaderActivity>(r, m, "");
}
std::unique_ptr<Activity> makeDeviceInfo(GfxRenderer& r, MappedInputManager& m) {
  return std::make_unique<DeviceInfoActivity>(r, m);
}
std::unique_ptr<Activity> makeDiceRoller(GfxRenderer& r, MappedInputManager& m) {
  return std::make_unique<DiceRollerActivity>(r, m);
}

using MakeFn = std::unique_ptr<Activity> (*)(GfxRenderer&, MappedInputManager&);

// Index-aligned with kMeta above.
constexpr std::array<MakeFn, 9> kMakers = {
    nullptr,
    &makeClock,
    &makeHttpMonitor,
    &makeBatteryMonitor,
    &makeFileBrowser,
    &makeCalculator,
    &makeReader,
    &makeDeviceInfo,
    &makeDiceRoller,
};

static_assert(kMakers.size() == kMeta.size(), "kMakers and kMeta must stay index-aligned");

}  // namespace

// Reflects the real factory table directly (rather than a hand-maintained parallel bool) so this
// can never drift from what make() actually does.
bool BootTargets::hasFactory(size_t index) { return index < kMakers.size() && kMakers[index] != nullptr; }

std::unique_ptr<Activity> BootTargets::make(size_t index, GfxRenderer& renderer, MappedInputManager& mappedInput) {
  if (index == 0 || index >= kMakers.size() || !kMakers[index]) return nullptr;
  return kMakers[index](renderer, mappedInput);
}

#else  // NATIVE_TEST

// kMakers doesn't exist under NATIVE_TEST (no HAL-backed activity headers are included), so
// hasFactory() falls back to the structural contract instead: every index except the apps-menu
// sentinel (0) has a factory in the real build. This is what the native unit tests verify.
bool BootTargets::hasFactory(size_t index) { return index != 0 && index < count(); }

// Stubbed out: constructing the real activities requires the HAL, which native unit
// tests must not link against. Native tests exercise hasFactory()/count()/labels()
// instead — see test/test_boot_target_registry/test_boot_target_registry.cpp.
std::unique_ptr<Activity> BootTargets::make(size_t, GfxRenderer&, MappedInputManager&) { return nullptr; }

#endif  // NATIVE_TEST
