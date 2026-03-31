#import "RNScrollCoordinatedViewView.h"

#import <react/renderer/components/RNCollectionViewSpec/ComponentDescriptors.h>
#import <react/renderer/components/RNCollectionViewSpec/EventEmitters.h>
#import <react/renderer/components/RNCollectionViewSpec/Props.h>
#import <react/renderer/components/RNCollectionViewSpec/RCTComponentViewHelpers.h>

#import <React/RCTFabricComponentsPlugins.h>

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
  CGFloat _headerHeight;
  BOOL    _isPush;
  BOOL    _enabled;
  BOOL    _isFooter;
  BOOL    _footerLogSeen;
  CGFloat _lastLoggedScrollY;
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
    _headerHeight = 0;
    _isPush      = YES;    // default behavior = push
    _enabled     = YES;
    _isFooter    = NO;
    _footerLogSeen = NO;
    _lastLoggedScrollY = CGFLOAT_MAX;
    _observing   = NO;
  }
  return self;
}

- (void)dealloc
{
  [self _stopObserving];
}

//̊ ── Props ────────────────────────────────────────────────────────────────────

- (void)updateProps:(const Props::Shared &)props oldProps:(const Props::Shared &)oldProps
{
  [super updateProps:props oldProps:oldProps];

  const auto &newProps = *std::static_pointer_cast<const RNScrollCoordinatedViewProps>(props);

  _boundaryY    = newProps.boundaryY;
  _headerHeight = newProps.headerHeight;
  _isPush       = isPush(newProps.behavior);
  _enabled      = newProps.enabled;
  _isFooter     = (newProps.kind == "footer");
  if (_isFooter) {
    _footerLogSeen = NO;
    _lastLoggedScrollY = CGFLOAT_MAX;
    RNCV_IOS_STICKY_LOG(@"[RNCV-IOS-STICKY-FOOTER] updateProps tag:%ld index:%d cacheKey:%s behavior:%s boundaryY:%.1f footerH:%.1f enabled:%d",
                        (long)self.tag,
                        (int)newProps.index,
                        newProps.cacheKey.c_str(),
                        _isPush ? "push" : "sticky",
                        _boundaryY,
                        _headerHeight,
                        (int)_enabled);
  }

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
    if (_isFooter) {
      const auto props = std::static_pointer_cast<const RNScrollCoordinatedViewProps>(_props);
      const int index = props ? (int)props->index : -1;
      RNCV_IOS_STICKY_LOG(@"[RNCV-IOS-STICKY-FOOTER] observing tag:%ld index:%d parent:%@",
                          (long)self.tag, index, NSStringFromClass([_parentScrollView class]));
    }
  } else {
    if (_isFooter) {
      const auto props = std::static_pointer_cast<const RNScrollCoordinatedViewProps>(_props);
      const int index = props ? (int)props->index : -1;
      RNCV_IOS_STICKY_LOG(@"[RNCV-IOS-STICKY-FOOTER] FAILED to find UIScrollView for tag:%ld index:%d", (long)self.tag, index);
    }
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
  if (self.bounds.size.height <= 0) return;

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
      // Prevent footer bottom from crossing boundaryY (start of next section).
      CGFloat maxTranslate = _boundaryY - naturalY - _headerHeight;
      translateY = MIN(0.0, MIN(desiredTop - naturalY, maxTranslate));
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

  const auto props = std::static_pointer_cast<const RNScrollCoordinatedViewProps>(_props);
  const int index = props ? (int)props->index : -1;
  const char *type = props ? props->type.c_str() : "";
  const char *kind = props ? props->kind.c_str() : "";
  const char *cacheKey = props ? props->cacheKey.c_str() : "";
  if (_isFooter) {
    const BOOL shouldLog = !_footerLogSeen ||
                           _lastLoggedScrollY == CGFLOAT_MAX ||
                           fabs(scrollY - _lastLoggedScrollY) >= 60.0;
    if (shouldLog) {
      _footerLogSeen = YES;
      _lastLoggedScrollY = scrollY;
      RNCV_IOS_STICKY_LOG(@"[RNCV-IOS-STICKY-FOOTER] apply tag:%ld index:%d behavior:%s type:%s kind:%s cacheKey:%s naturalY:%.1f centerY:%.1f bound:%.1f footerH:%.1f viewportH:%.1f scrollY:%.1f desiredTop:%.1f trans:%.1f",
                          (long)self.tag,
                          index,
                          _isPush ? "push" : "sticky",
                          type,
                          kind,
                          cacheKey,
                          naturalY,
                          self.center.y,
                          _boundaryY,
                          _headerHeight,
                          viewportH,
                          scrollY,
                          desiredTop,
                          translateY);
    }
  }

  // Elevate z when actively sticky (translated > 0) so it floats above siblings.
  self.layer.zPosition = fabs(translateY) > 0.1 ? 100 : 0;
}

@end

// Required export for Fabric component registry.
Class<RCTComponentViewProtocol> RNScrollCoordinatedViewCls(void)
{
  return RNScrollCoordinatedViewView.class;
}
