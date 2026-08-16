// test/test_http_monitor_layout/test_http_monitor_layout.cpp
// Tests HttpMonitorLayout.h — the pure layout arithmetic shared between
// HttpMonitorActivity::renderDashboard() (shipping code) and test_preview.cpp
// (the preview harness). Header-only, no Arduino/HAL dependency.
// Run: pio test -e native -f test_http_monitor_layout

#include <unity.h>

#include "../../src/activities/apps/HttpMonitorLayout.h"

using namespace HttpMonitorLayout;

// ---- computeRowColumns ----

void test_row_columns_with_bar_caps_label_to_a_third() {
  const auto cols = computeRowColumns(/*contentWidth=*/300, /*hasBar=*/true);
  TEST_ASSERT_EQUAL(100, cols.labelColMax);
  TEST_ASSERT_EQUAL(100, cols.valueColMax);
  TEST_ASSERT_EQUAL(100, cols.barColMax);
}

void test_row_columns_without_bar_gives_label_a_half() {
  const auto cols = computeRowColumns(/*contentWidth=*/300, /*hasBar=*/false);
  TEST_ASSERT_EQUAL(150, cols.labelColMax);
  TEST_ASSERT_EQUAL(100, cols.valueColMax);
}

// ---- computeBarSpan ----

void test_bar_span_is_remaining_room_between_label_and_value() {
  // pageWidth=440+2*20=480, sidePadding=20, labelWidth=60, valueWidth=60, gap=10
  const auto span = computeBarSpan(/*contentSidePadding=*/20, /*pageWidth=*/480, /*labelWidth=*/60,
                                    /*valueWidth=*/60, /*barColMax=*/1000, /*gap=*/10);
  // barX = 20+60+10 = 90; barRight = 480-20-60-10 = 390; span = 300
  TEST_ASSERT_EQUAL(90, span.barX);
  TEST_ASSERT_EQUAL(300, span.barSpan);
}

void test_bar_span_is_capped_so_it_cannot_absorb_all_slack() {
  const auto span = computeBarSpan(20, 480, 60, 60, /*barColMax=*/80, 10);
  TEST_ASSERT_EQUAL(80, span.barSpan);  // would be 300 uncapped — this is Finding 6's regression test
}

void test_bar_span_never_negative() {
  // Deliberately pathological: label + value alone exceed the page.
  const auto span = computeBarSpan(20, 200, 500, 500, 1000, 10);
  TEST_ASSERT_TRUE(span.barSpan >= 0);
}

// ---- computeTotalLines ----

void test_total_lines_sums_headings_and_rows() {
  TEST_ASSERT_EQUAL(6, computeTotalLines(/*headingCount=*/2, /*rowCount=*/4, /*alertCount=*/0));
}

void test_total_lines_adds_alerts_heading_plus_each_alert() {
  // 2 headings + 4 rows + ("Alerts" heading + 3 alert lines) = 6 + 4 = 10
  TEST_ASSERT_EQUAL(10, computeTotalLines(2, 4, 3));
}

void test_total_lines_no_alerts_heading_when_alert_count_zero() {
  TEST_ASSERT_EQUAL(6, computeTotalLines(2, 4, 0));
}

// ---- computeScrollMetrics ----

void test_scroll_metrics_no_scroll_when_content_fits() {
  // lineH=30, band = 300..600 -> 10 visible lines, only 5 needed
  const auto m = computeScrollMetrics(/*totalLines=*/5, /*contentTop=*/300, /*contentBottom=*/600, /*lineH=*/30);
  TEST_ASSERT_EQUAL(10, m.visibleLines);
  TEST_ASSERT_EQUAL(0, m.maxScroll);
}

void test_scroll_metrics_positive_maxScroll_when_content_overflows() {
  const auto m = computeScrollMetrics(/*totalLines=*/25, /*contentTop=*/300, /*contentBottom=*/600, /*lineH=*/30);
  TEST_ASSERT_EQUAL(10, m.visibleLines);
  TEST_ASSERT_EQUAL(15, m.maxScroll);
}

