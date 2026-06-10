package com.riff

import android.content.Context
import android.view.ViewGroup

/**
 * Content ViewGroup that does NOT measure or layout children via the standard
 * Android measure/layout cycle. Children are Fabric-managed ReactViewGroups
 * positioned exclusively by applyPositionsFromState (from the C++ ShadowNode).
 *
 * Without this, the parent ScrollView triggers measureChildWithMargins on
 * Fabric children, which throws "A catalyst view must have an explicit width
 * and height" because they expect EXACTLY MeasureSpecs from Yoga.
 */
class RiffContentView(context: Context) : ViewGroup(context) {

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        // Report the full content size (set via layoutParams from updateState)
        // so the parent ScrollView knows the content extends beyond the viewport.
        // Don't measure children — they're Fabric-managed.
        val lp = layoutParams
        val w = if (lp != null && lp.width > 0) lp.width
                else MeasureSpec.getSize(widthMeasureSpec)
        val h = if (lp != null && lp.height > 0) lp.height
                else MeasureSpec.getSize(heightMeasureSpec)
        setMeasuredDimension(w, h)
    }

    override fun onLayout(changed: Boolean, l: Int, t: Int, r: Int, b: Int) {
        // Children are positioned by applyPositionsFromState, not by Android layout.
    }
}
