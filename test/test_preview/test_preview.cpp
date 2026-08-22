// test/test_preview/test_preview.cpp
// ============================================================
// biscuit. Screen Preview Tool
//
// Renders activity screens to BMP files without hardware.
// After running: open test/preview_*.bmp in any image viewer.
//
// Usage:
//   pio test -e native -f test_preview
//
// To preview YOUR new activity:
//   1. Copy an existing render_xxx() function below
//   2. Replace the drawing calls with your activity's render() logic
//   3. Add a RUN_TEST line in main()
//   4. Run and open the BMP
// ============================================================

#include <unity.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "BitmapRenderer.h"

// Pure layout arithmetic shared with the real HttpMonitorActivity::renderDashboard()
// — the whole point is that this preview exercises the SAME column/bar/scroll math
// the device runs, not a hand-copy of it (drawing primitives still differ; the
// mock draws with drawRect/fillRect the same way the real renderer now does).
#include "../../src/activities/apps/HttpMonitorLayout.h"
// The typed row schema (RowType/RowAlign/BarSpec) that the firmware renderDashboard
// dispatches on — the preview mirrors that dispatch, so the enum values can't drift.
#include "../../src/activities/apps/HttpMonitorSchema.h"

// We use BitmapRenderer (a real pixel-drawing GfxRenderer) instead of the no-op mock
static GfxRenderer renderer;

// Font IDs matching the firmware
#define UI_10_FONT_ID 10
#define UI_12_FONT_ID 12
#define SMALL_FONT_ID 8

// ============================================================
// Helper: mimics GUI.drawHeader / GUI.drawButtonHints
// ============================================================
static void drawHeader(const char* title, const char* subtitle = nullptr) {
  renderer.drawCenteredText(UI_12_FONT_ID, 12, title, true, 1);
  renderer.drawLine(15, 42, 465, 42);
  if (subtitle) renderer.drawCenteredText(SMALL_FONT_ID, 48, subtitle);
}

static void drawButtonHints(const char* b1, const char* b2, const char* b3, const char* b4) {
  renderer.drawLine(0, 768, 480, 768);
  if (b1) renderer.drawText(SMALL_FONT_ID, 15, 775, b1);
  if (b2) renderer.drawCenteredText(SMALL_FONT_ID, 775, b2);
  if (b4) {
    int w = renderer.getTextWidth(SMALL_FONT_ID, b4);
    renderer.drawText(SMALL_FONT_ID, 465 - w, 775, b4);
  }
}

static void drawListItem(int y, const char* text, bool selected) {
  if (selected) {
    renderer.fillRect(0, y, 480, 36, true);
    renderer.drawText(UI_10_FONT_ID, 20, y + 8, text, false);
  } else {
    renderer.drawText(UI_10_FONT_ID, 20, y + 8, text, true);
  }
}

static void drawPip(int cx, int cy, int r) {
  for (int dy = -r; dy <= r; dy++) {
    int dx = 0;
    while ((dx + 1) * (dx + 1) + dy * dy <= r * r) dx++;
    renderer.fillRect(cx - dx, cy + dy, dx * 2 + 1, 1, true);
  }
}

static void drawDie(int x, int y, int size, int value) {
  renderer.fillRect(x + 4, y + 4, size, size, true);    // shadow
  renderer.fillRect(x, y, size, size, false);           // white face
  renderer.drawRect(x, y, size, size);                  // outer border
  renderer.drawRect(x + 2, y + 2, size - 4, size - 4);  // inner border

  int pip = size / 8;
  int m = size / 4;
  int l = x + m, r = x + size - m;
  int t = y + m, b = y + size - m;
  int mx = x + size / 2, my = y + size / 2;

  switch (value) {
    case 1:
      drawPip(mx, my, pip);
      break;
    case 2:
      drawPip(r, t, pip);
      drawPip(l, b, pip);
      break;
    case 3:
      drawPip(r, t, pip);
      drawPip(mx, my, pip);
      drawPip(l, b, pip);
      break;
    case 4:
      drawPip(l, t, pip);
      drawPip(r, t, pip);
      drawPip(l, b, pip);
      drawPip(r, b, pip);
      break;
    case 5:
      drawPip(l, t, pip);
      drawPip(r, t, pip);
      drawPip(mx, my, pip);
      drawPip(l, b, pip);
      drawPip(r, b, pip);
      break;
    case 6:
      drawPip(l, t, pip);
      drawPip(r, t, pip);
      drawPip(l, my, pip);
      drawPip(r, my, pip);
      drawPip(l, b, pip);
      drawPip(r, b, pip);
      break;
  }
}

// ============================================================
// Screen renders — copy & modify for your new activity
// ============================================================

void render_dice_2d6() {
  renderer.clearScreen();
  drawHeader("Dice Roller");

  // Two d6 dice thrown on table
  drawDie(75, 180, 120, 5);
  drawDie(285, 210, 120, 3);

  renderer.drawCenteredText(UI_10_FONT_ID, 420, "2d6: 5 + 3 = 8", true, 1);
  drawButtonHints("Back", "Reroll", "", "");

  TEST_ASSERT_TRUE(renderer.saveBMP("test/preview_dice_2d6.bmp"));
}

void render_dice_4d6() {
  renderer.clearScreen();
  drawHeader("Dice Roller");

  drawDie(55, 120, 100, 6);
  drawDie(265, 135, 100, 2);
  drawDie(65, 310, 100, 4);
  drawDie(275, 320, 100, 1);

  renderer.drawCenteredText(UI_10_FONT_ID, 500, "4d6: 6 + 2 + 4 + 1 = 13", true, 1);
  drawButtonHints("Back", "Reroll", "", "");

  TEST_ASSERT_TRUE(renderer.saveBMP("test/preview_dice_4d6.bmp"));
}

void render_dice_1d20() {
  renderer.clearScreen();
  drawHeader("Dice Roller");

  // Single d20 — large, centered, number inside
  int x = 170, y = 220, size = 140;
  renderer.fillRect(x + 4, y + 4, size, size, true);
  renderer.fillRect(x, y, size, size, false);
  renderer.drawRect(x, y, size, size);
  renderer.drawRect(x + 2, y + 2, size - 4, size - 4);
  renderer.drawCenteredText(UI_12_FONT_ID, y + size / 2 - 12, "17", true, 1);
  renderer.drawCenteredText(SMALL_FONT_ID, y + size - 20, "d20");

  renderer.drawCenteredText(UI_10_FONT_ID, 440, "d20: 17", true, 1);
  drawButtonHints("Back", "Reroll", "", "");

  TEST_ASSERT_TRUE(renderer.saveBMP("test/preview_dice_d20.bmp"));
}

void render_beacon_select() {
  renderer.clearScreen();
  drawHeader("Beacon Spam");

  const char* modes[] = {"Random", "Custom (SD)", "Rickroll", "Funny"};
  for (int i = 0; i < 4; i++) {
    drawListItem(70 + i * 38, modes[i], i == 2);
  }
  drawButtonHints("Back", "Select", "Up", "Down");

  TEST_ASSERT_TRUE(renderer.saveBMP("test/preview_beacon_select.bmp"));
}

void render_beacon_running() {
  renderer.clearScreen();
  drawHeader("Beacon Spam");

  renderer.drawCenteredText(UI_10_FONT_ID, 300, "RICKROLL");
  renderer.drawCenteredText(UI_12_FONT_ID, 340, "Never Gonna Give You Up", true, 1);
  renderer.drawCenteredText(UI_10_FONT_ID, 390, "SSID 1/8  Cycle 12");
  renderer.drawCenteredText(UI_10_FONT_ID, 420, "Interval: 2000ms");
  drawButtonHints("Stop", "", "", "");

  TEST_ASSERT_TRUE(renderer.saveBMP("test/preview_beacon_running.bmp"));
}

