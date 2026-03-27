#include "FlowLayout.h"
#include <algorithm>

namespace rncv {

using namespace facebook::jsi;

FlowLayout::FlowLayout(std::shared_ptr<LayoutCache> cache)
    : _cache(std::move(cache)) {}

void FlowLayout::compute(const FlowLayoutParams& params) {
  _cache->clear();

  if (params.itemCount <= 0) return;

  const double availWidth = params.viewportWidth
                          - params.sectionInsetLeft
                          - params.sectionInsetRight;

  struct CellFrame { double x, y, width, height; };
  std::vector<CellFrame> frames(params.itemCount);

  double x = params.sectionInsetLeft;
  double y = params.sectionInsetTop;
  double rowMaxHeight = 0.0;
  int rowStart = 0;

  for (int i = 0; i < params.itemCount; ++i) {
    double itemWidth = (i < static_cast<int>(params.itemWidths.size()))
                     ? params.itemWidths[i]
                     : 100.0;
    double itemHeight = (i < static_cast<int>(params.itemHeights.size()))
                      ? params.itemHeights[i]
                      : 44.0;

    // Clamp width to available space
    if (itemWidth > availWidth) itemWidth = availWidth;

    // Does this item fit in the current row?
    double widthNeeded = (x > params.sectionInsetLeft)
                       ? params.itemSpacing + itemWidth
                       : itemWidth;
    double usedWidth = x - params.sectionInsetLeft;

    if (usedWidth + widthNeeded > availWidth && x > params.sectionInsetLeft) {
      // Wrap to next line — finalize current row heights
      for (int j = rowStart; j < i; ++j) {
        frames[j].height = rowMaxHeight;
      }
      y += rowMaxHeight + params.lineSpacing;
      x = params.sectionInsetLeft;
      rowMaxHeight = 0.0;
      rowStart = i;
    }

    if (x > params.sectionInsetLeft) x += params.itemSpacing;

    frames[i] = { x, y, itemWidth, itemHeight };
    if (itemHeight > rowMaxHeight) rowMaxHeight = itemHeight;
    x += itemWidth;
  }

  // Finalize last row
  if (rowStart < params.itemCount) {
    for (int j = rowStart; j < params.itemCount; ++j) {
      frames[j].height = rowMaxHeight;
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
      key = (params.keyPrefix.empty() ? "flow-" : params.keyPrefix) + std::to_string(i);
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

FlowLayoutParams FlowLayout::paramsFromJSI(Runtime& rt, const Object& obj) {
  FlowLayoutParams p;

  auto getInt = [&](const char* name, int def) -> int {
    auto v = obj.getProperty(rt, name);
    return v.isNumber() ? static_cast<int>(v.getNumber()) : def;
  };
  auto getDbl = [&](const char* name, double def) -> double {
    auto v = obj.getProperty(rt, name);
    return v.isNumber() ? v.getNumber() : def;
  };

  p.itemCount       = getInt("itemCount", 0);
  p.itemSpacing     = getDbl("itemSpacing", 0.0);
  p.lineSpacing     = getDbl("lineSpacing", 0.0);
  p.viewportWidth   = getDbl("viewportWidth", 390.0);
  p.sectionInsetTop    = getDbl("sectionInsetTop", 0);
  p.sectionInsetBottom = getDbl("sectionInsetBottom", 0);
  p.sectionInsetLeft   = getDbl("sectionInsetLeft", 0);
  p.sectionInsetRight  = getDbl("sectionInsetRight", 0);

  // itemWidths: number[]
  auto wv = obj.getProperty(rt, "itemWidths");
  if (wv.isObject()) {
    auto arr = wv.asObject(rt).asArray(rt);
    size_t len = arr.length(rt);
    p.itemWidths.resize(len);
    for (size_t i = 0; i < len; ++i) {
      p.itemWidths[i] = arr.getValueAtIndex(rt, i).getNumber();
    }
  }

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

void FlowLayout::installJSIBindings(Runtime& rt, Object& target) {
  // computeFlowLayout(params) → { positions: number[], contentHeight: number }
  // positions is a flat array: [x0, y0, w0, h0, x1, y1, w1, h1, ...]
  target.setProperty(rt, "computeFlowLayout",
    Function::createFromHostFunction(rt,
      PropNameID::forAscii(rt, "computeFlowLayout"), 1,
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
