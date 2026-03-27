#pragma once

#include "../LayoutCache.h"
#include <jsi/jsi.h>
#include <memory>
#include <string>
#include <vector>

namespace rncv {

struct GridLayoutParams {
  int itemCount = 0;
  int columns = 2;
  double columnSpacing = 0.0;
  double rowSpacing = 0.0;
  double viewportWidth = 390.0;
  double sectionInsetTop = 0;
  double sectionInsetBottom = 0;
  double sectionInsetLeft = 0;
  double sectionInsetRight = 0;
  double rowHeight = 0.0;                // fixed row height (0 = dynamic)
  std::vector<double> itemHeights;       // per-item heights (for dynamic rows)
  std::vector<std::string> keys;         // per-item identity keys
  std::string keyPrefix;                 // fallback key prefix: "grid-"
};

class GridLayout {
public:
  explicit GridLayout(std::shared_ptr<LayoutCache> cache);

  /**
   * Compute grid layout: fixed columns, row-aligned heights.
   * Items placed left-to-right. Row height = tallest in row (or fixed rowHeight).
   * Writes LayoutAttributes to the shared LayoutCache.
   */
  void compute(const GridLayoutParams& params);

  void installJSIBindings(facebook::jsi::Runtime& rt, facebook::jsi::Object& target);

private:
  std::shared_ptr<LayoutCache> _cache;

  static GridLayoutParams paramsFromJSI(facebook::jsi::Runtime& rt,
                                        const facebook::jsi::Object& obj);
};

} // namespace rncv
