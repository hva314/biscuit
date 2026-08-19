#pragma once

#include <ArduinoJson.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

// HttpMonitorSchema.h — pure filtered-JSON -> Dashboard walk used by
// HttpMonitorActivity::fetch(). Header-only and ArduinoJson-only so the exact
// parse path that ships compiles natively (test/test_http_monitor_schema).
//
// The row model was extended from the legacy {label, value, bar, alert} tuple
// to a typed schema (kv/text/bar/spacer/divider/glyphs) with shared formatting
// (align/bold/size). Every cap and default is enforced here, before anything
// lands in the Dashboard the render task reads. MAX_BODY_BYTES stays in the
// activity — it is a fetch() (HTTP) concern, not a schema one.
namespace HttpMonitorSchema {

static constexpr int MAX_SECTIONS = 6;
static constexpr int MAX_ROWS_PER_SECTION = 12;
static constexpr int MAX_ALERTS = 4;
static constexpr int MAX_LABEL_LEN = 24;
static constexpr int MAX_VALUE_LEN = 16;
static constexpr int MAX_TEXT_LEN = 64;    // `text` rows wrap; 64 chars is plenty
static constexpr int MAX_GLYPHS_LEN = 32;  // concatenated glyph chars

// Row `size` field: ladder index into the dashboard font ladder (0..3, matching
// dashboardFontId()). 0xFF means "inherit the dashboard's current font size".
static constexpr uint8_t SIZE_INHERIT = 0xFF;

enum class RowType : uint8_t { KV = 0, TEXT, BAR, SPACER, DIVIDER, GLYPHS };
enum class RowAlign : uint8_t { LEFT = 0, CENTER, RIGHT };

// The bar. Legacy rows used a bare integer (0..100); the extended schema allows
// an object with segments/width/align. `fullWidth` is derived: legacy int bars
// and any object bar with width >= 1.0 span label->value; fractional widths use
// align to place the shorter bar.
struct BarSpec {
  int value = -1;      // -1 = no bar, else 0..100
  int segments = 1;    // 1..24 segment blocks
  int widthPct = 100;  // 10..100 (% of the label->value span)
  RowAlign align = RowAlign::LEFT;
  bool fullWidth = true;
};

struct Row {
  char label[MAX_LABEL_LEN + 1] = {};
  char value[MAX_VALUE_LEN + 1] = {};
  char text[MAX_TEXT_LEN + 1] = {};
  char glyphs[MAX_GLYPHS_LEN + 1] = {};
  BarSpec bar;
  RowType type = RowType::KV;
  RowAlign align = RowAlign::LEFT;
  uint8_t sizeIdx = SIZE_INHERIT;
  uint8_t flags = 0;         // bit0 = alert, bit1 = bold
  int spacerHeight = 10;     // 2..60 px
  int dividerInset = 0;      // >= 0 px from each side
  int dividerLineWidth = 1;  // 1..2 px

