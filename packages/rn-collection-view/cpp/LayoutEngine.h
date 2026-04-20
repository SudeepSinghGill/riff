#pragma once

/**
 * LayoutEngine — abstract protocol for all layout types.
 *
 * Layout engines compute positions for all items and write complete
 * LayoutAttributes to the shared LayoutCache. The ShadowNode is
 * layout-agnostic — it reads the cache, lets Yoga measure, then asks
 * the layout engine to cascade any measurement deltas.
 *
 * Built-in implementations: ListLayout, GridLayout, MasonryLayout, FlowLayout.
 * JS custom layouts return false from applyMeasurements() — ShadowNode
 * accepts a one-frame delay for the JS layout to recompute.
 */

#include "LayoutCache.h"
#include <string>
#include <vector>

namespace rncv {

enum class MeasurementAxis {
  Unknown,
  Width,
  Height,
};

/// A single Yoga measurement that differs from the cache.
struct MeasurementDelta {
  std::string key;        // cache key of the measured item
  int         index;      // data index
  double      oldValue;   // value in cache before Yoga
  double      newValue;   // Yoga-measured value
  MeasurementAxis axis = MeasurementAxis::Unknown;
};

/// Which dimensions are determined by cell content (Yoga measures these).
/// The layout governs all other dimensions.
enum class ContentDimension {
  None,    // Layout governs everything (circular, radial)
  Height,  // Vertical list/grid/masonry: Yoga measures height
  Width,   // Horizontal list: Yoga measures width
  Both,    // Flow: Yoga measures both width and height
};

class LayoutEngine {
public:
  virtual ~LayoutEngine() = default;

  /**
   * Apply Yoga measurement deltas and recompute cascading positions.
   *
   * Called by ShadowNode after Yoga runs, when measured dimensions differ
   * from cache values. The engine should:
   *   1. Update the affected items' frames in cache
   *   2. Recompute positions of all items affected by the cascade
   *
   * Returns true if the cascade was handled (C++ layouts).
   * Returns false if it couldn't be handled (JS custom layouts) —
   * ShadowNode accepts stale positions for one frame.
   */
  virtual bool applyMeasurements(
      const std::vector<MeasurementDelta>& deltas,
      LayoutCache& cache) = 0;

  /**
   * Which dimensions are content-determined (Yoga should measure these).
   * ShadowNode uses this to know which Yoga results to write back to cache.
   */
  virtual ContentDimension contentDeterminedDimension() const = 0;
};

} // namespace rncv
