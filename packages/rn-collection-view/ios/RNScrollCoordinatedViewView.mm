#import "RNScrollCoordinatedViewView.h"

#import <react/renderer/components/RiffSpec/ComponentDescriptors.h>
#import <react/renderer/components/RiffSpec/EventEmitters.h>
#import <react/renderer/components/RiffSpec/Props.h>
#import <react/renderer/components/RiffSpec/RCTComponentViewHelpers.h>

#import <React/RCTFabricComponentsPlugins.h>

#include <memory>
#include <cstdint>
#include <string>

#include "LayoutCache.h"

namespace facebook::react {
  std::shared_ptr<rncv::LayoutCache> layoutCacheForId(int32_t cacheId);
}

using namespace facebook::react;

#ifndef RNCV_ENABLE_STICKY_TRACE
#define RNCV_ENABLE_STICKY_TRACE 0
#endif

#if DEBUG && RNCV_ENABLE_STICKY_TRACE
  #define RNCV_IOS_STICKY_LOG(...) NSLog(__VA_ARGS__)
#else
  #define RNCV_IOS_STICKY_LOG(...) ((void)0)
#endif

// String→enum for behavior prop.
static inline bool isPush(RNScrollCoordinatedViewBehavior b) {
  return b == RNScrollCoordinatedViewBehavior::Push;
}

@interface RNScrollCoordinatedViewView () <RCTRNScrollCoordinatedViewViewProtocol>
@end

@implementation RNScrollCoordinatedViewView {
  __weak UIScrollView *_parentScrollView;
  BOOL _observing;

  // Cached props — read on UI thread in KVO callback.
  CGFloat _boundaryY;
  CGFloat _boundaryX;
  CGFloat _headerHeight;
  BOOL    _isPush;
  BOOL    _enabled;
  BOOL    _isFooter;
  BOOL    _isHorizontal;
}

// ── Fabric registration ──────────────────────────────────────────────────────

+ (ComponentDescriptorProvider)componentDescriptorProvider
{
  return concreteComponentDescriptorProvider<RNScrollCoordinatedViewComponentDescriptor>();
}

// ── Init ─────────────────────────────────────────────────────────────────────

- (instancetype)initWithFrame:(CGRect)frame
{
  if (self = [super initWithFrame:frame]) {
    static const auto defaultProps = std::make_shared<const RNScrollCoordinatedViewProps>();
    _props       = defaultProps;
    _boundaryY   = CGFLOAT_MAX;
    _boundaryX   = CGFLOAT_MAX;
    _headerHeight = 0;
    _isPush      = YES;    // default behavior = push
    _enabled     = YES;
    _isFooter    = NO;
    _isHorizontal = NO;
    _observing   = NO;
  }
  return self;
}

- (void)dealloc
{
  [self _stopObserving];
}

// ── Layout metrics ────────────────────────────────────────────────────────────
//
// Fabric calls this when Yoga assigns a new frame to the view.  The base class
// sets center/bounds from the Yoga-computed natural position but does NOT touch
// layer.transform.  Yoga computes a sequential flex-column layout that is WRONG
// for collection view items — the correct position is set exclusively by the
// container's applyPositionsFromState.  We preserve the current native position
// (set by applyPositionsFromState) and only forward SIZE changes from Yoga.
// After adjusting the frame, re-apply the sticky transform so the correct
// visual position is established with the preserved naturalY.
//
// IMPORTANT: Sticky views have active transforms (sticky translation).
// self.frame.origin includes the transform (visual position), NOT the natural
// position.  We must compute the natural origin from center/bounds.

- (CGPoint)cacheBasedOrigin
{
  CGFloat naturalX = self.center.x - self.bounds.size.width * 0.5f;
  CGFloat naturalY = self.center.y - self.bounds.size.height * 0.5f;

  std::shared_ptr<rncv::LayoutCache> cache = _layoutCacheId != 0 ?
      facebook::react::layoutCacheForId(_layoutCacheId) : nullptr;

  if (cache && _props) {
    const auto &props = *std::static_pointer_cast<const facebook::react::RNScrollCoordinatedViewProps>(_props);
    auto cached = cache->getAttributes(props.cacheKey.empty() ?
        "item-" + std::to_string(props.index) + "-" + props.kind : props.cacheKey);
    if (cached) {
      naturalX = cached->frame.x;
      naturalY = cached->frame.y;
    }
  }

  return CGPointMake(naturalX, naturalY);
}

