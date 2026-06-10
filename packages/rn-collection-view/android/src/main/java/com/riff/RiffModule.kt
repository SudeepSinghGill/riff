package com.riff

import android.app.ActivityManager
import android.content.ComponentCallbacks2
import android.content.Context
import android.content.res.Configuration
import android.os.Process
import android.view.Choreographer
import com.facebook.react.bridge.ReactApplicationContext

/**
 * Android TurboModule for Riff — extends the codegen-generated spec so the
 * native binary can find it via TurboModuleRegistry.getEnforcing('RiffModule').
 */
class RiffModule(reactContext: ReactApplicationContext) :
    NativeCollectionViewModuleSpec(reactContext), ComponentCallbacks2 {

    companion object {
        init {
            // Load the shared library that contains RiffModuleJNI.cpp (nativeInstall).
            // The library target is react_codegen_RiffSpec (CMakeLists.txt LIB_TARGET_NAME).
            System.loadLibrary("react_codegen_RiffSpec")
        }
    }

    // ── TurboModule: ping ────────────────────────────────────────────────────

    override fun ping(): String = "pong"

    // ── Frame metrics (Choreographer) ────────────────────────────────────────

    private var choreographerCallback: Choreographer.FrameCallback? = null
    private var lastFrameTimeNanos: Long = 0

    fun startFrameTimer() {
        stopFrameTimer()
        lastFrameTimeNanos = 0
        val callback = object : Choreographer.FrameCallback {
            override fun doFrame(frameTimeNanos: Long) {
                if (lastFrameTimeNanos > 0) {
                    // TODO: forward to C++ via JNI
                }
                lastFrameTimeNanos = frameTimeNanos
                Choreographer.getInstance().postFrameCallback(this)
            }
        }
        choreographerCallback = callback
        Choreographer.getInstance().postFrameCallback(callback)
    }

    fun stopFrameTimer() {
        choreographerCallback?.let { Choreographer.getInstance().removeFrameCallback(it) }
        choreographerCallback = null
    }

    // ── Memory ───────────────────────────────────────────────────────────────

    fun getAvailableMemory(): Long {
        val am = reactApplicationContext.getSystemService(Context.ACTIVITY_SERVICE) as? ActivityManager
            ?: return -1
        val memInfo = ActivityManager.MemoryInfo()
        am.getMemoryInfo(memInfo)
        return memInfo.availMem
    }

    // ── Memory pressure ──────────────────────────────────────────────────────

    override fun onTrimMemory(level: Int) {
        // TODO: forward to C++ via JNI
    }

    override fun onConfigurationChanged(newConfig: Configuration) {}

    override fun onLowMemory() = onTrimMemory(ComponentCallbacks2.TRIM_MEMORY_COMPLETE)

    // ── Lifecycle ────────────────────────────────────────────────────────────

    override fun initialize() {
        super.initialize()
        reactApplicationContext.registerComponentCallbacks(this)
        // Install CollectionViewModule as global.__riffNativeMod so JS can access
        // all JSI methods (createLayoutCache, layoutCacheById, etc.) via HostObject.
        // JavaTurboModule binding ignores C++ get() overrides, so we install separately.
        val holder = reactApplicationContext.javaScriptContextHolder
        if (holder != null && holder.get() != 0L) {
            nativeInstall(holder.get())
        }
    }

    private external fun nativeInstall(runtimePtr: Long)

    override fun invalidate() {
        stopFrameTimer()
        reactApplicationContext.unregisterComponentCallbacks(this)
        super.invalidate()
    }
}
