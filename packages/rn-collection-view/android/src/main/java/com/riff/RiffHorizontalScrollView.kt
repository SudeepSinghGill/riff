package com.riff

import android.content.Context
import android.view.MotionEvent
import android.view.ViewConfiguration
import android.widget.HorizontalScrollView
import kotlin.math.abs

/**
 * HorizontalScrollView that proactively claims the touch gesture on ACTION_DOWN
 * so a parent RiffScrollView (vertical) cannot intercept it prematurely.
 *
 * The disallow flag is released back to the parent if the move turns out to be
 * predominantly vertical (dy > touchSlop && dy > dx), allowing the parent to
 * take over and scroll vertically instead.
 */
class RiffHorizontalScrollView(context: Context) : HorizontalScrollView(context) {
    var onScrollListener: ((scrollX: Int) -> Unit)? = null

    private var downX = 0f
    private var downY = 0f
    private val touchSlop = ViewConfiguration.get(context).scaledTouchSlop.toFloat()

    override fun onTouchEvent(ev: MotionEvent): Boolean {
        when (ev.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                downX = ev.x
                downY = ev.y
                parent?.requestDisallowInterceptTouchEvent(true)
            }
            MotionEvent.ACTION_MOVE -> {
                val dx = abs(ev.x - downX)
                val dy = abs(ev.y - downY)
                if (dy > touchSlop && dy > dx) {
                    // Gesture is vertical — release parent so it can scroll.
                    parent?.requestDisallowInterceptTouchEvent(false)
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                parent?.requestDisallowInterceptTouchEvent(false)
            }
        }
        return super.onTouchEvent(ev)
    }

    override fun onScrollChanged(l: Int, t: Int, oldl: Int, oldt: Int) {
        super.onScrollChanged(l, t, oldl, oldt)
        onScrollListener?.invoke(l)
    }
}
