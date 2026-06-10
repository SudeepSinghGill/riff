package com.riff

import com.facebook.react.module.annotations.ReactModule
import com.facebook.react.uimanager.ViewGroupManager
import com.facebook.react.uimanager.ThemedReactContext
import com.facebook.react.uimanager.ViewManagerDelegate
import com.facebook.react.uimanager.annotations.ReactProp
import com.facebook.react.viewmanagers.RNScrollCoordinatedViewManagerDelegate
import com.facebook.react.viewmanagers.RNScrollCoordinatedViewManagerInterface

/**
 * Fabric ViewManager for RNScrollCoordinatedViewView (sticky headers/footers).
 */
@ReactModule(name = RNScrollCoordinatedViewViewManager.NAME)
class RNScrollCoordinatedViewViewManager : ViewGroupManager<RNScrollCoordinatedViewView>(),
    RNScrollCoordinatedViewManagerInterface<RNScrollCoordinatedViewView> {

    companion object {
        const val NAME = "RNScrollCoordinatedView"
    }

    private val delegate = RNScrollCoordinatedViewManagerDelegate(this)

    override fun getName(): String = NAME

    override fun getDelegate(): ViewManagerDelegate<RNScrollCoordinatedViewView> = delegate

    override fun createViewInstance(context: ThemedReactContext): RNScrollCoordinatedViewView {
        return RNScrollCoordinatedViewView(context)
    }

    @ReactProp(name = "behavior")
    override fun setBehavior(view: RNScrollCoordinatedViewView, value: String?) {
        view.updateProps(
            newBoundaryY = Float.MAX_VALUE,
            newBoundaryX = Float.MAX_VALUE,
            newHeaderHeight = 0f,
            behavior = value ?: "push",
            newEnabled = true,
            kind = "",
            horizontal = false
        )
    }

    @ReactProp(name = "naturalY", defaultFloat = 0f)
    override fun setNaturalY(view: RNScrollCoordinatedViewView, value: Float) {}

    @ReactProp(name = "boundaryY", defaultFloat = Float.MAX_VALUE)
    override fun setBoundaryY(view: RNScrollCoordinatedViewView, value: Float) {}

    @ReactProp(name = "boundaryX", defaultFloat = Float.MAX_VALUE)
    override fun setBoundaryX(view: RNScrollCoordinatedViewView, value: Float) {}

    @ReactProp(name = "headerHeight", defaultFloat = 0f)
    override fun setHeaderHeight(view: RNScrollCoordinatedViewView, value: Float) {}

    @ReactProp(name = "enabled", defaultBoolean = true)
    override fun setEnabled(view: RNScrollCoordinatedViewView, value: Boolean) {}

    @ReactProp(name = "horizontal", defaultBoolean = false)
    override fun setHorizontal(view: RNScrollCoordinatedViewView, value: Boolean) {}

    @ReactProp(name = "type")
    override fun setType(view: RNScrollCoordinatedViewView, value: String?) {}

    @ReactProp(name = "kind")
    override fun setKind(view: RNScrollCoordinatedViewView, value: String?) {}

    @ReactProp(name = "index", defaultInt = 0)
    override fun setIndex(view: RNScrollCoordinatedViewView, value: Int) {}

    @ReactProp(name = "cacheKey")
    override fun setCacheKey(view: RNScrollCoordinatedViewView, value: String?) {}

    @ReactProp(name = "isMeasureOnly", defaultBoolean = false)
    override fun setIsMeasureOnly(view: RNScrollCoordinatedViewView, value: Boolean) {}
}
