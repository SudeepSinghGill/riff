package com.riff

import android.content.Context
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.HorizontalScrollView
import android.widget.ScrollView
import com.facebook.react.bridge.ReactContext
import com.facebook.react.uimanager.UIManagerHelper
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

/**
 * Android equivalent of RNCollectionSubContainerView.mm.
 *
 * Generic host for a single section of the parent CollectionView. Owns a
 * contentView that holds cells as subviews, optionally embedded in a
 * HorizontalScrollView or ScrollView when scrollDirection != "none".
 *
 * Frames, transforms, opacity, and zIndex are applied natively from
 * CollectionSubContainerState driven by the C++ ShadowNode.
 */
class RNCollectionSubContainerView(context: Context) : FrameLayout(context) {

    // ── Public properties ────────────────────────────────────────────────────
    var shadowNodePositioned: Boolean = false
    val contentViewChild: RiffContentView = RiffContentView(context)

    // ── Cached props ─────────────────────────────────────────────────────────
    var sectionIndex: Int = 0
        private set
    var layoutCacheId: Int = 0
        private set

    private var scrollDirection: String = "none" // "none" | "horizontal" | "vertical"

    // ── Internal scroll view (created lazily) ────────────────────────────────
    private var hScrollView: HorizontalScrollView? = null
    private var vScrollView: ScrollView? = null

    // ── State from ShadowNode ────────────────────────────────────────────────
    // ChildVisualState data (flat arrays for cache-friendly iteration).
    private var childX: FloatArray = FloatArray(0)
    private var childY: FloatArray = FloatArray(0)
    private var childW: FloatArray = FloatArray(0)
    private var childH: FloatArray = FloatArray(0)
    private var childOpacity: FloatArray = FloatArray(0)
    private var childZIndex: FloatArray = FloatArray(0)
    private var stateChildTags: IntArray = IntArray(0)
    // TODO: transform matrix support (hasTransform + 16-float matrix per child)

    // ── Scroll position save/restore across Fabric recycles ──────────────────
    private var pendingRestoreScrollX: Float = 0f
    private var needsScrollRestore: Boolean = false

    // ── Scroll event throttle ────────────────────────────────────────────────
    private var lastScrollEventTime: Long = 0
    private val scrollEventMinIntervalMs: Long = 16

    // ── Bounds gate for layoutSubviews ───────────────────────────────────────
    private var lastAppliedWidth: Int = 0
    private var lastAppliedHeight: Int = 0

    // ── Scroll event dispatch ─────────────────────────────────────────────────

    private fun dispatchOnSubScroll(scrollXPx: Int, scrollYPx: Int) {
        val now = System.currentTimeMillis()
        if (now - lastScrollEventTime < scrollEventMinIntervalMs) return
        lastScrollEventTime = now

        val density = resources.displayMetrics.density
        val ctx = context as? ReactContext ?: return
        val surfaceId = UIManagerHelper.getSurfaceId(this)
        val dispatcher = UIManagerHelper.getEventDispatcherForReactTag(ctx, id)
            ?: UIManagerHelper.getUIManager(ctx, com.facebook.react.uimanager.common.UIManagerType.FABRIC)
                ?.eventDispatcher
            ?: return
        dispatcher.dispatchEvent(
            RiffSubScrollEvent(surfaceId, id, sectionIndex, scrollXPx / density, scrollYPx / density)
        )
    }

    init {
        // Default: non-scrollable. contentView is a direct child of self.
        contentViewChild.clipChildren = false
        addView(contentViewChild, LayoutParams(
            LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT
        ))
        clipChildren = true
    }

    // ── Props update ─────────────────────────────────────────────────────────

    fun updateProps(
        newSectionIndex: Int,
        newLayoutCacheId: Int,
        newScrollDirection: String,
        contentWidth: Float,
        contentHeight: Float
    ) {
        sectionIndex = newSectionIndex
        layoutCacheId = newLayoutCacheId

        if (newScrollDirection != scrollDirection) {
            scrollDirection = newScrollDirection
            reconfigureScrollView()
        }

        // Apply content size to scroll view.
        val density = resources.displayMetrics.density
        val cwPx = (contentWidth * density).toInt()
        val chPx = (contentHeight * density).toInt()
        if (cwPx > 0 && chPx > 0) {
            contentViewChild.layoutParams = contentViewChild.layoutParams?.apply {
                width = cwPx
                height = chPx
            } ?: LayoutParams(cwPx, chPx)
        }
    }

