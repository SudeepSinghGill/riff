#import "RNCollectionSubContainerView.h"
#import "RNMeasuredCellView.h"

// Our custom ShadowNode + descriptor (NOT the codegen default).
#import "CollectionSubContainerComponentDescriptor.h"
#import "CollectionSubContainerShadowNode.h"

// Codegen-generated helpers (props protocol, event emitter types).
#import <react/renderer/components/RNCollectionViewSpec/EventEmitters.h>
#import <react/renderer/components/RNCollectionViewSpec/Props.h>
#import <react/renderer/components/RNCollectionViewSpec/RCTComponentViewHelpers.h>

#import <React/RCTFabricComponentsPlugins.h>

using namespace facebook::react;

// Cross-platform debug logging for the sub-container view.
// Active only in DEBUG builds; no-op in release.
#ifndef RNCV_ENABLE_NATIVE_LOGS
#define RNCV_ENABLE_NATIVE_LOGS 0
#endif

#if DEBUG && RNCV_ENABLE_NATIVE_LOGS
#define RNSUB_LOG(fmt, ...) NSLog(@"[RNCV-SUB] " @ fmt, ##__VA_ARGS__)
#else
#define RNSUB_LOG(fmt, ...) ((void)0)
#endif

@interface RNCollectionSubContainerView () <RCTRNCollectionSubContainerViewProtocol>
@end

@implementation RNCollectionSubContainerView {
  // Optional UIScrollView (created when scrollDirection != 'none').
  UIScrollView *_scrollView;
  // Holds cells as subviews. Always present. When scrollable, sits inside _scrollView.
  UIView       *_contentView;

  // State from ShadowNode (rich ChildVisualState array).
  std::shared_ptr<const CollectionSubContainerShadowNode::ConcreteState> _state;

  // Cached props.
  int32_t       _sectionIndex;
  int32_t       _layoutCacheId;
  RNCollectionSubContainerScrollDirection _scrollDirection;
  CGSize        _propContentSize;

  // Throttle scroll events.
  NSTimeInterval _lastScrollEventTime;

  // Bounds that were active the last time _applyChildVisualStates ran from
  // layoutSubviews. Used to gate the cascade-cost: parent V-scroll triggers
  // the main container's Yoga relayout on every cell mount/unmount, which
  // cascades down to this wrapper's layoutSubviews even when our own bounds
  // didn't change. We re-apply child positions only when the wrapper itself
  // resized — updateState: handles the "real state changed" path separately.
  CGRect _lastAppliedBounds;
}

@synthesize contentView = _contentView;

// ── Fabric registration ─────────────────────────────────────────────────────

+ (ComponentDescriptorProvider)componentDescriptorProvider
{
  return concreteComponentDescriptorProvider<CollectionSubContainerComponentDescriptor>();
}

// ── Init ────────────────────────────────────────────────────────────────────

- (instancetype)initWithFrame:(CGRect)frame
{
  if (self = [super initWithFrame:frame]) {
    static const auto defaultProps =
        std::make_shared<const RNCollectionSubContainerProps>();
    _props = defaultProps;

    _sectionIndex          = 0;
    _layoutCacheId         = 0;
    _scrollDirection       = RNCollectionSubContainerScrollDirection::None;
    _propContentSize       = CGSizeZero;
    _shadowNodePositioned  = NO;
    _lastScrollEventTime   = 0;
    _lastAppliedBounds     = CGRectZero;

    // Default to non-scrollable: contentView is the immediate child of self.
    // _ensureScrollViewIfNeeded() will reparent it into a UIScrollView when
    // scrollDirection becomes != 'none' on first updateProps.
    _contentView = [[UIView alloc] initWithFrame:self.bounds];
    _contentView.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                                    UIViewAutoresizingFlexibleHeight;
    _contentView.clipsToBounds = NO;
    [self addSubview:_contentView];

    self.clipsToBounds = YES;
    RNSUB_LOG("init");
  }
  return self;
}

// ── Fabric recycling ────────────────────────────────────────────────────────

- (void)prepareForRecycle
{
  [super prepareForRecycle];
  _state = nullptr;
  _shadowNodePositioned = NO;
  _lastScrollEventTime  = 0;
  _propContentSize      = CGSizeZero;
  _lastAppliedBounds    = CGRectZero;

  if (_scrollView) {
    [_scrollView setContentOffset:CGPointZero animated:NO];
    _scrollView.contentSize = CGSizeZero;
  }
  _contentView.frame = self.bounds;
  RNSUB_LOG("prepareForRecycle");
}

