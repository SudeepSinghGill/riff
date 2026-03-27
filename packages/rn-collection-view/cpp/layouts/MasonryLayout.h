#pragma once

#include "../LayoutCache.h"
#include <jsi/jsi.h>
#include <memory>
#include <string>
#include <vector>

namespace rncv {

struct MasonryLayoutParams {
  int itemCount = 0;
  int columns = 2;
  double columnSpacing = 8.0;
  double rowSpacing = 8.0;
  double viewportWidth = 390.0;
  double sectionInsetTop = 0;
  double sectionInsetBottom = 0;
  double sectionInsetLeft = 0;
  double sectionInsetRight = 0;
  std::vector<double> itemHeights; // per-item heights (required)
  std::vector<std::string> keys;   // per-item identity keys
  std::string keyPrefix;           // fallback key prefix: "masonry-"
};

class MasonryLayout {
public:
  explicit MasonryLayout(std::shared_ptr<LayoutCache> cache);

  /**
   * Compute masonry layout: place each item in the shortest column.
   * Writes LayoutAttributes to the shared LayoutCache.
   */
  void compute(const MasonryLayoutParams& params);

  void installJSIBindings(facebook::jsi::Runtime& rt, facebook::jsi::Object& target);

private:
  std::shared_ptr<LayoutCache> _cache;

  static MasonryLayoutParams paramsFromJSI(facebook::jsi::Runtime& rt,
                                           const facebook::jsi::Object& obj);
};

} // namespace rncv
