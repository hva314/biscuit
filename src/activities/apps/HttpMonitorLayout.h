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

// How many entries, starting from the top of the content band, fit completely.
//
// The dashboard does not scroll: the server owns both the font size and the
// amount of content, so anything that does not fit is clipped. Entries must fit
// *entirely* -- a partially drawn row reads as a rendering bug, not as a hint
// that there is more below. Degenerate inputs yield 0 rather than indexing.
inline int visibleEntryCount(const int* entryHeights, int entryCount, int contentTop, int contentBottom) {
  if (entryCount <= 0 || entryHeights == nullptr) return 0;
  int y = contentTop;
  int visible = 0;
  while (visible < entryCount) {
    if (y + entryHeights[visible] > contentBottom) break;
    y += entryHeights[visible];
    ++visible;
  }
  return visible;
}

// Top edge of entry `i`, given every entry above it is drawn in order from
// contentTop. Plain prefix sum -- with scrolling gone there is no offset.
inline int entryY(const int* entryHeights, int i, int contentTop) {
  if (entryHeights == nullptr || i <= 0) return contentTop;
  int y = contentTop;
  for (int k = 0; k < i; ++k) y += entryHeights[k];
  return y;
}

// ---- header layout ----
// The dashboard header renders one line, left to right: title (truncated to
// titleZone) ... interval ... updated timestamp ... the far-right 24x24 wifi
// indicator. This is the anti-drift mechanism for that line: both
// HttpMonitorActivity::renderDashboard() (shipping) and test_preview.cpp (the
// preview harness) call this single function so the positioning math cannot
// silently diverge between them -- only the actual drawing primitives differ.
struct HeaderLayout {
  int titleZone;  // max width available for the (possibly truncated) title
  int intervalX;  // left edge of the interval text ("600s")
  int updatedX;   // left edge of the `updated` timestamp text
  int iconX;      // left edge of the iconSize x iconSize wifi indicator
};

// Widths (intervalW, updatedW) are measured text widths in pixels; updatedW is
// 0 when there is no timestamp to show (e.g. before the first successful
// poll), which collapses its reserved gap to a single `gap` rather than two.
// titleZone is clamped to 0 rather than allowed to go negative -- a caller
// handing it straight to a truncate/measure call must never see a negative
// width, regardless of how narrow pageWidth is or how wide the other fields are.
inline HeaderLayout computeHeaderLayout(int pageWidth, int sidePadding, int intervalW, int updatedW, int iconSize,
                                        int gap) {
  const int iconX = pageWidth - sidePadding - iconSize;
  const int updatedX = iconX - gap - updatedW;
  const int intervalX = updatedX - gap - intervalW;
  int titleZone = intervalX - sidePadding - gap;
  if (titleZone < 0) titleZone = 0;
  return HeaderLayout{titleZone, intervalX, updatedX, iconX};
}

}  // namespace HttpMonitorLayout