// ── Layout authority for THIS wrapper (positioned by parent) ────────────────

- (void)updateLayoutMetrics:(const facebook::react::LayoutMetrics &)layoutMetrics
           oldLayoutMetrics:(const facebook::react::LayoutMetrics &)oldLayoutMetrics
{
  auto adjusted = layoutMetrics;
  if (_shadowNodePositioned) {
    // Parent container's ShadowNode is the position authority for this
    // wrapper. Preserve current native origin; only let SIZE updates through.
    adjusted.frame.origin.x = self.frame.origin.x;
    adjusted.frame.origin.y = self.frame.origin.y;
  }
  [super updateLayoutMetrics:adjusted oldLayoutMetrics:oldLayoutMetrics];
}

// ── Props ───────────────────────────────────────────────────────────────────

- (void)updateProps:(const Props::Shared &)props
           oldProps:(const Props::Shared &)oldProps
{
  [super updateProps:props oldProps:oldProps];

  const auto &p =
      *std::static_pointer_cast<const RNCollectionSubContainerProps>(props);

  _sectionIndex   = p.sectionIndex;
  _layoutCacheId  = p.layoutCacheId;

  // Lazily create / tear down the embedded scroll view based on scrollDirection.
  if (p.scrollDirection != _scrollDirection) {
    _scrollDirection = p.scrollDirection;
    [self _reconfigureScrollViewForDirection:_scrollDirection];
  }

  // Apply content size hints from props (layout.contentSize()).
  //
  // We track _propContentSize unconditionally so it can serve as the fallback
  // in updateState:, but we only apply it to _scrollView.contentSize when both
  // dimensions are strictly positive. A zero-or-negative dim would collapse
  // the scrollview's scroll bounds and lock out gestures on that axis until
  // the next valid prop arrives — visible to the user as "scrollview just
  // doesn't think it can scroll" after a transient zero prop.
  CGSize newSize = CGSizeMake((CGFloat)p.contentWidth, (CGFloat)p.contentHeight);
  if (!CGSizeEqualToSize(newSize, _propContentSize)) {
    _propContentSize = newSize;
    if (_scrollView && newSize.width > 0 && newSize.height > 0) {
      _scrollView.contentSize = newSize;
      _contentView.frame = CGRectMake(0, 0, newSize.width, newSize.height);
    }
  }
}

- (void)_reconfigureScrollViewForDirection:(RNCollectionSubContainerScrollDirection)dir
{
  const BOOL needsScroll = (dir != RNCollectionSubContainerScrollDirection::None);

  if (needsScroll && !_scrollView) {
    // Promote: move _contentView into a fresh UIScrollView.
    _scrollView = [[UIScrollView alloc] initWithFrame:self.bounds];
    _scrollView.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                                   UIViewAutoresizingFlexibleHeight;
    _scrollView.delegate = self;
    _scrollView.scrollEnabled = YES;
    _scrollView.bounces = YES;
    _scrollView.showsHorizontalScrollIndicator = NO;
    _scrollView.showsVerticalScrollIndicator   = NO;

    if (dir == RNCollectionSubContainerScrollDirection::Horizontal) {
      _scrollView.alwaysBounceHorizontal = YES;
      _scrollView.alwaysBounceVertical   = NO;
      _scrollView.directionalLockEnabled = YES;
    } else {
      _scrollView.alwaysBounceHorizontal = NO;
      _scrollView.alwaysBounceVertical   = YES;
      _scrollView.directionalLockEnabled = YES;
    }

    [_contentView removeFromSuperview];
    [_scrollView addSubview:_contentView];
    [self addSubview:_scrollView];

    if (!CGSizeEqualToSize(_propContentSize, CGSizeZero)) {
      _scrollView.contentSize = _propContentSize;
      _contentView.frame = CGRectMake(0, 0, _propContentSize.width, _propContentSize.height);
    }
  } else if (!needsScroll && _scrollView) {
    // Demote: detach _contentView from _scrollView and re-attach to self.
    [_contentView removeFromSuperview];
    [_scrollView removeFromSuperview];
    _scrollView.delegate = nil;
    _scrollView = nil;
    _contentView.frame = self.bounds;
    [self addSubview:_contentView];
  } else if (needsScroll && _scrollView) {
    // Existing scroll view but axis changed.
    if (dir == RNCollectionSubContainerScrollDirection::Horizontal) {
      _scrollView.alwaysBounceHorizontal = YES;
      _scrollView.alwaysBounceVertical   = NO;
    } else {
      _scrollView.alwaysBounceHorizontal = NO;
      _scrollView.alwaysBounceVertical   = YES;
    }
  }
}