void render_evil_portal() {
  renderer.clearScreen();
  drawHeader("Evil Portal");

  int y = 100;
  renderer.drawText(SMALL_FONT_ID, 15, y, "ACTIVE", true, 1);
  y += 45;
  renderer.drawText(SMALL_FONT_ID, 15, y, "SSID:", true, 1);
  renderer.drawText(UI_10_FONT_ID, 80, y, "Free WiFi");
  y += 45;
  renderer.drawText(UI_10_FONT_ID, 15, y, "Clients: 3");
  y += 45;
  renderer.drawText(UI_10_FONT_ID, 15, y, "Captured: 2", true, 1);
  y += 45;
  renderer.drawText(UI_10_FONT_ID, 15, y, "Last: john@example.com");
  drawButtonHints("Stop", "", "", "");

  TEST_ASSERT_TRUE(renderer.saveBMP("test/preview_evil_portal.bmp"));
}

void render_apps_menu() {
  renderer.clearScreen();
  drawHeader("Apps");

  const char* cats[] = {"Network Tools", "Wireless Testing", "Games", "Utilities"};
  for (int i = 0; i < 4; i++) {
    drawListItem(70 + i * 38, cats[i], i == 1);
  }
  drawButtonHints("Back", "Select", "Up", "Down");

  TEST_ASSERT_TRUE(renderer.saveBMP("test/preview_apps_menu.bmp"));
}

void render_countdowntimer() {
  renderer.clearScreen();
  drawHeader("CountdownTimer");

  // === YOUR RENDER CODE HERE ===
  // Copy drawing calls from your activity's render() method.
  // Available functions:
  //   renderer.drawCenteredText(UI_12_FONT_ID, y, "text", true, 1);  // bold
  //   renderer.drawCenteredText(UI_10_FONT_ID, y, "text");            // normal
  //   renderer.drawText(UI_10_FONT_ID, x, y, "text");
  //   renderer.drawText(SMALL_FONT_ID, x, y, "small text");
  //   renderer.fillRect(x, y, w, h, true);    // black
  //   renderer.fillRect(x, y, w, h, false);   // white
  //   renderer.drawRect(x, y, w, h);           // border
  //   renderer.drawLine(x1, y1, x2, y2);
  //   drawListItem(y, "Item", selected);       // list row
  //   drawDie(x, y, size, value);              // d6 die face
  //   drawHeader("Title", "subtitle");
  //
  // Screen: 480 wide x 800 tall
  // Header ends at y=42, button hints start at y=768

  renderer.drawCenteredText(UI_12_FONT_ID, 380, "TODO: add render code", true, 1);

  drawButtonHints("Back", "Select", "Up", "Down");

  TEST_ASSERT_TRUE(renderer.saveBMP("test/preview_countdowntimer.bmp"));
}

void render_diceroller() {
  // Reference: src\activities\apps\DiceRollerActivity.cpp
  renderer.clearScreen();
  drawHeader("DiceRoller");

  // === YOUR RENDER CODE HERE ===
  // Copy drawing calls from your activity's render() method.
  // Available functions:
  //   renderer.drawCenteredText(UI_12_FONT_ID, y, "text", true, 1);  // bold
  //   renderer.drawCenteredText(UI_10_FONT_ID, y, "text");            // normal
  //   renderer.drawText(UI_10_FONT_ID, x, y, "text");
  //   renderer.drawText(SMALL_FONT_ID, x, y, "small text");
  //   renderer.fillRect(x, y, w, h, true);    // black
  //   renderer.fillRect(x, y, w, h, false);   // white
  //   renderer.drawRect(x, y, w, h);           // border
  //   renderer.drawLine(x1, y1, x2, y2);
  //   drawListItem(y, "Item", selected);       // list row
  //   drawDie(x, y, size, value);              // d6 die face
  //   drawHeader("Title", "subtitle");
  //
  // Screen: 480 wide x 800 tall
  // Header ends at y=42, button hints start at y=768

  renderer.drawCenteredText(UI_12_FONT_ID, 380, "TODO: add render code", true, 1);

  drawButtonHints("Back", "Select", "Up", "Down");

  TEST_ASSERT_TRUE(renderer.saveBMP("test/preview_diceroller.bmp"));
}
// ============================================================
// HttpMonitorActivity previews — rendered at BOTH panel geometries (X4 480x800,
// X3 528x792) using a dedicated renderer instance sized for each, since the
// module-level `renderer` above stays fixed at the X4 default for the rest of
// this file's previews.
//
// Mirrors HttpMonitorActivity::render{Dashboard,Error,NoConfig}() layout logic:
// header -> content band (contentTop..contentBottom) -> button hints, with every
// coordinate derived from screen size / UITheme-equivalent metrics (mocked here
// as the Classic-theme constants baked into the drawHeader/drawButtonHints
// helpers above) rather than literals.
// ============================================================

// The module-level drawButtonHints() above always draws on the module-level
// `renderer`, not on whatever GfxRenderer instance is passed to it — wrong for
// these previews, which use a dedicated instance per panel geometry. This
// variant draws on `r` and derives its position from `r`'s own dimensions.
static void drawButtonHintsOn(GfxRenderer& r, const char* b1, const char* b2, const char* b3, const char* b4) {
  const int pageWidth = r.getScreenWidth();
  const int pageHeight = r.getScreenHeight();
  const int hintsY = pageHeight - 32;
  r.drawLine(0, hintsY - 7, pageWidth, hintsY - 7);
  if (b1 && b1[0]) r.drawText(SMALL_FONT_ID, 15, hintsY, b1);
  if (b2 && b2[0]) r.drawCenteredText(SMALL_FONT_ID, hintsY, b2);
  if (b4 && b4[0]) {
    int w = r.getTextWidth(SMALL_FONT_ID, b4);
    r.drawText(SMALL_FONT_ID, pageWidth - 15 - w, hintsY, b4);
  }
}

struct HttpMonitorPreviewRow {
  const char* label = nullptr;
  const char* value = nullptr;
  int bar = -1;        // legacy int bar: 0..100, -1 = none
  bool alert = false;  // 6x6 marker + BOLD (kv rows and alert lines)
  // Extended fields mirroring the typed schema Row (HttpMonitorSchema.h). All
  // defaulted so the legacy {label, value, bar, alert} aggregate init used by the
  // older fixtures below keeps compiling unchanged.
  const char* text = nullptr;    // `text` rows (wrap to <= 2 lines)
  const char* glyphs = nullptr;  // `glyphs` rows (concatenated glyph chars)
  int type = 0;                  // HttpMonitorSchema::RowType (0 = KV)
  int align = 0;                 // HttpMonitorSchema::RowAlign (0 = LEFT)
  bool bold = false;
  int sizeIdx = 0xFF;                 // HttpMonitorSchema::SIZE_INHERIT (ladder 0..3)
  int spacerHeight = 10;              // `spacer` px
  int dividerInset = 0;               // `divider` px in from each side
  int dividerLineWidth = 1;           // `divider` rule thickness (1..2)
  HttpMonitorSchema::BarSpec barObj;  // object-form bar; overrides `bar` when value >= 0
};

struct HttpMonitorPreviewSection {
  const char* heading;
  std::vector<HttpMonitorPreviewRow> rows;
};

