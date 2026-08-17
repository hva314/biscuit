// test/test_http_monitor_schema/test_http_monitor_schema.cpp
// Tests HttpMonitorSchema.h — the pure filtered-JSON -> Dashboard walk that
// HttpMonitorActivity::fetch() uses. Header-only + ArduinoJson (itself pure
// C++), so it compiles natively. Every cap (sections/rows/alerts/label/value/
// text/glyphs) and every default is asserted here, not just the happy path.
// Run: pio test -e native -f test_http_monitor_schema

#include <unity.h>

#include <ArduinoJson.h>
#include <cstring>

#include "../../src/activities/apps/HttpMonitorSchema.h"

using namespace HttpMonitorSchema;

static Dashboard parse(const char* json, const char* defaultTitle = "monitor") {
  JsonDocument doc;
  deserializeJson(doc, json);
  Dashboard d;
  apply(doc, d, defaultTitle);
  return d;
}

static const Row& firstRow(const Dashboard& d) { return d.sections[0].rows[0]; }

// ---- defaults ----

void test_legacy_kv_row_defaults() {
  const Dashboard d = parse(R"({"sections":[{"heading":"Sys","rows":[{"label":"CPU","value":"23%"}]}]})");
  TEST_ASSERT_EQUAL(1, (int)d.sections.size());
  TEST_ASSERT_EQUAL_STRING("Sys", d.sections[0].heading);
  const Row& r = firstRow(d);
  TEST_ASSERT_EQUAL((int)RowType::KV, (int)r.type);
  TEST_ASSERT_EQUAL((int)RowAlign::LEFT, (int)r.align);
  TEST_ASSERT_EQUAL((int)SIZE_INHERIT, (int)r.sizeIdx);
  TEST_ASSERT_FALSE(r.bold());
  TEST_ASSERT_FALSE(r.alert());
  TEST_ASSERT_EQUAL(-1, r.bar.value);
  TEST_ASSERT_EQUAL_STRING("CPU", r.label);
  TEST_ASSERT_EQUAL_STRING("23%", r.value);
}

void test_title_updated_defaults() {
  const Dashboard d = parse(R"({"sections":[]})", /*defaultTitle=*/"fallback");
  TEST_ASSERT_EQUAL_STRING("fallback", d.title);
  TEST_ASSERT_EQUAL_STRING("", d.updated);
  const Dashboard d2 = parse(R"({"title":"prod","updated":"12:01:02","sections":[]})");
  TEST_ASSERT_EQUAL_STRING("prod", d2.title);
  TEST_ASSERT_EQUAL_STRING("12:01:02", d2.updated);
}

// ---- unknown type -> kv ----

void test_unknown_type_falls_back_to_kv() {
  const Dashboard d = parse(R"({"sections":[{"rows":[{"type":"bogus","label":"L","value":"V"}]}]})");
  const Row& r = firstRow(d);
  TEST_ASSERT_EQUAL((int)RowType::KV, (int)r.type);
  TEST_ASSERT_EQUAL_STRING("L", r.label);
  TEST_ASSERT_EQUAL_STRING("V", r.value);
}

// ---- bar int: clamp 0..100, out of range -> absent ----

void test_bar_int_in_range() {
  const Dashboard d = parse(R"({"sections":[{"rows":[{"label":"D","value":"88%","bar":88}]}]})");
  const Row& r = firstRow(d);
  TEST_ASSERT_EQUAL(88, r.bar.value);
  TEST_ASSERT_EQUAL(1, r.bar.segments);
  TEST_ASSERT_EQUAL(100, r.bar.widthPct);  // legacy int == full width
  TEST_ASSERT_TRUE(r.bar.fullWidth);
  TEST_ASSERT_EQUAL((int)RowAlign::LEFT, (int)r.bar.align);
}

void test_bar_int_out_of_range_becomes_absent() {
  const Dashboard d = parse(R"({"sections":[{"rows":[{"label":"D","bar":150}]}]})");
  TEST_ASSERT_EQUAL(-1, firstRow(d).bar.value);
  const Dashboard d2 = parse(R"({"sections":[{"rows":[{"label":"D","bar":-5}]}]})");
  TEST_ASSERT_EQUAL(-1, firstRow(d2).bar.value);
  const Dashboard d3 = parse(R"({"sections":[{"rows":[{"label":"D","bar":0}]}]})");
  TEST_ASSERT_EQUAL(0, firstRow(d3).bar.value);
  const Dashboard d4 = parse(R"({"sections":[{"rows":[{"label":"D","bar":100}]}]})");
  TEST_ASSERT_EQUAL(100, firstRow(d4).bar.value);
}

// ---- bar object: value/segments/width/align ----

void test_bar_object_parses() {
  const Dashboard d =
      parse(R"({"sections":[{"rows":[{"label":"D","value":"50%","bar":{"value":50,"segments":5,"width":0.5,"align":"right"}}]}]})");
  const BarSpec& b = firstRow(d).bar;
  TEST_ASSERT_EQUAL(50, b.value);
  TEST_ASSERT_EQUAL(5, b.segments);
  TEST_ASSERT_EQUAL(50, b.widthPct);  // 0.5 -> 50
  TEST_ASSERT_FALSE(b.fullWidth);
  TEST_ASSERT_EQUAL((int)RowAlign::RIGHT, (int)b.align);
}

