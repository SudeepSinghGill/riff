#include "CollectionSubContainerShadowNode.h"
#include "CollectionViewModule.h"
#include "LayoutEngine.h"

#include <algorithm>
#include <cmath>
#include <yoga/YGNode.h>

// Cross-platform debug logging for the sub-container ShadowNode.
// Active only in DEBUG builds; no-op in release.
#ifndef RNCV_ENABLE_NATIVE_LOGS
#define RNCV_ENABLE_NATIVE_LOGS 0
#endif

#if DEBUG && RNCV_ENABLE_NATIVE_LOGS
  #ifdef __APPLE__
    #include <cstdio>
    #define RNCV_SUB_LOG(fmt, ...) do { fprintf(stderr, "[RNCV-SUB] " fmt "\n", ##__VA_ARGS__); fflush(stderr); } while(0)
  #else
    #include <android/log.h>
    #define RNCV_SUB_LOG(fmt, ...) __android_log_print(ANDROID_LOG_INFO, "RNCV-SUB", fmt, ##__VA_ARGS__)
  #endif
#else
  #define RNCV_SUB_LOG(fmt, ...) ((void)0)
#endif

namespace facebook::react {

// Must match the string used in codegenNativeComponent('RNCollectionSubContainer').
const char CollectionSubContainerComponentName[] = "RNCollectionSubContainer";

void CollectionSubContainerShadowNode::layout(LayoutContext layoutContext) {
  RNCV_SUB_LOG("layout() BEGIN");

  // Step 1: Yoga sizes children.
  ConcreteViewShadowNode::layout(layoutContext);

  // Step 2: Read final visual state from cache, cascade Yoga deltas if any.
  correctChildPositionsIfNeeded();

  // Step 3: Update component state for the iOS view to consume.
  updateStateIfNeeded();

  RNCV_SUB_LOG("layout() END children=%zu contentW=%.1f contentH=%.1f",
               correctedChildren_.size(),
               correctedContentSize_.width,
               correctedContentSize_.height);
}

void CollectionSubContainerShadowNode::correctChildPositionsIfNeeded() {
  const auto& props =
      *std::static_pointer_cast<const RNCollectionSubContainerProps>(getProps());
  const auto children = getLayoutableChildNodes();

  correctedChildren_.clear();
  childTags_.clear();
  correctedBoundingRect_ = Rect{};

  if (children.empty()) {
    correctedContentSize_ = Size{};
    return;
  }

  auto cache = CollectionViewModule::getLayoutCacheForId(props.layoutCacheId);

  // Sub-containers in compositional layouts use the compositional engine for
  // measurement cascade — it knows how to dispatch deltas to the per-section
  // sub-layout. Standalone sub-containers can override this in future via a
  // new prop (e.g. sectionLayoutType) when needed.
  auto engine = CollectionViewModule::getLayoutEngineForId(
      props.layoutCacheId, "compositional");

  const size_t N = children.size();
  correctedChildren_.reserve(N);
  childTags_.reserve(N);

  // ── Phase 1: Extract cacheKeys from RNMeasuredCellProps ──────────────────
  // Direct children of a sub-container are RNMeasuredCell instances (cells).
  // Each cell carries its own cacheKey prop — we use that as the lookup key.
  std::vector<std::string> keys(N);
  std::vector<int32_t>     indices(N, -1);
  for (size_t i = 0; i < N; ++i) {
    if (auto p = std::dynamic_pointer_cast<const RNMeasuredCellProps>(
            children[i]->getProps())) {
      keys[i]    = p->cacheKey;
      indices[i] = p->index;
    }
  }

  // ── Phase 2: Bulk-read frames from cache ─────────────────────────────────
  rncv::BulkFrameResult bulkFrames;
  if (cache) {
    bulkFrames = cache->getFramesForKeys(keys);
  }

  // ── Phase 2b: Resolve Y-shift for compositional H-sections ───────────────
  // When this sub-container hosts a compositional H section, the cache stores
  // each cell's Y in V-absolute coordinates (after CompositionalLayout::
  // finalizeHSection shifts them by the section's contentCursorY). The cells
  // live INSIDE this sub-container's contentView, whose origin is the section's
  // top-left in scroll content space — so we must subtract the section Y to
  // get section-local Y for placement.
  //
  // Standalone use of the sub-container (RiffDemo H-2 tabs) has no compositional
  // wrapper entry → ySectionShift stays 0 → cache positions are used as-is
  // (those layouts already write section-local positions).
  Float ySectionShift = 0;
  if (cache) {
    auto sectionWrapperKey =
        std::string("h-section-wrapper-") + std::to_string(props.sectionIndex);
    auto sectionAttrs = cache->getAttributes(sectionWrapperKey);
    if (sectionAttrs) {
      ySectionShift = static_cast<Float>(sectionAttrs->frame.y);
    }
  }

  // ── Phase 3: Initialize correctedChildren_ from cache + Yoga deltas ──────
  // For dimensions that are content-determined (Yoga measures), build deltas
  // and apply via the layout engine. For layouts that govern all dimensions
  // (radial, spiral, hex), no deltas are needed.
  auto contentDim = engine
      ? engine->contentDeterminedDimension()
      : rncv::ContentDimension::Height;

  std::vector<rncv::MeasurementDelta> deltas;

  for (size_t i = 0; i < N; ++i) {
    ChildVisualState cv;

    // Initialize from cache. Subtract section Y so cells land inside the
    // sub-container's local coordinate space (origin at section top-left).
    if (cache && i < bulkFrames.found.size() && bulkFrames.found[i]) {
      cv.x = static_cast<Float>(bulkFrames.frames[i * 4 + 0]);
      cv.y = static_cast<Float>(bulkFrames.frames[i * 4 + 1]) - ySectionShift;
      cv.w = static_cast<Float>(bulkFrames.frames[i * 4 + 2]);
      cv.h = static_cast<Float>(bulkFrames.frames[i * 4 + 3]);
    }

    // Pull richer attributes (transform/opacity/zIndex) from cache when present.
    // These are written by scroll-driven layouts (radial, spiral, carousel3D)
    // via setAttributesBatch.
    if (cache && !keys[i].empty()) {
      auto attrs = cache->getAttributes(keys[i]);
      if (attrs) {
        cv.opacity = static_cast<Float>(attrs->alpha);
        cv.zIndex  = static_cast<Float>(attrs->zIndex);
        if (attrs->transform3D != rncv::kIdentityTransform3D) {
          for (size_t k = 0; k < 16; ++k) {
            cv.transform[k] = static_cast<Float>(attrs->transform3D[k]);
          }
          cv.hasTransform = true;
        }
      }
    }

    // Compare Yoga measurement against cache for content-determined axes.
    const auto& childMetrics = children[i]->getLayoutMetrics();
    const auto yogaWidth  = childMetrics.frame.size.width;
    const auto yogaHeight = childMetrics.frame.size.height;

    if ((contentDim == rncv::ContentDimension::Height ||
         contentDim == rncv::ContentDimension::Both) &&
        yogaHeight > 0 &&
        std::abs(yogaHeight - cv.h) > 0.5f &&
        !keys[i].empty()) {
      deltas.push_back({
        keys[i],
        indices[i],
        cv.h,
        yogaHeight,
        rncv::MeasurementAxis::Height,
      });
      cv.h = yogaHeight;
    }

    if ((contentDim == rncv::ContentDimension::Width ||
         contentDim == rncv::ContentDimension::Both) &&
        yogaWidth > 0 &&
        std::abs(yogaWidth - cv.w) > 0.5f &&
        !keys[i].empty()) {
      deltas.push_back({
        keys[i],
        indices[i],
        cv.w,
        yogaWidth,
        rncv::MeasurementAxis::Width,
      });
      cv.w = yogaWidth;
    }

    correctedChildren_.push_back(cv);
    childTags_.push_back(static_cast<int32_t>(children[i]->getTag()));
  }

  // ── Phase 4: Apply cascade and re-read final attributes ──────────────────
  if (!deltas.empty() && engine && cache) {
    cache->snapshotAnchorIfNeeded();
    bool handled = engine->applyMeasurements(deltas, *cache);
    RNCV_SUB_LOG("applyMeasurements deltas=%zu handled=%s",
                 deltas.size(), handled ? "YES" : "NO");

    if (handled) {
      // Re-read after cascade to pick up new positions for items downstream
      // of the deltas (later items shifted by height/width changes).
      // Apply the same V-absolute → section-local Y shift as the initial read.
      auto reread = cache->getFramesForKeys(keys);
      for (size_t i = 0; i < N; ++i) {
        if (i >= reread.found.size() || !reread.found[i]) continue;
        correctedChildren_[i].x = static_cast<Float>(reread.frames[i * 4 + 0]);
        correctedChildren_[i].y = static_cast<Float>(reread.frames[i * 4 + 1]) - ySectionShift;
        correctedChildren_[i].w = static_cast<Float>(reread.frames[i * 4 + 2]);
        correctedChildren_[i].h = static_cast<Float>(reread.frames[i * 4 + 3]);
      }
    }
  } else if (!deltas.empty() && cache) {
    // No engine — write Yoga measurements back to cache directly.
    for (const auto& d : deltas) {
      auto cached = cache->getAttributes(d.key);
      if (cached) {
        auto updated = *cached;
        if (d.axis == rncv::MeasurementAxis::Height) {
          updated.frame.height = d.newValue;
        } else if (d.axis == rncv::MeasurementAxis::Width) {
          updated.frame.width = d.newValue;
        }
        updated.sizingState = rncv::SizingState::Measured;
        cache->setAttributes(updated);
      }
    }
  }

  // ── Phase 5: Compute content size + bounding rect ────────────────────────
  //
  // Content size MUST be the layout-declared total extent (props.contentWidth /
  // contentHeight), NOT a max() with the windowed-children bounding rect.
  //
  // Why: the ShadowNode's correctedChildren_ contains ONLY the items currently
  // inside the H render window (windowing in handleHScroll excludes the rest).
  // If we let `maxRight` grow to the rightmost windowed child's edge, the
  // computed contentSize fluctuates as the window slides — UIScrollView re-
  // clamps `contentOffset` to the new bounds on every state commit, which
  // shows up as snap/cut during the bounce-back animation and gesture fight
  // when the user H-scrolls fast (H-3 amplifies via velocity-adaptive width;
  // H-3.5 amplifies further by mounting more cells).
  //
  // The layout authority (CompositionalLayout::finalizeHSection in C++ +
  // hSectionInfoFn in JS) is the single source of truth for the section's
  // full content extent. We trust it. If it's 0 (transient — first layout
  // pass before sections are sized), we fall back to children bounds purely
  // so the scrollview is functional, but this is a degenerate case.
  if (props.contentWidth > 0 || props.contentHeight > 0) {
    correctedContentSize_ = Size{props.contentWidth, props.contentHeight};
  } else {
    // Degenerate fallback only — props haven't been populated yet.
    Float maxRight = 0, maxBottom = 0;
    for (size_t i = 0; i < N; ++i) {
      const auto& cv = correctedChildren_[i];
      Float right  = cv.x + cv.w;
      Float bottom = cv.y + cv.h;
      if (right  > maxRight)  maxRight  = right;
      if (bottom > maxBottom) maxBottom = bottom;
    }
    correctedContentSize_ = Size{maxRight, maxBottom};
  }

  // Bounding rect (debug / hit-test) is still the union of child frames —
  // independent of contentSize and not surfaced to UIScrollView.
  for (size_t i = 0; i < N; ++i) {
    const auto& cv = correctedChildren_[i];
    correctedBoundingRect_.unionInPlace(Rect{
      Point{cv.x, cv.y},
      Size{cv.w, cv.h},
    });
  }
}

void CollectionSubContainerShadowNode::updateStateIfNeeded() {
  auto state = getStateData();

  bool changed = false;

  if (state.contentSize != correctedContentSize_) {
    state.contentSize = correctedContentSize_;
    changed = true;
  }

  if (state.contentBoundingRect != correctedBoundingRect_) {
    state.contentBoundingRect = correctedBoundingRect_;
    changed = true;
  }

  // Compare child arrays element-wise via ChildVisualState's operator!=.
  bool childrenChanged = (state.children.size() != correctedChildren_.size());
  if (!childrenChanged) {
    for (size_t i = 0; i < correctedChildren_.size(); ++i) {
      if (state.children[i] != correctedChildren_[i]) {
        childrenChanged = true;
        break;
      }
    }
  }

  if (childrenChanged || state.childTags != childTags_) {
    state.children   = correctedChildren_;
    state.childTags  = childTags_;
    state.layoutRevision++;
    changed = true;
  }

  if (changed) {
    setStateData(std::move(state));
  }
}

} // namespace facebook::react
