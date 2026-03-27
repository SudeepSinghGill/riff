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
