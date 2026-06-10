/**
 * CollectionSubContainerState — serialization for Android Fabric state bridge.
 *
 * Serializes per-child visual states (frame, opacity, zIndex, transform) into
 * a folly::dynamic object that Android's StateWrapper delivers to Kotlin.
 */

#include "CollectionSubContainerState.h"

#ifdef RN_SERIALIZABLE_STATE
#include <folly/dynamic.h>

namespace facebook::react {

CollectionSubContainerState::CollectionSubContainerState(
    const CollectionSubContainerState &previousState,
    folly::dynamic data)
    : contentSize(previousState.contentSize),
      contentBoundingRect(previousState.contentBoundingRect),
      children(previousState.children),
      childTags(previousState.childTags),
      layoutRevision(previousState.layoutRevision) {
  // No Java → C++ state updates expected for sub-containers.
}

folly::dynamic CollectionSubContainerState::getDynamic() const {
  // Per-child visual state — flat arrays for efficient JNI transfer.
  folly::dynamic xs = folly::dynamic::array;
  folly::dynamic ys = folly::dynamic::array;
  folly::dynamic ws = folly::dynamic::array;
  folly::dynamic hs = folly::dynamic::array;
  folly::dynamic opacities = folly::dynamic::array;
  folly::dynamic zIndexes = folly::dynamic::array;

  for (const auto &child : children) {
    xs.push_back((double)child.x);
    ys.push_back((double)child.y);
    ws.push_back((double)child.w);
    hs.push_back((double)child.h);
    opacities.push_back((double)child.opacity);
    zIndexes.push_back((double)child.zIndex);
  }

  folly::dynamic tagsArray = folly::dynamic::array;
  for (auto t : childTags) {
    tagsArray.push_back(t);
  }

  return folly::dynamic::object
      ("contentWidth", (double)contentSize.width)
      ("contentHeight", (double)contentSize.height)
      ("childX", std::move(xs))
      ("childY", std::move(ys))
      ("childW", std::move(ws))
      ("childH", std::move(hs))
      ("childOpacity", std::move(opacities))
      ("childZIndex", std::move(zIndexes))
      ("childTags", std::move(tagsArray))
      ("layoutRevision", layoutRevision);
}

} // namespace facebook::react

#endif // RN_SERIALIZABLE_STATE