// Row `type` values are HttpMonitorSchema::RowType — the dispatch switch below
// casts row.type straight to that enum, so the preview can't drift from the
// schema values the firmware dispatches on.

// Maps a row `size` ladder index (0..3) to a font id, mirroring the firmware's
// fontForSize(). The preview has no getFontMap(); index 3 (BOOKERLY_14) isn't in
// the preview font set, so it falls back to UI_12 exactly as fontForSize() does
// when a candidate is absent. SIZE_INHERIT -> UI_12 (preview dashboards don't
// change the dashboard font size).
static int previewFontForSize(int sizeIdx) {
  switch (sizeIdx) {
    case 0:
      return SMALL_FONT_ID;
    case 1:
      return UI_10_FONT_ID;
    case 2:
      return UI_12_FONT_ID;
    case 3:
      return UI_12_FONT_ID;  // BOOKERLY_14 not registered in previews
    default:
      return UI_12_FONT_ID;  // SIZE_INHERIT
  }
}

// Merge the legacy int `bar` into the bar object form — a legacy int bar IS a
// BarSpec with defaults (1 segment, 100% width, LEFT); the object form wins when
// present.
static HttpMonitorSchema::BarSpec effectiveBar(const HttpMonitorPreviewRow& row) {
  HttpMonitorSchema::BarSpec b = row.barObj;
  if (b.value < 0 && row.bar >= 0) b.value = row.bar;
  return b;
}

// ---- mock-adapted drawing primitives ----
// The real renderer has drawArc / fillPolygon / drawLine(lineWidth, state); the
// preview mock has none of those, so each is approximated with mock primitives
// (scanlines / drawPixel circles). The mock draws 1-bit fills the same way the
// real renderer does, so the geometry is faithful even if the rasterization
// differs.

// Filled disk via scanlines (the firmware's '.' glyph draws four quadrant arcs).
// Same loop as drawPip() above but on the passed renderer.
static void drawPipOn(GfxRenderer& r, int cx, int cy, int rad) {
  for (int dy = -rad; dy <= rad; dy++) {
    int dx = 0;
    while ((dx + 1) * (dx + 1) + dy * dy <= rad * rad) dx++;
    r.fillRect(cx - dx, cy + dy, dx * 2 + 1, 1, true);
  }
}

// Filled up-triangle, apex (cx+8, cy), base (cx..cx+15, cy+15) — the firmware's
// '!' glyph calls fillPolygon, which the mock lacks; fill row-by-row with a
// linearly widening half-width.
static void fillUpTriangle(GfxRenderer& r, int cx, int cy) {
  for (int d = 0; d < 16; ++d) {
    const int half = (d * 8 + 15) / 16;  // 0..8, rounding so the base hits both corners
    int left = cx + 8 - half;
    if (left < cx) left = cx;
    int right = cx + 8 + half;
    if (right > cx + 15) right = cx + 15;
    r.fillRect(left, cy + d, right - left + 1, 1, true);
  }
}

// Circle outline via the midpoint algorithm — the mock has no drawArc, so the
// wifi indicator's dot outline and signal arcs are approximated as plain pixel
// circles here (the real renderer draws each with drawArc quadrant calls).
// upperHalfOnly restricts the plot to the two upper quadrants (yDir=-1 in the
// real renderer's terms), which is what the two signal arcs need.
static void drawCircleOutline(GfxRenderer& r, int ccx, int ccy, int radius, bool upperHalfOnly = false) {
  int x = radius;
  int y = 0;
  int err = 1 - radius;
  while (x >= y) {
    r.drawPixel(ccx + x, ccy - y, true);
    r.drawPixel(ccx + y, ccy - x, true);
    r.drawPixel(ccx - y, ccy - x, true);
    r.drawPixel(ccx - x, ccy - y, true);
    if (!upperHalfOnly) {
      r.drawPixel(ccx - x, ccy + y, true);
      r.drawPixel(ccx - y, ccy + x, true);
      r.drawPixel(ccx + y, ccy + x, true);
      r.drawPixel(ccx + x, ccy + y, true);
    }
    ++y;
    if (err < 0) {
      err += 2 * y + 1;
    } else {
      --x;
      err += 2 * (y - x) + 1;
    }
  }
}

// Mirrors HttpMonitorActivity::WifiIndicator -- that enum is private to the
// activity (which isn't natively includable: it pulls in Activity.h's Arduino
// dependency), so the preview keeps its own copy of the three states, same as
// it already does for the schema's row-type constants.
enum class PreviewWifiState { AlwaysOn, Dropped, Held };

// The header's far-right 24x24 wifi-indicator corner: dot (filled for
// AlwaysOn/Held, outline for Dropped) + two signal arcs, plus a diagonal slash
// (Dropped) or underscore bar (Held). Mirrors HttpMonitorActivity::
// drawWifiIndicator()'s geometry; only the drawing primitives differ (the mock
// lacks drawArc and drawLine's lineWidth overload).
static void drawWifiIndicatorOn(GfxRenderer& r, int x, int y, PreviewWifiState state) {
  constexpr int SIZE = 24;
  constexpr int DOT_R = 2;
  constexpr int ARC1_R = 7;
  constexpr int ARC2_R = 11;
  const int cx = x + SIZE / 2;
  const int cy = y + SIZE - 6;

  if (state == PreviewWifiState::Dropped) {
    drawCircleOutline(r, cx, cy, DOT_R);
  } else {
    drawPipOn(r, cx, cy, DOT_R);
  }
  drawCircleOutline(r, cx, cy, ARC1_R, /*upperHalfOnly=*/true);
  drawCircleOutline(r, cx, cy, ARC2_R, /*upperHalfOnly=*/true);

  if (state == PreviewWifiState::Dropped) {
    r.drawLine(x + 1, y + 1, x + SIZE - 2, y + SIZE - 2);
  } else if (state == PreviewWifiState::Held) {
    r.drawLine(cx - 6, y + SIZE - 1, cx + 6, y + SIZE - 1);
  }
}

// ---- fixture-building helpers for the extended schema ----

static HttpMonitorPreviewRow kvRow(const char* label, const char* value, int bar = -1, bool alert = false) {
  HttpMonitorPreviewRow r;
  r.label = label;
  r.value = value;
  r.bar = bar;
  r.alert = alert;
  return r;
}

static HttpMonitorPreviewRow barObjRow(const char* label, const char* value, int barValue, int segments, int widthPct,
                                       int align) {
  HttpMonitorPreviewRow r;
  r.label = label;
  r.value = value;
  r.barObj.value = barValue;
  r.barObj.segments = segments;
  r.barObj.widthPct = widthPct;
  r.barObj.align = static_cast<HttpMonitorSchema::RowAlign>(align);
  return r;
}

static HttpMonitorPreviewRow textRow(const char* text, int align = 0, bool bold = false, int sizeIdx = 0xFF) {
  HttpMonitorPreviewRow r;
  r.type = static_cast<int>(HttpMonitorSchema::RowType::TEXT);
  r.text = text;
  r.align = align;
  r.bold = bold;
  r.sizeIdx = sizeIdx;
  return r;
}

static HttpMonitorPreviewRow spacerRow(int height) {
  HttpMonitorPreviewRow r;
  r.type = static_cast<int>(HttpMonitorSchema::RowType::SPACER);
  r.spacerHeight = height;
  return r;
}

static HttpMonitorPreviewRow dividerRow(const char* label, int inset, int lineWidth) {
  HttpMonitorPreviewRow r;
  r.type = static_cast<int>(HttpMonitorSchema::RowType::DIVIDER);
  r.label = label;
  r.dividerInset = inset;
  r.dividerLineWidth = lineWidth;
  return r;
}

