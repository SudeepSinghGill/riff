#include "GridLayout.h"
#include <algorithm>

namespace rncv {

using namespace facebook::jsi;

GridLayout::GridLayout(std::shared_ptr<LayoutCache> cache)
    : _cache(std::move(cache)) {}

void GridLayout::compute(const GridLayoutParams& params) {
  _cache->clear();

  if (params.itemCount <= 0 || params.columns <= 0) return;

  const int cols = params.columns;
  const double availWidth = params.viewportWidth
                          - params.sectionInsetLeft
                          - params.sectionInsetRight;
  const double totalSpacing = params.columnSpacing * (cols - 1);
  const double itemWidth = (availWidth - totalSpacing) / cols;

  const bool fixedRow = params.rowHeight > 0;

  double y = params.sectionInsetTop;
  int col = 0;
  double rowMaxHeight = 0.0;
  int rowStart = 0;

  // First pass: compute frames, tracking row max heights
  struct CellFrame { double x, y, width, height; };
  std::vector<CellFrame> frames(params.itemCount);

  for (int i = 0; i < params.itemCount; ++i) {
    double itemHeight = fixedRow
      ? params.rowHeight
      : (i < static_cast<int>(params.itemHeights.size())
         ? params.itemHeights[i]
         : 44.0);

    double x = params.sectionInsetLeft + col * (itemWidth + params.columnSpacing);
    frames[i] = { x, y, itemWidth, itemHeight };

    if (itemHeight > rowMaxHeight) rowMaxHeight = itemHeight;
    col++;

    if (col >= cols) {
      // End of row — align all items to rowMaxHeight
      if (!fixedRow) {
        for (int j = rowStart; j <= i; ++j) {
          frames[j].height = rowMaxHeight;
        }
      }
      y += rowMaxHeight + params.rowSpacing;
      col = 0;
      rowMaxHeight = 0.0;
      rowStart = i + 1;
    }
  }

  // Handle last partial row
  if (col > 0) {
    if (!fixedRow) {
      for (int j = rowStart; j < params.itemCount; ++j) {
        frames[j].height = rowMaxHeight;
      }
    }
    y += rowMaxHeight;
  }

  y += params.sectionInsetBottom;

  // Write to cache
  for (int i = 0; i < params.itemCount; ++i) {
    std::string key;
    if (i < static_cast<int>(params.keys.size())) {
      key = params.keys[i];
    } else {
      key = (params.keyPrefix.empty() ? "grid-" : params.keyPrefix) + std::to_string(i);
    }

    LayoutAttributes attrs;
    attrs.key    = key;
    attrs.index  = i;
    attrs.frame  = { frames[i].x, frames[i].y, frames[i].width, frames[i].height };
    attrs.zIndex = 0;
    attrs.sizingState = SizingState::Measured;
    _cache->setAttributes(attrs);
  }
}

// ── JSI parameter extraction ──────────────────────────────────────────────────

GridLayoutParams GridLayout::paramsFromJSI(Runtime& rt, const Object& obj) {
  GridLayoutParams p;

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
  p.columnSpacing   = getDbl("columnSpacing", 0.0);
  p.rowSpacing      = getDbl("rowSpacing", 0.0);
  p.viewportWidth   = getDbl("viewportWidth", 390.0);
  p.sectionInsetTop    = getDbl("sectionInsetTop", 0);
  p.sectionInsetBottom = getDbl("sectionInsetBottom", 0);
  p.sectionInsetLeft   = getDbl("sectionInsetLeft", 0);
  p.sectionInsetRight  = getDbl("sectionInsetRight", 0);
  p.rowHeight       = getDbl("rowHeight", 0.0);

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

void GridLayout::installJSIBindings(Runtime& rt, Object& target) {
  // computeGridLayout(params) → { positions: number[], contentHeight: number }
  // positions is a flat array: [x0, y0, w0, h0, x1, y1, w1, h1, ...]
  target.setProperty(rt, "computeGridLayout",
    Function::createFromHostFunction(rt,
      PropNameID::forAscii(rt, "computeGridLayout"), 1,
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
