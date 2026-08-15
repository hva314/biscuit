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
#include <vector>
#include "BitmapRenderer.h"

// Pure layout arithmetic shared with the real HttpMonitorActivity::renderDashboard()
// — the whole point is that this preview exercises the SAME column/bar/scroll math
// the device runs, not a hand-copy of it (drawing primitives still differ; the
// mock draws with drawRect/fillRect the same way the real renderer now does).
#include "../../src/activities/apps/HttpMonitorLayout.h"

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
    while ((dx+1)*(dx+1) + dy*dy <= r*r) dx++;
    renderer.fillRect(cx - dx, cy + dy, dx * 2 + 1, 1, true);
  }
}

static void drawDie(int x, int y, int size, int value) {
  renderer.fillRect(x + 4, y + 4, size, size, true);    // shadow
  renderer.fillRect(x, y, size, size, false);             // white face
  renderer.drawRect(x, y, size, size);                    // outer border
  renderer.drawRect(x + 2, y + 2, size - 4, size - 4);   // inner border

  int pip = size / 8;
  int m = size / 4;
  int l = x + m, r = x + size - m;
  int t = y + m, b = y + size - m;
  int mx = x + size/2, my = y + size/2;

  switch (value) {
    case 1: drawPip(mx,my,pip); break;
    case 2: drawPip(r,t,pip); drawPip(l,b,pip); break;
    case 3: drawPip(r,t,pip); drawPip(mx,my,pip); drawPip(l,b,pip); break;
    case 4: drawPip(l,t,pip); drawPip(r,t,pip); drawPip(l,b,pip); drawPip(r,b,pip); break;
    case 5: drawPip(l,t,pip); drawPip(r,t,pip); drawPip(mx,my,pip); drawPip(l,b,pip); drawPip(r,b,pip); break;
    case 6: drawPip(l,t,pip); drawPip(r,t,pip); drawPip(l,my,pip); drawPip(r,my,pip); drawPip(l,b,pip); drawPip(r,b,pip); break;
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
  renderer.fillRect(x+4, y+4, size, size, true);
  renderer.fillRect(x, y, size, size, false);
  renderer.drawRect(x, y, size, size);
  renderer.drawRect(x+2, y+2, size-4, size-4);
  renderer.drawCenteredText(UI_12_FONT_ID, y + size/2 - 12, "17", true, 1);
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
  renderer.drawText(SMALL_FONT_ID, 15, y, "ACTIVE", true, 1); y += 45;
  renderer.drawText(SMALL_FONT_ID, 15, y, "SSID:", true, 1);
  renderer.drawText(UI_10_FONT_ID, 80, y, "Free WiFi"); y += 45;
  renderer.drawText(UI_10_FONT_ID, 15, y, "Clients: 3"); y += 45;
  renderer.drawText(UI_10_FONT_ID, 15, y, "Captured: 2", true, 1); y += 45;
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

struct HttpMonitorPreviewRow { const char* label; const char* value; int bar; bool alert; };
struct HttpMonitorPreviewSection { const char* heading; std::vector<HttpMonitorPreviewRow> rows; };

// Exercises the SAME pure layout math as HttpMonitorActivity::renderDashboard()
// via HttpMonitorLayout.h — column allocation, bar span, and totalLines/
// visibleLines/maxScroll are computed by the shared functions, not a hand-copy.
// Only the drawing primitives are mock-specific (drawRect/fillRect either way,
// matching what the real renderer now does since GUI.drawProgressBar was
// dropped). Returns the clamped maxScroll so callers can drive a "scrolled to
// bottom" preview and prove scrolling actually works.
static int renderHttpMonitorDashboardGeneric(GfxRenderer& r, const std::vector<HttpMonitorPreviewSection>& sections,
                                              const char* title, int scrollOffset) {
  const int pageWidth = r.getScreenWidth();
  const int pageHeight = r.getScreenHeight();
  const int headerH = 45;
  const int buttonHintsH = 40;
  const int verticalSpacing = 10;
  const int sidePadding = 20;
  const int lineH = r.getLineHeight(UI_12_FONT_ID) + 6;
  const int contentBottom = pageHeight - buttonHintsH - verticalSpacing;
  const int contentWidth = pageWidth - 2 * sidePadding;
  const int unreservedContentTop = headerH + verticalSpacing;
  const int BAR_GAP = 10;

  r.clearScreen();
  r.drawCenteredText(UI_12_FONT_ID, 12, title, true, 1);
  r.drawLine(15, 42, pageWidth - 15, 42);

  int headingCount = 0;
  int rowCount = 0;
  for (auto& section : sections) {
    if (section.heading[0] != '\0') headingCount++;
    rowCount += static_cast<int>(section.rows.size());
  }
  const int totalLines = HttpMonitorLayout::computeTotalLines(headingCount, rowCount, 0);

  const auto unreservedScroll =
      HttpMonitorLayout::computeScrollMetrics(totalLines, unreservedContentTop, contentBottom, lineH);
  const bool needsScrollIndicator = unreservedScroll.maxScroll > 0;
  const int smallLineH = r.getLineHeight(SMALL_FONT_ID) + 4;
  const int contentTop = needsScrollIndicator ? (unreservedContentTop + smallLineH) : unreservedContentTop;

  const auto scrollMetrics = HttpMonitorLayout::computeScrollMetrics(totalLines, contentTop, contentBottom, lineH);
  const int visibleLines = scrollMetrics.visibleLines;
  const int maxScroll = scrollMetrics.maxScroll;
  scrollOffset = HttpMonitorLayout::clampScrollOffset(scrollOffset, maxScroll);

  int lineIdx = 0;
  int y = contentTop;

  auto drawLine = [&](bool isHeading, const char* label, const char* value, int bar, bool alert) {
    std::string labelStr, valueStr;
    int labelWidth = 0, valueWidth = 0, barX = 0, barSpan = 0;

    if (!isHeading) {
      const auto cols = HttpMonitorLayout::computeRowColumns(contentWidth, bar >= 0);

      labelStr = r.truncatedText(UI_12_FONT_ID, label, cols.labelColMax);
      labelWidth = r.getTextWidth(UI_12_FONT_ID, labelStr.c_str());

      valueWidth = r.getTextWidth(UI_12_FONT_ID, value);
      valueStr = (valueWidth > cols.valueColMax) ? r.truncatedText(UI_12_FONT_ID, value, cols.valueColMax) : value;
      valueWidth = r.getTextWidth(UI_12_FONT_ID, valueStr.c_str());

      if (bar >= 0) {
        const auto barSpanResult =
            HttpMonitorLayout::computeBarSpan(sidePadding, pageWidth, labelWidth, valueWidth, cols.barColMax, BAR_GAP);
        barX = barSpanResult.barX;
        barSpan = barSpanResult.barSpan;
      }
    }

    const bool visible = (lineIdx >= scrollOffset) && (y + lineH <= contentBottom);
    if (visible) {
      if (isHeading) {
        r.drawText(UI_12_FONT_ID, sidePadding, y, label, true, 1 /* BOLD */);
      } else {
        r.drawText(UI_12_FONT_ID, sidePadding, y, labelStr.c_str(), true, alert ? 1 : 0);
        r.drawText(UI_12_FONT_ID, pageWidth - sidePadding - valueWidth, y, valueStr.c_str());

        // No percentage label, no logging, clamped fill — mirrors dropping
        // GUI.drawProgressBar() in favor of drawing the bar directly.
        if (bar >= 0 && barSpan > 0) {
          const int barH = lineH - 12;
          const int barY = y + (lineH - barH) / 2;
          r.drawRect(barX, barY, barSpan, barH);
          const int fillWidth = (barSpan > 4) ? (barSpan - 4) * bar / 100 : 0;
          if (fillWidth > 0) r.fillRect(barX + 2, barY + 2, fillWidth, barH - 4, true);
        }
      }
      y += lineH;
    }
    lineIdx++;
  };

  for (auto& section : sections) {
    if (section.heading[0] != '\0') drawLine(true, section.heading, nullptr, -1, false);
    for (auto& row : section.rows) drawLine(false, row.label, row.value, row.bar, row.alert);
  }

  if (needsScrollIndicator) {
    char scrollBuf[16];
    const int pageSize = (visibleLines > 0) ? visibleLines : 1;
    const int totalPages = HttpMonitorLayout::computeTotalPages(totalLines, pageSize);
    snprintf(scrollBuf, sizeof(scrollBuf), "%d/%d", scrollOffset / pageSize + 1, totalPages);
    const int w = r.getTextWidth(SMALL_FONT_ID, scrollBuf);
    r.drawText(SMALL_FONT_ID, pageWidth - sidePadding - w, unreservedContentTop, scrollBuf);
  }

  drawButtonHintsOn(r, "Back", "Refresh", "Up", "Down");
  return maxScroll;
}

static void renderHttpMonitorDashboard(GfxRenderer& r) {
  const std::vector<HttpMonitorPreviewSection> sections = {
    {"System", {{"CPU", "23%", 23, false}, {"Memory", "6.0/16 GB", 37, false}}},
    {"Disks", {{"/data", "88%", 88, true}, {"/", "41%", 41, false}}},
  };
  renderHttpMonitorDashboardGeneric(r, sections, "prod-1", 0);
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

// Dense-budget preview: exactly the "≤ 12 rows + ≤ 4 headings" no-scroll target
// documented in docs/http-monitor-server-api.md, at the X3 geometry (528x792) —
// the tighter of the two panels (shorter, and Lyra-style headers cut it further).
// Rendered twice: at rest (top) and scrolled to the computed maxScroll (bottom),
// to prove the entry-based scroll bookkeeping neither clips nor gets stuck.
static std::vector<HttpMonitorPreviewSection> denseHttpMonitorSections() {
  return {
    {"System", {{"CPU", "23%", 23, false}, {"Memory", "6.0/16 GB", 37, false}, {"Load", "1.24", -1, false}}},
    {"Disks", {{"/data", "88%", 88, true}, {"/", "41%", 41, false}, {"/boot", "12%", 12, false}}},
    {"Network", {{"eth0 rx", "1.2 MB/s", -1, false}, {"eth0 tx", "340 KB/s", -1, false}, {"Conns", "128", -1, false}}},
    {"Services", {{"nginx", "up 4d", -1, false}, {"postgres", "up 4d", -1, false}, {"redis", "DOWN", -1, true}}},
  };
}

void render_httpmonitor_dense_x3_top() {
  GfxRenderer r(528, 792);
  // The documented ≤12-row + ≤4-heading budget must fit on the tighter X3 panel
  // with NO scrolling required — that's the guarantee docs/http-monitor-server-api.md
  // makes to server authors. maxScroll == 0 is the proof; a positive value here
  // would mean the doc's promise is false.
  const int maxScroll = renderHttpMonitorDashboardGeneric(r, denseHttpMonitorSections(), "prod-1", 0);
  TEST_ASSERT_EQUAL(0, maxScroll);
  TEST_ASSERT_TRUE(r.saveBMP("test/preview_httpmonitor_dense_x3_top.bmp"));
}

// Beyond the recommended (but not hard-capped) budget: 6 sections x 4 rows — still
// inside every hard cap (<=6 sections, <=12 rows/section) but enough content to
// overflow a single X3 screen, so scrolling has to engage. Rendered twice — at rest
// and scrolled to the computed maxScroll — to prove the entry-based scroll
// bookkeeping neither clips nor gets stuck before reaching the final rows.
static std::vector<HttpMonitorPreviewSection> overflowHttpMonitorSections() {
  return {
    {"System", {{"CPU", "23%", 23, false}, {"Memory", "6.0/16 GB", 37, false}, {"Load", "1.24", -1, false}, {"Temp", "52C", -1, false}}},
    {"Disks", {{"/data", "88%", 88, true}, {"/", "41%", 41, false}, {"/boot", "12%", 12, false}, {"/var", "63%", 63, false}}},
    {"Network", {{"eth0 rx", "1.2 MB/s", -1, false}, {"eth0 tx", "340 KB/s", -1, false}, {"Conns", "128", -1, false}, {"Drops", "0", -1, false}}},
    {"Services", {{"nginx", "up 4d", -1, false}, {"postgres", "up 4d", -1, false}, {"redis", "DOWN", -1, true}, {"cron", "up 4d", -1, false}}},
    {"Sensors", {{"Fan1", "1200rpm", -1, false}, {"Fan2", "1180rpm", -1, false}, {"PSU", "OK", -1, false}, {"UPS", "98%", 98, false}}},
    {"Backups", {{"Last run", "02:14", -1, false}, {"Size", "4.2 GB", -1, false}, {"Status", "OK", -1, false}, {"Next", "tonight", -1, false}}},
  };
}

void render_httpmonitor_overflow_x3_top() {
  GfxRenderer r(528, 792);
  const int maxScroll = renderHttpMonitorDashboardGeneric(r, overflowHttpMonitorSections(), "prod-1", 0);
  TEST_ASSERT_GREATER_THAN(0, maxScroll);
  TEST_ASSERT_TRUE(r.saveBMP("test/preview_httpmonitor_overflow_x3_top.bmp"));
}

void render_httpmonitor_overflow_x3_scrolled() {
  GfxRenderer r(528, 792);
  const int maxScroll = renderHttpMonitorDashboardGeneric(r, overflowHttpMonitorSections(), "prod-1", 0);
  TEST_ASSERT_GREATER_THAN(0, maxScroll);
  renderHttpMonitorDashboardGeneric(r, overflowHttpMonitorSections(), "prod-1", maxScroll);
  TEST_ASSERT_TRUE(r.saveBMP("test/preview_httpmonitor_overflow_x3_scrolled.bmp"));
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
  renderHttpMonitorDashboardGeneric(r, worstCaseHttpMonitorSections(), "prod-1", 0);
  TEST_ASSERT_TRUE(r.saveBMP("test/preview_httpmonitor_worstcase_x4.bmp"));
}

void render_httpmonitor_worstcase_x3() {
  GfxRenderer r(528, 792);
  renderHttpMonitorDashboardGeneric(r, worstCaseHttpMonitorSections(), "prod-1", 0);
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
  RUN_TEST(render_httpmonitor_dense_x3_top);
  RUN_TEST(render_httpmonitor_overflow_x3_top);
  RUN_TEST(render_httpmonitor_overflow_x3_scrolled);
  RUN_TEST(render_httpmonitor_worstcase_x4);
  RUN_TEST(render_httpmonitor_worstcase_x3);
  RUN_TEST(render_httpmonitor_error_x4);
  RUN_TEST(render_httpmonitor_error_x3);
  RUN_TEST(render_httpmonitor_noconfig_x4);
  RUN_TEST(render_httpmonitor_noconfig_x3);
  return UNITY_END();
}