static HttpMonitorPreviewRow glyphsRow(const char* glyphs, const char* label = nullptr, int align = 0) {
  HttpMonitorPreviewRow r;
  r.type = static_cast<int>(HttpMonitorSchema::RowType::GLYPHS);
  r.glyphs = glyphs;
  r.label = label;
  r.align = align;
  return r;
}

// Exercises the SAME pure layout math as HttpMonitorActivity::renderDashboard()
// via HttpMonitorLayout.h — column allocation, fractional-bar placement, per-entry
// mixed heights, and the visible-entry count are computed by the shared functions,
// not a hand-copy. Only the drawing primitives are mock-specific (drawRect/fillRect,
// scanline approximations of drawArc/fillPolygon). The dispatch switch casts each
// row's `type` to HttpMonitorSchema::RowType, so the preview can't drift from the
// schema values the firmware dispatches on. Returns the number of entries that did
// NOT fit, so callers can assert on clipping (the dashboard does not scroll).
static int renderHttpMonitorDashboardGeneric(GfxRenderer& r, const std::vector<HttpMonitorPreviewSection>& sections,
                                             const char* title, const char* updated = nullptr,
                                             const std::vector<const char*>& alerts = {}, int intervalSec = 30,
                                             PreviewWifiState wifiState = PreviewWifiState::AlwaysOn) {
  const int pageWidth = r.getScreenWidth();
  const int pageHeight = r.getScreenHeight();
  const int headerY = 5;  // metrics.topPadding
  const int headerH = 45;
  const int buttonHintsH = 40;
  const int verticalSpacing = 10;
  const int sidePadding = 20;
  const int contentBottom = pageHeight - buttonHintsH - verticalSpacing;
  const int contentWidth = pageWidth - 2 * sidePadding;
  const int unreservedContentTop = headerY + headerH + verticalSpacing;
  const int BAR_GAP = 10;
  const int rowFontGlobal = previewFontForSize(2 /* UI_12, the preview's dashboard font */);
  const int rowLineHGlobal = r.getLineHeight(rowFontGlobal) + 6;

  r.clearScreen();

  // ---- explicit header: title (left, BOLD) / interval / updated (both SMALL) /
  // wifi indicator (far-right 24x24 corner). NO rule line — the old drawHeader
  // rule is gone. Positioning comes from HttpMonitorLayout::computeHeaderLayout()
  // — the SAME function HttpMonitorActivity::renderDashboard() calls — so this
  // can't silently drift from the shipping header; only the wifi glyph's actual
  // drawing primitives differ (mock vs real renderer).
  const char* displayTitle = (title && title[0] != '\0') ? title : "Http Monitor";
  const std::string updatedStr = updated ? updated : "";
  const int updatedW = !updatedStr.empty() ? r.getTextWidth(SMALL_FONT_ID, updatedStr.c_str()) : 0;
  char intervalBuf[16];
  snprintf(intervalBuf, sizeof(intervalBuf), "%ds", intervalSec);
  const int intervalW = r.getTextWidth(SMALL_FONT_ID, intervalBuf);
  constexpr int WIFI_ICON_SIZE = 24;
  constexpr int HEADER_GAP = 8;
  const auto headerLayout =
      HttpMonitorLayout::computeHeaderLayout(pageWidth, sidePadding, intervalW, updatedW, WIFI_ICON_SIZE, HEADER_GAP);
  const int titleY = headerY + (headerH - r.getLineHeight(UI_12_FONT_ID)) / 2;
  const std::string titleStr = r.truncatedText(UI_12_FONT_ID, displayTitle, headerLayout.titleZone, 1 /* BOLD */);
  r.drawText(UI_12_FONT_ID, sidePadding, titleY, titleStr.c_str(), true, 1);
  const int smallY = headerY + (headerH - r.getLineHeight(SMALL_FONT_ID)) / 2;
  r.drawText(SMALL_FONT_ID, headerLayout.intervalX, smallY, intervalBuf);
  if (!updatedStr.empty()) r.drawText(SMALL_FONT_ID, headerLayout.updatedX, smallY, updatedStr.c_str());
  const int iconY = headerY + (headerH - WIFI_ICON_SIZE) / 2;
  drawWifiIndicatorOn(r, headerLayout.iconX, iconY, wifiState);

  // ---- per-entry heights + parallel entry list (headings, typed rows, alerts).
  enum class EntryKind { HEADING, ROW, ALERT };
  struct PreviewEntry {
    EntryKind kind;
    const char* text;
    const HttpMonitorPreviewRow* row;
  };
  std::vector<int> entryHeights;
  std::vector<PreviewEntry> entries;

  for (auto& section : sections) {
    if (section.heading[0] != '\0') {
      entryHeights.push_back(rowLineHGlobal);
      entries.push_back({EntryKind::HEADING, section.heading, nullptr});
    }
    for (auto& row : section.rows) {
      const int rowFont = previewFontForSize(row.sizeIdx);
      const int rowLineH = r.getLineHeight(rowFont) + 6;
      switch (static_cast<HttpMonitorSchema::RowType>(row.type)) {
        case HttpMonitorSchema::RowType::SPACER:
          entryHeights.push_back(HttpMonitorLayout::spacerEntryHeight(row.spacerHeight));
          break;
        case HttpMonitorSchema::RowType::DIVIDER:
          entryHeights.push_back(HttpMonitorLayout::dividerEntryHeight());
          break;
        case HttpMonitorSchema::RowType::GLYPHS:
          entryHeights.push_back(HttpMonitorLayout::glyphsEntryHeight());
          break;
        case HttpMonitorSchema::RowType::TEXT: {
          // Alert text rows draw a 6x6 marker + indent (drawTextRow below), so
          // they wrap at a narrower width; the height must match what is drawn.
          const int markerPad = row.alert ? 10 : 0;
          const auto lines =
              r.wrappedText(rowFont, row.text ? row.text : "", contentWidth - markerPad, 2, row.bold ? 1 : 0);
          entryHeights.push_back(HttpMonitorLayout::textEntryHeight(rowLineH, static_cast<int>(lines.size())));
          break;
        }
        default:  // KV (also covers BAR — a bar is a kv row with a bar)
          entryHeights.push_back(HttpMonitorLayout::kvEntryHeight(rowLineH));
          break;
      }
      entries.push_back({EntryKind::ROW, nullptr, &row});
    }
  }
  if (!alerts.empty()) {
    entryHeights.push_back(rowLineHGlobal);
    entries.push_back({EntryKind::HEADING, "Alerts", nullptr});
    for (const char* alert : alerts) {
      entryHeights.push_back(rowLineHGlobal);
      entries.push_back({EntryKind::ALERT, alert, nullptr});
    }
  }
  const int entryCount = static_cast<int>(entryHeights.size());

  const int contentTop = unreservedContentTop;
  const int visibleEntries =
      HttpMonitorLayout::visibleEntryCount(entryHeights.data(), entryCount, contentTop, contentBottom);

  // ---- drawing lambdas (mock-adapted; geometry mirrors HttpMonitorActivity) ----

  auto drawBarShape = [&](int barX, int y, int barSpan, int rowLineH, const HttpMonitorSchema::BarSpec& bar) {
    int barH = rowLineH - 12;
    if (barH < 2) barH = 2;
    const int barY = y + (rowLineH - barH) / 2;
    if (bar.segments > 1 && barSpan > 8) {
      const int totalGap = (bar.segments - 1) * 2;
      const int cellW = (barSpan - totalGap) / bar.segments;
      if (cellW >= 2) {
        const int filledCells = (bar.value * bar.segments) / 100;
        int cx = barX;
        for (int s = 0; s < bar.segments; ++s) {
          r.drawRect(cx, barY, cellW, barH);
          if (s < filledCells) r.fillRect(cx, barY, cellW, barH, true);
          cx += cellW + 2;
        }
        return;
      }
    }
    // Single block: outline + flush fill (mirrors HttpMonitorActivity.cpp —
    // value==100 fills the outline edge-to-edge, no (barSpan-4)/+2 inset).
    r.drawRect(barX, barY, barSpan, barH);
    const int fillWidth = barSpan * bar.value / 100;
    if (fillWidth > 0) r.fillRect(barX, barY, fillWidth, barH, true);
  };

  auto drawAlertMarker = [&](int x, int y, int rowLineH) {
    const int mh = 6;
    const int my = y + (rowLineH - mh) / 2;
    r.fillRect(x, my, mh, mh, true);
  };

  // 16x16 glyph cell at (gx, gy). Mirrors HttpMonitorActivity::drawGlyph(): '#'
  // filled box, 'o' outline, '.' disk, '+' / 'x' crossed lines, '!' filled
  // up-triangle, '^' / 'v' outline triangles, ' ' blank, anything else drawn as
  // a centered SMALL glyph.
  auto drawGlyph = [&](int gx, int gy, char ch) {
    const int cx = gx, cy = gy;
    switch (ch) {
      case '#':
        r.fillRect(cx, cy, 16, 16, true);
        break;
      case 'o':
        r.drawRect(cx, cy, 16, 16);
        break;
      case '.':
        drawPipOn(r, cx + 8, cy + 8, 8);
        break;
      case '+':
        r.drawLine(cx + 8, cy + 2, cx + 8, cy + 13);
        r.drawLine(cx + 2, cy + 8, cx + 13, cy + 8);
        break;
      case 'x':
        r.drawLine(cx + 3, cy + 3, cx + 12, cy + 12);
        r.drawLine(cx + 3, cy + 12, cx + 12, cy + 3);
        break;
      case '!':
        fillUpTriangle(r, cx, cy);
        break;
      case '^':
        r.drawLine(cx + 8, cy, cx, cy + 15);
        r.drawLine(cx + 8, cy, cx + 15, cy + 15);
        r.drawLine(cx, cy + 15, cx + 15, cy + 15);
        break;
      case 'v':
        r.drawLine(cx, cy, cx + 8, cy + 15);
        r.drawLine(cx + 15, cy, cx + 8, cy + 15);
        r.drawLine(cx, cy, cx + 15, cy);
        break;
      case ' ':
        break;
      default: {
        char buf[2] = {ch, '\0'};
        const int w = r.getTextWidth(SMALL_FONT_ID, buf);
        r.drawText(SMALL_FONT_ID, cx + (16 - w) / 2, cy + 2, buf, true);
        break;
      }
    }
  };

  // kv (and bar) rows. Mirrors HttpMonitorActivity::drawKv(): label/value column
  // budgets, fractional-bar placement with the MIN_BAR_SPAN floor (bar-less kv
  // below it), alert marker, and align-based block placement.
  auto drawKv = [&](const HttpMonitorPreviewRow& row, int y, int rowLineH) {
    const HttpMonitorSchema::BarSpec bar = effectiveBar(row);
    const bool hasBar = bar.value >= 0;
    const int rowFont = previewFontForSize(row.sizeIdx);
    const int markerPad = row.alert ? 10 : 0;
    const int style = (row.bold || row.alert) ? 1 : 0;
    const auto cols = HttpMonitorLayout::computeRowColumns(contentWidth, hasBar);

    const std::string labelStr =
        r.truncatedText(rowFont, row.label ? row.label : "", cols.labelColMax - markerPad, style);
    const int labelWidth = r.getTextWidth(rowFont, labelStr.c_str(), style);

    int valueWidth = r.getTextWidth(rowFont, row.value ? row.value : "");
    const std::string valueStr = (valueWidth > cols.valueColMax)
                                     ? r.truncatedText(rowFont, row.value ? row.value : "", cols.valueColMax)
                                     : (row.value ? row.value : "");
    valueWidth = r.getTextWidth(rowFont, valueStr.c_str());

    int barX = 0, barSpan = 0;
    if (hasBar) {
      const auto barResult =
          HttpMonitorLayout::placeFractionalBar(sidePadding, pageWidth, markerPad + labelWidth, valueWidth, BAR_GAP,
                                                bar.widthPct, static_cast<HttpMonitorLayout::BarAlign>(bar.align));
      barX = barResult.barX;
      barSpan = barResult.barSpan;
    }

    if (hasBar && barSpan >= HttpMonitorLayout::MIN_BAR_SPAN) {
      // In-row bar between label and value; label left, value right-aligned.
      if (row.alert) drawAlertMarker(sidePadding, y, rowLineH);
      r.drawText(rowFont, sidePadding + markerPad, y, labelStr.c_str(), true, style);
      r.drawText(rowFont, pageWidth - sidePadding - valueWidth, y, valueStr.c_str());
      drawBarShape(barX, y, barSpan, rowLineH, bar);
      return;
    }

    // Bar-less (or bar too stubby): label + value block, aligned per row.align.
    const int blockGap = 16;
    const int blockWidth = markerPad + labelWidth + blockGap + valueWidth;
    int labelX;
    switch (row.align) {
      case 2:  // RIGHT
        labelX = pageWidth - sidePadding - valueWidth - blockGap - labelWidth;
        break;
      case 1:  // CENTER
        labelX = sidePadding + (contentWidth - blockWidth) / 2 + markerPad;
        break;
      default:  // LEFT
        labelX = sidePadding + markerPad;
        break;
    }
    const int valueX = labelX + labelWidth + blockGap;
    if (row.alert) drawAlertMarker(labelX - markerPad, y, rowLineH);
    r.drawText(rowFont, labelX, y, labelStr.c_str(), true, style);
    r.drawText(rowFont, valueX, y, valueStr.c_str());
  };

  auto drawTextRow = [&](const HttpMonitorPreviewRow& row, int y, int rowLineH) {
    const int rowFont = previewFontForSize(row.sizeIdx);
    // Alert text rows are bolded and get the 6x6 marker + indent (mirrors the
    // firmware drawTextRow); the wrap width shrinks by markerPad so the entry
    // height (computed above) matches what is drawn.
    const int markerPad = row.alert ? 10 : 0;
    const int style = (row.bold || row.alert) ? 1 : 0;
    const auto lines = r.wrappedText(rowFont, row.text ? row.text : "", contentWidth - markerPad, 2, style);
    int ty = y;
    for (const auto& line : lines) {
      const int w = r.getTextWidth(rowFont, line.c_str());
      if (row.align == 2) {  // RIGHT
        r.drawText(rowFont, pageWidth - sidePadding - w, ty, line.c_str(), true, style);
      } else if (row.align == 1) {  // CENTER (within the marker-indented band)
        r.drawText(rowFont, sidePadding + markerPad + (contentWidth - markerPad - w) / 2, ty, line.c_str(), true,
                   style);
      } else {  // LEFT
        r.drawText(rowFont, sidePadding + markerPad, ty, line.c_str(), true, style);
      }
      ty += rowLineH;
    }
    if (row.alert) drawAlertMarker(sidePadding, y, rowLineH);
  };

  auto drawDivider = [&](const HttpMonitorPreviewRow& row, int y) {
    const int inset = std::min(row.dividerInset, contentWidth / 2);
    const int left = sidePadding + inset;
    const int right = pageWidth - sidePadding - inset;
    const int ruleY = y + 6;
    if (row.label && row.label[0] != '\0') {
      const int labelW = r.getTextWidth(SMALL_FONT_ID, row.label);
      const int cx = (left + right) / 2;
      constexpr int LABEL_GAP = 8;
      r.drawText(SMALL_FONT_ID, cx - labelW / 2, y, row.label, true);
      const int leftEnd = cx - labelW / 2 - LABEL_GAP;
      const int rightStart = cx + labelW / 2 + LABEL_GAP;
      if (leftEnd > left) r.drawLine(left, ruleY, leftEnd, ruleY);
      if (right > rightStart) r.drawLine(rightStart, ruleY, right, ruleY);
    } else {
      if (right > left) r.drawLine(left, ruleY, right, ruleY);
    }
  };

  auto drawGlyphs = [&](const HttpMonitorPreviewRow& row, int y) {
    const int rawCount = row.glyphs ? static_cast<int>(strlen(row.glyphs)) : 0;
    if (rawCount <= 0) return;
    // Clamp the run to the band (mirrors the firmware drawGlyphs): a 32-char
    // glyphs string (636px) would overflow the ~440px band. Account for the
    // label so the whole block stays in-band.
    const int labelW = (row.label && row.label[0] != '\0') ? r.getTextWidth(SMALL_FONT_ID, row.label) : 0;
    const int glyphsAvailable = contentWidth - ((row.label && row.label[0] != '\0') ? (labelW + 12) : 0);
    const int glyphCount = std::min(rawCount, HttpMonitorLayout::maxGlyphsFor(glyphsAvailable));
    const int glyphsW = glyphCount * 20 - 4;
    int gx;
    if (row.align == 2) {  // RIGHT
      gx = pageWidth - sidePadding - glyphsW;
    } else if (row.align == 1) {  // CENTER
      gx = sidePadding + (contentWidth - glyphsW) / 2;
    } else {  // LEFT
      gx = sidePadding;
    }
    if (row.label && row.label[0] != '\0') {
      r.drawText(SMALL_FONT_ID, gx, y, row.label, true);
      gx += labelW + 12;
    }
    const int gy = y + (20 - 16) / 2;
    for (int i = 0; i < glyphCount; ++i) {
      drawGlyph(gx, gy, row.glyphs[i]);
      gx += 20;
    }
  };

  auto dispatchRow = [&](const HttpMonitorPreviewRow& row, int y, int rowLineH) {
    switch (static_cast<HttpMonitorSchema::RowType>(row.type)) {
      case HttpMonitorSchema::RowType::SPACER:
        break;
      case HttpMonitorSchema::RowType::DIVIDER:
        drawDivider(row, y);
        break;
      case HttpMonitorSchema::RowType::GLYPHS:
        drawGlyphs(row, y);
        break;
      case HttpMonitorSchema::RowType::TEXT:
        drawTextRow(row, y, rowLineH);
        break;
      default:
        drawKv(row, y, rowLineH);
        break;  // KV (and BAR)
    }
  };

  // ---- draw loop: only entries that fully fit the band; the rest are clipped. ----
  for (int i = 0; i < visibleEntries; ++i) {
    const int y = HttpMonitorLayout::entryY(entryHeights.data(), i, contentTop);
    const PreviewEntry& e = entries[i];
    if (e.kind == EntryKind::HEADING) {
      r.drawText(rowFontGlobal, sidePadding, y, e.text, true, 1 /* BOLD */);
    } else if (e.kind == EntryKind::ALERT) {
      drawAlertMarker(sidePadding, y, rowLineHGlobal);
      r.drawText(rowFontGlobal, sidePadding + 10, y, e.text, true, 1 /* BOLD */);
    } else {
      dispatchRow(*e.row, y, rowLineHGlobal);
    }
  }

  if (sections.empty() && alerts.empty()) {
    r.drawCenteredText(UI_10_FONT_ID, (contentTop + contentBottom) / 2, "No data");
  }

  drawButtonHintsOn(r, "Back", "Refresh", "", "");
  return entryCount - visibleEntries;
}

