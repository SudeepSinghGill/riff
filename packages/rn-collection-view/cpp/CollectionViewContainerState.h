#pragma once

/**
 * CollectionViewContainerState — shared state between ShadowNode and native view.
 *
 * The ShadowNode writes layout corrections during layout().
 * The native iOS view reads them in updateState: to apply positions
 * and scroll offset corrections.
 *
 * State is immutable per revision — updates create new instances.
 */

#include <react/renderer/graphics/Float.h>
#include <react/renderer/graphics/Geometry.h>
#include <vector>
#include <unordered_map>

#ifdef RN_SERIALIZABLE_STATE
#include <folly/dynamic.h>
#endif

namespace facebook::react {

struct StickyHeaderInfo {
  int32_t section = 0;
  Float pinnedOffset = 0;
  Float naturalOffset = 0;
  bool isPinned = false;
};

class CollectionViewContainerState final {
 public:
  CollectionViewContainerState() = default;

  CollectionViewContainerState(
      Size contentSize,
      Point contentOffset,
      Rect contentBoundingRect,
      std::vector<Float> positions,
      Float contentOffsetCorrectionY,
      int32_t layoutRevision)
      : contentSize(contentSize),
        contentOffset(contentOffset),
        contentBoundingRect(contentBoundingRect),
        positions(std::move(positions)),
        contentOffsetCorrectionY(contentOffsetCorrectionY),
        layoutRevision(layoutRevision) {}

  // ── Scroll container state (superset of ScrollViewState) ──────────

  /// Total scrollable content size.
  Size contentSize{};

  /// Current scroll position (updated by native view via state update).
  Point contentOffset{};

  /// Bounding rect of all mounted children.
  Rect contentBoundingRect{};

  // ── Layout correction state ───────────────────────────────────────

  /// Flat array [x0,y0,w0,h0, x1,y1,w1,h1, ...] for mounted children.
  /// Written by ShadowNode::layout(), read by native view for positioning.
  std::vector<Float> positions;

  /// Scroll offset correction delta. When items above viewport change
  /// height, native view applies this to UIScrollView.contentOffset.y.
  Float contentOffsetCorrectionY = 0;

  /// Bumped on every layout correction. Native view uses to detect changes.
  int32_t layoutRevision = 0;

  // ── Window state ──────────────────────────────────────────────────

  int32_t renderFirst = 0;
  int32_t renderLast = -1;
  int32_t visibleFirst = 0;
  int32_t visibleLast = -1;

  // ── Measurement cache ─────────────────────────────────────────────

  /// Per-item measured heights. Keyed by item index in full data set.
  /// Persists across render range changes — once measured, cached.
  std::unordered_map<int32_t, Float> measuredHeights;

  // ── Supplementary ─────────────────────────────────────────────────

  std::vector<StickyHeaderInfo> stickyHeaders;

  // ── Helpers ───────────────────────────────────────────────────────

  Size getContentSize() const {
    return contentSize;
  }

#ifdef RN_SERIALIZABLE_STATE
  folly::dynamic getDynamic() const;
#endif
};

} // namespace facebook::react
