package com.riff

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.HorizontalScrollView
import android.widget.ScrollView
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

/**
 * Android equivalent of RNCollectionViewContainerView.mm.
 *
 * Owns an internal ScrollView (or HorizontalScrollView). The ShadowNode handles
 * child positioning via layout() override. This view reads CollectionViewContainerState
 * to apply child frames, content size, scroll offset corrections, and forward scroll
 * events to JS for render range computation.
 */
class RNCollectionViewContainerView(context: Context) : FrameLayout(context) {

    // ── Internal scroll hierarchy ────────────────────────────────────────────
    private var scrollView: RiffScrollView? = null
    private var hScrollView: RiffHorizontalScrollView? = null
    private val contentView: RiffContentView = RiffContentView(context)

    // ── State from ShadowNode ────────────────────────────────────────────────
    // These are populated by the Fabric state delivery mechanism.
    // For now, we store the raw position data as flat arrays.
    private var positions: FloatArray = FloatArray(0)
    private var childTags: IntArray = IntArray(0)
    private var layoutRevision: Int = -1
    private var hasReceivedFirstState: Boolean = false

    // ── Scroll event throttling ──────────────────────────────────────────────
    private var lastScrollEventTime: Long = 0
    private var scrollEventMinIntervalMs: Long = 16 // 16ms default

    // ── MVC correction (deferred from updateState to layout pass) ────────────
    private var applyingCorrection: Boolean = false
    private var pendingMVCCorrection: Double = 0.0
    private var pendingMVCScrollTarget: Double = 0.0

    // ── Cached props ─────────────────────────────────────────────────────────
    var layoutCacheId: Int = 0
        private set
    var horizontal: Boolean = false
        private set
    var layoutWritesVisualAttributes: Boolean = false
        private set

    private val mainHandler = Handler(Looper.getMainLooper())

    // ── Event callbacks (set by ViewManager) ─────────────────────────────────
    var onScrollCallback: ((
        contentOffsetX: Float, contentOffsetY: Float,
        contentSizeW: Float, contentSizeH: Float,
        layoutW: Float, layoutH: Float
    ) -> Unit)? = null
    var onScrollBeginDragCallback: ((
        contentOffsetX: Float, contentOffsetY: Float,
        contentSizeW: Float, contentSizeH: Float,
        layoutW: Float, layoutH: Float
    ) -> Unit)? = null
    var onScrollEndDragCallback: ((
        contentOffsetX: Float, contentOffsetY: Float,
        contentSizeW: Float, contentSizeH: Float,
        layoutW: Float, layoutH: Float
    ) -> Unit)? = null
    var onMomentumScrollBeginCallback: ((
        contentOffsetX: Float, contentOffsetY: Float,
        contentSizeW: Float, contentSizeH: Float,
        layoutW: Float, layoutH: Float
    ) -> Unit)? = null
    var onMomentumScrollEndCallback: ((
        contentOffsetX: Float, contentOffsetY: Float,
        contentSizeW: Float, contentSizeH: Float,
        layoutW: Float, layoutH: Float
    ) -> Unit)? = null

    init {
        // Create internal vertical ScrollView by default.
        setupVerticalScroll()
    }

    private fun setupVerticalScroll() {
        (contentView.parent as? ViewGroup)?.removeView(contentView)
        removeAllViews()
        hScrollView = null

        val sv = RiffScrollView(context).apply {
            layoutParams = LayoutParams(
                LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT
            )
            isFillViewport = false
            isVerticalScrollBarEnabled = true
            onScrollListener = { scrollY -> onScrollChanged(scrollY) }
        }
        sv.addView(contentView, LayoutParams(
            LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT
        ))
        addView(sv)
        scrollView = sv
    }

    private fun setupHorizontalScroll() {
        (contentView.parent as? ViewGroup)?.removeView(contentView)
        removeAllViews()
        scrollView = null

        val hsv = RiffHorizontalScrollView(context).apply {
            layoutParams = LayoutParams(
                LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT
            )
            isFillViewport = false
            isHorizontalScrollBarEnabled = true
            onScrollListener = { scrollX -> onScrollChanged(scrollX) }
        }
        hsv.addView(contentView, LayoutParams(
            LayoutParams.WRAP_CONTENT, LayoutParams.MATCH_PARENT
        ))
        addView(hsv)
        hScrollView = hsv
    }

    // ── Props update ─────────────────────────────────────────────────────────