static void renderHttpMonitorDashboard(GfxRenderer& r, PreviewWifiState wifiState = PreviewWifiState::AlwaysOn) {
  const std::vector<HttpMonitorPreviewSection> sections = {
      {"System", {{"CPU", "23%", 23, false}, {"Memory", "6.0/16 GB", 37, false}}},
      {"Disks", {{"/data", "88%", 88, true}, {"/", "41%", 41, false}}},
  };
  renderHttpMonitorDashboardGeneric(r, sections, "prod-1", "12:01:02", {}, /*intervalSec=*/30, wifiState);
}

static void renderHttpMonitorError(GfxRenderer& r) {
  const int pageHeight = r.getScreenHeight();
  const int pageWidth = r.getScreenWidth();
  const int headerH = 45;
  const int verticalSpacing = 10;
  const int sidePadding = 20;
  const int lineH = r.getLineHeight(UI_10_FONT_ID) + 6;
  const int smallLineH = r.getLineHeight(SMALL_FONT_ID) + 2;
  const int maxTextWidth = pageWidth - 2 * sidePadding;

  r.clearScreen();
  r.drawCenteredText(UI_12_FONT_ID, 12, "prod-1", true, 1);
  r.drawLine(15, 42, pageWidth - 15, 42);

  int y = headerH + verticalSpacing * 3;
  r.drawCenteredText(UI_10_FONT_ID, y, "HTTP 500", true, 1);
  y += lineH + 6;

  // Goes through the SAME wrappedText(..., maxLines) call the real renderError()
  // makes — this is what proves B1 (an omitted/zero maxLines silently drops every
  // line) is actually fixed, rather than just asserting it against a hand-typed
  // single line that never exercised the buggy code path in the first place.
  const auto errLines =
      r.wrappedText(SMALL_FONT_ID, "JSON parse failed: InvalidInput at offset 42 of response body", maxTextWidth, 3);
  TEST_ASSERT_FALSE(errLines.empty());
  for (const auto& line : errLines) {
    r.drawCenteredText(SMALL_FONT_ID, y, line.c_str());
    y += smallLineH;
  }

  drawButtonHintsOn(r, "Back", "Retry", "", "");
}