// A larger font (larger lineH) fits fewer lines on screen, so it must yield
// fewer visibleLines and a larger maxScroll than a smaller font over the same
// content band and total line count — this is the arithmetic that backs
// HttpMonitorActivity's Up/Down font-size feature.
void test_scroll_metrics_larger_lineH_yields_fewer_visible_lines_and_more_scroll() {
  const auto small = computeScrollMetrics(/*totalLines=*/20, /*contentTop=*/300, /*contentBottom=*/600, /*lineH=*/20);
  const auto large = computeScrollMetrics(/*totalLines=*/20, /*contentTop=*/300, /*contentBottom=*/600, /*lineH=*/40);
  TEST_ASSERT_EQUAL(15, small.visibleLines);
  TEST_ASSERT_EQUAL(5, small.maxScroll);
  TEST_ASSERT_EQUAL(7, large.visibleLines);
  TEST_ASSERT_EQUAL(13, large.maxScroll);
  TEST_ASSERT_TRUE(large.visibleLines < small.visibleLines);
  TEST_ASSERT_TRUE(large.maxScroll > small.maxScroll);
}

// ---- clampScrollOffset ----

void test_clamp_scroll_offset_clamps_high() {
  TEST_ASSERT_EQUAL(15, clampScrollOffset(999, 15));
}

void test_clamp_scroll_offset_clamps_low() {
  TEST_ASSERT_EQUAL(0, clampScrollOffset(-5, 15));
}

void test_clamp_scroll_offset_passes_through_in_range() {
  TEST_ASSERT_EQUAL(7, clampScrollOffset(7, 15));
}

// ---- computeTotalPages ----

void test_total_pages_exact_multiple() {
  TEST_ASSERT_EQUAL(2, computeTotalPages(/*totalLines=*/20, /*pageSize=*/10));
}

void test_total_pages_rounds_up_partial_last_page() {
  // This is Finding B7's regression test: maxScroll/pageSize+1 would read "1/1"
  // here (maxScroll=15, pageSize=10 -> 15/10+1 = 2... but the real bug case is
  // smaller: totalLines=21, pageSize=10 -> old formula: maxScroll=11,
  // 11/10+1=2 which happens to agree; the actual mismatch shows up when
  // maxScroll < pageSize but there IS a second page, e.g. totalLines=11.
  TEST_ASSERT_EQUAL(2, computeTotalPages(/*totalLines=*/11, /*pageSize=*/10));
}

void test_total_pages_single_page() {
  TEST_ASSERT_EQUAL(1, computeTotalPages(5, 10));
}

// ============================================================
void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_row_columns_with_bar_caps_label_to_a_third);
  RUN_TEST(test_row_columns_without_bar_gives_label_a_half);
  RUN_TEST(test_bar_span_is_remaining_room_between_label_and_value);
  RUN_TEST(test_bar_span_is_capped_so_it_cannot_absorb_all_slack);
  RUN_TEST(test_bar_span_never_negative);
  RUN_TEST(test_total_lines_sums_headings_and_rows);
  RUN_TEST(test_total_lines_adds_alerts_heading_plus_each_alert);
  RUN_TEST(test_total_lines_no_alerts_heading_when_alert_count_zero);
  RUN_TEST(test_scroll_metrics_no_scroll_when_content_fits);
  RUN_TEST(test_scroll_metrics_positive_maxScroll_when_content_overflows);
  RUN_TEST(test_scroll_metrics_larger_lineH_yields_fewer_visible_lines_and_more_scroll);
  RUN_TEST(test_clamp_scroll_offset_clamps_high);
  RUN_TEST(test_clamp_scroll_offset_clamps_low);
  RUN_TEST(test_clamp_scroll_offset_passes_through_in_range);
  RUN_TEST(test_total_pages_exact_multiple);
  RUN_TEST(test_total_pages_rounds_up_partial_last_page);
  RUN_TEST(test_total_pages_single_page);
  return UNITY_END();
}
