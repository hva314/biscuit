// test/test_http_monitor_layout/test_http_monitor_layout.cpp
// Tests HttpMonitorLayout.h — the pure layout arithmetic shared between
// HttpMonitorActivity::renderDashboard() (shipping code) and test_preview.cpp
// (the preview harness). Header-only, no Arduino/HAL dependency.
// Run: pio test -e native -f test_http_monitor_layout

#include <unity.h>

#include "../../src/activities/apps/HttpMonitorLayout.h"

using namespace HttpMonitorLayout;

// ---- computeRowColumns ----

// The core whitespace fix: the bar is decoration, not data, and it no longer
// takes a 1/3 column budget. Label and value each get half the content width;
// the bar uses whatever is left between them (computeBarSpan). hasBar is
// retained in the signature but no longer changes the column split.
void test_row_columns_bar_gets_no_column_budget() {
  const auto cols = computeRowColumns(/*contentWidth=*/300, /*hasBar=*/true);
  TEST_ASSERT_EQUAL(150, cols.labelColMax);
  TEST_ASSERT_EQUAL(150, cols.valueColMax);
  TEST_ASSERT_EQUAL(300, cols.barColMax);  // no longer capped at contentWidth/3
}

void test_row_columns_without_bar_gives_same_split() {
  const auto cols = computeRowColumns(/*contentWidth=*/300, /*hasBar=*/false);
  TEST_ASSERT_EQUAL(150, cols.labelColMax);
  TEST_ASSERT_EQUAL(150, cols.valueColMax);
  TEST_ASSERT_EQUAL(300, cols.barColMax);
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

// Regression test for the old cap: even a tiny barColMax must NOT shrink the
// bar — it spans label->value (this is the core whitespace fix). barColMax is
// retained in the signature for call-site stability but ignored.
void test_bar_span_spans_label_to_value_regardless_of_bar_col_max() {
  const auto span = computeBarSpan(20, 480, 60, 60, /*barColMax=*/80, 10);
  TEST_ASSERT_EQUAL(300, span.barSpan);  // was capped to 80 — this is the bug being fixed
}

void test_bar_span_never_negative() {
  // Deliberately pathological: label + value alone exceed the page.
  const auto span = computeBarSpan(20, 200, 500, 500, 1000, 10);
  TEST_ASSERT_TRUE(span.barSpan >= 0);
}

void test_min_bar_span_floor_is_24() { TEST_ASSERT_EQUAL(24, MIN_BAR_SPAN); }

// ---- placeFractionalBar (bar.width < 1.0) ----

void test_fractional_bar_scales_span_and_left_aligns() {
  // fullSpan = 300, widthPct=50 -> span 150, left-aligned at the full barX
  const auto b = placeFractionalBar(20, 480, 60, 60, 10, /*widthPct=*/50, BarAlign::LEFT);
  TEST_ASSERT_EQUAL(90, b.barX);
  TEST_ASSERT_EQUAL(150, b.barSpan);
}

void test_fractional_bar_center_aligns() {
  const auto b = placeFractionalBar(20, 480, 60, 60, 10, /*widthPct=*/50, BarAlign::CENTER);
  // fullSpan=300, span=150, offset = (300-150)/2 = 75 -> barX = 90+75 = 165
  TEST_ASSERT_EQUAL(165, b.barX);
  TEST_ASSERT_EQUAL(150, b.barSpan);
}

void test_fractional_bar_right_aligns() {
  const auto b = placeFractionalBar(20, 480, 60, 60, 10, /*widthPct=*/50, BarAlign::RIGHT);
  // fullSpan=300, span=150, offset = 150 -> barX = 90+150 = 240
  TEST_ASSERT_EQUAL(240, b.barX);
  TEST_ASSERT_EQUAL(150, b.barSpan);
}

void test_fractional_bar_full_width_ignores_align() {
  const auto b = placeFractionalBar(20, 480, 60, 60, 10, /*widthPct=*/100, BarAlign::RIGHT);
  TEST_ASSERT_EQUAL(90, b.barX);  // span == fullSpan, so align leaves barX unchanged
  TEST_ASSERT_EQUAL(300, b.barSpan);
}

// ---- computeTotalLines ----

void test_total_lines_sums_headings_and_rows() {
  TEST_ASSERT_EQUAL(6, computeTotalLines(/*headingCount=*/2, /*rowCount=*/4, /*alertCount=*/0));
}

void test_total_lines_adds_alerts_heading_plus_each_alert() {
  // 2 headings + 4 rows + ("Alerts" heading + 3 alert lines) = 6 + 4 = 10
  TEST_ASSERT_EQUAL(10, computeTotalLines(2, 4, 3));
}

void test_total_lines_no_alerts_heading_when_alert_count_zero() { TEST_ASSERT_EQUAL(6, computeTotalLines(2, 4, 0)); }

// ---- per-entry heights ----

void test_kv_entry_height_is_one_line() { TEST_ASSERT_EQUAL(30, kvEntryHeight(/*lineH=*/30)); }

void test_text_entry_height_wraps_to_line_count() {
  TEST_ASSERT_EQUAL(30, textEntryHeight(/*lineH=*/30, /*lineCount=*/1));
  TEST_ASSERT_EQUAL(60, textEntryHeight(/*lineH=*/30, /*lineCount=*/2));
  // A zero/negative wrap result must still occupy at least one line.
  TEST_ASSERT_EQUAL(30, textEntryHeight(/*lineH=*/30, /*lineCount=*/0));
}

void test_spacer_divider_glyphs_entry_heights() {
  TEST_ASSERT_EQUAL(24, spacerEntryHeight(24));
  TEST_ASSERT_EQUAL(12, dividerEntryHeight());
  TEST_ASSERT_EQUAL(20, glyphsEntryHeight());
}

// ---- per-entry scroll metrics ----

void test_scroll_metrics_no_scroll_when_content_fits() {
  // band = 300..600, five 30px entries all fit (could hold 10)
  const int heights[] = {30, 30, 30, 30, 30};
  const auto m = computeScrollMetrics(heights, /*entryCount=*/5, /*contentTop=*/300, /*contentBottom=*/600);
  TEST_ASSERT_EQUAL(5, m.visibleEntries);
  TEST_ASSERT_EQUAL(0, m.maxScroll);
}

void test_scroll_metrics_positive_maxScroll_when_content_overflows() {
  int heights[25];
  for (int i = 0; i < 25; i++) heights[i] = 30;
  const auto m = computeScrollMetrics(heights, /*entryCount=*/25, /*contentTop=*/300, /*contentBottom=*/600);
  TEST_ASSERT_EQUAL(10, m.visibleEntries);
  TEST_ASSERT_EQUAL(15, m.maxScroll);
}

void test_scroll_metrics_variable_heights() {
  // band 100..200 (height 100): entries 40/20/40/20 -> 40+20+40 = 100 fits, the
  // trailing 20 does not. visibleEntries=3, maxScroll=1.
  const int heights[] = {40, 20, 40, 20};
  const auto m = computeScrollMetrics(heights, /*entryCount=*/4, /*contentTop=*/100, /*contentBottom=*/200);
  TEST_ASSERT_EQUAL(3, m.visibleEntries);
  TEST_ASSERT_EQUAL(1, m.maxScroll);
}

void test_scroll_metrics_tall_entry_beyond_band_counts_zero() {
  // A single entry taller than the whole band can never be shown fully, but it
  // is still the last (and only) entry, so the farthest useful offset is
  // entryCount-1 = 0 — NOT 1, which would let scrollOffset read entries[1] on a
  // one-element array (out-of-bounds). The old entryCount-visibleEntries formula
  // produced maxScroll=1 here; the reverse-suffix walk corrects it.
  const int heights[] = {500};
  const auto m = computeScrollMetrics(heights, /*entryCount=*/1, /*contentTop=*/300, /*contentBottom=*/600);
  TEST_ASSERT_EQUAL(0, m.visibleEntries);
  TEST_ASSERT_EQUAL(0, m.maxScroll);
}

void test_max_glyphs_clamps_run_to_band() {
  // Each glyph cell is 16px + 4px gap, and the last cell has no trailing gap,
  // so a run of n cells spans 20*n - 4. maxGlyphsFor(w) is the largest n with
  // 20*n - 4 <= w. A 440px band (X4 contentWidth) holds 22 cells; a 32-char
  // glyphs string must truncate to 22 instead of drawing 636px across it (the
  // pre-fix behavior that spilled out of the band and spammed the GFX
  // out-of-range log).
  TEST_ASSERT_EQUAL(22, maxGlyphsFor(440));
  TEST_ASSERT_TRUE(20 * maxGlyphsFor(440) - 4 <= 440);
  TEST_ASSERT_EQUAL(1, maxGlyphsFor(20));   // exactly one cell (16px) fits
  TEST_ASSERT_EQUAL(0, maxGlyphsFor(4));    // band too narrow for even one cell
  TEST_ASSERT_EQUAL(0, maxGlyphsFor(-10));  // degenerate width never overflows
}

void test_scroll_metrics_tall_trailing_row_is_reachable() {
  // band 0..100: leading entries are 40/40/40, the final one is 80 (a tall
  // wrapped-text row). entryCount - visibleEntries = 4 - 2 = 2, but offset 2
  // draws entry2 at y=0 and entry3 at y=40 -> 40+80=120 > 100, so the tall
  // trailing row is never fully on screen. The farthest useful offset is 3
  // (only entry3, y=0, 80 <= 100). maxScroll must reach it. This is the
  // variable-height scroll regression the naive formula misses.
  const int heights[] = {40, 40, 40, 80};
  const auto m = computeScrollMetrics(heights, /*entryCount=*/4, /*contentTop=*/0, /*contentBottom=*/100);
  TEST_ASSERT_EQUAL(2, m.visibleEntries);
  TEST_ASSERT_EQUAL(3, m.maxScroll);
}

// ---- entryY (prefix-sum y cursor) ----

void test_entry_y_is_prefix_sum_from_scroll_offset() {
  const int heights[] = {30, 20, 40};
  TEST_ASSERT_EQUAL(100, entryY(heights, /*scrollOffset=*/1, /*i=*/1, /*contentTop=*/100));
  TEST_ASSERT_EQUAL(120, entryY(heights, /*scrollOffset=*/1, /*i=*/2, /*contentTop=*/100));
  TEST_ASSERT_EQUAL(190, entryY(heights, /*scrollOffset=*/0, /*i=*/3, /*contentTop=*/100));
  TEST_ASSERT_EQUAL(140, entryY(heights, /*scrollOffset=*/2, /*i=*/3, /*contentTop=*/100));
}

// ---- clampScrollOffset ----

void test_clamp_scroll_offset_clamps_high() { TEST_ASSERT_EQUAL(15, clampScrollOffset(999, 15)); }

void test_clamp_scroll_offset_clamps_low() { TEST_ASSERT_EQUAL(0, clampScrollOffset(-5, 15)); }

void test_clamp_scroll_offset_passes_through_in_range() { TEST_ASSERT_EQUAL(7, clampScrollOffset(7, 15)); }

// ---- computeTotalPages ----

void test_total_pages_exact_multiple() {
  TEST_ASSERT_EQUAL(2, computeTotalPages(/*totalEntries=*/20, /*pageSize=*/10));
}

void test_total_pages_rounds_up_partial_last_page() {
  // This is Finding B7's regression test: maxScroll/pageSize+1 would read "1/1"
  // here (maxScroll=15, pageSize=10 -> 15/10+1 = 2... but the real bug case is
  // smaller: totalEntries=21, pageSize=10 -> old formula: maxScroll=11,
  // 11/10+1=2 which happens to agree; the actual mismatch shows up when
  // maxScroll < pageSize but there IS a second page, e.g. totalEntries=11.
  TEST_ASSERT_EQUAL(2, computeTotalPages(/*totalEntries=*/11, /*pageSize=*/10));
}

void test_total_pages_single_page() { TEST_ASSERT_EQUAL(1, computeTotalPages(5, 10)); }

// ============================================================
void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_row_columns_bar_gets_no_column_budget);
  RUN_TEST(test_row_columns_without_bar_gives_same_split);
  RUN_TEST(test_bar_span_is_remaining_room_between_label_and_value);
  RUN_TEST(test_bar_span_spans_label_to_value_regardless_of_bar_col_max);
  RUN_TEST(test_bar_span_never_negative);
  RUN_TEST(test_min_bar_span_floor_is_24);
  RUN_TEST(test_fractional_bar_scales_span_and_left_aligns);
  RUN_TEST(test_fractional_bar_center_aligns);
  RUN_TEST(test_fractional_bar_right_aligns);
  RUN_TEST(test_fractional_bar_full_width_ignores_align);
  RUN_TEST(test_total_lines_sums_headings_and_rows);
  RUN_TEST(test_total_lines_adds_alerts_heading_plus_each_alert);
  RUN_TEST(test_total_lines_no_alerts_heading_when_alert_count_zero);
  RUN_TEST(test_kv_entry_height_is_one_line);
  RUN_TEST(test_text_entry_height_wraps_to_line_count);
  RUN_TEST(test_spacer_divider_glyphs_entry_heights);
  RUN_TEST(test_scroll_metrics_no_scroll_when_content_fits);
  RUN_TEST(test_scroll_metrics_positive_maxScroll_when_content_overflows);
  RUN_TEST(test_scroll_metrics_variable_heights);
  RUN_TEST(test_scroll_metrics_tall_entry_beyond_band_counts_zero);
  RUN_TEST(test_scroll_metrics_tall_trailing_row_is_reachable);
  RUN_TEST(test_max_glyphs_clamps_run_to_band);
  RUN_TEST(test_entry_y_is_prefix_sum_from_scroll_offset);
  RUN_TEST(test_clamp_scroll_offset_clamps_high);
  RUN_TEST(test_clamp_scroll_offset_clamps_low);
  RUN_TEST(test_clamp_scroll_offset_passes_through_in_range);
  RUN_TEST(test_total_pages_exact_multiple);
  RUN_TEST(test_total_pages_rounds_up_partial_last_page);
  RUN_TEST(test_total_pages_single_page);
  return UNITY_END();
}
