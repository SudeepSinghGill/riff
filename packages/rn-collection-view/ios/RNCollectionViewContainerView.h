#pragma once

#import <React/RCTViewComponentView.h>
#import <UIKit/UIKit.h>

#import "RNFabricLayoutInterceptor.h"

NS_ASSUME_NONNULL_BEGIN

/**
 * RNCollectionViewContainerView — native Fabric view for the collection container.
 *
 * Owns an internal UIScrollView. The ShadowNode handles child positioning
 * via layout() override. This view reads CollectionViewContainerState to:
 * - Set UIScrollView.contentSize
 * - Apply scroll offset corrections when items above viewport change height
 * - Forward scroll events back to JS (for render range computation)
 *
 * Phase 1: Basic UIScrollView wrapper, children rendered inside scroll content.
 */
@interface RNCollectionViewContainerView : RCTViewComponentView <UIScrollViewDelegate, RNContentViewProvider>

@end

NS_ASSUME_NONNULL_END
