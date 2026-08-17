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

// Column budgets for a single row. Text has priority: label and value each get
// half the content width. The bar — decoration, not data — gets no column
// budget of its own; it uses whatever is left between the label and value
// (computeBarSpan). barColMax is retained in the struct for call-site
// stability but is never a real constraint anymore.
struct RowColumns {
  int labelColMax;
  int valueColMax;
  int barColMax;
};

inline RowColumns computeRowColumns(int contentWidth, bool hasBar) {
  (void)hasBar;  // hasBar is retained for call-site stability; it no longer
                 // changes the split — the bar is in-row, between label/value.
  return RowColumns{
      contentWidth / 2,
      contentWidth / 2,
      contentWidth,  // no longer capped at contentWidth/3
  };
}

// Where the bar goes: whatever's left between the (already-measured, already-
// truncated) label and value, minus an explicit gap on each side. The old
// contentWidth/3 cap is gone — the bar now spans label->value (the core
// whitespace fix). barColMax is retained in the signature for call-site
// stability but ignored. Never negative — callers skip drawing when
// barSpan < MIN_BAR_SPAN (rendering as a bar-less kv row instead).
struct BarSpan {
  int barX;
  int barSpan;
};

// Below this the bar is too stubby to draw; the row renders as a bar-less kv.
inline constexpr int MIN_BAR_SPAN = 24;

inline BarSpan computeBarSpan(int contentSidePadding, int pageWidth, int labelWidth, int valueWidth, int barColMax,
                              int gap) {
  (void)barColMax;  // retained for call-site stability; the cap is removed
  const int barX = contentSidePadding + labelWidth + gap;
  const int barRight = pageWidth - contentSidePadding - valueWidth - gap;
  int barSpan = barRight - barX;
  if (barSpan < 0) barSpan = 0;
  return BarSpan{barX, barSpan};
}

// Horizontal alignment for a fractional bar (bar.width < 1.0) and for bar-less
// kv / text / glyphs rows.
enum class BarAlign : uint8_t { LEFT = 0, CENTER = 1, RIGHT = 2 };

// A bar whose width is a fraction of the full gap (bar.width in the schema,
// 0.1..1.0). widthPct is 0..100. The full bar's left edge is where a full
// bar would start; the fractional bar is placed within that span per align.
inline BarSpan placeFractionalBar(int contentSidePadding, int pageWidth, int labelWidth, int valueWidth, int gap,
                                  int widthPct, BarAlign align) {
  const int fullX = contentSidePadding + labelWidth + gap;
  const int fullRight = pageWidth - contentSidePadding - valueWidth - gap;
  int fullSpan = fullRight - fullX;
  if (fullSpan < 0) fullSpan = 0;
  if (widthPct < 0) widthPct = 0;
  if (widthPct > 100) widthPct = 100;
  const int span = fullSpan * widthPct / 100;
  int offset = 0;
  if (align == BarAlign::CENTER) {
    offset = (fullSpan - span) / 2;
  } else if (align == BarAlign::RIGHT) {
    offset = fullSpan - span;
  }
  return BarSpan{fullX + offset, span};
}

// Total scrollable entries: one per non-empty heading, one per row, plus an
// "Alerts" heading and one line per alert if any were sent.
inline int computeTotalLines(int headingCount, int rowCount, int alertCount) {
  int total = headingCount + rowCount;
  if (alertCount > 0) total += 1 + alertCount;
  return total;
}

// ---- per-entry heights ----
// Rows no longer share one uniform pitch (size ladder, wrapped text, spacers,
// dividers, glyph bands all break it), so every entry contributes its own
// height. The scroll model is a prefix sum over entryHeights[].

inline int kvEntryHeight(int lineH) { return lineH; }

inline int textEntryHeight(int lineH, int lineCount) {
  if (lineCount < 1) lineCount = 1;
  return lineH * lineCount;
}

inline int spacerEntryHeight(int px) { return px; }

inline int dividerEntryHeight() { return 12; }

inline int glyphsEntryHeight() { return 20; }

// Largest glyph-cell count that fits a horizontal band: each cell is 16px + 4px
// gap and the last cell has no trailing gap, so n cells span 20*n - 4, and the
// largest n with 20*n - 4 <= width is floor((width + 4) / 20). Glyphs rows clamp
// their rendered run to this so a max-length string (32 cells = 636px) never
// overflows a ~440px band. Degenerate/negative widths clamp to 0.
inline int maxGlyphsFor(int contentWidth) {
  const int n = (contentWidth + 4) / 20;
  return n < 0 ? 0 : n;
}

// Per-entry scroll metrics over the entryHeights[] prefix sum.
struct ScrollMetrics {
  int visibleEntries;  // leading entries that fully fit the band from contentTop
  int maxScroll;       // farthest offset whose trailing suffix fits the band
};

inline ScrollMetrics computeScrollMetrics(const int* entryHeights, int entryCount, int contentTop, int contentBottom) {
  if (entryCount <= 0 || entryHeights == nullptr) return ScrollMetrics{0, 0};
  int y = contentTop;
  int visible = 0;
  while (visible < entryCount) {
    if (y + entryHeights[visible] > contentBottom) break;  // must fully fit
    y += entryHeights[visible];
    ++visible;
  }
  // Farthest offset that still shows the final entry on screen. Reverse walk:
  // accumulate the trailing suffix heights from the last entry; the smallest
  // offset whose suffix fits the band is the last useful scroll position.
  // entryCount - visible undercounts this with variable heights (a tall
  // trailing row would be permanently unreachable). Falls back to the last
  // index when even the final entry alone is taller than the band.
  const int band = contentBottom - contentTop;
  int acc = 0;
  int maxScroll = entryCount - 1;
  for (int i = entryCount - 1; i >= 0; --i) {
    acc += entryHeights[i];
    if (acc > band) break;
    maxScroll = i;
  }
  return ScrollMetrics{visible, maxScroll};
}

// Prefix-sum y cursor: entry i sits at contentTop + sum of entryHeights[]
// from scrollOffset (inclusive) up to i (exclusive).
inline int entryY(const int* entryHeights, int scrollOffset, int i, int contentTop) {
  if (scrollOffset < 0) scrollOffset = 0;
  if (i < scrollOffset) i = scrollOffset;
  int y = contentTop;
  for (int k = scrollOffset; k < i; ++k) y += entryHeights[k];
  return y;
}

inline int clampScrollOffset(int scrollOffset, int maxScroll) {
  if (scrollOffset > maxScroll) return maxScroll;
  if (scrollOffset < 0) return 0;
  return scrollOffset;
}

// Total page count for the "current/total" scroll indicator — NOT maxScroll /
// pageSize + 1, which reads "1/1" on a genuinely scrollable list whenever the
// last page is a partial page smaller than pageSize.
inline int computeTotalPages(int totalEntries, int pageSize) {
  if (pageSize <= 0) return 1;
  return (totalEntries + pageSize - 1) / pageSize;
}

}  // namespace HttpMonitorLayout
