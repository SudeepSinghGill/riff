#pragma once
#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

// For views whose Fabric-assigned origin should be preserved when the
// ShadowNode (applyPositionsFromState) is the position authority.
// When shadowNodePositioned = YES, the interceptor overrides Fabric's Yoga
// origin with the current native origin. Size comes from Yoga unchanged.
@protocol RNExternallyPositioned <NSObject>
@property (nonatomic, assign) BOOL shadowNodePositioned;
@end

// Extends RNExternallyPositioned for views that require BOTH origin AND size
// preserved once positioned, plus sub-pixel size hysteresis before positioning.
// Used by sub-container wrappers (RNCollectionSubContainerView).
@protocol RNFullFrameExternallyPositioned <RNExternallyPositioned>
@end

// For views that always override their Fabric-assigned origin with a value
// derived from the LayoutCache or a transform-adjusted center calculation.
// Used by sticky/push header views (RNScrollCoordinatedViewView).
@protocol RNCacheBasedOrigin <NSObject>
// Returns the authoritative native origin for use in updateLayoutMetrics:.
// Called on the UI thread inside the Fabric commit.
- (CGPoint)cacheBasedOrigin;
@end

// For container views that redirect Fabric child mounts to an inner content
// view rather than inserting into self directly.
@protocol RNContentViewProvider <NSObject>
@property (nonatomic, readonly) UIView *contentViewForChildMounting;
@end

// Result of decisionForView:incomingFrame:.
// adjustedFrame is always valid: equals incomingFrame when no override applies.
typedef struct {
  CGRect adjustedFrame;
} RNLayoutInterceptDecision;

// B1.14 — Centralises the "creative Fabric override" logic shared across
// Riff's four updateLayoutMetrics: sites and three mountChildComponentView:
// sites. A single file to update if Fabric's override API surface changes.
@interface RNFabricLayoutInterceptor : NSObject

// Returns the frame that should be forwarded to [super updateLayoutMetrics:].
// When no override applies, adjustedFrame == incomingFrame (no-op).
//
// Dispatch table (checked in order):
//   RNCacheBasedOrigin          → always overrides origin via cacheBasedOrigin
//   RNFullFrameExternallyPositioned → full frame when positioned; size hysteresis before
//   RNExternallyPositioned      → origin-only when positioned; pass-through before
//   (no protocol)               → pass-through
+ (RNLayoutInterceptDecision)decisionForView:(UIView *)view
                               incomingFrame:(CGRect)incomingFrame;

// Returns the UIView target for inserting a Fabric child.
// If container conforms to RNContentViewProvider, returns contentViewForChildMounting.
// Otherwise returns defaultTarget unchanged.
+ (UIView *)mountTargetForChild:(UIView *)child
                    inContainer:(UIView *)container
                  defaultTarget:(UIView *)defaultTarget;

@end

NS_ASSUME_NONNULL_END