    private fun reconfigureScrollView() {
        // Remove contentView from current parent and tear down old scroll view.
        (contentViewChild.parent as? ViewGroup)?.removeView(contentViewChild)
        hScrollView?.let { removeView(it) }
        vScrollView?.let { removeView(it) }
        hScrollView = null
        vScrollView = null

        when (scrollDirection) {
            "horizontal" -> {
                val hsv = object : HorizontalScrollView(context) {
                    private var downX = 0f
                    private var downY = 0f
                    private val touchSlop =
                        android.view.ViewConfiguration.get(context).scaledTouchSlop.toFloat()

                    override fun onTouchEvent(ev: MotionEvent): Boolean {
                        when (ev.actionMasked) {
                            MotionEvent.ACTION_DOWN -> {
                                downX = ev.x
                                downY = ev.y
                                // Preemptively disallow — the parent decides on first MOVE
                                // whether to take the gesture back (vertical) or leave it.
                                parent?.requestDisallowInterceptTouchEvent(true)
                            }
                            MotionEvent.ACTION_MOVE -> {
                                val dx = abs(ev.x - downX)
                                val dy = abs(ev.y - downY)
                                if (dy > touchSlop && dy > dx) {
                                    // Gesture turned out vertical — release the parent.
                                    parent?.requestDisallowInterceptTouchEvent(false)
                                }
                            }
                        }
                        return super.onTouchEvent(ev)
                    }

                    override fun onScrollChanged(l: Int, t: Int, oldl: Int, oldt: Int) {
                        super.onScrollChanged(l, t, oldl, oldt)
                        dispatchOnSubScroll(l, 0)
                    }
                }.apply {
                    layoutParams = LayoutParams(
                        LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT
                    )
                    isHorizontalScrollBarEnabled = false
                    isHorizontalFadingEdgeEnabled = false
                    isNestedScrollingEnabled = true
                }
                hsv.addView(contentViewChild, LayoutParams(
                    LayoutParams.WRAP_CONTENT, LayoutParams.MATCH_PARENT
                ))
                // Remove any existing children (the non-scrollable contentView was added in init).
                removeAllViews()
                addView(hsv)
                hScrollView = hsv
            }
            "vertical" -> {
                val sv = ScrollView(context).apply {
                    layoutParams = LayoutParams(
                        LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT
                    )
                    isVerticalScrollBarEnabled = false
                    isVerticalFadingEdgeEnabled = false
                    setOnScrollChangeListener { _, _, scrollY, _, _ ->
                        dispatchOnSubScroll(0, scrollY)
                    }
                }
                sv.addView(contentViewChild, LayoutParams(
                    LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT
                ))
                removeAllViews()
                addView(sv)
                vScrollView = sv
            }
            else -> {
                // Non-scrollable: contentView is a direct child.
                removeAllViews()
                addView(contentViewChild, LayoutParams(
                    LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT
                ))
            }
        }
    }

    // ── State update from ShadowNode ─────────────────────────────────────────

    fun updateState(
        xs: FloatArray, ys: FloatArray, ws: FloatArray, hs: FloatArray,
        opacities: FloatArray, zIndexes: FloatArray,
        tags: IntArray,
        contentWidth: Float, contentHeight: Float
    ) {
        childX = xs
        childY = ys
        childW = ws
        childH = hs
        childOpacity = opacities
        childZIndex = zIndexes
        stateChildTags = tags

        // Apply content size to scroll view (scroll-axis only to avoid bounce disruption).
        val density = resources.displayMetrics.density
        val cwPx = (contentWidth * density).toInt()
        val chPx = (contentHeight * density).toInt()
        if (cwPx > 0 && chPx > 0) {
            contentViewChild.layoutParams = contentViewChild.layoutParams?.apply {
                width = cwPx
                height = chPx
            } ?: LayoutParams(cwPx, chPx)
        }

        applyChildVisualStates()
    }