    fun updateProps(
        newHorizontal: Boolean,
        scrollEnabled: Boolean,
        bounces: Boolean,
        showsVerticalScrollIndicator: Boolean,
        scrollEventThrottle: Float,
        newLayoutWritesVisualAttributes: Boolean,
        newLayoutCacheId: Int
    ) {
        if (newHorizontal != horizontal) {
            horizontal = newHorizontal
            if (horizontal) setupHorizontalScroll() else setupVerticalScroll()
        }

        scrollView?.isVerticalScrollBarEnabled = showsVerticalScrollIndicator && !horizontal
        hScrollView?.isHorizontalScrollBarEnabled = horizontal

        // ScrollView doesn't have a direct "scrollEnabled" — we handle it
        // by intercepting touch events in a custom scroll view if needed.
        // For now, leave the default behavior.

        if (scrollEventThrottle > 0) {
            scrollEventMinIntervalMs = scrollEventThrottle.toLong()
        }

        layoutWritesVisualAttributes = newLayoutWritesVisualAttributes
        layoutCacheId = newLayoutCacheId
        if (newLayoutCacheId != 0) nativeRegisterScrollHandler(newLayoutCacheId)
    }

    // ── State update from ShadowNode ─────────────────────────────────────────

    fun updateState(
        newPositions: FloatArray,
        newChildTags: IntArray,
        contentWidth: Float,
        contentHeight: Float,
        newLayoutRevision: Int
    ) {
        positions = newPositions
        childTags = newChildTags

        if (!hasReceivedFirstState) {
            hasReceivedFirstState = true
            scrollTo(0, 0)
        }

        // Apply content size.
        val density = resources.displayMetrics.density
        val contentWidthPx = (contentWidth * density).toInt()
        val contentHeightPx = (contentHeight * density).toInt()
        contentView.layoutParams = contentView.layoutParams?.apply {
            width = contentWidthPx
            height = contentHeightPx
        } ?: LayoutParams(contentWidthPx, contentHeightPx)

        val revisionChanged = newLayoutRevision != layoutRevision
        layoutRevision = newLayoutRevision

        // Fire onScroll for content size changes or revision changes.
        if (revisionChanged) {
            emitOnScroll()
        }

        // Apply positions immediately (don't wait for layout pass).
        applyPositionsFromState()
        requestLayout()
    }

    // ── Position application ─────────────────────────────────────────────────
    // Applies child frames from the ShadowNode-computed positions.
    // Uses tag-based (not index-based) lookup — same as iOS.

    fun applyPositionsFromState() {
        val childCount = positions.size / 4
        if (childCount == 0 || contentView.childCount == 0) return

        val density = resources.displayMetrics.density

        // Build tag → View map for identity-based lookup.
        val tagToView = HashMap<Int, View>(contentView.childCount)
        for (i in 0 until contentView.childCount) {
            val child = contentView.getChildAt(i)
            tagToView[child.id] = child
        }

        for (i in 0 until childCount) {
            if (i >= childTags.size) break
            val child = tagToView[childTags[i]] ?: continue

            val targetX = (positions[i * 4] * density).toInt()
            val targetY = (positions[i * 4 + 1] * density).toInt()
            val targetW = (positions[i * 4 + 2] * density).toInt()
            val targetH = (positions[i * 4 + 3] * density).toInt()

            if (targetW <= 0 || targetH <= 0) continue

            val currentL = child.left
            val currentT = child.top
            val currentW = child.width
            val currentH = child.height

            val diffX = abs(currentL - targetX) > 1
            val diffY = abs(currentT - targetY) > 1
            val diffW = abs(currentW - targetW) > 1
            val diffH = abs(currentH - targetH) > 1

            if (diffX || diffY || diffW || diffH) {
                // Mark as ShadowNode-positioned so layout doesn't overwrite.
                when (child) {
                    is RNMeasuredCellView -> child.shadowNodePositioned = true
                    is RNCollectionSubContainerView -> child.shadowNodePositioned = true
                }

                // Measure with EXACTLY specs before layout — sub-container views
                // (HorizontalScrollView inside RNCollectionSubContainerView) need a
                // valid measure pass to render their children.
                child.measure(
                    View.MeasureSpec.makeMeasureSpec(targetW, View.MeasureSpec.EXACTLY),
                    View.MeasureSpec.makeMeasureSpec(targetH, View.MeasureSpec.EXACTLY)
                )
                child.layout(targetX, targetY, targetX + targetW, targetY + targetH)
            }
        }
    }

    // ── MVC correction application (in layout pass) ──────────────────────────

    override fun onLayout(changed: Boolean, left: Int, top: Int, right: Int, bottom: Int) {
        super.onLayout(changed, left, top, right, bottom)

        // Emit an initial scroll event so JS knows the real viewport dimensions.
        // Without this, layoutMeasurement.height = 0 in the initial onScroll,
        // causing processScroll to compute a near-zero render range.
        if (changed && height > 0) {
            emitOnScroll()
        }

        applyPositionsFromState()

        // Apply deferred MVC correction after positions are set.
        if (abs(pendingMVCCorrection) > 0.5) {
            val density = resources.displayMetrics.density
            val targetPx = (pendingMVCScrollTarget * density).toInt()
            applyingCorrection = true
            if (horizontal) {
                hScrollView?.scrollTo(max(0, targetPx), 0)
            } else {
                scrollView?.scrollTo(0, max(0, targetPx))
            }
            applyingCorrection = false
            pendingMVCCorrection = 0.0
            pendingMVCScrollTarget = 0.0

            emitOnScroll()
        }
    }

