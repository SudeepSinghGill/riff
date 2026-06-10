package com.riff

import android.content.Context
import android.view.MotionEvent
import android.view.ViewConfiguration
import android.widget.ScrollView
import kotlin.math.abs

/**
 * ScrollView subclass that fires a callback on every scroll position change
 * and yields to child HorizontalScrollViews when a horizontal swipe is detected.
 *
 * Direction decision is deferred until the accumulated displacement exceeds
 * touchSlop — below that threshold both dx and dy are within finger noise
 * and making an early call causes intermittent gesture failures.
 *
 * Combined with requestDisallowInterceptTouchEvent(true) called by the child
 * HorizontalScrollView once it also detects horizontal movement, this gives
 * reliable nested scroll behaviour without requiring NestedScrollingParent.
 */
class RiffScrollView(context: Context) : ScrollView(context) {
    var onScrollListener: ((scrollY: Int) -> Unit)? = null

    private var downX = 0f
    private var downY = 0f
    private val touchSlop = ViewConfiguration.get(context).scaledTouchSlop.toFloat()

    override fun onInterceptTouchEvent(ev: MotionEvent): Boolean {
        when (ev.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                downX = ev.x
                downY = ev.y
            }
            MotionEvent.ACTION_MOVE -> {
                val dx = abs(ev.x - downX)
                val dy = abs(ev.y - downY)
                // Only commit to horizontal-yield once we've exceeded touch slop —
                // below slop, dx and dy are within normal finger noise.
                if (dx > touchSlop && dx > dy) return false
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                downX = 0f
                downY = 0f
            }
        }
        return super.onInterceptTouchEvent(ev)
    }

    override fun onScrollChanged(l: Int, t: Int, oldl: Int, oldt: Int) {
        super.onScrollChanged(l, t, oldl, oldt)
        onScrollListener?.invoke(t)
    }
}