  bool alert() const { return (flags & (1u << 0)) != 0; }
  bool bold() const { return (flags & (1u << 1)) != 0; }
};

struct Section {
  char heading[MAX_LABEL_LEN + 1] = {};
  std::vector<Row> rows;
};

struct Dashboard {
  char title[32] = {};
  char updated[32] = {};
  // Server-driven dashboard font size (ladder index 0..3). SIZE_INHERIT means
  // the server omitted it, in which case the activity falls back to
  // monitor.conf's `font_size`. Rows may still override per-row via `size`.
  uint8_t fontSize = SIZE_INHERIT;
  std::vector<Section> sections;
  std::vector<std::array<char, 48>> alerts;
};

// ---- Equality ----
// HttpMonitorActivity compares a freshly parsed Dashboard against the one on
// screen and skips the e-ink update entirely when they match. That comparison is
// the whole point: a monitor whose values are stable must not repaint (and so
// must not flash) on every poll. Every field that renderDashboard() draws has to
// participate, or a real change would be missed and the screen would go stale.
//
// The char arrays are safe to strcmp: copyBounded() always null-terminates, and
// both Row and Dashboard value-initialize their buffers.

inline bool operator==(const BarSpec& a, const BarSpec& b) {
  return a.value == b.value && a.segments == b.segments && a.widthPct == b.widthPct && a.align == b.align &&
         a.fullWidth == b.fullWidth;
}
inline bool operator!=(const BarSpec& a, const BarSpec& b) { return !(a == b); }

inline bool operator==(const Row& a, const Row& b) {
  return a.type == b.type && a.align == b.align && a.sizeIdx == b.sizeIdx && a.flags == b.flags &&
         a.spacerHeight == b.spacerHeight && a.dividerInset == b.dividerInset &&
         a.dividerLineWidth == b.dividerLineWidth && a.bar == b.bar && strcmp(a.label, b.label) == 0 &&
         strcmp(a.value, b.value) == 0 && strcmp(a.text, b.text) == 0 && strcmp(a.glyphs, b.glyphs) == 0;
}
inline bool operator!=(const Row& a, const Row& b) { return !(a == b); }

inline bool operator==(const Section& a, const Section& b) {
  return strcmp(a.heading, b.heading) == 0 && a.rows == b.rows;
}
inline bool operator!=(const Section& a, const Section& b) { return !(a == b); }

inline bool operator==(const Dashboard& a, const Dashboard& b) {
  return a.fontSize == b.fontSize && strcmp(a.title, b.title) == 0 && strcmp(a.updated, b.updated) == 0 &&
         a.sections == b.sections && a.alerts == b.alerts;
}
inline bool operator!=(const Dashboard& a, const Dashboard& b) { return !(a == b); }

// Null-terminated copy into a fixed buffer, truncating (not overflowing) if needed.
inline void copyBounded(char* dst, size_t dstSize, const char* src) {
  if (!src) src = "";
  if (dstSize == 0) return;
  strncpy(dst, src, dstSize - 1);
  dst[dstSize - 1] = '\0';
}

inline int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline RowType parseType(JsonVariantConst v) {
  const char* s = v | "";
  if (strcmp(s, "text") == 0) return RowType::TEXT;
  if (strcmp(s, "bar") == 0) return RowType::BAR;
  if (strcmp(s, "spacer") == 0) return RowType::SPACER;
  if (strcmp(s, "divider") == 0) return RowType::DIVIDER;
  if (strcmp(s, "glyphs") == 0) return RowType::GLYPHS;
  return RowType::KV;  // legacy rows and unknown types render as kv
}

inline RowAlign parseAlign(JsonVariantConst v) {
  const char* s = v | "";
  if (strcmp(s, "center") == 0) return RowAlign::CENTER;
  if (strcmp(s, "right") == 0) return RowAlign::RIGHT;
  return RowAlign::LEFT;
}

// Concatenate a glyph array ["#","o","#"] into "#o#". Full-width glyph chars
// (e.g. a "W" from the server) are appended verbatim, so they keep their width.
inline void copyGlyphs(char* dst, size_t dstSize, JsonVariantConst glyphs) {
  std::string out;
  if (glyphs.is<JsonArrayConst>()) {
    for (JsonVariantConst g : glyphs.as<JsonArrayConst>()) {
      if (!g.is<const char*>()) continue;
      if (out.size() >= dstSize - 1) break;
      out += g.as<const char*>();
    }
  }
  copyBounded(dst, dstSize, out.c_str());
}

inline void parseBar(JsonVariantConst v, BarSpec& out) {
  if (v.is<int>()) {
    // Legacy int form: 0..100, full width, single segment, left aligned.
    const int raw = v.as<int>();
    out.value = (raw >= 0 && raw <= 100) ? raw : -1;
    out.segments = 1;
    out.widthPct = 100;
    out.align = RowAlign::LEFT;
    out.fullWidth = true;
    return;
  }
  if (v.is<JsonObjectConst>()) {
    const JsonObjectConst obj = v.as<JsonObjectConst>();
    const int valueRaw = obj["value"] | -1;
    out.value = (valueRaw >= 0 && valueRaw <= 100) ? valueRaw : -1;
    out.segments = clampInt(obj["segments"] | 1, 1, 24);
    const double width = obj["width"] | 1.0;  // 0.1..1.0, e.g. 0.5 = half span
    out.widthPct = clampInt(static_cast<int>(width * 100.0 + 0.5), 10, 100);
    out.fullWidth = out.widthPct >= 100;
    out.align = parseAlign(obj["align"]);
    return;
  }
  // Wrong type (string, null, bool, ...) -> no bar.
  out = BarSpec{};
}

inline void parseRow(JsonObjectConst obj, Row& out) {
  out.type = parseType(obj["type"]);
  out.align = parseAlign(obj["align"]);
  const int sizeRaw = obj["size"] | -1;
  out.sizeIdx = (sizeRaw >= 0 && sizeRaw <= 3) ? static_cast<uint8_t>(sizeRaw) : SIZE_INHERIT;

  out.flags = 0;
  if (obj["alert"] | false) out.flags |= (1u << 0);
  if (obj["bold"] | false) out.flags |= (1u << 1);

  copyBounded(out.label, sizeof(out.label), obj["label"] | "");
  copyBounded(out.value, sizeof(out.value), obj["value"] | "");
  copyBounded(out.text, sizeof(out.text), obj["text"] | "");
  copyGlyphs(out.glyphs, sizeof(out.glyphs), obj["glyphs"]);
  parseBar(obj["bar"], out.bar);

  out.spacerHeight = clampInt(obj["height"] | 10, 2, 60);
  out.dividerInset = clampInt(obj["inset"] | 0, 0, 1 << 20);
  out.dividerLineWidth = clampInt(obj["lineWidth"] | 1, 1, 2);
}

// Walk the (already filtered) document into `next`, enforcing every cap.
inline void apply(const JsonDocument& doc, Dashboard& next, const char* defaultTitle) {
  next = Dashboard{};  // wipe any previous parse's state (vectors + fixed buffers)

  copyBounded(next.title, sizeof(next.title), doc["title"] | (defaultTitle ? defaultTitle : ""));
  copyBounded(next.updated, sizeof(next.updated), doc["updated"] | "");

  // Top-level font size: same 0..3 ladder as a row's `size`. Anything else
  // (absent, wrong type, out of range) leaves SIZE_INHERIT so the activity keeps
  // using monitor.conf's font_size -- existing servers stay unaffected.
  const int fontRaw = doc["fontSize"] | -1;
  next.fontSize = (fontRaw >= 0 && fontRaw <= 3) ? static_cast<uint8_t>(fontRaw) : SIZE_INHERIT;

  if (doc["alerts"].is<JsonArrayConst>()) {
    for (JsonVariantConst a : doc["alerts"].as<JsonArrayConst>()) {
      if (static_cast<int>(next.alerts.size()) >= MAX_ALERTS) break;
      if (!a.is<const char*>()) continue;
      std::array<char, 48> text{};
      copyBounded(text.data(), text.size(), a.as<const char*>());
      next.alerts.push_back(text);
    }
  }

  if (doc["sections"].is<JsonArrayConst>()) {
    for (JsonObjectConst secObj : doc["sections"].as<JsonArrayConst>()) {
      if (static_cast<int>(next.sections.size()) >= MAX_SECTIONS) break;
      Section section;
      copyBounded(section.heading, sizeof(section.heading), secObj["heading"] | "");
      if (secObj["rows"].is<JsonArrayConst>()) {
        for (JsonObjectConst rowObj : secObj["rows"].as<JsonArrayConst>()) {
          if (static_cast<int>(section.rows.size()) >= MAX_ROWS_PER_SECTION) break;
          Row row;
          parseRow(rowObj, row);
          section.rows.push_back(std::move(row));
        }
      }
      next.sections.push_back(std::move(section));
    }
  }
}

}  // namespace HttpMonitorSchema
