#include "CollectionViewContainerShadowNode.h"
#include "CollectionViewModule.h"
#include "LayoutEngine.h"

#include <algorithm>
#include <cmath>
#include <yoga/YGNode.h>
#include <react/debug/react_native_assert.h>

// Cross-platform logging for ShadowNode (runs on background thread).
// Active only in DEBUG builds; no-op in release.
#if DEBUG
  #ifdef __APPLE__
    #include <os/log.h>
    #define RNCV_SN_LOG(fmt, ...) os_log_info(os_log_create("com.rncv", "shadownode"), "[RNCV-SN] " fmt, ##__VA_ARGS__)
  #else
    #include <android/log.h>
    #define RNCV_SN_LOG(fmt, ...) __android_log_print(ANDROID_LOG_INFO, "RNCV-SN", fmt, ##__VA_ARGS__)
  #endif
#else
  #define RNCV_SN_LOG(fmt, ...) ((void)0)
#endif

namespace facebook::react {

// Must match the string used in codegenNativeComponent('RNCollectionViewContainer').
const char CollectionViewContainerComponentName[] = "RNCollectionViewContainer";

void CollectionViewContainerShadowNode::layout(LayoutContext layoutContext) {
  RNCV_SN_LOG("layout() BEGIN");

  // Step 1: Call parent layout — Yoga computes child dimensions.
  ConcreteViewShadowNode::layout(layoutContext);

  // Step 2: Read Yoga-computed heights, compute correct positions.
  correctChildPositionsIfNeeded();

  // Step 3: Update state with positions and content size.
  updateStateIfNeeded();

  RNCV_SN_LOG("layout() END children=%zu contentH=%.1f",
              getLayoutableChildNodes().size(), correctedContentHeight_);
}

void CollectionViewContainerShadowNode::correctChildPositionsIfNeeded() {
  const auto& props =
      *std::static_pointer_cast<const RNCollectionViewContainerProps>(getProps());
  const auto children = getLayoutableChildNodes();

  hadMeasurementDeltas_ = false;

  if (children.empty()) {
    correctedContentHeight_ = 0;
    correctedPositions_.clear();
    return;
  }

  const auto renderRangeStart = props.renderRangeStart;
  const auto estimatedItemHeight = props.estimatedItemHeight;
  const auto containerWidth = getLayoutMetrics().frame.size.width;

  // Look up the shared LayoutCache and LayoutEngine via static registry.
  auto cache = CollectionViewModule::getLayoutCacheForId(props.layoutCacheId);

  // Determine layout type string from the enum prop.
  // codegen generates: 0=list, 1=grid, 2=masonry, 3=flow
  std::string layoutType = "list";
  switch (props.layoutType) {
    case RNCollectionViewContainerLayoutType::Grid:    layoutType = "grid";    break;
    case RNCollectionViewContainerLayoutType::Masonry: layoutType = "masonry"; break;
    case RNCollectionViewContainerLayoutType::Flow:    layoutType = "flow";    break;
    default: break;
  }

  auto engine = CollectionViewModule::getLayoutEngineForId(
      props.layoutCacheId, layoutType);

  // Key prefix: must match what the JS layout engine wrote to cache.
  std::string keyPrefix = "item-0-";
  if (layoutType == "grid")    keyPrefix = "grid-";
  else if (layoutType == "masonry") keyPrefix = "masonry-";
  else if (layoutType == "flow")    keyPrefix = "flow-";

  RNCV_SN_LOG("correctPositions cacheId=%d cache=%s engine=%s layout=%s renderStart=%d children=%zu containerW=%.1f",
              props.layoutCacheId, cache ? "YES" : "NO", engine ? "YES" : "NO",
              layoutType.c_str(), renderRangeStart, children.size(), containerWidth);

  // ── Phase 1: Read positions from cache for each mounted child ──────────
  //
  // The layout engine has already written complete LayoutAttributes to the cache.
  // ShadowNode is layout-agnostic — it reads whatever the cache has.
  // Fallback to estimatedItemHeight only when cache has no entry.

  correctedPositions_.clear();
  correctedPositions_.reserve(children.size() * 4);

  for (size_t i = 0; i < children.size(); ++i) {
    const auto dataIndex = renderRangeStart + static_cast<int32_t>(i);
    const auto key = keyPrefix + std::to_string(dataIndex);

    Float effectiveX = 0;
    Float effectiveY = 0;
    Float effectiveWidth = containerWidth;
    Float effectiveHeight = estimatedItemHeight;

    if (cache) {
      auto cached = cache->getAttributes(key);
      if (cached) {
        effectiveX = static_cast<Float>(cached->frame.x);
        effectiveY = static_cast<Float>(cached->frame.y);
        if (cached->frame.width > 0) {
          effectiveWidth = static_cast<Float>(cached->frame.width);
        }
        if (cached->frame.height > 0) {
          effectiveHeight = static_cast<Float>(cached->frame.height);
        }
      }
    }

    correctedPositions_.push_back(effectiveX);
    correctedPositions_.push_back(effectiveY);
    correctedPositions_.push_back(effectiveWidth);
    correctedPositions_.push_back(effectiveHeight);

    // Log first 5 children: cache position vs what we'll store
    if (i < 5) {
      const auto& childMetrics = children[i]->getLayoutMetrics();
      RNCV_SN_LOG("  child[%zu] key=%s cache=(%.1f,%.1f,%.1f,%.1f) yoga=(%.1f,%.1f,%.1f,%.1f)",
                  i, key.c_str(),
                  effectiveX, effectiveY, effectiveWidth, effectiveHeight,
                  childMetrics.frame.origin.x, childMetrics.frame.origin.y,
                  childMetrics.frame.size.width, childMetrics.frame.size.height);
    }
  }

  // ── Phase 2: Diff Yoga measurements vs cache, build deltas ─────────────
  //
  // After Yoga runs, compare Yoga-measured content-determined dimensions
  // against what the cache had. Collect deltas for the layout engine.

  auto contentDim = engine
      ? engine->contentDeterminedDimension()
      : rncv::ContentDimension::Height;

  std::vector<rncv::MeasurementDelta> deltas;

  for (size_t i = 0; i < children.size(); ++i) {
    const auto& childMetrics = children[i]->getLayoutMetrics();
    const auto yogaHeight = childMetrics.frame.size.height;
    const auto yogaWidth = childMetrics.frame.size.width;

    const auto dataIndex = renderRangeStart + static_cast<int32_t>(i);
    const auto key = keyPrefix + std::to_string(dataIndex);

    // Check height delta (for Height and Both).
    if (contentDim == rncv::ContentDimension::Height ||
        contentDim == rncv::ContentDimension::Both) {
      Float cachedHeight = correctedPositions_[i * 4 + 3];
      if (yogaHeight > 0 && std::abs(yogaHeight - cachedHeight) > 0.5f) {
        deltas.push_back({key, dataIndex, cachedHeight, yogaHeight});
        // Update our local positions immediately with Yoga measurement.
        correctedPositions_[i * 4 + 3] = yogaHeight;
      }
    }

    // TODO: Check width delta for Flow layout (ContentDimension::Both/Width).
    // For now, width deltas are deferred — flow layout handles them via
    // applyMeasurements full re-layout.
  }

  // ── Phase 3: Apply deltas via layout engine ────────────────────────────
  //
  // If there are deltas, ask the layout engine to cascade position changes.
  // The engine updates the cache in-place. We re-read affected positions.

  if (!deltas.empty() && engine && cache) {
    hadMeasurementDeltas_ = true;
    RNCV_SN_LOG("applyMeasurements: %zu deltas (first: key=%s old=%.1f new=%.1f)",
                deltas.size(), deltas[0].key.c_str(), deltas[0].oldValue, deltas[0].newValue);
    bool handled = engine->applyMeasurements(deltas, *cache);

    if (handled) {
      // Re-read all positions from cache (engine may have cascaded changes).
      for (size_t i = 0; i < children.size(); ++i) {
        const auto dataIndex = renderRangeStart + static_cast<int32_t>(i);
        const auto key = keyPrefix + std::to_string(dataIndex);
        auto cached = cache->getAttributes(key);
        if (cached) {
          correctedPositions_[i * 4 + 0] = static_cast<Float>(cached->frame.x);
          correctedPositions_[i * 4 + 1] = static_cast<Float>(cached->frame.y);
          correctedPositions_[i * 4 + 2] = static_cast<Float>(cached->frame.width);
          correctedPositions_[i * 4 + 3] = static_cast<Float>(cached->frame.height);
        }
      }
    }
    // If !handled (JS custom layout), we keep the stale Y positions for one frame.
    // The JS layout will recompute on the next tick.
  } else if (!deltas.empty() && cache) {
    // No engine available — just write Yoga measurements back to cache directly.
    for (const auto& d : deltas) {
      auto cached = cache->getAttributes(d.key);
      if (cached) {
        auto updated = *cached;
        updated.frame.height = d.newValue;
        updated.sizingState = rncv::SizingState::Measured;
        cache->setAttributes(updated);
      }
    }
  }

  // ── Phase 4: Compute content height from cache ─────────────────────────

  if (cache) {
    auto contentSize = cache->getTotalContentSize();
    correctedContentHeight_ = static_cast<Float>(contentSize.height);
  } else {
    // Fallback: compute from positions.
    Float maxBottom = 0;
    for (size_t i = 0; i < children.size(); ++i) {
      Float bottom = correctedPositions_[i * 4 + 1] + correctedPositions_[i * 4 + 3];
      if (bottom > maxBottom) maxBottom = bottom;
    }
    correctedContentHeight_ = maxBottom;
  }
}

Float CollectionViewContainerShadowNode::computeOffsetCorrection(
    Float scrollY,
    const std::vector<Float>& oldPositions,
    const std::vector<Float>& newPositions,
    size_t childCount) const {
  // Phase 4: Compute scroll offset correction.
  //
  // When content above the viewport changes (insert, remove, resize),
  // adjust scrollY so visible content doesn't jump.
  //
  // Strategy: find the first visible item in the OLD layout, locate it
  // in the NEW layout, and return the Y delta.
  //
  // For resize (same item count): same index maps to same item.
  // For insert/remove: child count changes by k. Assuming mutations are
  // at a single point, the old item at index F is now at index F+k in
  // the new layout. This is correct for prepend/append; for scattered
  // mutations, production will use item keys passed via props.

  const auto oldCount = oldPositions.size() / 4;
  const auto newCount = newPositions.size() / 4;

  if (oldCount == 0 || newCount == 0) return 0;

  // Find first visible item in old layout (first whose bottom > scrollY).
  size_t oldFirstVisible = oldCount; // sentinel
  for (size_t i = 0; i < oldCount; ++i) {
    const auto y = oldPositions[i * 4 + 1];
    const auto h = oldPositions[i * 4 + 3];
    if (y + h > scrollY) {
      oldFirstVisible = i;
      break;
    }
  }
  if (oldFirstVisible >= oldCount) return 0;

  const auto oldY = oldPositions[oldFirstVisible * 4 + 1];

  // Case 1: Same child count (resize only). Same index = same item.
  if (oldCount == newCount) {
    const auto newY = newPositions[oldFirstVisible * 4 + 1];
    return newY - oldY;
  }

  // Case 2: Child count changed (insert or remove).
  // Check if the item at the same index moved — if not, change was below viewport.
  if (oldFirstVisible < newCount) {
    const auto sameIdxY = newPositions[oldFirstVisible * 4 + 1];
    if (std::abs(sameIdxY - oldY) < 0.5f) {
      // Item at same index didn't move — mutation was below viewport.
      return 0;
    }
  }

  // Item at same index DID move — mutation was above viewport.
  // The old first-visible item shifted by countDelta indices.
  const auto countDelta = static_cast<int64_t>(newCount) - static_cast<int64_t>(oldCount);
  const auto newIdx = static_cast<int64_t>(oldFirstVisible) + countDelta;

  if (newIdx < 0 || newIdx >= static_cast<int64_t>(newCount)) return 0;

  const auto newY = newPositions[newIdx * 4 + 1];
  return newY - oldY;
}

void CollectionViewContainerShadowNode::updateStateIfNeeded() {
  auto state = getStateData();

  const auto containerWidth = getLayoutMetrics().frame.size.width;
  auto contentSize = Size{containerWidth, correctedContentHeight_};

  // Compute content bounding rect from corrected positions.
  auto contentBoundingRect = Rect{};
  const auto childCount = correctedPositions_.size() / 4;
  for (size_t i = 0; i < childCount; ++i) {
    auto x = correctedPositions_[i * 4];
    auto y = correctedPositions_[i * 4 + 1];
    auto w = correctedPositions_[i * 4 + 2];
    auto h = correctedPositions_[i * 4 + 3];
    contentBoundingRect.unionInPlace(Rect{Point{x, y}, Size{w, h}});
  }

  bool changed = false;

  if (state.contentSize != contentSize) {
    state.contentSize = contentSize;
    changed = true;
  }

  if (state.contentBoundingRect != contentBoundingRect) {
    state.contentBoundingRect = contentBoundingRect;
    changed = true;
  }

  if (state.positions != correctedPositions_) {
    // Only compute offset correction when measurement deltas actually
    // changed item positions (e.g., Yoga measured a different height).
    // A render-range shift alone doesn't mean content moved — the same
    // items are at the same absolute positions, just a different window.
    if (hadMeasurementDeltas_) {
      const auto& oldPositions = state.positions;
      // Read scroll offset from LayoutCache (written by native view)
      // instead of state (which would trigger stale Fabric commits).
      const auto& props2 =
          *std::static_pointer_cast<const RNCollectionViewContainerProps>(getProps());
      auto scrollCache = CollectionViewModule::getLayoutCacheForId(props2.layoutCacheId);
      Float scrollY = 0;
      if (scrollCache) {
        auto scrollOffset = scrollCache->getScrollOffset();
        scrollY = static_cast<Float>(scrollOffset.y);
      }
      const auto oldChildCount = oldPositions.size() / 4;

      if (scrollY > 0 && oldChildCount > 0) {
        const auto minCount = std::min(childCount, oldChildCount);
        auto correction = computeOffsetCorrection(
            scrollY, oldPositions, correctedPositions_, minCount);
        if (std::abs(correction) > 0.5f) {
          state.contentOffsetCorrectionY = correction;
          RNCV_SN_LOG("offset correction=%.1f scrollY=%.1f", correction, scrollY);
        } else {
          state.contentOffsetCorrectionY = 0;
        }
      } else {
        state.contentOffsetCorrectionY = 0;
      }
    } else {
      state.contentOffsetCorrectionY = 0;
    }

    state.positions = correctedPositions_;
    state.layoutRevision++;
    changed = true;
  } else {
    // Positions unchanged — clear any previous correction.
    if (state.contentOffsetCorrectionY != 0) {
      state.contentOffsetCorrectionY = 0;
      changed = true;
    }
  }

  if (changed) {
    setStateData(std::move(state));
  }
}

} // namespace facebook::react
