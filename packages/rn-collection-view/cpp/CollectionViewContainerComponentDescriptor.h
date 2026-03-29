#pragma once

/**
 * CollectionViewContainerComponentDescriptor — factory for our custom ShadowNode.
 *
 * This overrides the codegen-generated descriptor so Fabric creates our
 * CollectionViewContainerShadowNode (with layout() override and custom state)
 * instead of the default ConcreteViewShadowNode.
 */

#include <react/renderer/core/ConcreteComponentDescriptor.h>
#include "CollectionViewContainerShadowNode.h"

namespace facebook::react {

using CollectionViewContainerComponentDescriptor =
    ConcreteComponentDescriptor<CollectionViewContainerShadowNode>;

} // namespace facebook::react
