#pragma once

#include <I18n.h>

#include <cstddef>
#include <memory>
#include <vector>

class GfxRenderer;         // forward declaration
class MappedInputManager;  // forward declaration
class Activity;            // forward declaration

// Curated registry of activities that can be launched as the boot app, in place of
// the apps menu. Index 0 is always the apps-menu sentinel (make() returns nullptr;
// callers fall back to ActivityManager::goHome()). Index order is the persisted
// ordinal stored in CrossPointSettings::bootTarget, so entries must never be
// reordered or removed once shipped — only appended.
namespace BootTargets {

// Total number of curated boot targets, including the apps-menu sentinel at index 0.
size_t count();

// Display label for the given index. Out-of-range indices fall back to index 0's label.
StrId label(size_t index);

// Convenience accessor for building the settings enum's value list. Size == count().
std::vector<StrId> labels();

// Whether `index` has a real activity factory (i.e. is not the apps-menu sentinel and
// is in range). Pure metadata, safe to call without any HAL/Arduino dependency —
// used by native unit tests in place of make(), which requires the real HAL-backed
// activity constructors.
bool hasFactory(size_t index);

// Constructs the activity for `index`. Returns nullptr for index 0 (apps menu) or any
// out-of-range index; the caller falls back to goHome() in that case.
std::unique_ptr<Activity> make(size_t index, GfxRenderer& renderer, MappedInputManager& mappedInput);

}  // namespace BootTargets