    // ── Child management ─────────────────────────────────────────────────────

    fun mountChildComponentView(childView: View, index: Int) {
        contentViewChild.addView(childView, index)
        applyChildVisualStates()
    }

    fun unmountChildComponentView(childView: View) {
        contentViewChild.removeView(childView)
    }

    fun getContentChildAt(index: Int): View? = contentViewChild.getChildAt(index)
    fun getContentChildCount(): Int = contentViewChild.childCount
    fun removeAllContentChildren() { contentViewChild.removeAllViews() }

    // ── Apply ChildVisualState array to subviews via tag map ─────────────────

    private fun applyChildVisualStates() {
        val childCount = stateChildTags.size
        if (childCount == 0 || contentViewChild.childCount == 0) return

        val density = resources.displayMetrics.density

        // Build tag → View map.
        val tagToView = HashMap<Int, View>(contentViewChild.childCount)
        for (i in 0 until contentViewChild.childCount) {
            val child = contentViewChild.getChildAt(i)
            tagToView[child.id] = child
        }

        for (i in 0 until childCount) {
            if (i >= childX.size) break
            val child = tagToView[stateChildTags[i]] ?: continue

            val targetX = (childX[i] * density).toInt()
            val targetY = (childY[i] * density).toInt()
            val targetW = (childW[i] * density).toInt()
            val targetH = (childH[i] * density).toInt()

            if (targetW <= 0 || targetH <= 0) continue

            // Claim authority.
            if (child is RNMeasuredCellView && !child.shadowNodePositioned) {
                child.shadowNodePositioned = true
            }

            val diffX = abs(child.left - targetX) > 1
            val diffY = abs(child.top - targetY) > 1
            val diffW = abs(child.width - targetW) > 1
            val diffH = abs(child.height - targetH) > 1

            if (diffX || diffY || diffW || diffH) {
                child.measure(
                    View.MeasureSpec.makeMeasureSpec(targetW, View.MeasureSpec.EXACTLY),
                    View.MeasureSpec.makeMeasureSpec(targetH, View.MeasureSpec.EXACTLY)
                )
                child.layout(targetX, targetY, targetX + targetW, targetY + targetH)
            }

            // Opacity.
            if (i < childOpacity.size && abs(child.alpha - childOpacity[i]) > 1e-3f) {
                child.alpha = childOpacity[i]
            }

            // Z-ordering via translationZ (elevation for shadows, translationZ for layering).
            if (i < childZIndex.size && abs(child.translationZ - childZIndex[i]) > 1e-3f) {
                child.translationZ = childZIndex[i]
            }
        }
    }

    // ── Layout ───────────────────────────────────────────────────────────────

    override fun onLayout(changed: Boolean, left: Int, top: Int, right: Int, bottom: Int) {
        super.onLayout(changed, left, top, right, bottom)

        val w = right - left
        val h = bottom - top
        val boundsChanged = w != lastAppliedWidth || h != lastAppliedHeight

        if (boundsChanged) {
            lastAppliedWidth = w
            lastAppliedHeight = h
            applyChildVisualStates()
        }
    }

    // ── Recycling ────────────────────────────────────────────────────────────

    fun prepareForRecycle() {
        // Save scroll position for restoration.
        if (hScrollView != null && layoutCacheId > 0) {
            val ox = hScrollView?.scrollX ?: 0
            if (ox > 1) {
                savedHScrollOffsets["${layoutCacheId}:${sectionIndex}"] = ox
            }
        }

        shadowNodePositioned = false
        childX = FloatArray(0)
        childY = FloatArray(0)
        childW = FloatArray(0)
        childH = FloatArray(0)
        childOpacity = FloatArray(0)
        childZIndex = FloatArray(0)
        stateChildTags = IntArray(0)
        lastScrollEventTime = 0
        lastAppliedWidth = 0
        lastAppliedHeight = 0
        needsScrollRestore = false
        pendingRestoreScrollX = 0f

        hScrollView?.scrollTo(0, 0)
        vScrollView?.scrollTo(0, 0)
    }

    companion object {
        /** Static scroll-offset registry — persists H scroll position across Fabric recycles. */
        val savedHScrollOffsets = HashMap<String, Int>()
    }
}