- (void)updateLayoutMetrics:(const facebook::react::LayoutMetrics &)layoutMetrics
           oldLayoutMetrics:(const facebook::react::LayoutMetrics &)oldLayoutMetrics
{
  CGRect incoming = CGRectMake(layoutMetrics.frame.origin.x, layoutMetrics.frame.origin.y,
                               layoutMetrics.frame.size.width, layoutMetrics.frame.size.height);
  RNLayoutInterceptDecision d = [RNFabricLayoutInterceptor decisionForView:self incomingFrame:incoming];
  auto adjusted = layoutMetrics;
  adjusted.frame.origin.x = d.adjustedFrame.origin.x;
  adjusted.frame.origin.y = d.adjustedFrame.origin.y;
  [super updateLayoutMetrics:adjusted oldLayoutMetrics:oldLayoutMetrics];
  [self _applyTransform];
}

// ── Props ─────────────────────────────────────────────────────────────────────

- (void)updateProps:(const Props::Shared &)props oldProps:(const Props::Shared &)oldProps
{
  [super updateProps:props oldProps:oldProps];

  const auto &newProps = *std::static_pointer_cast<const RNScrollCoordinatedViewProps>(props);

  _boundaryY    = newProps.boundaryY;
  _boundaryX    = newProps.boundaryX;
  _headerHeight = newProps.headerHeight;
  _isPush       = isPush(newProps.behavior);
  _enabled      = newProps.enabled;
  _isFooter     = (newProps.kind == "footer");
  _isHorizontal = newProps.horizontal;

  // Re-apply transform immediately with current scroll position.
  [self _applyTransform];
}

// ── View hierarchy — find parent UIScrollView ────────────────────────────────

- (void)didMoveToWindow
{
  [super didMoveToWindow];

  if (self.window) {
    [self _findAndObserveScrollView];
  } else {
    [self _stopObserving];
  }
}

- (void)didMoveToSuperview
{
  [super didMoveToSuperview];

  // Re-search if we're reparented (e.g. cell recycling, though we don't recycle).
  if (self.superview && self.window) {
    [self _findAndObserveScrollView];
  }
}

- (void)layoutSubviews
{
  [super layoutSubviews];

  // Fabric may assemble the view hierarchy after didMoveToWindow / didMoveToSuperview.
  // layoutSubviews is a reliable late hook — the hierarchy is fully connected by now.
  if (!_observing && self.window) {
    [self _findAndObserveScrollView];
  }

  // Recompute the sticky transform whenever our geometry changes.
  // applyPositionsFromState (in the container view) sets our bounds+center,
  // which marks us as needing layout and lands here.  Without this call,
  // the transform stays stale until the next scroll event fires KVO.
  [self _applyTransform];
}

// ── UIKit Geometry Interceptors ──────────────────────────────────────────────
//
// The ultimate source of truth for Collection View positions is NOT Yoga, but
// the C++ ListLayout engine. When a node changes size, the engine cascades new
// Y positions downward. The Container View's `applyPositionsFromState` method
// forces these new Y coordinates onto children by manipulating `child.center`
// and `child.bounds` directly, bypassing standard Fabric metrics flow.
//
// If we don't intercept these writes, the view physically moves instantly,
// but its `layer.transform` uses the OLD translation against the NEW center!
// Overriding these UIKit primitive setters guarantees 100% transform sync.

- (void)setFrame:(CGRect)frame
{
  [super setFrame:frame];
  [self _applyTransform];
}

- (void)setCenter:(CGPoint)center
{
  [super setCenter:center];
  [self _applyTransform];
}

- (void)setBounds:(CGRect)bounds
{
  [super setBounds:bounds];
  [self _applyTransform];
}

- (void)_findAndObserveScrollView
{
  [self _stopObserving];

  UIView *v = self.superview;
  while (v) {
    if ([v isKindOfClass:[UIScrollView class]]) {
      _parentScrollView = (UIScrollView *)v;
      break;
    }
    v = v.superview;
  }

  if (_parentScrollView) {
    [_parentScrollView addObserver:self
                        forKeyPath:@"contentOffset"
                           options:NSKeyValueObservingOptionNew
                           context:nil];
    _observing = YES;

    // Apply immediately so the view starts at the correct position.
    [self _applyTransform];
  }
}

- (void)_stopObserving
{
  if (_observing && _parentScrollView) {
    @try {
      [_parentScrollView removeObserver:self forKeyPath:@"contentOffset"];
    } @catch (NSException *e) {
      // Already removed — ignore.
    }
  }
  _observing = NO;
  _parentScrollView = nil;
}

// ── KVO — fires on UI thread, same run loop as scroll ────────────────────────