void test_bar_object_clamps_segments_width_and_value() {
  const Dashboard d = parse(
      R"({"sections":[{"rows":[{"label":"D","bar":{"value":150,"segments":99,"width":2.5,"align":"center"}}]}]})");
  const BarSpec& b = firstRow(d).bar;
  TEST_ASSERT_EQUAL(-1, b.value);  // value out of range -> absent
  TEST_ASSERT_EQUAL(24, b.segments);
  TEST_ASSERT_EQUAL(100, b.widthPct);  // 2.5 -> clamped to 1.0
  const Dashboard d2 = parse(R"({"sections":[{"rows":[{"label":"D","bar":{"value":50,"segments":0,"width":0.05}}]}]})");
  const BarSpec& b2 = firstRow(d2).bar;
  TEST_ASSERT_EQUAL(50, b2.value);
  TEST_ASSERT_EQUAL(1, b2.segments);
  TEST_ASSERT_EQUAL(10, b2.widthPct);  // 0.05 -> clamped to 0.1
}

// ---- truncation: label 24, value 16, text 64, glyphs 32 ----

void test_label_value_truncated() {
  const std::string label(40, 'A');
  const std::string value(30, 'B');
  const std::string json = R"({"sections":[{"rows":[{"label":")" + label + R"(","value":")" + value + R"("}]}]})";
  const Dashboard d = parse(json.c_str());
  const Row& r = firstRow(d);
  TEST_ASSERT_EQUAL(MAX_LABEL_LEN, (int)strlen(r.label));
  TEST_ASSERT_EQUAL(MAX_VALUE_LEN, (int)strlen(r.value));
  for (int i = 0; i < MAX_LABEL_LEN; i++) TEST_ASSERT_EQUAL('A', r.label[i]);
  for (int i = 0; i < MAX_VALUE_LEN; i++) TEST_ASSERT_EQUAL('B', r.value[i]);
}

void test_text_truncated_to_64() {
  const std::string longText(90, 'T');
  const std::string json = R"({"sections":[{"rows":[{"type":"text","text":")" + longText + R"("}]}]})";
  const Dashboard d = parse(json.c_str());
  TEST_ASSERT_EQUAL((int)RowType::TEXT, (int)firstRow(d).type);
  TEST_ASSERT_EQUAL(MAX_TEXT_LEN, (int)strlen(firstRow(d).text));
  TEST_ASSERT_EQUAL('T', firstRow(d).text[MAX_TEXT_LEN - 1]);
}

