#import "RNFabricLayoutInterceptor.h"
#import <math.h>

@implementation RNFabricLayoutInterceptor

+ (RNLayoutInterceptDecision)decisionForView:(UIView *)view
                               incomingFrame:(CGRect)incomingFrame
{
  // 1. Cache-based origin (sticky/push headers) — always overrides origin.
  if ([view conformsToProtocol:@protocol(RNCacheBasedOrigin)]) {
    CGPoint origin = [(id<RNCacheBasedOrigin>)view cacheBasedOrigin];
    CGRect adjusted = incomingFrame;
    adjusted.origin = origin;
    return (RNLayoutInterceptDecision){ .adjustedFrame = adjusted };
  }

  // 2. Full-frame preservation with sub-pixel hysteresis (sub-container wrappers).
  if ([view conformsToProtocol:@protocol(RNFullFrameExternallyPositioned)]) {
    id<RNFullFrameExternallyPositioned> ep = (id<RNFullFrameExternallyPositioned>)view;
    if (ep.shadowNodePositioned) {
      // Parent has asserted full position authority — preserve exact native frame.
      return (RNLayoutInterceptDecision){ .adjustedFrame = view.frame };
    }
    // Before parent asserts authority: ignore sub-pixel size deltas to prevent
    // jitter on the first commit (C++ finalizeHSection has already stabilised
    // the height; real deltas are >= 1pt).
    if (view.frame.size.height > 0 && view.frame.size.width > 0) {
      CGRect adjusted = incomingFrame;
      if (fabs(incomingFrame.size.height - view.frame.size.height) < 0.5f)
        adjusted.size.height = view.frame.size.height;
      if (fabs(incomingFrame.size.width  - view.frame.size.width)  < 0.5f)
        adjusted.size.width  = view.frame.size.width;
      return (RNLayoutInterceptDecision){ .adjustedFrame = adjusted };
    }
    return (RNLayoutInterceptDecision){ .adjustedFrame = incomingFrame };
  }

  // 3. Origin-only preservation (cells and section wrappers).
  if ([view conformsToProtocol:@protocol(RNExternallyPositioned)]) {
    id<RNExternallyPositioned> ep = (id<RNExternallyPositioned>)view;
    if (ep.shadowNodePositioned) {
      // Preserve native origin set by applyPositionsFromState; let Yoga size through.
      CGRect adjusted = incomingFrame;
      adjusted.origin = view.frame.origin;
      return (RNLayoutInterceptDecision){ .adjustedFrame = adjusted };
    }
    return (RNLayoutInterceptDecision){ .adjustedFrame = incomingFrame };
  }

  return (RNLayoutInterceptDecision){ .adjustedFrame = incomingFrame };
}

+ (UIView *)mountTargetForChild:(UIView *)child
                    inContainer:(UIView *)container
                  defaultTarget:(UIView *)defaultTarget
{
  if ([container conformsToProtocol:@protocol(RNContentViewProvider)]) {
    return [(id<RNContentViewProvider>)container contentViewForChildMounting];
  }
  return defaultTarget;
}

@end
