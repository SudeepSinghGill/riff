#include "GridLayout.h"
#include <algorithm>
#include <climits>
#include <limits>

namespace rncv {

using namespace facebook::jsi;

GridLayout::GridLayout(std::shared_ptr<LayoutCache> cache)
    : _cache(std::move(cache)) {}

// ─── JSI helpers ──────────────────────────────────────────────────────────────

static double gdbl(Runtime& rt, const Object& o, const char* k, double def = 0.0) {
  Value v = o.getProperty(rt, k);
  return v.isNumber() ? v.getNumber() : def;
}
static int gi32(Runtime& rt, const Object& o, const char* k, int def = 0) {
  Value v = o.getProperty(rt, k);
  return v.isNumber() ? static_cast<int>(v.getNumber()) : def;
}
static bool gbln(Runtime& rt, const Object& o, const char* k, bool def = false) {
  Value v = o.getProperty(rt, k);
  return v.isBool() ? v.getBool() : def;
}

// ─── compute (legacy single-section) ─────────────────────────────────────────

void GridLayout::compute(const GridLayoutParams& params) {
  _cache->clear();
  _columns = params.columns > 0 ? params.columns : 1;
  _horizontal = false;
  _viewportHeight = 0.0;
  _sectionParams = { params };
  computeSection(params, 0, 0.0);
}

// ─── computeSection ───────────────────────────────────────────────────────────

double GridLayout::computeSection(const GridLayoutParams& p,
                                   int sectionIndex,
                                   double startPrimary) {
  const int cols = p.columns > 0 ? p.columns : 1;

  const std::string prefix = p.keyPrefix.empty()
      ? "grid-" + std::to_string(sectionIndex) + "-"
      : p.keyPrefix;

  double primary       = startPrimary;
  double bgStartPrimary = startPrimary;

  // Vertical layout only (horizontal grid handled in a later pass).
  const double contentWidth     = p.viewportWidth - p.sectionInsetLeft - p.sectionInsetRight;
  const double totalColSpacing  = p.columnSpacing * (cols - 1);
  const double itemWidth        = contentWidth > 0
      ? (contentWidth - totalColSpacing) / cols
      : 0.0;

  // ── Header ────────────────────────────────────────────────────────────────
  if (p.headerHeight > 0) {
    LayoutAttributes hdr;
    hdr.key               = prefix + "header";
    hdr.section           = sectionIndex;
    hdr.index             = -1;
    hdr.isSupplementary   = true;
    hdr.supplementaryKind = "header";
    hdr.sizingState       = SizingState::Measured;
    hdr.isDirty           = false;
    hdr.alpha             = 1.0;
    hdr.frame             = { p.sectionInsetLeft, primary, contentWidth, p.headerHeight };
    _cache->setAttributes(hdr);
    primary       += p.headerHeight;
    bgStartPrimary = primary; // bg starts after header
  }

  primary += p.sectionInsetTop; // gap between header (or section start) and first row

  // ── Items in grid ─────────────────────────────────────────────────────────
  const bool fixedRow = p.rowHeight > 0;

  struct CellFrame { double x, y, width, height; };
  std::vector<CellFrame> frames(p.itemCount);

  int    col          = 0;
  double rowStartY    = primary;
  double rowMaxHeight = 0.0;
  int    rowStart     = 0;

  for (int i = 0; i < p.itemCount; ++i) {
    double itemHeight = fixedRow
        ? p.rowHeight
        : (i < static_cast<int>(p.itemHeights.size()) ? p.itemHeights[i] : 44.0);

    const double x = p.sectionInsetLeft + col * (itemWidth + p.columnSpacing);
    frames[i] = { x, rowStartY, itemWidth, itemHeight };

    if (itemHeight > rowMaxHeight) rowMaxHeight = itemHeight;
    col++;

    if (col >= cols) {
      // End of row — all items in this row are top-aligned (frame.y = rowStartY).
      // Future: rowAlignment ('top'|'center'|'bottom') — shorter items would have
      // their frame.y offset by (rowMaxHeight - itemHeight) / 2 or (rowMaxHeight - itemHeight).
      // See PLAN.md F3.1 and src/types/protocol.ts GridLayoutDelegate for the API sketch.
      if (!fixedRow) {
        for (int j = rowStart; j <= i; ++j) frames[j].height = rowMaxHeight;
      }
      // Row separator between this row and the next
      if (p.emitSeparators && i < p.itemCount - 1) {
        LayoutAttributes sep;
        sep.key            = "separator-" + std::to_string(sectionIndex) + "-row-" +
                             std::to_string(rowStart / cols);
        sep.section        = sectionIndex;
        sep.index          = -1;
        sep.isDecoration   = true;
        sep.decorationKind = "separator";
        sep.zIndex         = 0;
        sep.sizingState    = SizingState::Measured;
        sep.isDirty        = false;
        sep.alpha          = 1.0;
        sep.frame          = {
          p.sectionInsetLeft + p.separatorInsetLeading,
          rowStartY + rowMaxHeight,
          contentWidth - p.separatorInsetLeading - p.separatorInsetTrailing,
          p.separatorHeight
        };
        _cache->setAttributes(sep);
      }
      // Column separators between adjacent columns in this row
      if (p.emitSeparators && cols > 1) {
        const int rowIdx = rowStart / cols;
        for (int c = 0; c < cols - 1; ++c) {
          LayoutAttributes colSep;
          colSep.key            = "separator-" + std::to_string(sectionIndex) + "-col-" +
                                  std::to_string(rowIdx) + "-" + std::to_string(c);
          colSep.section        = sectionIndex;
          colSep.index          = -1;
          colSep.isDecoration   = true;
          colSep.decorationKind = "separator";
          colSep.zIndex         = 0;
          colSep.sizingState    = SizingState::Measured;
          colSep.isDirty        = false;
          colSep.alpha          = 1.0;
          // Center the thin line in the column gap
          const double gapX = p.sectionInsetLeft +
                              (c + 1) * itemWidth + c * p.columnSpacing +
                              (p.columnSpacing - p.separatorHeight) / 2.0;
          colSep.frame = { gapX, rowStartY, p.separatorHeight, rowMaxHeight };
          _cache->setAttributes(colSep);
        }
      }
      rowStartY   += rowMaxHeight + p.rowSpacing;
      col          = 0;
      rowMaxHeight = 0.0;
      rowStart     = i + 1;
    }
  }

  // Handle last partial row
  if (col > 0 && p.itemCount > 0) {
    if (!fixedRow) {
      for (int j = rowStart; j < p.itemCount; ++j) frames[j].height = rowMaxHeight;
    }
    // Column separators for last partial row (only between occupied columns)
    if (p.emitSeparators && col > 1) {
      const int rowIdx = rowStart / cols;
      for (int c = 0; c < col - 1; ++c) {
        LayoutAttributes colSep;
        colSep.key            = "separator-" + std::to_string(sectionIndex) + "-col-" +
                                std::to_string(rowIdx) + "-" + std::to_string(c);
        colSep.section        = sectionIndex;
        colSep.index          = -1;
        colSep.isDecoration   = true;
        colSep.decorationKind = "separator";
        colSep.zIndex         = 0;
        colSep.sizingState    = SizingState::Measured;
        colSep.isDirty        = false;
        colSep.alpha          = 1.0;
        const double gapX = p.sectionInsetLeft +
                            (c + 1) * itemWidth + c * p.columnSpacing +
                            (p.columnSpacing - p.separatorHeight) / 2.0;
        colSep.frame = { gapX, rowStartY, p.separatorHeight, rowMaxHeight };
        _cache->setAttributes(colSep);
      }
    }
    rowStartY += rowMaxHeight;
  }

  // Write items to cache
  for (int i = 0; i < p.itemCount; ++i) {
    const std::string key = (i < static_cast<int>(p.keys.size()))
        ? p.keys[i]
        : prefix + std::to_string(i);
    LayoutAttributes attrs;
    attrs.key         = key;
    attrs.section     = sectionIndex;
    attrs.index       = i;
    attrs.zIndex      = 0;
    attrs.alpha       = 1.0;
    attrs.frame       = { frames[i].x, frames[i].y, frames[i].width, frames[i].height };
    attrs.sizingState = fixedRow ? SizingState::Measured : SizingState::Placeholder;
    _cache->setAttributes(attrs);
  }

  primary = rowStartY;
  primary += p.sectionInsetBottom; // gap between last row and footer

  // ── Section background (items area only) ──────────────────────────────────
  // Content insets applied in absolute visual coords so windowed rect and
  // ShadowNode positions use the inset-adjusted frame.
  if (p.emitSectionBackground) {
    LayoutAttributes bg;
    bg.key            = "decoration-" + std::to_string(sectionIndex) + "-sectionBackground";
    bg.section        = sectionIndex;
    bg.index          = -1;
    bg.frame          = {
      p.sectionInsetLeft + p.sectionBackgroundInsetLeft,
      bgStartPrimary     + p.sectionBackgroundInsetTop,
      contentWidth                 - p.sectionBackgroundInsetLeft - p.sectionBackgroundInsetRight,
      primary - bgStartPrimary     - p.sectionBackgroundInsetTop  - p.sectionBackgroundInsetBottom,
    };
    bg.isDecoration   = true;
    bg.decorationKind = "sectionBackground";
    bg.zIndex         = -1;
    bg.sizingState    = SizingState::Measured;
    bg.isDirty        = false;
    bg.alpha          = 1.0;
    _cache->setAttributes(bg);
  }

  // ── Footer ────────────────────────────────────────────────────────────────
  if (p.footerHeight > 0) {
    LayoutAttributes ftr;
    ftr.key               = prefix + "footer";
    ftr.section           = sectionIndex;
    ftr.index             = -1;
    ftr.isSupplementary   = true;
    ftr.supplementaryKind = "footer";
    ftr.sizingState       = SizingState::Measured;
    ftr.isDirty           = false;
    ftr.alpha             = 1.0;
    ftr.frame             = { p.sectionInsetLeft, primary, contentWidth, p.footerHeight };
    _cache->setAttributes(ftr);
    primary += p.footerHeight;
  }

  primary += p.sectionSpacing; // inter-section gap
  return primary;
}

// ─── computeSectionFromCache ──────────────────────────────────────────────────
// Like computeSection but reads item heights from the cache instead of params.
// Used by applyMeasurements and invalidateSectionsFrom to preserve Yoga measurements.

double GridLayout::computeSectionFromCache(const GridLayoutParams& p,
                                            int sectionIndex,
                                            double startPrimary) {
  const int cols = p.columns > 0 ? p.columns : 1;

  const std::string prefix = p.keyPrefix.empty()
      ? "grid-" + std::to_string(sectionIndex) + "-"
      : p.keyPrefix;

  double primary        = startPrimary;
  double bgStartPrimary = startPrimary;

  const double contentWidth    = p.viewportWidth - p.sectionInsetLeft - p.sectionInsetRight;
  const double totalColSpacing = p.columnSpacing * (cols - 1);
  const double itemWidth       = contentWidth > 0
      ? (contentWidth - totalColSpacing) / cols
      : 0.0;
  const bool   fixedRow        = p.rowHeight > 0;

  // ── Header ────────────────────────────────────────────────────────────────
  if (p.headerHeight > 0) {
    const std::string hdrKey = prefix + "header";
    auto existingHdr = _cache->getAttributes(hdrKey);
    LayoutAttributes hdr;
    hdr.key               = hdrKey;
    hdr.section           = sectionIndex;
    hdr.index             = -1;
    hdr.isSupplementary   = true;
    hdr.supplementaryKind = "header";
    hdr.sizingState       = SizingState::Measured;
    hdr.isDirty           = false;
    hdr.alpha             = 1.0;
    // Preserve measured header height (Yoga may have refined it)
    const double hH = (existingHdr && existingHdr->frame.height > 0)
        ? existingHdr->frame.height
        : p.headerHeight;
    hdr.frame = { p.sectionInsetLeft, primary, contentWidth, hH };
    _cache->setAttributes(hdr);
    primary        += hH;
    bgStartPrimary  = primary;
  }

  primary += p.sectionInsetTop;

  // ── Read item heights from cache ──────────────────────────────────────────
  std::vector<double> heights(p.itemCount);
  for (int i = 0; i < p.itemCount; ++i) {
    const std::string key = (i < static_cast<int>(p.keys.size()))
        ? p.keys[i]
        : prefix + std::to_string(i);
    auto existing = _cache->getAttributes(key);
    heights[i] = existing && existing->frame.height > 0
        ? existing->frame.height
        : (fixedRow ? p.rowHeight : 44.0);
  }

  // ── Layout items using cached heights ─────────────────────────────────────
  struct CellFrame { double x, y, width, height; };
  std::vector<CellFrame> frames(p.itemCount);

  int    col          = 0;
  double rowStartY    = primary;
  double rowMaxHeight = 0.0;
  int    rowStart     = 0;

  for (int i = 0; i < p.itemCount; ++i) {
    const double h = heights[i];
    const double x = p.sectionInsetLeft + col * (itemWidth + p.columnSpacing);
    frames[i] = { x, rowStartY, itemWidth, h };

    if (h > rowMaxHeight) rowMaxHeight = h;
    col++;

    if (col >= cols) {
      // Align row to max
      for (int j = rowStart; j <= i; ++j) frames[j].height = rowMaxHeight;
      // Row separator
      if (p.emitSeparators && i < p.itemCount - 1) {
        LayoutAttributes sep;
        sep.key            = "separator-" + std::to_string(sectionIndex) + "-row-" +
                             std::to_string(rowStart / cols);
        sep.section        = sectionIndex;
        sep.index          = -1;
        sep.isDecoration   = true;
        sep.decorationKind = "separator";
        sep.zIndex         = 0;
        sep.sizingState    = SizingState::Measured;
        sep.isDirty        = false;
        sep.alpha          = 1.0;
        sep.frame          = {
          p.sectionInsetLeft + p.separatorInsetLeading,
          rowStartY + rowMaxHeight,
          contentWidth - p.separatorInsetLeading - p.separatorInsetTrailing,
          p.separatorHeight
        };
        _cache->setAttributes(sep);
      }
      // Column separators between adjacent columns in this row
      if (p.emitSeparators && cols > 1) {
        const int rowIdx = rowStart / cols;
        for (int c = 0; c < cols - 1; ++c) {
          LayoutAttributes colSep;
          colSep.key            = "separator-" + std::to_string(sectionIndex) + "-col-" +
                                  std::to_string(rowIdx) + "-" + std::to_string(c);
          colSep.section        = sectionIndex;
          colSep.index          = -1;
          colSep.isDecoration   = true;
          colSep.decorationKind = "separator";
          colSep.zIndex         = 0;
          colSep.sizingState    = SizingState::Measured;
          colSep.isDirty        = false;
          colSep.alpha          = 1.0;
          const double gapX = p.sectionInsetLeft +
                              (c + 1) * itemWidth + c * p.columnSpacing +
                              (p.columnSpacing - p.separatorHeight) / 2.0;
          colSep.frame = { gapX, rowStartY, p.separatorHeight, rowMaxHeight };
          _cache->setAttributes(colSep);
        }
      }
      rowStartY   += rowMaxHeight + p.rowSpacing;
      col          = 0;
      rowMaxHeight = 0.0;
      rowStart     = i + 1;
    }
  }

  if (col > 0 && p.itemCount > 0) {
    for (int j = rowStart; j < p.itemCount; ++j) frames[j].height = rowMaxHeight;
    // Column separators for last partial row (only between occupied columns)
    if (p.emitSeparators && col > 1) {
      const int rowIdx = rowStart / cols;
      for (int c = 0; c < col - 1; ++c) {
        LayoutAttributes colSep;
        colSep.key            = "separator-" + std::to_string(sectionIndex) + "-col-" +
                                std::to_string(rowIdx) + "-" + std::to_string(c);
        colSep.section        = sectionIndex;
        colSep.index          = -1;
        colSep.isDecoration   = true;
        colSep.decorationKind = "separator";
        colSep.zIndex         = 0;
        colSep.sizingState    = SizingState::Measured;
        colSep.isDirty        = false;
        colSep.alpha          = 1.0;
        const double gapX = p.sectionInsetLeft +
                            (c + 1) * itemWidth + c * p.columnSpacing +
                            (p.columnSpacing - p.separatorHeight) / 2.0;
        colSep.frame = { gapX, rowStartY, p.separatorHeight, rowMaxHeight };
        _cache->setAttributes(colSep);
      }
    }
    rowStartY += rowMaxHeight;
  }

  // Write updated positions to cache
  for (int i = 0; i < p.itemCount; ++i) {
    const std::string key = (i < static_cast<int>(p.keys.size()))
        ? p.keys[i]
        : prefix + std::to_string(i);
    auto existing = _cache->getAttributes(key);
    LayoutAttributes attrs;
    attrs.key         = key;
    attrs.section     = sectionIndex;
    attrs.index       = i;
    attrs.zIndex      = 0;
    attrs.alpha       = 1.0;
    attrs.frame       = { frames[i].x, frames[i].y, frames[i].width, frames[i].height };
    attrs.sizingState = existing ? existing->sizingState : SizingState::Placeholder;
    _cache->setAttributes(attrs);
  }

  primary = rowStartY;
  primary += p.sectionInsetBottom;

  // Section background — content insets applied in absolute visual coords.
  if (p.emitSectionBackground) {
    LayoutAttributes bg;
    bg.key            = "decoration-" + std::to_string(sectionIndex) + "-sectionBackground";
    bg.section        = sectionIndex;
    bg.index          = -1;
    bg.frame          = {
      p.sectionInsetLeft + p.sectionBackgroundInsetLeft,
      bgStartPrimary     + p.sectionBackgroundInsetTop,
      contentWidth                 - p.sectionBackgroundInsetLeft - p.sectionBackgroundInsetRight,
      primary - bgStartPrimary     - p.sectionBackgroundInsetTop  - p.sectionBackgroundInsetBottom,
    };
    bg.isDecoration   = true;
    bg.decorationKind = "sectionBackground";
    bg.zIndex         = -1;
    bg.sizingState    = SizingState::Measured;
    bg.isDirty        = false;
    bg.alpha          = 1.0;
    _cache->setAttributes(bg);
  }

  // Footer
  if (p.footerHeight > 0) {
    const std::string ftrKey = prefix + "footer";
    auto existingFtr = _cache->getAttributes(ftrKey);
    LayoutAttributes ftr;
    ftr.key               = ftrKey;
    ftr.section           = sectionIndex;
    ftr.index             = -1;
    ftr.isSupplementary   = true;
    ftr.supplementaryKind = "footer";
    ftr.sizingState       = SizingState::Measured;
    ftr.isDirty           = false;
    ftr.alpha             = 1.0;
    const double fH = (existingFtr && existingFtr->frame.height > 0)
        ? existingFtr->frame.height
        : p.footerHeight;
    ftr.frame = { p.sectionInsetLeft, primary, contentWidth, fH };
    _cache->setAttributes(ftr);
    primary += fH;
  }

  primary += p.sectionSpacing;
  return primary;
}

// ─── computeSections ──────────────────────────────────────────────────────────

void GridLayout::computeSections(const std::vector<GridLayoutParams>& sections) {
  _cache->clear();
  if (sections.empty()) return;

  _horizontal     = sections[0].horizontal;
  _viewportHeight = sections[0].viewportHeight;
  _columns        = sections[0].columns > 0 ? sections[0].columns : 2;
  _sectionParams  = sections;

  double primary = 0.0;
  for (int s = 0; s < static_cast<int>(sections.size()); ++s) {
    primary = computeSection(sections[s], s, primary);
  }
}

// ─── invalidateSectionsFrom ───────────────────────────────────────────────────

void GridLayout::invalidateSectionsFrom(int fromSection,
                                         const std::vector<GridLayoutParams>& sections) {
  if (fromSection < 0 || fromSection >= static_cast<int>(sections.size())) return;

  _sectionParams = sections;

  const auto& p0 = sections[fromSection];
  const std::string prefix0 = p0.keyPrefix.empty()
      ? "grid-" + std::to_string(fromSection) + "-"
      : p0.keyPrefix;

  // Determine where this section starts (its leading edge in primary axis)
  double startPrimary = 0.0;
  if (p0.headerHeight > 0) {
    auto header = _cache->getAttributes(prefix0 + "header");
    if (header) startPrimary = header->frame.y;
  } else if (p0.itemCount > 0) {
    const std::string firstKey = p0.keys.empty()
        ? prefix0 + "0"
        : p0.keys[0];
    auto firstItem = _cache->getAttributes(firstKey);
    if (firstItem) {
      startPrimary = firstItem->frame.y - p0.sectionInsetTop;
    }
  }

  double primary = computeSectionFromCache(p0, fromSection, startPrimary);
  for (int s = fromSection + 1; s < static_cast<int>(sections.size()); ++s) {
    primary = computeSection(sections[s], s, primary);
  }
}

// ─── applyMeasurements (LayoutEngine protocol) ──────────────────────────────

bool GridLayout::applyMeasurements(
    const std::vector<MeasurementDelta>& deltas,
    LayoutCache& cache) {
  if (deltas.empty()) return true;

  // Apply height deltas and track the first changed section.
  //
  // Section backgrounds are layout-determined (height = primary - bgStartPrimary).
  // Writing Yoga's measurement to a section background would corrupt its height
  // because the ShadowNode sees items+separators as siblings; its Yoga tree does
  // not know which height belongs to the "background of section N". Instead:
  //   - Don't write Yoga height to sectionBackground decorations.
  //   - Trigger firstChangedSection so computeSectionFromCache restores the
  //     correct height from the item positions.
  //
  // Separators: height is also layout-determined but stable (0.5 or rowMaxH).
  // Writing Yoga's measurement is fine for separators; they are small enough that
  // any rounding error won't cause visible corruption. We still skip them for
  // firstChangedSection because a separator delta alone doesn't shift item positions.
  int firstChangedSection = INT_MAX;
  for (const auto& d : deltas) {
    auto attrs = cache.getAttributes(d.key);
    if (attrs) {
      const bool isSectionBg = attrs->isDecoration &&
                               attrs->decorationKind == "sectionBackground";
      if (!isSectionBg) {
        // Items, supplementary (headers/footers), and separators: update height.
        auto updated = *attrs;
        updated.frame.height = d.newValue;
        updated.sizingState  = SizingState::Measured;
        cache.setAttributes(updated);
      }
      if (!attrs->isDecoration && !attrs->isSupplementary) {
        // Item delta → reflow from this section.
        firstChangedSection = std::min(firstChangedSection, attrs->section);
      } else if (isSectionBg) {
        // Section background height mismatch → reflow this section to restore it.
        firstChangedSection = std::min(firstChangedSection, attrs->section);
      }
    }
  }

  if (firstChangedSection == INT_MAX) return true;

  if (_sectionParams.empty()) {
    // Legacy single-section path: re-layout from scratch using updated heights.
    // Reconstruct a single-section params with heights from cache.
    // We can't reflow perfectly without knowing rowSpacing; use computeSectionFromCache
    // with whatever _sectionParams[0] we have (set in compute()).
    return true; // heights already applied; positions approximate until next prepare()
  }

  if (firstChangedSection >= static_cast<int>(_sectionParams.size())) return true;

  const auto& p0     = _sectionParams[firstChangedSection];
  const std::string prefix0 = p0.keyPrefix.empty()
      ? "grid-" + std::to_string(firstChangedSection) + "-"
      : p0.keyPrefix;

  // Determine where the changed section starts
  double startPrimary = 0.0;
  if (p0.headerHeight > 0) {
    auto header = _cache->getAttributes(prefix0 + "header");
    if (header) startPrimary = header->frame.y;
  } else if (p0.itemCount > 0) {
    const std::string firstKey = p0.keys.empty() ? prefix0 + "0" : p0.keys[0];
    auto firstItem = _cache->getAttributes(firstKey);
    if (firstItem) {
      startPrimary = firstItem->frame.y - p0.sectionInsetTop;
    }
  }

  // Reflow from the changed section, then full-recompute all subsequent sections
  double primary = computeSectionFromCache(p0, firstChangedSection, startPrimary);
  for (int s = firstChangedSection + 1; s < static_cast<int>(_sectionParams.size()); ++s) {
    primary = computeSection(_sectionParams[s], s, primary);
  }

  return true;
}

// ─── JSI parameter extraction ──────────────────────────────────────────────────

GridLayoutParams GridLayout::paramsFromJSI(Runtime& rt, const Object& obj) {
  GridLayoutParams p;

  p.itemCount       = gi32(rt, obj, "itemCount", 0);
  p.columns         = gi32(rt, obj, "columns", 2);
  p.columnSpacing   = gdbl(rt, obj, "columnSpacing", 0.0);
  p.rowSpacing      = gdbl(rt, obj, "rowSpacing", 0.0);
  p.viewportWidth   = gdbl(rt, obj, "viewportWidth", 390.0);
  p.viewportHeight  = gdbl(rt, obj, "viewportHeight", 0.0);
  p.sectionInsetTop    = gdbl(rt, obj, "sectionInsetTop", 0);
  p.sectionInsetBottom = gdbl(rt, obj, "sectionInsetBottom", 0);
  p.sectionInsetLeft   = gdbl(rt, obj, "sectionInsetLeft", 0);
  p.sectionInsetRight  = gdbl(rt, obj, "sectionInsetRight", 0);
  p.rowHeight       = gdbl(rt, obj, "rowHeight", 0.0);

  p.headerHeight          = gdbl(rt, obj, "headerHeight", 0.0);
  p.footerHeight          = gdbl(rt, obj, "footerHeight", 0.0);
  p.emitSectionBackground = gbln(rt, obj, "emitSectionBackground", false);
  p.emitSeparators        = gbln(rt, obj, "emitSeparators", false);
  p.separatorHeight       = gdbl(rt, obj, "separatorHeight", 0.5);
  p.separatorInsetLeading  = gdbl(rt, obj, "separatorInsetLeading", 0.0);
  p.separatorInsetTrailing = gdbl(rt, obj, "separatorInsetTrailing", 0.0);
  p.sectionSpacing        = gdbl(rt, obj, "sectionSpacing", 0.0);

  p.horizontal               = gbln(rt, obj, "horizontal", false);
  p.estimatedCrossAxisHeight = gdbl(rt, obj, "estimatedCrossAxisHeight", 200.0);

  p.sectionBackgroundInsetTop    = gdbl(rt, obj, "sectionBackgroundInsetTop",    0.0);
  p.sectionBackgroundInsetBottom = gdbl(rt, obj, "sectionBackgroundInsetBottom", 0.0);
  p.sectionBackgroundInsetLeft   = gdbl(rt, obj, "sectionBackgroundInsetLeft",   0.0);
  p.sectionBackgroundInsetRight  = gdbl(rt, obj, "sectionBackgroundInsetRight",  0.0);

  // itemHeights: number[]
  auto hv = obj.getProperty(rt, "itemHeights");
  if (hv.isObject()) {
    auto arr = hv.asObject(rt).asArray(rt);
    size_t len = arr.length(rt);
    p.itemHeights.resize(len);
    for (size_t i = 0; i < len; ++i) {
      p.itemHeights[i] = arr.getValueAtIndex(rt, i).getNumber();
    }
  }

  // keys: string[]
  auto kv = obj.getProperty(rt, "keys");
  if (kv.isObject()) {
    auto arr = kv.asObject(rt).asArray(rt);
    size_t len = arr.length(rt);
    p.keys.resize(len);
    for (size_t i = 0; i < len; ++i) {
      p.keys[i] = arr.getValueAtIndex(rt, i).asString(rt).utf8(rt);
    }
  }

  auto kp = obj.getProperty(rt, "keyPrefix");
  if (kp.isString()) p.keyPrefix = kp.asString(rt).utf8(rt);

  return p;
}

std::vector<GridLayoutParams> GridLayout::sectionsFromJSI(Runtime& rt, const Array& arr) {
  size_t len = arr.size(rt);
  std::vector<GridLayoutParams> sections;
  sections.reserve(len);
  for (size_t i = 0; i < len; ++i) {
    Value v = arr.getValueAtIndex(rt, i);
    if (!v.isObject()) continue;
    GridLayoutParams p = paramsFromJSI(rt, v.getObject(rt));
    p.section = static_cast<int>(i); // section index = position in array
    sections.push_back(std::move(p));
  }
  return sections;
}

// ─── JSI bindings ──────────────────────────────────────────────────────────────

void GridLayout::installJSIBindings(Runtime& rt, Object& target) {
  // ── Legacy single-section method (kept for compatibility) ──────────────────
  // computeGridLayout(params) → { positions: number[], contentHeight: number }
  target.setProperty(rt, "computeGridLayout",
    Function::createFromHostFunction(rt,
      PropNameID::forAscii(rt, "computeGridLayout"), 1,
      [this](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
        if (count < 1 || !args[0].isObject()) return Value::undefined();
        auto params = paramsFromJSI(rt, args[0].getObject(rt));
        compute(params);

        auto all = _cache->getAll();
        Array positions(rt, all.size() * 4);
        for (size_t i = 0; i < all.size(); ++i) {
          positions.setValueAtIndex(rt, i * 4 + 0, Value(all[i].frame.x));
          positions.setValueAtIndex(rt, i * 4 + 1, Value(all[i].frame.y));
          positions.setValueAtIndex(rt, i * 4 + 2, Value(all[i].frame.width));
          positions.setValueAtIndex(rt, i * 4 + 3, Value(all[i].frame.height));
        }
        auto size = _cache->getTotalContentSize();
        Object result(rt);
        result.setProperty(rt, "positions", std::move(positions));
        result.setProperty(rt, "contentHeight", Value(size.height));
        return Value(rt, result);
      }));

  // ── Standard contract: computeSections(sections: object[]) → undefined ─────
  target.setProperty(rt, "computeSections",
    Function::createFromHostFunction(rt,
      PropNameID::forAscii(rt, "computeSections"), 1,
      [this](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
        if (count < 1 || !args[0].isObject()) return Value::undefined();
        auto arr = args[0].getObject(rt).asArray(rt);
        computeSections(sectionsFromJSI(rt, arr));
        return Value::undefined();
      }));

  // ── Standard contract: invalidateSectionsFrom(fromSection, sections[]) ─────
  target.setProperty(rt, "invalidateSectionsFrom",
    Function::createFromHostFunction(rt,
      PropNameID::forAscii(rt, "invalidateSectionsFrom"), 2,
      [this](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
        if (count < 2 || !args[0].isNumber() || !args[1].isObject()) return Value::undefined();
        int fromSection = static_cast<int>(args[0].getNumber());
        auto arr = args[1].getObject(rt).asArray(rt);
        invalidateSectionsFrom(fromSection, sectionsFromJSI(rt, arr));
        return Value::undefined();
      }));
}

} // namespace rncv