void test_glyphs_string_from_array() {
  const Dashboard d = parse(R"({"sections":[{"rows":[{"type":"glyphs","label":"LED","glyphs":["#","o","#","x"]}]}]})");
  const Row& r = firstRow(d);
  TEST_ASSERT_EQUAL((int)RowType::GLYPHS, (int)r.type);
  TEST_ASSERT_EQUAL_STRING("LED", r.label);
  TEST_ASSERT_EQUAL_STRING("#o#x", r.glyphs);
}

void test_glyphs_truncated_to_32() {
  std::string arr = "[";
  for (int i = 0; i < 40; i++) arr += "\"#\",";
  arr += "\"x\"]";
  const std::string json = R"({"sections":[{"rows":[{"type":"glyphs","glyphs":)" + arr + R"(}]}]})";
  const Dashboard d = parse(json.c_str());
  TEST_ASSERT_EQUAL(MAX_GLYPHS_LEN, (int)strlen(firstRow(d).glyphs));
}

// ---- caps: 6 sections, 12 rows/section, 4 alerts ----

void test_caps_sections_rows_alerts() {
  std::string json = "{\"alerts\":[";
  for (int i = 0; i < 8; i++) json += "\"alert\",";
  json += "\"last\"],\"sections\":[";
  for (int s = 0; s < 8; s++) {
    json += R"({"heading":"H","rows":[)";
    for (int r = 0; r < 15; r++) json += R"({"label":"r"},)";
    json += "{\"label\":\"last\"}]},";
  }
  json = json.substr(0, json.size() - 1) + "]}";
  const Dashboard d = parse(json.c_str());
  TEST_ASSERT_EQUAL(MAX_SECTIONS, (int)d.sections.size());
  TEST_ASSERT_EQUAL(MAX_ROWS_PER_SECTION, (int)d.sections[0].rows.size());
  TEST_ASSERT_EQUAL(MAX_ALERTS, (int)d.alerts.size());
}

// ---- shared formatting: align / bold / size ----

void test_align_parsing() {
  const Dashboard d = parse(R"({"sections":[{"rows":[{"label":"a","align":"center"},{"label":"b","align":"right"},{"label":"c","align":"bogus"}]}]})");
  TEST_ASSERT_EQUAL((int)RowAlign::CENTER, (int)d.sections[0].rows[0].align);
  TEST_ASSERT_EQUAL((int)RowAlign::RIGHT, (int)d.sections[0].rows[1].align);
  TEST_ASSERT_EQUAL((int)RowAlign::LEFT, (int)d.sections[0].rows[2].align);
}

void test_bold_and_size_roundtrip() {
  const Dashboard d = parse(R"({"sections":[{"rows":[{"label":"a","bold":true,"size":0}]}]})");
  const Row& r = firstRow(d);
  TEST_ASSERT_TRUE(r.bold());
  TEST_ASSERT_EQUAL(0, (int)r.sizeIdx);
  const Dashboard d2 = parse(R"({"sections":[{"rows":[{"label":"a","bold":false}]}]})");
  TEST_ASSERT_FALSE(firstRow(d2).bold());
}

void test_size_out_of_range_inherits() {
  const Dashboard d = parse(R"({"sections":[{"rows":[{"label":"a","size":7}]}]})");
  TEST_ASSERT_EQUAL((int)SIZE_INHERIT, (int)firstRow(d).sizeIdx);
}

// ---- row types ----

void test_bar_row_parses_like_kv() {
  const Dashboard d = parse(R"({"sections":[{"rows":[{"type":"bar","label":"B","value":"9","bar":9}]}]})");
  const Row& r = firstRow(d);
  TEST_ASSERT_EQUAL((int)RowType::BAR, (int)r.type);
  TEST_ASSERT_EQUAL_STRING("B", r.label);
  TEST_ASSERT_EQUAL_STRING("9", r.value);
  TEST_ASSERT_EQUAL(9, r.bar.value);
}

void test_text_row_parses_text() {
  const Dashboard d = parse(R"({"sections":[{"rows":[{"type":"text","text":"some status","align":"center","bold":true}]}]})");
  const Row& r = firstRow(d);
  TEST_ASSERT_EQUAL((int)RowType::TEXT, (int)r.type);
  TEST_ASSERT_EQUAL_STRING("some status", r.text);
  TEST_ASSERT_EQUAL((int)RowAlign::CENTER, (int)r.align);
  TEST_ASSERT_TRUE(r.bold());
}

void test_spacer_height_clamped_2_to_60() {
  const Dashboard d = parse(R"({"sections":[{"rows":[{"type":"spacer","height":20},{"type":"spacer","height":1},{"type":"spacer","height":100}]}]})");
  TEST_ASSERT_EQUAL((int)RowType::SPACER, (int)d.sections[0].rows[0].type);
  TEST_ASSERT_EQUAL(20, d.sections[0].rows[0].spacerHeight);
  TEST_ASSERT_EQUAL(2, d.sections[0].rows[1].spacerHeight);
  TEST_ASSERT_EQUAL(60, d.sections[0].rows[2].spacerHeight);
  const Dashboard d2 = parse(R"({"sections":[{"rows":[{"type":"spacer"}]}]})");
  TEST_ASSERT_EQUAL(10, firstRow(d2).spacerHeight);  // default 10
}

void test_divider_inset_linewidth_label() {
  const Dashboard d =
      parse(R"({"sections":[{"rows":[{"type":"divider","inset":15,"lineWidth":2,"label":"Section A"}]}]})");
  const Row& r = firstRow(d);
  TEST_ASSERT_EQUAL((int)RowType::DIVIDER, (int)r.type);
  TEST_ASSERT_EQUAL(15, r.dividerInset);
  TEST_ASSERT_EQUAL(2, r.dividerLineWidth);
  TEST_ASSERT_EQUAL_STRING("Section A", r.label);
  // negative inset / out-of-range lineWidth clamp to sane values
  const Dashboard d2 = parse(R"({"sections":[{"rows":[{"type":"divider","inset":-5,"lineWidth":9}]}]})");
  TEST_ASSERT_EQUAL(0, firstRow(d2).dividerInset);
  TEST_ASSERT_EQUAL(2, firstRow(d2).dividerLineWidth);
}

// ============================================================
void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_legacy_kv_row_defaults);
  RUN_TEST(test_title_updated_defaults);
  RUN_TEST(test_unknown_type_falls_back_to_kv);
  RUN_TEST(test_bar_int_in_range);
  RUN_TEST(test_bar_int_out_of_range_becomes_absent);
  RUN_TEST(test_bar_object_parses);
  RUN_TEST(test_bar_object_clamps_segments_width_and_value);
  RUN_TEST(test_label_value_truncated);
  RUN_TEST(test_text_truncated_to_64);
  RUN_TEST(test_glyphs_string_from_array);
  RUN_TEST(test_glyphs_truncated_to_32);
  RUN_TEST(test_caps_sections_rows_alerts);
  RUN_TEST(test_align_parsing);
  RUN_TEST(test_bold_and_size_roundtrip);
  RUN_TEST(test_size_out_of_range_inherits);
  RUN_TEST(test_bar_row_parses_like_kv);
  RUN_TEST(test_text_row_parses_text);
  RUN_TEST(test_spacer_height_clamped_2_to_60);
  RUN_TEST(test_divider_inset_linewidth_label);
  return UNITY_END();
}
