#include "MasonryLayout.h"
#include <algorithm>
#include <limits>

namespace rncv {

using namespace facebook::jsi;

MasonryLayout::MasonryLayout(std::shared_ptr<LayoutCache> cache)
    : _cache(std::move(cache)) {}

void MasonryLayout::compute(const MasonryLayoutParams& params) {
  _cache->clear();

  if (params.itemCount <= 0 || params.columns <= 0) return;

  const int cols = params.columns;
  const double availWidth = params.viewportWidth
                          - params.sectionInsetLeft
                          - params.sectionInsetRight;
  const double colWidth = (availWidth - (cols - 1) * params.columnSpacing) / cols;

  // Track the current Y height of each column.
  std::vector<double> colHeights(cols, params.sectionInsetTop);

  for (int i = 0; i < params.itemCount; ++i) {
    // Find shortest column.
    int shortestCol = 0;
    double minHeight = colHeights[0];
    for (int c = 1; c < cols; ++c) {
      if (colHeights[c] < minHeight) {
        minHeight = colHeights[c];
        shortestCol = c;
      }
    }

    const double x = params.sectionInsetLeft
                   + shortestCol * (colWidth + params.columnSpacing);
    const double y = colHeights[shortestCol];
    const double h = (i < static_cast<int>(params.itemHeights.size()))
                   ? params.itemHeights[i]
                   : 100.0; // fallback height

    // Build key.
    std::string key;
    if (i < static_cast<int>(params.keys.size())) {
      key = params.keys[i];
    } else {
      key = (params.keyPrefix.empty() ? "masonry-" : params.keyPrefix) + std::to_string(i);
    }

    LayoutAttributes attrs;
    attrs.key    = key;
    attrs.index  = i;
    attrs.frame  = { x, y, colWidth, h };
    attrs.zIndex = 0;
    attrs.sizingState = SizingState::Measured;
    _cache->setAttributes(attrs);

    colHeights[shortestCol] = y + h + params.rowSpacing;
  }
}

// ── LayoutEngine protocol ────────────────────────────────────────────────────

bool MasonryLayout::applyMeasurements(
    const std::vector<MeasurementDelta>& deltas,
    LayoutCache& cache) {
  if (deltas.empty()) return true;

  // Masonry cascade: when an item's height changes, all subsequent items in
  // the same column shift, AND items may change column assignment if the
  // shortest-column changes. For correctness, do a full re-layout from the
  // earliest affected item, using the same shortest-column algorithm.
  //
  // This is fast in C++ — masonry layout is O(n) with tiny constant.

  // First, update heights in cache.
  int earliestIndex = std::numeric_limits<int>::max();
  for (const auto& d : deltas) {
    auto attrs = cache.getAttributes(d.key);
    if (attrs) {
      auto updated = *attrs;
      updated.frame.height = d.newValue;
      updated.sizingState = SizingState::Measured;
      cache.setAttributes(updated);
      if (d.index < earliestIndex) earliestIndex = d.index;
    }
  }

  // Get all items sorted by index.
  auto all = cache.getAll();
  std::sort(all.begin(), all.end(), [](const LayoutAttributes& a, const LayoutAttributes& b) {
    return a.index < b.index;
  });

  if (all.empty()) return true;

  // Determine number of columns from distinct x positions.
  std::vector<double> colXs;
  for (const auto& a : all) {
    bool found = false;
    for (double cx : colXs) {
      if (std::abs(cx - a.frame.x) < 0.5) { found = true; break; }
    }
    if (!found) colXs.push_back(a.frame.x);
  }
  std::sort(colXs.begin(), colXs.end());
  int cols = static_cast<int>(colXs.size());
  if (cols <= 0) cols = 1;

  double colWidth = all[0].frame.width;
  double rowSpacing = 0;
  // Estimate row spacing from items in same column.
  for (size_t i = 1; i < all.size(); ++i) {
    if (std::abs(all[i].frame.x - all[0].frame.x) < 0.5) {
      // Same column as item 0 — check gap.
      rowSpacing = all[i].frame.y - (all[0].frame.y + all[0].frame.height);
      if (rowSpacing < 0) rowSpacing = 0;
      break;
    }
  }

  // Re-run masonry from the beginning (simplest correct approach).
  // Could optimize to start from earliestIndex but column heights before that
  // point may be affected by the height change. Full re-layout is safe.
  double insetTop = all[0].frame.y; // preserve starting Y
  std::vector<double> colHeights(cols, insetTop);

  for (auto& item : all) {
    // Find shortest column.
    int shortestCol = 0;
    double minH = colHeights[0];
    for (int c = 1; c < cols; ++c) {
      if (colHeights[c] < minH) {
        minH = colHeights[c];
        shortestCol = c;
      }
    }

    double newX = colXs[shortestCol];
    double newY = colHeights[shortestCol];
    if (item.frame.x != newX || item.frame.y != newY) {
      item.frame.x = newX;
      item.frame.y = newY;
      item.frame.width = colWidth;
      cache.setAttributes(item);
    }

    colHeights[shortestCol] = newY + item.frame.height + rowSpacing;
  }

  return true;
}

// ── JSI parameter extraction ──────────────────────────────────────────────────

MasonryLayoutParams MasonryLayout::paramsFromJSI(Runtime& rt, const Object& obj) {
  MasonryLayoutParams p;

  auto getInt = [&](const char* name, int def) -> int {
    auto v = obj.getProperty(rt, name);
    return v.isNumber() ? static_cast<int>(v.getNumber()) : def;
  };
  auto getDbl = [&](const char* name, double def) -> double {
    auto v = obj.getProperty(rt, name);
    return v.isNumber() ? v.getNumber() : def;
  };

  p.itemCount       = getInt("itemCount", 0);
  p.columns         = getInt("columns", 2);
  p.columnSpacing   = getDbl("columnSpacing", 8.0);
  p.rowSpacing      = getDbl("rowSpacing", 8.0);
  p.viewportWidth   = getDbl("viewportWidth", 390.0);
  p.sectionInsetTop    = getDbl("sectionInsetTop", 0);
  p.sectionInsetBottom = getDbl("sectionInsetBottom", 0);
  p.sectionInsetLeft   = getDbl("sectionInsetLeft", 0);
  p.sectionInsetRight  = getDbl("sectionInsetRight", 0);

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
  if (kp.isString()) {
    p.keyPrefix = kp.asString(rt).utf8(rt);
  }

  return p;
}

// ── JSI bindings ──────────────────────────────────────────────────────────────

void MasonryLayout::installJSIBindings(Runtime& rt, Object& target) {
  // computeMasonryLayout(params) → { positions: number[], contentHeight: number }
  // positions is a flat array: [x0, y0, w0, h0, x1, y1, w1, h1, ...]
  target.setProperty(rt, "computeMasonryLayout",
    Function::createFromHostFunction(rt,
      PropNameID::forAscii(rt, "computeMasonryLayout"), 1,
      [this](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
        if (count < 1 || !args[0].isObject()) return Value::undefined();
        auto params = paramsFromJSI(rt, args[0].getObject(rt));
        compute(params);

        // Return positions as a flat array for JS consumption.
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
}

} // namespace rncv
