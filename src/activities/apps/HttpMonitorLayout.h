#pragma once

// Pure layout arithmetic shared between HttpMonitorActivity::renderDashboard()
// (the shipping render path) and test_preview.cpp (the preview harness).
//
// Drawing primitives differ between the two (GfxRenderer vs. the BitmapRenderer
// preview mock), and that's fine — but the *math* that decides where things go
// must not be duplicated. A preview built from a hand-copy of this arithmetic
// only proves the copy is self-consistent, not that the shipping code is
// correct. No Arduino/HAL dependency — safe to include natively.

namespace HttpMonitorLayout {

// Column budgets for a single row. Text has priority over the bar: the label
// gets a modest share, the value gets a generous share (it's already bounded to
// 16 chars by the config parser, so a generous cap is never actually reached),
// and the bar — decoration, not data — is capped so it can never balloon to
// absorb slack that belongs to the value.
struct RowColumns {
  int labelColMax;
  int valueColMax;
  int barColMax;
};

inline RowColumns computeRowColumns(int contentWidth, bool hasBar) {
  return RowColumns{
      hasBar ? (contentWidth / 3) : (contentWidth / 2),
      contentWidth / 3,
      contentWidth / 3,
  };
}

// Where the bar goes: whatever's left between the (already-measured, already-
// truncated) label and value, minus an explicit gap on each side, capped to
// barColMax. Never negative — callers should skip drawing when barSpan <= 0,
// though with the column budgets above this can't happen in practice (label
// and value can each claim at most contentWidth/3, leaving at least
// contentWidth/3 - 2*gap for the bar).
struct BarSpan {
  int barX;
  int barSpan;
};

inline BarSpan computeBarSpan(int contentSidePadding, int pageWidth, int labelWidth, int valueWidth, int barColMax,
                              int gap) {
  const int barX = contentSidePadding + labelWidth + gap;
  const int barRight = pageWidth - contentSidePadding - valueWidth - gap;
  int barSpan = barRight - barX;
  if (barSpan > barColMax) barSpan = barColMax;
  if (barSpan < 0) barSpan = 0;
  return BarSpan{barX, barSpan};
}

// Total scrollable entries: one per non-empty heading, one per row, plus an
// "Alerts" heading and one line per alert if any were sent.
inline int computeTotalLines(int headingCount, int rowCount, int alertCount) {
  int total = headingCount + rowCount;
  if (alertCount > 0) total += 1 + alertCount;
  return total;
}

struct ScrollMetrics {
  int visibleLines;
  int maxScroll;
};

inline ScrollMetrics computeScrollMetrics(int totalLines, int contentTop, int contentBottom, int lineH) {
  const int visibleLines = (lineH > 0) ? (contentBottom - contentTop) / lineH : 1;
  int maxScroll = totalLines - visibleLines;
  if (maxScroll < 0) maxScroll = 0;
  return ScrollMetrics{visibleLines, maxScroll};
}

inline int clampScrollOffset(int scrollOffset, int maxScroll) {
  if (scrollOffset > maxScroll) return maxScroll;
  if (scrollOffset < 0) return 0;
  return scrollOffset;
}

// Total page count for the "current/total" scroll indicator — NOT maxScroll /
// pageSize + 1, which reads "1/1" on a genuinely scrollable list whenever the
// last page is a partial page smaller than pageSize.
inline int computeTotalPages(int totalLines, int pageSize) {
  if (pageSize <= 0) return 1;
  return (totalLines + pageSize - 1) / pageSize;
}

}  // namespace HttpMonitorLayout