static void renderHttpMonitorNoConfig(GfxRenderer& r) {
  const int pageWidth = r.getScreenWidth();
  const int headerH = 45;
  const int verticalSpacing = 10;
  const int lineH = r.getLineHeight(UI_10_FONT_ID) + 6;
  const int smallLineH = r.getLineHeight(SMALL_FONT_ID) + 4;

  r.clearScreen();
  r.drawCenteredText(UI_12_FONT_ID, 12, "HTTP Monitor", true, 1);
  r.drawLine(15, 42, pageWidth - 15, 42);

  int y = headerH + verticalSpacing * 3;
  r.drawCenteredText(UI_10_FONT_ID, y, "No config found", true, 1);
  y += lineH + 10;
  r.drawCenteredText(SMALL_FONT_ID, y, "Create this file on the SD card:");
  y += smallLineH;
  r.drawCenteredText(SMALL_FONT_ID, y, "/biscuit/monitor.conf");
  y += smallLineH + 12;
  r.drawCenteredText(SMALL_FONT_ID, y, "Required key:");
  y += smallLineH;
  r.drawCenteredText(SMALL_FONT_ID, y, "url = http://host:port/status.json");

  drawButtonHintsOn(r, "Back", "", "", "");
}

void render_httpmonitor_dashboard_x4() {
  GfxRenderer r(480, 800);
  renderHttpMonitorDashboard(r);
  TEST_ASSERT_TRUE(r.saveBMP("test/preview_httpmonitor_x4.bmp"));
}