// ── Child management ────────────────────────────────────────────────────────
// Cells land in _contentView, where their frames + transforms are applied
// from the ShadowNode-driven state in updateState:.

- (void)mountChildComponentView:(UIView<RCTComponentViewProtocol> *)childComponentView
                          index:(NSInteger)index
{
  [_contentView insertSubview:childComponentView atIndex:index];
}

- (void)unmountChildComponentView:(UIView<RCTComponentViewProtocol> *)childComponentView
                            index:(NSInteger)index
{
  [childComponentView removeFromSuperview];
}

// ── State ───────────────────────────────────────────────────────────────────

- (void)updateState:(const facebook::react::State::Shared &)state
           oldState:(const facebook::react::State::Shared &)oldState
{
  _state = std::static_pointer_cast<
      const CollectionSubContainerShadowNode::ConcreteState>(state);

  if (!_state) return;

  const auto &data = _state->getData();

  // Apply content size to scroll view ONLY when both dims are strictly
  // positive AND it actually changed. Touching _scrollView.contentSize or
  // _contentView.frame mid-gesture re-clamps contentOffset and disrupts the
  // bounce-back animation. Applying a zero dim disables the scroll axis and
  // locks the gesture out — visible after a right-edge bounce as "scrollview
  // just doesn't think it can scroll".
  //
  // _propContentSize (from updateProps:) is the fallback when state contentSize
  // hasn't been written yet; if both signals are still zero we keep the last
  // valid _scrollView.contentSize untouched.
  if (_scrollView) {
    CGSize cs = CGSizeMake(data.contentSize.width, data.contentSize.height);
    if (cs.width <= 0)  cs.width  = _propContentSize.width;
    if (cs.height <= 0) cs.height = _propContentSize.height;
    if (cs.width > 0 && cs.height > 0 &&
        !CGSizeEqualToSize(_scrollView.contentSize, cs)) {
      _scrollView.contentSize = cs;
      _contentView.frame = CGRectMake(0, 0, cs.width, cs.height);
    }
  }

  [self _applyChildVisualStates];
}

// ── layoutSubviews ──────────────────────────────────────────────────────────
// Re-apply child positions only when our own bounds actually changed.
//
// Why the gate matters: every cell mount/unmount in the parent main container
// triggers Yoga to relayout its direct children, which cascades down to this
// wrapper's layoutSubviews — even when our bounds are unchanged. Without the
// gate, every V scroll tick during fast scrolling re-runs _applyChildVisualStates
// (NSDictionary build + per-child eps comparison + setFrame for any drift) for
// every H sub-container on screen. With H-3.5's wider H window mounting more
// children, this is the dominant V-scroll jank source.
//
// State-driven changes (real frame deltas after a Fabric commit) still flow
// through updateState: → _applyChildVisualStates, so this gate only suppresses
// the no-op path.

- (void)layoutSubviews
{
  [super layoutSubviews];

  // Keep contentView frame in sync with bounds when not scrollable.
  if (!_scrollView) {
    _contentView.frame = self.bounds;
  }

  if (!CGRectEqualToRect(self.bounds, _lastAppliedBounds)) {
    _lastAppliedBounds = self.bounds;
    [self _applyChildVisualStates];
  }
}

// ── Apply ChildVisualState array to subviews via tag map ────────────────────