    // ── Child management ─────────────────────────────────────────────────────
    // Children from React are mounted into contentView, not directly into self.

    fun mountChildComponentView(childView: View, index: Int) {
        contentView.addView(childView, index)

        // Pass layoutCacheId to sticky children.
        if (childView is RNScrollCoordinatedViewView) {
            childView.layoutCacheId = layoutCacheId
        }
    }

    fun unmountChildComponentView(childView: View) {
        contentView.removeView(childView)
    }

    fun getContentChildAt(index: Int): View? = contentView.getChildAt(index)
    fun getContentChildCount(): Int = contentView.childCount
    fun removeAllContentChildren() { contentView.removeAllViews() }

    // ── Scroll handling ────────────────────────────────────────────────────

    private fun onScrollChanged(primaryOffset: Int) {
        if (applyingCorrection) return

        // Write scroll position to LayoutCache immediately (unthrottled) so
        // processScroll() reads the correct position on every JS scroll event.
        if (layoutCacheId != 0) {
            val density = resources.displayMetrics.density
            val dp = primaryOffset / density
            if (horizontal) {
                nativeSetScrollOffset(layoutCacheId, dp, 0f)
            } else {
                nativeSetScrollOffset(layoutCacheId, 0f, dp)
            }
        }

        // Throttle JS scroll events
        val now = System.currentTimeMillis()
        if (now - lastScrollEventTime < scrollEventMinIntervalMs) return
        lastScrollEventTime = now

        emitOnScroll()
    }

    private external fun nativeSetScrollOffset(cacheId: Int, scrollX: Float, scrollY: Float)
    private external fun nativeRegisterScrollHandler(cacheId: Int)
    private external fun nativeUnregisterScrollHandler(cacheId: Int)

    // Called from C++ (JS thread) via JNI — must post to main thread.
    @androidx.annotation.Keep
    fun scrollToFromNative(x: Float, y: Float, animated: Boolean) {
        val density = resources.displayMetrics.density
        val xPx = (x * density).toInt()
        val yPx = (y * density).toInt()
        mainHandler.post {
            if (animated) {
                if (horizontal) hScrollView?.smoothScrollTo(xPx, 0)
                else            scrollView?.smoothScrollTo(0, yPx)
            } else {
                if (horizontal) hScrollView?.scrollTo(xPx, 0)
                else            scrollView?.scrollTo(0, yPx)
            }
        }
    }

    private fun emitOnScroll() {
        val ctx = context as? com.facebook.react.bridge.ReactContext ?: return
        val density = resources.displayMetrics.density
        val offsetX: Float
        val offsetY: Float
        if (horizontal) {
            offsetX = (hScrollView?.scrollX ?: 0) / density
            offsetY = 0f
        } else {
            offsetX = 0f
            offsetY = (scrollView?.scrollY ?: 0) / density
        }

        // Prefer layoutParams dimensions for content size — contentView.width/height
        // are only valid AFTER children are laid out, but emitOnScroll can fire from
        // RNCollectionViewContainerView.onLayout (before children are laid out).
        // layoutParams are set synchronously by updateState, so they're always current.
        val contentLp = contentView.layoutParams
        val cw = (if (contentLp != null && contentLp.width > 0) contentLp.width
                  else contentView.width) / density
        val ch = (if (contentLp != null && contentLp.height > 0) contentLp.height
                  else contentView.height) / density
        val lw = width / density
        val lh = height / density

        val surfaceId = com.facebook.react.uimanager.UIManagerHelper.getSurfaceId(this)
        val dispatcher = com.facebook.react.uimanager.UIManagerHelper
            .getEventDispatcherForReactTag(ctx, id)
            ?: com.facebook.react.uimanager.UIManagerHelper.getUIManager(
                ctx, com.facebook.react.uimanager.common.UIManagerType.FABRIC
            )?.eventDispatcher
        dispatcher?.dispatchEvent(RiffScrollEvent(surfaceId, id, offsetX, offsetY, cw, ch, lw, lh))
    }

    // ── Recycling ────────────────────────────────────────────────────────────

    fun prepareForRecycle() {
        if (layoutCacheId != 0) nativeUnregisterScrollHandler(layoutCacheId)
        positions = FloatArray(0)
        childTags = IntArray(0)
        hasReceivedFirstState = false
        lastScrollEventTime = 0
        pendingMVCCorrection = 0.0
        pendingMVCScrollTarget = 0.0
        layoutRevision = -1
        scrollView?.scrollTo(0, 0)
        hScrollView?.scrollTo(0, 0)
    }
}