void render_httpmonitor_dashboard_x3() {
  GfxRenderer r(528, 792);
  renderHttpMonitorDashboard(r);
  TEST_ASSERT_TRUE(r.saveBMP("test/preview_httpmonitor_x3.bmp"));
}

// The other two WifiIndicator states — render_httpmonitor_dashboard_x4/_x3
// above already cover AlwaysOn (the default).
void render_httpmonitor_wifi_dropped_x4() {
  GfxRenderer r(480, 800);
  renderHttpMonitorDashboard(r, PreviewWifiState::Dropped);
  TEST_ASSERT_TRUE(r.saveBMP("test/preview_httpmonitor_wifi_dropped_x4.bmp"));
}

void render_httpmonitor_wifi_held_x4() {
  GfxRenderer r(480, 800);
  renderHttpMonitorDashboard(r, PreviewWifiState::Held);
  TEST_ASSERT_TRUE(r.saveBMP("test/preview_httpmonitor_wifi_held_x4.bmp"));
}

// Server-driven rotation (rotation: "reverse"): sets the renderer to
// PortraitInverted before drawing the same dashboard as render_httpmonitor_
// dashboard_x4, same as HttpMonitorActivity::render() does. BitmapRenderer's
// setPixel() actually flips coordinates for this orientation (see
// test/mocks/BitmapRenderer.h), so unlike a pixel-blind mock this produces a
// genuinely upside-down BMP -- open it and compare against
// preview_httpmonitor_x4.bmp to confirm the whole dashboard, not just the
// wiring, survives a 180deg flip.
void render_httpmonitor_rotated_x4() {
  GfxRenderer normal(480, 800);
  renderHttpMonitorDashboard(normal);

  GfxRenderer rotated(480, 800);
  rotated.setOrientation(GfxRenderer::PortraitInverted);
  renderHttpMonitorDashboard(rotated);

  // A real, cheap check that the transform actually ran, not just that
  // setOrientation() was called: a 180deg point reflection means pixel i of
  // the flat row-major buffer in one frame must equal pixel (N-1-i) in the
  // other -- reversing `normal`'s buffer must exactly reproduce `rotated`'s.
  // If BitmapRenderer's setPixel() stopped transforming coordinates (e.g. the
  // orientation branch was deleted), this would fail with the two buffers
  // identical instead of mirrored.
  const uint8_t* normalBuf = normal.getFrameBuffer();
  const uint8_t* rotatedBuf = rotated.getFrameBuffer();
  const size_t n = normal.getBufferSize();
  TEST_ASSERT_EQUAL(n, rotated.getBufferSize());
  bool mismatch = false;
  for (size_t i = 0; i < n; i++) {
    if (normalBuf[n - 1 - i] != rotatedBuf[i]) {
      mismatch = true;
      break;
    }
  }
  TEST_ASSERT_FALSE_MESSAGE(mismatch, "rotated buffer is not normal's buffer reversed -- 180deg transform is broken");

  TEST_ASSERT_TRUE(rotated.saveBMP("test/preview_httpmonitor_rotated_x4.bmp"));
}

// Dense-budget preview: exactly the "≤ 12 rows + ≤ 4 headings" no-scroll target
// documented in docs/http-monitor-server-api.md, at the X3 geometry (528x792) —
// the tighter of the two panels (shorter, and Lyra-style headers cut it further).
// Nothing may be clipped at this budget — the dashboard does not scroll, so
// "fits" is the whole guarantee.
static std::vector<HttpMonitorPreviewSection> denseHttpMonitorSections() {
  return {
      {"System", {{"CPU", "23%", 23, false}, {"Memory", "6.0/16 GB", 37, false}, {"Load", "1.24", -1, false}}},
      {"Disks", {{"/data", "88%", 88, true}, {"/", "41%", 41, false}, {"/boot", "12%", 12, false}}},
      {"Network",
       {{"eth0 rx", "1.2 MB/s", -1, false}, {"eth0 tx", "340 KB/s", -1, false}, {"Conns", "128", -1, false}}},
      {"Services", {{"nginx", "up 4d", -1, false}, {"postgres", "up 4d", -1, false}, {"redis", "DOWN", -1, true}}},
  };
}

void render_httpmonitor_dense_x3_top() {
  GfxRenderer r(528, 792);
  // The documented ≤12-row + ≤4-heading budget must fit on the tighter X3 panel.
  // That's the guarantee docs/http-monitor-server-api.md makes to server authors,
  // and with scrolling gone it is the only thing standing between an over-budget
  // response and silently dropped rows. Zero clipped entries is the proof.
  const int clipped = renderHttpMonitorDashboardGeneric(r, denseHttpMonitorSections(), "prod-1");
  TEST_ASSERT_EQUAL(0, clipped);
  TEST_ASSERT_TRUE(r.saveBMP("test/preview_httpmonitor_dense_x3_top.bmp"));
}

// Beyond the recommended (but not hard-capped) budget: 6 sections x 4 rows — still
// inside every hard cap (<=6 sections, <=12 rows/section) but enough content to
// overflow a single X3 screen. With scrolling removed this content is clipped by
// design; the preview exists to show what an over-budget server response looks
// like on the panel, and to prove the clip is clean (no partially drawn row).
static std::vector<HttpMonitorPreviewSection> overflowHttpMonitorSections() {
  return {
      {"System",
       {{"CPU", "23%", 23, false},
        {"Memory", "6.0/16 GB", 37, false},
        {"Load", "1.24", -1, false},
        {"Temp", "52C", -1, false}}},
      {"Disks",
       {{"/data", "88%", 88, true}, {"/", "41%", 41, false}, {"/boot", "12%", 12, false}, {"/var", "63%", 63, false}}},
      {"Network",
       {{"eth0 rx", "1.2 MB/s", -1, false},
        {"eth0 tx", "340 KB/s", -1, false},
        {"Conns", "128", -1, false},
        {"Drops", "0", -1, false}}},
      {"Services",
       {{"nginx", "up 4d", -1, false},
        {"postgres", "up 4d", -1, false},
        {"redis", "DOWN", -1, true},
        {"cron", "up 4d", -1, false}}},
      {"Sensors",
       {{"Fan1", "1200rpm", -1, false},
        {"Fan2", "1180rpm", -1, false},
        {"PSU", "OK", -1, false},
        {"UPS", "98%", 98, false}}},
      {"Backups",
       {{"Last run", "02:14", -1, false},
        {"Size", "4.2 GB", -1, false},
        {"Status", "OK", -1, false},
        {"Next", "tonight", -1, false}}},
  };
}

void render_httpmonitor_overflow_x3_top() {
  GfxRenderer r(528, 792);
  const int clipped = renderHttpMonitorDashboardGeneric(r, overflowHttpMonitorSections(), "prod-1");
  TEST_ASSERT_GREATER_THAN(0, clipped);
  TEST_ASSERT_TRUE(r.saveBMP("test/preview_httpmonitor_overflow_x3_top.bmp"));
}