- (void)observeValueForKeyPath:(NSString *)keyPath
                      ofObject:(id)object
                        change:(NSDictionary<NSKeyValueChangeKey,id> *)change
                       context:(void *)context
{
  if ([keyPath isEqualToString:@"contentOffset"]) {
    [self _applyTransform];
  }
}

// ── Transform computation ────────────────────────────────────────────────────

- (void)_applyTransform
{
    if (!_enabled || !_parentScrollView) {
        self.layer.transform = CATransform3DIdentity;
        return;
    }
    
    // Skip if Fabric hasn't laid us out yet (bounds are zero on the first frame
    // after mount — applying a transform here would flash the view at (0,0)).
    if (self.bounds.size.width <= 0 || self.bounds.size.height <= 0) return;
    
    if (_isHorizontal) {
        // ── Horizontal path: X-axis sticky/push ────────────────────────────────
        CGFloat scrollX   = _parentScrollView.contentOffset.x;
        CGFloat viewportW = _parentScrollView.bounds.size.width;
        // Derive naturalX from Fabric-managed center (ignores any active transform).
        CGFloat naturalX  = self.center.x - self.bounds.size.width * 0.5;
        CGFloat translateX;
        
        if (_isFooter) {
            // Footer pins to trailing (right) edge.
            // desiredLeft: left edge position that places the footer's right edge at viewport right.
            CGFloat desiredLeft = scrollX + viewportW - _headerHeight; // _headerHeight holds primary-axis size
            if (_isPush) {
                // Footer can't move left past section start (boundaryX).
                CGFloat minTranslate = _boundaryX - naturalX;
                translateX = MIN(0.0, MAX(desiredLeft - naturalX, minTranslate));
            } else {
                translateX = MIN(0.0, desiredLeft - naturalX);
            }
        } else {
            // Header pins to leading (left) edge.
            if (_isPush) {
                CGFloat maxTranslate = _boundaryX - naturalX - _headerHeight;
                translateX = MAX(0.0, MIN(scrollX - naturalX, maxTranslate));
            } else {
                translateX = MAX(0.0, scrollX - naturalX);
            }
        }
        
        self.layer.transform = CATransform3DMakeTranslation(translateX, 0, 0);
        self.layer.zPosition = fabs(translateX) > 0.1 ? 100 : 0;
        return;
    }
    
    // ── Vertical path (unchanged) ────────────────────────────────────────────
    CGFloat scrollY = _parentScrollView.contentOffset.y;
    CGFloat viewportH = _parentScrollView.bounds.size.height;
    
    // Derive naturalY from the view's actual Fabric-managed position.
    // self.center and self.bounds are independent of self.layer.transform,
    // so this always reflects the true layout position even while a transform
    // is active.  This eliminates sync issues with the JS-side prop which can
    // lag behind as variable heights are measured and positions shift.
    CGFloat naturalY = self.center.y - self.bounds.size.height * 0.5;
    CGFloat translateY;
    CGFloat desiredTop = 0.0;
    
    if (_isFooter) {
        // Footer pins to viewport bottom: desiredTop = scrollY + viewportH - height
        // translate = min(0, desiredTop - naturalY), with optional push boundary.
        desiredTop = scrollY + viewportH - _headerHeight;
        if (_isPush) {
            // boundaryY = section start Y (header or first item). Footer shouldn't
            // be pulled above it. Use MAX so we pick the LESS negative value —
            // constraining upward movement, not exaggerating it.
            CGFloat minTranslate = _boundaryY - naturalY;
            translateY = MIN(0.0, MAX(desiredTop - naturalY, minTranslate));
        } else {
            translateY = MIN(0.0, desiredTop - naturalY);
        }
    } else {
        // Header pins to viewport top: translate = max(0, scrollY - naturalY), with push boundary.
        if (_isPush) {
            CGFloat maxTranslate = _boundaryY - naturalY - _headerHeight;
            translateY = MAX(0.0, MIN(scrollY - naturalY, maxTranslate));
        } else {
            translateY = MAX(0.0, scrollY - naturalY);
        }
    }
    
    self.layer.transform = CATransform3DMakeTranslation(0, translateY, 0);
    
    // Elevate z when actively sticky (translated > 0) so it floats above siblings.
    self.layer.zPosition = fabs(translateY) > 0.1 ? 100 : 0;
}

@end

// Required export for Fabric component registry.
Class<RCTComponentViewProtocol> RNScrollCoordinatedViewCls(void)
{
  return RNScrollCoordinatedViewView.class;
}
