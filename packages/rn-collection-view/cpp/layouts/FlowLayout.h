#pragma once

#include "../LayoutCache.h"
#include "../LayoutEngine.h"
#include <jsi/jsi.h>
#include <memory>
#include <string>
#include <vector>

namespace rncv {

struct FlowLayoutParams {
  int itemCount = 0;
  double itemSpacing = 0.0;
  double lineSpacing = 0.0;
  double viewportWidth = 390.0;
  double sectionInsetTop = 0;
  double sectionInsetBottom = 0;
  double sectionInsetLeft = 0;
  double sectionInsetRight = 0;
  std::vector<double> itemWidths;        // per-item widths (required)
  std::vector<double> itemHeights;       // per-item heights (required)
  std::vector<std::string> keys;         // per-item identity keys
  std::string keyPrefix;                 // fallback key prefix: "flow-"
};

class FlowLayout : public LayoutEngine {
public:
  explicit FlowLayout(std::shared_ptr<LayoutCache> cache);

  /**
   * Compute flow layout: pack items left-to-right, wrap to next line when full.
   * Line height = tallest item in that line.
   * Writes LayoutAttributes to the shared LayoutCache.
   */
  void compute(const FlowLayoutParams& params);

  // ── LayoutEngine protocol ──────────────────────────────────────────────

  bool applyMeasurements(
      const std::vector<MeasurementDelta>& deltas,
      LayoutCache& cache) override;

  ContentDimension contentDeterminedDimension() const override {
    return ContentDimension::Both;
  }

  void installJSIBindings(facebook::jsi::Runtime& rt, facebook::jsi::Object& target);

private:
  std::shared_ptr<LayoutCache> _cache;

  static FlowLayoutParams paramsFromJSI(facebook::jsi::Runtime& rt,
                                        const facebook::jsi::Object& obj);
};

} // namespace rncv
