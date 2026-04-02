#include "ListLayout.h"

#include <sstream>
#include <stdexcept>

#ifndef RNCV_ENABLE_NATIVE_LOGS
#define RNCV_ENABLE_NATIVE_LOGS 0
#endif

#if DEBUG && RNCV_ENABLE_NATIVE_LOGS
  #ifdef __APPLE__
    #include <os/log.h>
    #define RNCV_LIST_LOG(fmt, ...) os_log_info(os_log_create("com.rncv", "listlayout"), "[RNCV-LIST] " fmt, ##__VA_ARGS__)
  #else
    #include <android/log.h>
    #define RNCV_LIST_LOG(fmt, ...) __android_log_print(ANDROID_LOG_INFO, "RNCV-LIST", fmt, ##__VA_ARGS__)
  #endif
#else
  #define RNCV_LIST_LOG(fmt, ...) ((void)0)
#endif

namespace rncv {

using namespace facebook;
using namespace facebook::jsi;

// ─── Construction ─────────────────────────────────────────────────────────────

ListLayout::ListLayout(std::shared_ptr<LayoutCache> cache)
    : _cache(std::move(cache)) {
  // Pre-fill scratch with non-varying defaults so the hot loop
  // only touches fields that actually change per item.
  _scratch.zIndex          = 0;
  _scratch.isSupplementary = false;
  _scratch.supplementaryKind.clear();
  _scratch.sizingState     = SizingState::Measured; // fixed height = known size
  _scratch.isDirty         = false;
  _scratch.tier            = WindowTier::Outside;   // window controller sets this
  _scratch.isSticky        = false;
  _scratch.alpha           = 1.0;
  _scratch.isAnimating     = false;
}

// ─── Key helpers ─────────────────────────────────────────────────────────────

// Returns the cache key for item i in params p.
// Uses p.keys[i] when available (identity-keyed mode), otherwise falls back
// to the positional prefix+index form.
static std::string itemKey(const ListLayoutParams& p,
                            int i,
                            const std::string& prefix) {
  if (!p.keys.empty() && i < static_cast<int>(p.keys.size())) {
    return p.keys[i];
  }
  return prefix + std::to_string(i);
}

// ─── compute ─────────────────────────────────────────────────────────────────

void ListLayout::compute(const ListLayoutParams& p) {
  if (p.itemHeights.empty()) {
    computeFixed(p);
  } else {
    computeEstimated(p);
  }
}

void ListLayout::computeFixed(const ListLayoutParams& p) {
  const double contentWidth = p.viewportWidth
                              - p.sectionInsetLeft
                              - p.sectionInsetRight;
  const double stride       = p.itemHeight + p.itemSpacing;
  const std::string prefix  = p.keyPrefix.empty()
                              ? "item-" + std::to_string(p.section) + "-"
                              : p.keyPrefix;

  _scratch.section          = p.section;
  _scratch.frame.x          = p.sectionInsetLeft;
  _scratch.frame.width      = contentWidth;
  _scratch.frame.height     = p.itemHeight;
  _scratch.sizingState      = SizingState::Measured;

  for (int i = 0; i < p.itemCount; ++i) {
    _scratch.key            = itemKey(p, i, prefix);
    _scratch.index          = i;
    _scratch.frame.y        = p.sectionInsetTop + i * stride;
    _cache->setAttributes(_scratch);
  }
}

void ListLayout::computeEstimated(const ListLayoutParams& p) {
  const int count           = static_cast<int>(p.itemHeights.size());
  const double contentWidth = p.viewportWidth
                              - p.sectionInsetLeft
                              - p.sectionInsetRight;
  const std::string prefix  = p.keyPrefix.empty()
                              ? "item-" + std::to_string(p.section) + "-"
                              : p.keyPrefix;

  _scratch.section          = p.section;
  _scratch.frame.x          = p.sectionInsetLeft;
  _scratch.frame.width      = contentWidth;
  _scratch.sizingState      = SizingState::Placeholder; // estimated = not finalised

  double y = p.sectionInsetTop;
  for (int i = 0; i < count; ++i) {
    const double h          = p.itemHeights[i];
    _scratch.key            = itemKey(p, i, prefix);
    _scratch.index          = i;
    _scratch.frame.y        = y;
    _scratch.frame.height   = h;
    _cache->setAttributes(_scratch);
    y += h + p.itemSpacing;
  }
}

// ─── applyMeasurements (LayoutEngine protocol) ──────────────────────────────

bool ListLayout::applyMeasurements(
    const std::vector<MeasurementDelta>& deltas,
    LayoutCache& cache) {
  if (deltas.empty()) return true;

  // For a 1D list layout, changing item heights simply shifts everything below them.
  // Instead of recalculating Y from scratch (which loses section insets and headers),
  // we accumulate a rolling `aggregateShift` and apply it sequentially.

  // 1. Convert deltas to a fast lookup map: key -> delta height.
  // d.newValue is the new height. We need to know (new - old) later.
  std::unordered_map<std::string, double> newHeights;
  for (const auto& d : deltas) {
    newHeights[d.key] = d.newValue;
  }

  auto all = cache.getAll();
  if (all.empty()) return true;

  // Sort by strictly increasing Y to process top-to-bottom.
  std::sort(all.begin(), all.end(), [](const LayoutAttributes& a, const LayoutAttributes& b) {
    return a.frame.y < b.frame.y;
  });

  double aggregateShift = 0.0;

  // Track per-section aggregateShift at entry (first item) and exit (last item).
  // Used in the second pass to shift section backgrounds while preserving their
  // original frame structure (inset gaps, header/footer padding).
  struct SectionShifts { double entryShift = 0; double exitShift = 0; bool entered = false; };
  std::unordered_map<int, SectionShifts> sectionShifts;

  for (auto& attr : all) {
    // Section backgrounds are updated in a second pass below.
    if (attr.isDecoration && attr.decorationKind == "sectionBackground") continue;

    bool changed = false;

    // Apply any accumulated shift from items above us
    if (aggregateShift != 0.0) {
      attr.frame.y += aggregateShift;
      changed = true;
    }

    if (attr.isDecoration) {
      // Separator — just shifts, no height change.
      if (changed) cache.setAttributes(attr);
      continue;
    }

    // Track entry shift for section background: shift BEFORE this item's own delta.
    // Only track items (not supplementaries) — bg frame is anchored to items area start.
    if (!attr.isSupplementary) {
      auto& ss = sectionShifts[attr.section];
      if (!ss.entered) {
        ss.entryShift = aggregateShift;
        ss.entered = true;
      }
    }

    // If this item itself has a new measurement, update it and add to rolling shift
    auto it = newHeights.find(attr.key);
    if (it != newHeights.end()) {
      double newHeight = it->second;
      double heightDiff = newHeight - attr.frame.height;

      if (heightDiff != 0.0) {
        attr.frame.height = newHeight;
        aggregateShift += heightDiff;
        changed = true;
      }
      if (attr.sizingState != SizingState::Measured) {
        attr.sizingState = SizingState::Measured;
        changed = true;
      }
    }

    // Track exit shift: shift AFTER this item's own delta.
    if (!attr.isSupplementary) {
      sectionShifts[attr.section].exitShift = aggregateShift;
    }

    // Write back to cache if modified
    if (changed) {
      cache.setAttributes(attr);
    }
  }

  // Second pass: adjust section background frames using per-section shift deltas.
  // This preserves the original bg frame (inset gaps, header/footer padding) and
  // only applies the effect of measurement changes.
  for (auto& attr : all) {
    if (!attr.isDecoration || attr.decorationKind != "sectionBackground") continue;
    auto sit = sectionShifts.find(attr.section);
    if (sit == sectionShifts.end() || !sit->second.entered) continue;
    const double shiftY      = sit->second.entryShift;
    const double heightDelta = sit->second.exitShift - sit->second.entryShift;
    if (std::abs(shiftY) > 0.01 || std::abs(heightDelta) > 0.01) {
      attr.frame.y      += shiftY;
      attr.frame.height += heightDelta;
      cache.setAttributes(attr);
    }
  }

  return true;
}

// ─── invalidateFrom ───────────────────────────────────────────────────────────

void ListLayout::invalidateFrom(
    const std::string& startKey,
    const ListLayoutParams& p) {
  // Find the starting item's current Y so we can reflow from there.
  auto startAttrs = _cache->getAttributes(startKey);
  if (!startAttrs) return;

  const double contentWidth = p.viewportWidth
                              - p.sectionInsetLeft
                              - p.sectionInsetRight;
  const std::string prefix  = p.keyPrefix.empty()
                              ? "item-" + std::to_string(p.section) + "-"
                              : p.keyPrefix;

  // Determine start index from the stored attrs.
  const int startIndex = startAttrs->index;

  _scratch.section     = p.section;
  _scratch.frame.x     = p.sectionInsetLeft;
  _scratch.frame.width = contentWidth;

  double y = startAttrs->frame.y;

  // invalidateFrom always reads heights from the cache — the cache is the
  // single source of truth. The caller updates the corrected item's attrs
  // (via layoutCache.setAttributes) BEFORE calling invalidateFrom, so the
  // new height is already present in the cache at startKey.
  for (int i = startIndex; i < p.itemCount; ++i) {
    const std::string key = itemKey(p, i, prefix);

    auto existing = _cache->getAttributes(key);
    const double h = existing ? existing->frame.height : p.itemHeight;
    _scratch.sizingState  = existing ? existing->sizingState
                                     : SizingState::Placeholder;

    _scratch.key          = key;
    _scratch.index        = i;
    _scratch.frame.y      = y;
    _scratch.frame.height = h;
    _cache->setAttributes(_scratch);
    y += h + p.itemSpacing;
  }
}

// ─── JSI helpers ──────────────────────────────────────────────────────────────

static double dbl(Runtime& rt, const Object& o, const char* k, double def = 0.0) {
  Value v = o.getProperty(rt, k);
  return v.isNumber() ? v.getNumber() : def;
}
static int i32(Runtime& rt, const Object& o, const char* k, int def = 0) {
  Value v = o.getProperty(rt, k);
  return v.isNumber() ? static_cast<int>(v.getNumber()) : def;
}
static std::string str(Runtime& rt, const Object& o, const char* k, std::string def = "") {
  Value v = o.getProperty(rt, k);
  return v.isString() ? v.getString(rt).utf8(rt) : def;
}
static bool bln(Runtime& rt, const Object& o, const char* k, bool def = false) {
  Value v = o.getProperty(rt, k);
  return v.isBool() ? v.getBool() : def;
}

ListLayoutParams ListLayout::paramsFromJSI(Runtime& rt, const Object& obj) {
  ListLayoutParams p;
  p.itemCount           = i32(rt, obj, "itemCount");
  p.itemHeight          = dbl(rt, obj, "itemHeight", 44.0);
  p.viewportWidth       = dbl(rt, obj, "viewportWidth", 390.0);
  p.sectionInsetTop     = dbl(rt, obj, "sectionInsetTop");
  p.sectionInsetBottom  = dbl(rt, obj, "sectionInsetBottom");
  p.sectionInsetLeft    = dbl(rt, obj, "sectionInsetLeft");
  p.sectionInsetRight   = dbl(rt, obj, "sectionInsetRight");
  p.itemSpacing         = dbl(rt, obj, "itemSpacing");
  p.section             = i32(rt, obj, "section");
  p.keyPrefix               = str(rt, obj, "keyPrefix");
  p.headerHeight            = dbl(rt, obj, "headerHeight");
  p.footerHeight            = dbl(rt, obj, "footerHeight");
  p.emitSectionBackground   = bln(rt, obj, "emitSectionBackground");
  p.emitSeparators          = bln(rt, obj, "emitSeparators");
  p.separatorHeight         = dbl(rt, obj, "separatorHeight", 0.5);
  p.separatorInsetLeading   = dbl(rt, obj, "separatorInsetLeading");
  p.separatorInsetTrailing  = dbl(rt, obj, "separatorInsetTrailing");
  p.sectionSpacing          = dbl(rt, obj, "sectionSpacing");

  // Optional per-item heights array (estimated mode)
  Value heights = obj.getProperty(rt, "itemHeights");
  if (heights.isObject()) {
    auto arr = heights.getObject(rt).asArray(rt);
    size_t len = arr.size(rt);
    p.itemHeights.reserve(len);
    for (size_t i = 0; i < len; ++i) {
      Value h = arr.getValueAtIndex(rt, i);
      p.itemHeights.push_back(h.isNumber() ? h.getNumber() : 44.0);
    }
  }

  // Optional per-item identity keys (cache key = React key alignment)
  Value keysVal = obj.getProperty(rt, "keys");
  if (keysVal.isObject()) {
    auto arr = keysVal.getObject(rt).asArray(rt);
    size_t len = arr.size(rt);
    p.keys.reserve(len);
    for (size_t i = 0; i < len; ++i) {
      Value k = arr.getValueAtIndex(rt, i);
      p.keys.push_back(k.isString() ? k.getString(rt).utf8(rt) : std::to_string(i));
    }
  }

  return p;
}

std::vector<ListLayoutParams> ListLayout::sectionsFromJSI(Runtime& rt, const Array& arr) {
  size_t len = arr.size(rt);
  std::vector<ListLayoutParams> sections;
  sections.reserve(len);
  for (size_t i = 0; i < len; ++i) {
    Value v = arr.getValueAtIndex(rt, i);
    if (!v.isObject()) continue;
    ListLayoutParams p = paramsFromJSI(rt, v.getObject(rt));
    p.section = static_cast<int>(i);  // section index = position in array
    sections.push_back(std::move(p));
  }
  return sections;
}

// ─── computeSection (private) ─────────────────────────────────────────────────

double ListLayout::computeSection(const ListLayoutParams& p,
                                   int sectionIndex,
                                   double startY) {
  const std::string prefix = p.keyPrefix.empty()
      ? "item-" + std::to_string(sectionIndex) + "-"
      : p.keyPrefix;
  const double contentWidth = p.viewportWidth - p.sectionInsetLeft - p.sectionInsetRight;
  const double sectionStartY = startY; // saved for section background frame
  double bgStartY = startY; // updated to after-header once header is emitted

  double y = startY;

  // Separator prototype — reused for each inter-item separator.
  // Constructed once outside the item loop so only key/frame.y change per separator.
  LayoutAttributes sep;
  if (p.emitSeparators) {
    sep.section         = sectionIndex;
    sep.index           = -1;
    sep.isDecoration    = true;
    sep.decorationKind  = "separator";
    sep.zIndex          = 0;
    sep.sizingState     = SizingState::Measured;
    sep.isDirty         = false;
    sep.alpha           = 1.0;
    sep.frame.x         = p.sectionInsetLeft + p.separatorInsetLeading;
    sep.frame.width     = contentWidth - p.separatorInsetLeading - p.separatorInsetTrailing;
    sep.frame.height    = p.separatorHeight;
  }
  RNCV_LIST_LOG("computeSection start section=%d p.section=%d prefix=%s itemCount=%d headerH=%.1f footerH=%.1f startY=%.1f keysCount=%zu",
                sectionIndex, p.section, prefix.c_str(), p.itemCount, p.headerHeight, p.footerHeight,
                startY, p.keys.size());

  // ── Header ────────────────────────────────────────────────────────────────
  if (p.headerHeight > 0) {
    _scratch.key              = prefix + "header";
    _scratch.section          = sectionIndex;
    _scratch.index            = -1;
    _scratch.frame            = { p.sectionInsetLeft, y, contentWidth, p.headerHeight };
    _scratch.isSupplementary  = true;
    _scratch.supplementaryKind = "header";
    _scratch.sizingState      = SizingState::Measured;
    _scratch.isDirty          = false;
    _cache->setAttributes(_scratch);
    RNCV_LIST_LOG("write key=%s kind=%s section=%d index=%d frame=(%.1f,%.1f,%.1f,%.1f)",
                  _scratch.key.c_str(), _scratch.supplementaryKind.c_str(), _scratch.section, _scratch.index,
                  _scratch.frame.x, _scratch.frame.y, _scratch.frame.width, _scratch.frame.height);
    y += p.headerHeight;
    bgStartY = y; // bg starts after header so top rounded corners are exposed
  }

  // Gap between header bottom (or section start) and first item
  y += p.sectionInsetTop;
  // y is now sectionOffsets[sectionIndex] — first item starts here

  // ── Items ─────────────────────────────────────────────────────────────────
  _scratch.isSupplementary   = false;
  _scratch.supplementaryKind.clear();
  _scratch.section           = sectionIndex;
  _scratch.frame.x           = p.sectionInsetLeft;
  _scratch.frame.width       = contentWidth;

  if (p.itemHeights.empty()) {
    // Fixed height
    _scratch.sizingState  = SizingState::Measured;
    _scratch.frame.height = p.itemHeight;
    for (int i = 0; i < p.itemCount; ++i) {
      _scratch.key    = itemKey(p, i, prefix);
      _scratch.index  = i;
      _scratch.frame.y = y;
      _cache->setAttributes(_scratch);
      if (i < 5 || i == p.itemCount - 1) {
        RNCV_LIST_LOG("write key=%s kind=item section=%d index=%d frame=(%.1f,%.1f,%.1f,%.1f)",
                      _scratch.key.c_str(), _scratch.section, _scratch.index,
                      _scratch.frame.x, _scratch.frame.y, _scratch.frame.width, _scratch.frame.height);
      }
      y += p.itemHeight + p.itemSpacing;
      if (p.emitSeparators && i < p.itemCount - 1) {
        sep.key      = "separator-" + std::to_string(sectionIndex) + "-" + std::to_string(i);
        sep.frame.y  = y - p.itemSpacing; // item bottom (before spacing gap)
        _cache->setAttributes(sep);
      }
    }
  } else {
    // Estimated heights
    _scratch.sizingState = SizingState::Placeholder;
    int count = std::min(p.itemCount, static_cast<int>(p.itemHeights.size()));
    for (int i = 0; i < count; ++i) {
      double h            = p.itemHeights[i];
      _scratch.key        = itemKey(p, i, prefix);
      _scratch.index      = i;
      _scratch.frame.y    = y;
      _scratch.frame.height = h;
      _cache->setAttributes(_scratch);
      if (i < 5 || i == count - 1) {
        RNCV_LIST_LOG("write key=%s kind=item section=%d index=%d frame=(%.1f,%.1f,%.1f,%.1f)",
                      _scratch.key.c_str(), _scratch.section, _scratch.index,
                      _scratch.frame.x, _scratch.frame.y, _scratch.frame.width, _scratch.frame.height);
      }
      y += h + p.itemSpacing;
      if (p.emitSeparators && i < count - 1) {
        sep.key      = "separator-" + std::to_string(sectionIndex) + "-" + std::to_string(i);
        sep.frame.y  = y - p.itemSpacing; // item bottom (before spacing gap)
        _cache->setAttributes(sep);
      }
    }
  }

  // Undo trailing itemSpacing after last item (spacing is between items, not after)
  if (p.itemCount > 0) y -= p.itemSpacing;

  // Bottom inset: padding between last item and footer (UIKit-correct)
  y += p.sectionInsetBottom;

  // ── Section background (decoration) ──────────────────────────────────────
  // Frame: after-header to before-footer — rows area only.
  // Header sits above, footer sits below; both have full rounded corners exposed.
  if (p.emitSectionBackground) {
    LayoutAttributes bg;
    bg.key            = "decoration-" + std::to_string(sectionIndex) + "-sectionBackground";
    bg.section        = sectionIndex;
    bg.index          = -1;
    bg.frame          = { p.sectionInsetLeft, bgStartY, contentWidth, y - bgStartY };
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
    _scratch.key              = prefix + "footer";
    _scratch.section          = sectionIndex;
    _scratch.index            = -1;
    _scratch.frame            = { p.sectionInsetLeft, y, contentWidth, p.footerHeight };
    _scratch.isSupplementary  = true;
    _scratch.supplementaryKind = "footer";
    _scratch.sizingState      = SizingState::Measured;
    _scratch.isDirty          = false;
    _cache->setAttributes(_scratch);
    RNCV_LIST_LOG("write key=%s kind=%s section=%d index=%d frame=(%.1f,%.1f,%.1f,%.1f)",
                  _scratch.key.c_str(), _scratch.supplementaryKind.c_str(), _scratch.section, _scratch.index,
                  _scratch.frame.x, _scratch.frame.y, _scratch.frame.width, _scratch.frame.height);
    y += p.footerHeight;
  }

  // Inter-section gap: sits after footer, before the next section's header.
  y += p.sectionSpacing;

  RNCV_LIST_LOG("computeSection end section=%d endY=%.1f", sectionIndex, y);
  return y; // Y where next section starts
}

// ─── computeSections ──────────────────────────────────────────────────────────

void ListLayout::computeSections(const std::vector<ListLayoutParams>& sections) {
  double y = 0.0;
  RNCV_LIST_LOG("computeSections begin sections=%zu", sections.size());
  for (int s = 0; s < static_cast<int>(sections.size()); ++s) {
    y = computeSection(sections[s], s, y);
  }
  RNCV_LIST_LOG("computeSections end totalContentHeight=%.1f", y);
}

// ─── computeSectionFromCache ──────────────────────────────────────────────────

double ListLayout::computeSectionFromCache(const ListLayoutParams& p,
                                            int sectionIndex,
                                            double startY) {
  const std::string prefix = p.keyPrefix.empty()
      ? "item-" + std::to_string(sectionIndex) + "-"
      : p.keyPrefix;
  const double contentWidth = p.viewportWidth - p.sectionInsetLeft - p.sectionInsetRight;
  const double sectionStartY = startY;
  double bgStartY = startY; // updated to after-header once header is emitted

  double y = startY;

  LayoutAttributes sep;
  if (p.emitSeparators) {
    sep.section         = sectionIndex;
    sep.index           = -1;
    sep.isDecoration    = true;
    sep.decorationKind  = "separator";
    sep.zIndex          = 0;
    sep.sizingState     = SizingState::Measured;
    sep.isDirty         = false;
    sep.alpha           = 1.0;
    sep.frame.x         = p.sectionInsetLeft + p.separatorInsetLeading;
    sep.frame.width     = contentWidth - p.separatorInsetLeading - p.separatorInsetTrailing;
    sep.frame.height    = p.separatorHeight;
  }

  // ── Header ────────────────────────────────────────────────────────────────
  if (p.headerHeight > 0) {
    _scratch.key               = prefix + "header";
    _scratch.section           = sectionIndex;
    _scratch.index             = -1;
    _scratch.frame             = { p.sectionInsetLeft, y, contentWidth, p.headerHeight };
    _scratch.isSupplementary   = true;
    _scratch.supplementaryKind = "header";
    _scratch.sizingState       = SizingState::Measured;
    _scratch.isDirty           = false;
    _cache->setAttributes(_scratch);
    y += p.headerHeight;
    bgStartY = y; // bg starts after header so top rounded corners are exposed
  }

  y += p.sectionInsetTop;

  // ── Items — read heights from cache ───────────────────────────────────────
  _scratch.isSupplementary   = false;
  _scratch.supplementaryKind.clear();
  _scratch.section           = sectionIndex;
  _scratch.frame.x           = p.sectionInsetLeft;
  _scratch.frame.width       = contentWidth;

  for (int i = 0; i < p.itemCount; ++i) {
    const std::string key = itemKey(p, i, prefix);
    auto existing = _cache->getAttributes(key);
    const double h = existing ? existing->frame.height : p.itemHeight;
    _scratch.sizingState  = existing ? existing->sizingState : SizingState::Measured;
    _scratch.key          = key;
    _scratch.index        = i;
    _scratch.frame.y      = y;
    _scratch.frame.height = h;
    _cache->setAttributes(_scratch);
    y += h + p.itemSpacing;
    if (p.emitSeparators && i < p.itemCount - 1) {
      sep.key     = "separator-" + std::to_string(sectionIndex) + "-" + std::to_string(i);
      sep.frame.y = y - p.itemSpacing;
      _cache->setAttributes(sep);
    }
  }

  if (p.itemCount > 0) y -= p.itemSpacing;

  // Bottom inset: padding between last item and footer (UIKit-correct)
  y += p.sectionInsetBottom;

  // ── Section background ─────────────────────────────────────────────────
  // Emitted before footer — bg covers items area only (bgStartY to before footer),
  // matching NSCollectionLayoutDecorationItem.background behavior.
  if (p.emitSectionBackground) {
    LayoutAttributes bg;
    bg.key            = "decoration-" + std::to_string(sectionIndex) + "-sectionBackground";
    bg.section        = sectionIndex;
    bg.index          = -1;
    bg.frame          = { p.sectionInsetLeft, bgStartY, contentWidth, y - bgStartY };
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
    _scratch.key               = prefix + "footer";
    _scratch.section           = sectionIndex;
    _scratch.index             = -1;
    _scratch.frame             = { p.sectionInsetLeft, y, contentWidth, p.footerHeight };
    _scratch.isSupplementary   = true;
    _scratch.supplementaryKind = "footer";
    _scratch.sizingState       = SizingState::Measured;
    _scratch.isDirty           = false;
    _cache->setAttributes(_scratch);
    y += p.footerHeight;
  }

  // Inter-section gap: sits after footer, before the next section's header.
  y += p.sectionSpacing;

  return y;
}

// ─── invalidateSectionsFrom ───────────────────────────────────────────────────

void ListLayout::invalidateSectionsFrom(int fromSection,
                                         const std::vector<ListLayoutParams>& sections) {
  if (fromSection < 0 || fromSection >= static_cast<int>(sections.size())) return;

  // Find the Y where fromSection starts by reading the cache.
  const auto& p0 = sections[fromSection];
  const std::string prefix0 = p0.keyPrefix.empty()
      ? "item-" + std::to_string(fromSection) + "-"
      : p0.keyPrefix;

  double startY = 0.0;
  if (p0.headerHeight > 0) {
    auto header = _cache->getAttributes(prefix0 + "header");
    if (header) startY = header->frame.y;
  } else {
    auto firstItem = _cache->getAttributes(itemKey(p0, 0, prefix0));
    if (firstItem) startY = firstItem->frame.y - p0.sectionInsetTop;
  }

  // Reflow fromSection reading item heights from cache (so any measured heights
  // written before this call are preserved), then full-recompute subsequent sections.
  double y = computeSectionFromCache(p0, fromSection, startY);
  for (int s = fromSection + 1; s < static_cast<int>(sections.size()); ++s) {
    y = computeSection(sections[s], s, y);
  }
}

// ─── JSI bindings ─────────────────────────────────────────────────────────────

void ListLayout::installJSIBindings(Runtime& rt, Object& target) {
  // computeListLayout(params) → undefined  [M1.2/M1.3]
  target.setProperty(rt, "computeListLayout",
    Function::createFromHostFunction(rt,
      PropNameID::forAscii(rt, "computeListLayout"), 1,
      [this](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
        if (count < 1 || !args[0].isObject()) return Value::undefined();
        compute(paramsFromJSI(rt, args[0].getObject(rt)));
        return Value::undefined();
      }));

  // invalidateListLayoutFrom(key, params) → undefined  [M1.3]
  target.setProperty(rt, "invalidateListLayoutFrom",
    Function::createFromHostFunction(rt,
      PropNameID::forAscii(rt, "invalidateListLayoutFrom"), 2,
      [this](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
        if (count < 2 || !args[0].isString() || !args[1].isObject()) {
          return Value::undefined();
        }
        std::string key = args[0].getString(rt).utf8(rt);
        invalidateFrom(key, paramsFromJSI(rt, args[1].getObject(rt)));
        return Value::undefined();
      }));

  // computeSections(sections: object[]) → undefined  [M1.5]
  target.setProperty(rt, "computeSections",
    Function::createFromHostFunction(rt,
      PropNameID::forAscii(rt, "computeSections"), 1,
      [this](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
        if (count < 1 || !args[0].isObject()) return Value::undefined();
        auto arr = args[0].getObject(rt).asArray(rt);
        computeSections(sectionsFromJSI(rt, arr));
        return Value::undefined();
      }));

  // invalidateSectionsFrom(sectionIndex: number, sections: object[]) → undefined  [M1.5]
  target.setProperty(rt, "invalidateSectionsFrom",
    Function::createFromHostFunction(rt,
      PropNameID::forAscii(rt, "invalidateSectionsFrom"), 2,
      [this](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
        if (count < 2 || !args[0].isNumber() || !args[1].isObject()) {
          return Value::undefined();
        }
        int fromSection = static_cast<int>(args[0].getNumber());
        auto arr = args[1].getObject(rt).asArray(rt);
        invalidateSectionsFrom(fromSection, sectionsFromJSI(rt, arr));
        return Value::undefined();
      }));
}

} // namespace rncv