- (void)_applyChildVisualStates
{
  if (!_state) return;

  const auto &data = _state->getData();
  const auto &children  = data.children;
  const auto &childTags = data.childTags;

  NSArray<UIView *> *subviews = _contentView.subviews;
  if (children.empty() || subviews.count == 0) return;

  // Build tag → UIView map for identity-based lookup. Same rationale as the
  // main container: Fabric's reconciler "last index" optimization can leave
  // native subview order out of sync with ShadowNode child order.
  NSMutableDictionary<NSNumber *, UIView *> *tagToView =
      [NSMutableDictionary dictionaryWithCapacity:subviews.count];
  for (UIView *sv in subviews) {
    tagToView[@(sv.tag)] = sv;
  }

  static const CGFloat kEps = 0.1f;

  for (size_t i = 0; i < children.size() && i < childTags.size(); i++) {
    UIView *child = tagToView[@(childTags[i])];
    if (!child) continue;

    const auto &cv = children[i];

    const CGFloat targetX = cv.x;
    const CGFloat targetY = cv.y;
    const CGFloat targetW = cv.w;
    const CGFloat targetH = cv.h;
    if (targetW <= 0 || targetH <= 0) continue;

    // Frame application (preserve transform — apply via bounds + center
    // when an active transform is present, like the main container does).
    const BOOL hasActiveTransform = !CGAffineTransformIsIdentity(child.transform) ||
        !CATransform3DIsIdentity(child.layer.transform) || cv.hasTransform;
    const CGFloat curNX = hasActiveTransform
        ? (child.center.x - child.bounds.size.width  * 0.5f)
        : child.frame.origin.x;
    const CGFloat curNY = hasActiveTransform
        ? (child.center.y - child.bounds.size.height * 0.5f)
        : child.frame.origin.y;
    const CGFloat curNW = hasActiveTransform ? child.bounds.size.width  : child.frame.size.width;
    const CGFloat curNH = hasActiveTransform ? child.bounds.size.height : child.frame.size.height;

    const BOOL diffX = std::abs(curNX - targetX) > kEps;
    const BOOL diffY = std::abs(curNY - targetY) > kEps;
    const BOOL diffW = std::abs(curNW - targetW) > kEps;
    const BOOL diffH = std::abs(curNH - targetH) > kEps;

    if (diffX || diffY || diffW || diffH) {
      // Tell the cell its position came from us so its updateLayoutMetrics:
      // override preserves the origin instead of letting Yoga overwrite.
      if ([child isKindOfClass:[RNMeasuredCellView class]]) {
        ((RNMeasuredCellView *)child).shadowNodePositioned = YES;
      }
      if (hasActiveTransform) {
        child.bounds = CGRectMake(0, 0, targetW, targetH);
        child.center = CGPointMake(targetX + targetW * 0.5f, targetY + targetH * 0.5f);
      } else {
        child.frame = CGRectMake(targetX, targetY, targetW, targetH);
      }
    }

    // Opacity (skip when 1.0 to avoid no-op CALayer dirties).
    if (std::abs(child.alpha - cv.opacity) > 1e-3) {
      child.alpha = cv.opacity;
    }

    // Z-ordering. Use layer.zPosition for sub-frame z control without
    // requiring subview reordering.
    if (std::abs(child.layer.zPosition - cv.zIndex) > 1e-3) {
      child.layer.zPosition = cv.zIndex;
    }

    // Transform. Skip when identity to avoid re-rasterization cost.
    if (cv.hasTransform) {
      // Convert our 16-Float column-major matrix into a CATransform3D.
      CATransform3D t;
      t.m11 = cv.transform[0];  t.m12 = cv.transform[1];  t.m13 = cv.transform[2];  t.m14 = cv.transform[3];
      t.m21 = cv.transform[4];  t.m22 = cv.transform[5];  t.m23 = cv.transform[6];  t.m24 = cv.transform[7];
      t.m31 = cv.transform[8];  t.m32 = cv.transform[9];  t.m33 = cv.transform[10]; t.m34 = cv.transform[11];
      t.m41 = cv.transform[12]; t.m42 = cv.transform[13]; t.m43 = cv.transform[14]; t.m44 = cv.transform[15];

      if (!CATransform3DEqualToTransform(child.layer.transform, t)) {
        child.layer.transform = t;
      }
    } else if (!CATransform3DIsIdentity(child.layer.transform)) {
      // Layout dropped its transform — restore identity.
      child.layer.transform = CATransform3DIdentity;
    }
  }
}

// ── UIScrollViewDelegate ────────────────────────────────────────────────────

- (void)scrollViewDidScroll:(UIScrollView *)scrollView
{
  // Throttle to ~60fps.
  NSTimeInterval now = CACurrentMediaTime();
  if (now - _lastScrollEventTime < 0.016) return;
  _lastScrollEventTime = now;

  if (!_eventEmitter) return;
  auto emitter = std::static_pointer_cast<
      const RNCollectionSubContainerEventEmitter>(_eventEmitter);

  RNCollectionSubContainerEventEmitter::OnSubScroll event;
  event.sectionIndex = _sectionIndex;
  event.scrollX      = scrollView.contentOffset.x;
  event.scrollY      = scrollView.contentOffset.y;
  emitter->onSubScroll(event);
}

@end

// Required export for Fabric component registry.
Class<RCTComponentViewProtocol> RNCollectionSubContainerCls(void)
{
  return RNCollectionSubContainerView.class;
}
