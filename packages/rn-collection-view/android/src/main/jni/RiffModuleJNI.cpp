/**
 * RiffModuleJNI.cpp — JNI bridge for RiffModule.nativeInstall().
 *
 * Installs a CollectionViewModule C++ HostObject as global.__riffNativeMod on
 * the JSI runtime. JS then accesses this global instead of the JavaTurboModule,
 * getting the full JSI binding surface (createLayoutCache, layoutCacheById, etc.).
 *
 * Called from RiffModule.initialize() in Kotlin, which runs synchronously during
 * TurboModuleRegistry.getEnforcing('RiffModule') — so the global is set before
 * NativeCollectionViewModule.ts finishes evaluating.
 */

#include "CollectionViewModule.h"
#include "LayoutCacheRegistry.h"
#include <jsi/jsi.h>
#include <jni.h>
#include <mutex>
#include <unordered_map>

using namespace facebook;

// ── JavaVM reference ─────────────────────────────────────────────────────────
// Captured once in JNI_OnLoad; used by the scroll handler to switch threads.

static JavaVM* gJavaVM = nullptr;

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
  gJavaVM = vm;
  return JNI_VERSION_1_6;
}

// ── Scroll handler registry ───────────────────────────────────────────────────
// Each RNCollectionViewContainerView registers itself here so that C++
// invokeScrollHandler (called from the JS thread via nativeMod.scrollTo) can
// bounce back to Kotlin on the main thread.

struct JScrollEntry {
  jobject viewRef;   // GlobalRef to RNCollectionViewContainerView
  jmethodID mid;     // scrollToFromNative(float x, float y, boolean animated)
};

static std::mutex                            gScrollMutex;
static std::unordered_map<int32_t, JScrollEntry> gScrollEntries;

// ── nativeSetScrollOffset ─────────────────────────────────────────────────────

extern "C" JNIEXPORT void JNICALL
Java_com_riff_RNCollectionViewContainerView_nativeSetScrollOffset(
    JNIEnv *env, jobject /*thiz*/, jint cacheId, jfloat scrollX, jfloat scrollY) {
  auto cache = facebook::react::layoutCacheForId(static_cast<int32_t>(cacheId));
  if (cache) {
    cache->setScrollOffset(
        static_cast<double>(scrollX),
        static_cast<double>(scrollY),
        0.0 /* timestampMs — 0 disables velocity calculation from here */);
  }
}

// ── nativeRegisterScrollHandler ───────────────────────────────────────────────
// Called from Kotlin when layoutCacheId is assigned. Stores a GlobalRef to the
// view and registers a C++ lambda that calls scrollToFromNative() via JNI.

extern "C" JNIEXPORT void JNICALL
Java_com_riff_RNCollectionViewContainerView_nativeRegisterScrollHandler(
    JNIEnv* env, jobject thiz, jint cacheId) {

  jclass cls = env->GetObjectClass(thiz);
  jmethodID mid = env->GetMethodID(cls, "scrollToFromNative", "(FFZ)V");
  env->DeleteLocalRef(cls);
  if (!mid) return;

  jobject globalRef = env->NewGlobalRef(thiz);

  {
    std::lock_guard<std::mutex> lock(gScrollMutex);
    // Release any previous entry for this cacheId.
    auto it = gScrollEntries.find(static_cast<int32_t>(cacheId));
    if (it != gScrollEntries.end()) {
      JNIEnv* e = nullptr;
      if (gJavaVM && gJavaVM->GetEnv((void**)&e, JNI_VERSION_1_6) == JNI_OK) {
        e->DeleteGlobalRef(it->second.viewRef);
      }
      gScrollEntries.erase(it);
    }
    gScrollEntries[static_cast<int32_t>(cacheId)] = { globalRef, mid };
  }

  facebook::react::registerScrollHandler(
      static_cast<int32_t>(cacheId),
      [cacheId](double x, double y, bool animated) {
        if (!gJavaVM) return;

        JScrollEntry entry;
        {
          std::lock_guard<std::mutex> lock(gScrollMutex);
          auto it = gScrollEntries.find(cacheId);
          if (it == gScrollEntries.end()) return;
          entry = it->second;
        }

        JNIEnv* e = nullptr;
        bool attached = false;
        int status = gJavaVM->GetEnv((void**)&e, JNI_VERSION_1_6);
        if (status == JNI_EDETACHED) {
          gJavaVM->AttachCurrentThread(&e, nullptr);
          attached = true;
        } else if (status != JNI_OK) {
          return;
        }

        e->CallVoidMethod(
            entry.viewRef, entry.mid,
            static_cast<jfloat>(x),
            static_cast<jfloat>(y),
            static_cast<jboolean>(animated ? JNI_TRUE : JNI_FALSE));

        if (attached) gJavaVM->DetachCurrentThread();
      });
}

// ── nativeUnregisterScrollHandler ────────────────────────────────────────────

extern "C" JNIEXPORT void JNICALL
Java_com_riff_RNCollectionViewContainerView_nativeUnregisterScrollHandler(
    JNIEnv* env, jobject /*thiz*/, jint cacheId) {

  facebook::react::unregisterScrollHandler(static_cast<int32_t>(cacheId));

  std::lock_guard<std::mutex> lock(gScrollMutex);
  auto it = gScrollEntries.find(static_cast<int32_t>(cacheId));
  if (it != gScrollEntries.end()) {
    env->DeleteGlobalRef(it->second.viewRef);
    gScrollEntries.erase(it);
  }
}

// ── nativeInstall ─────────────────────────────────────────────────────────────

extern "C" JNIEXPORT void JNICALL
Java_com_riff_RiffModule_nativeInstall(JNIEnv *env, jobject /*thiz*/, jlong runtimePtr) {
  if (runtimePtr == 0) return;
  auto &rt = *reinterpret_cast<jsi::Runtime *>(runtimePtr);

  // Create a CollectionViewModule C++ HostObject.
  // jsInvoker is null here — it's only needed for memory pressure callbacks
  // (metrics/memory features). Core layout operations (createLayoutCache,
  // layoutCacheById, listLayout, etc.) don't require it.
  auto cppModule = std::make_shared<facebook::react::CollectionViewModule>(nullptr);

  // Install as global.__riffNativeMod — a JSI HostObject whose get() provides
  // createLayoutCache(), layoutCacheById(), layoutCache, listLayout, etc.
  rt.global().setProperty(
      rt,
      "__riffNativeMod",
      jsi::Object::createFromHostObject(rt, cppModule));
}