// Worst case the contract permits: label at the full 24-char cap, value at the
// full 16-char cap, with a bar — on BOTH panels. X4 (480 wide) is the tighter of
// the two, so it's the real test that label/bar/value never collide.
static std::vector<HttpMonitorPreviewSection> worstCaseHttpMonitorSections() {
  return {
      {"System", {{"Really Long Metric Label", "9999.99/10000 GB", 62, false}}},
  };
}

void render_httpmonitor_worstcase_x4() {
  GfxRenderer r(480, 800);
  renderHttpMonitorDashboardGeneric(r, worstCaseHttpMonitorSections(), "prod-1");
  TEST_ASSERT_TRUE(r.saveBMP("test/preview_httpmonitor_worstcase_x4.bmp"));
}

void render_httpmonitor_worstcase_x3() {
  GfxRenderer r(528, 792);
  renderHttpMonitorDashboardGeneric(r, worstCaseHttpMonitorSections(), "prod-1");
  TEST_ASSERT_TRUE(r.saveBMP("test/preview_httpmonitor_worstcase_x3.bmp"));
}

void render_httpmonitor_error_x4() {
  GfxRenderer r(480, 800);
  renderHttpMonitorError(r);
  TEST_ASSERT_TRUE(r.saveBMP("test/preview_httpmonitor_error_x4.bmp"));
}

void render_httpmonitor_error_x3() {
  GfxRenderer r(528, 792);
  renderHttpMonitorError(r);
  TEST_ASSERT_TRUE(r.saveBMP("test/preview_httpmonitor_error_x3.bmp"));
}

void render_httpmonitor_noconfig_x4() {
  GfxRenderer r(480, 800);
  renderHttpMonitorNoConfig(r);
  TEST_ASSERT_TRUE(r.saveBMP("test/preview_httpmonitor_noconfig_x4.bmp"));
}

void render_httpmonitor_noconfig_x3() {
  GfxRenderer r(528, 792);
  renderHttpMonitorNoConfig(r);
  TEST_ASSERT_TRUE(r.saveBMP("test/preview_httpmonitor_noconfig_x3.bmp"));
}

// ---- extended-schema previews ----
// A dashboard that exercises every typed row (text / spacer / divider / glyphs),
// object-form bars with segments + fractional width + align, a per-row SMALL
// `size`, an `updated` timestamp, and an alerts section — at both panel
// geometries. The mixed heights (wrapped text, 8px spacer, 12px dividers, 20px
// glyph bands) are what the per-entry height model exists for: the clip boundary
// has to land on an entry edge regardless of how uneven the heights are.

static std::vector<HttpMonitorPreviewSection> mixedHttpMonitorSections() {
  std::vector<HttpMonitorPreviewSection> s;
  s.push_back({"System",
               {
                   kvRow("CPU", "23%", 23),
                   textRow("All services nominal", 1 /* CENTER */, true),
                   kvRow("Memory", "6.0/16 GB", 37),
                   dividerRow("Storage", 10, 2),
                   kvRow("/data", "88%", 88, true),
                   kvRow("/", "41%", 41),
               }});
  s.push_back({"Tuned",
               {
                   barObjRow("cache", "62%", 62, 8, 100, 0 /* LEFT */),
                   kvRow("temp", "52C", -1),
                   barObjRow("buff", "33%", 33, 5, 50, 2 /* RIGHT */),
                   textRow("Log rotation paused; retry once the disk is under 90%", 0 /* LEFT */, false, 0 /* SMALL */),
                   spacerRow(8),
                   kvRow("swap", "4%", 4),
               }});
  s.push_back({"LEDs",
               {
                   glyphsRow("#o#x+!", "status", 0 /* LEFT */),
                   glyphsRow(".o#", nullptr, 1 /* CENTER */),
                   kvRow("eth0 rx", "1.2 MB/s"),
                   kvRow("eth0 tx", "340 KB/s"),
                   dividerRow(nullptr, 0, 1),
                   textRow("Network nominal", 1 /* CENTER */, true),
               }});
  s.push_back({"Services",
               {
                   kvRow("nginx", "up 4d"),
                   kvRow("postgres", "up 4d"),
                   kvRow("redis", "DOWN", -1, true),
                   kvRow("cron", "up 4d"),
                   textRow("Maintenance window 02:00-03:00 UTC", 1 /* CENTER */),
                   kvRow("backup", "OK"),
               }});
  return s;
}

void render_httpmonitor_mixed_x4_top() {
  GfxRenderer r(480, 800);
  const std::vector<const char*> alerts = {"eth0: link down for 2m", "swap below 5% free"};
  const int clipped = renderHttpMonitorDashboardGeneric(r, mixedHttpMonitorSections(), "prod-1", "12:01:02", alerts);
  TEST_ASSERT_GREATER_THAN(0, clipped);
  TEST_ASSERT_TRUE(r.saveBMP("test/preview_httpmonitor_mixed_x4_top.bmp"));
}

void render_httpmonitor_mixed_x3_top() {
  GfxRenderer r(528, 792);
  const std::vector<const char*> alerts = {"eth0: link down for 2m", "swap below 5% free"};
  const int clipped = renderHttpMonitorDashboardGeneric(r, mixedHttpMonitorSections(), "prod-1", "12:01:02", alerts);
  TEST_ASSERT_GREATER_THAN(0, clipped);
  TEST_ASSERT_TRUE(r.saveBMP("test/preview_httpmonitor_mixed_x3_top.bmp"));
}

// ============================================================
// YOUR NEW ACTIVITY — copy this template:
// ============================================================
//
// void render_my_new_activity() {
//   renderer.clearScreen();
//   drawHeader("My Activity");
//
//   // ... your drawing code here ...
//   // Use the same calls as in your real render() method:
//   //   renderer.drawCenteredText(...)
//   //   renderer.drawText(...)
//   //   renderer.fillRect(...)
//   //   renderer.drawRect(...)
//   //   renderer.drawLine(...)
//   //   drawListItem(y, "text", selected)
//
//   drawButtonHints("Back", "Select", "Up", "Down");
//
//   TEST_ASSERT_TRUE(renderer.saveBMP("test/preview_my_activity.bmp"));
// }

// ============================================================
void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(render_dice_2d6);
  RUN_TEST(render_dice_4d6);
  RUN_TEST(render_dice_1d20);
  RUN_TEST(render_beacon_select);
  RUN_TEST(render_beacon_running);
  RUN_TEST(render_evil_portal);
  RUN_TEST(render_apps_menu);
  // RUN_TEST(render_my_new_activity);  // ← uncomment when ready
  RUN_TEST(render_countdowntimer);
  RUN_TEST(render_diceroller);
  RUN_TEST(render_httpmonitor_dashboard_x4);
  RUN_TEST(render_httpmonitor_dashboard_x3);
  RUN_TEST(render_httpmonitor_wifi_dropped_x4);
  RUN_TEST(render_httpmonitor_wifi_held_x4);
  RUN_TEST(render_httpmonitor_rotated_x4);
  RUN_TEST(render_httpmonitor_dense_x3_top);
  RUN_TEST(render_httpmonitor_overflow_x3_top);
  RUN_TEST(render_httpmonitor_worstcase_x4);
  RUN_TEST(render_httpmonitor_worstcase_x3);
  RUN_TEST(render_httpmonitor_error_x4);
  RUN_TEST(render_httpmonitor_error_x3);
  RUN_TEST(render_httpmonitor_noconfig_x4);
  RUN_TEST(render_httpmonitor_noconfig_x3);
  RUN_TEST(render_httpmonitor_mixed_x4_top);
  RUN_TEST(render_httpmonitor_mixed_x3_top);
  return UNITY_END();
}
