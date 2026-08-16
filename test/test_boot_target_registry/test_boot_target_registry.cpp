// test/test_boot_target_registry/test_boot_target_registry.cpp
// Tests BootTargets registry invariants — pure logic, no Arduino/HAL dependency.
// Run: pio test -e native -f test_boot_target_registry
//
// This pulls in the real implementation (BootTargetRegistry.cpp) directly so the
// tests exercise the exact table that ships on-device. BootTargetRegistry.cpp guards
// its concrete-activity includes and the real make() factories behind
// `#ifndef NATIVE_TEST` (same precedent as HttpMonitorConfig.cpp's loadFromSd()), so
// including it here (with -DNATIVE_TEST set by the native env) never pulls in the
// HAL-backed Activity subclasses. That also means make() itself is stubbed out to
// always return nullptr under this build — it is NOT exercised here. Instead, the
// registry exposes hasFactory(), a pure-metadata seam that tells us whether a real
// factory exists for a given index without ever constructing one.
//
// NOTE: this test was written but never compiled/run (project rule forbids running
// pio/build commands from this environment) — the owner should run the command
// above to get the first RED/GREEN result.

#include <unity.h>

#include "../../src/activities/BootTargetRegistry.h"
#include "../../src/activities/BootTargetRegistry.cpp"

void test_count_matches_labels_size() {
  TEST_ASSERT_EQUAL(BootTargets::count(), BootTargets::labels().size());
}

void test_count_is_six() {
  // 1 apps-menu sentinel + 5 curated apps.
  TEST_ASSERT_EQUAL(6, BootTargets::count());
}

void test_index_zero_is_apps_menu_sentinel() {
  TEST_ASSERT_FALSE(BootTargets::hasFactory(0));
}

void test_every_curated_index_has_a_factory() {
  for (size_t i = 1; i < BootTargets::count(); i++) {
    TEST_ASSERT_TRUE_MESSAGE(BootTargets::hasFactory(i), "expected a factory for every non-zero index");
  }
}

void test_out_of_range_index_has_no_factory() {
  TEST_ASSERT_FALSE(BootTargets::hasFactory(BootTargets::count()));
  TEST_ASSERT_FALSE(BootTargets::hasFactory(BootTargets::count() + 100));
}

void test_out_of_range_label_falls_back_to_index_zero() {
  TEST_ASSERT_EQUAL(BootTargets::label(0), BootTargets::label(BootTargets::count()));
}

void test_labels_match_label_accessor_in_order() {
  const std::vector<StrId> labels = BootTargets::labels();
  for (size_t i = 0; i < labels.size(); i++) {
    TEST_ASSERT_EQUAL(BootTargets::label(i), labels[i]);
  }
}

// ============================================================
void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_count_matches_labels_size);
  RUN_TEST(test_count_is_six);
  RUN_TEST(test_index_zero_is_apps_menu_sentinel);
  RUN_TEST(test_every_curated_index_has_a_factory);
  RUN_TEST(test_out_of_range_index_has_no_factory);
  RUN_TEST(test_out_of_range_label_falls_back_to_index_zero);
  RUN_TEST(test_labels_match_label_accessor_in_order);
  return UNITY_END();
}
