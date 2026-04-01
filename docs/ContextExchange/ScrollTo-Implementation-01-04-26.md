# ScrollTo Implementation — 01 Apr 2026

## What was done

Implemented `scrollToItem` and `scrollToOffset` imperative API (L4). Three commits on `cur-scrollto`, merged to main.

## Architecture: 3-layer scroll dispatch

```
JS: scrollToItem(key, options)
  → nativeLayoutCache.getAttributes(key) → frame
  → compute targetY for position mode (top/center/bottom/nearest)
  → clamp to [0, contentHeight - viewportHeight]
  → nativeMod.scrollTo(cacheId, x, y, animated)

C++ JSI: CollectionViewModule::get("scrollTo")
  → returns HostFunction that calls invokeScrollHandler(cacheId, x, y, animated)
  → scroll handler registry: static map<int32_t, function> in CollectionViewModule.cpp
  → handler was registered by native view, dispatches to main queue

Native: RNCollectionViewContainerView
  → _scrollToX:y:animated: → [_scrollView setContentOffset:target animated:]
  → clamped to valid range (0..contentSize-bounds)
```

## Scroll handler registry design

The C++ JSI module cannot reference the ObjC native view directly. Instead:

- **Registry:** `static unordered_map<int32_t, ScrollHandler>` + mutex in `CollectionViewModule.cpp`
- **Registration:** Native view registers in `updateProps:` when `layoutCacheId` changes
- **Unregistration:** Native view unregisters in `prepareForRecycle`
- **Invocation:** JSI `scrollTo` function calls `invokeScrollHandler(cacheId, ...)` which copies the handler out of the map (under lock), then calls it outside the lock
- **Thread safety:** Handler is a lambda that captures `__weak` view ref and `dispatch_async`s to main queue

Declarations in `LayoutCacheRegistry.h` (thin header, no heavy deps). Implementations in `CollectionViewModule.cpp` (same file as the registry static).

## Key design decision: stable keys

`scrollToItem` accepts the consumer-facing stable key format: `"sectionKey:itemId"` (e.g. `"cell-animation:s1-17"`).

This is the **same** key format the C++ ListLayout uses when `keyExtractor` is provided:
- JS builds `layoutContext.sections[s].itemKeys = s.data.map((item, i) => sectionKey:keyExtractor(item, i))`
- `list.ts` passes this as `params.keys = sec.itemKeys` to C++ `computeSections`
- C++ `paramsFromJSI` reads `keys` property → `ListLayoutParams::keys`
- C++ `itemKey()` returns `p.keys[i]` when available, fallback `prefix + i` (positional)

No key translation layer needed. The layout cache, renderCell, measuredHeightForItem, attributesForItem, and scrollToItem all use the same stable key.

Headers/footers always use positional keys: `item-{sectionIndex}-header` / `item-{sectionIndex}-footer`.

## Key design decision: contentHeightRef

`useImperativeHandle` deps are `[data, keyExtractor, onDataChange]`. Adding `contentHeight` would recreate the handle on every layout pass (content height changes whenever items are measured).

Solution: `contentHeightRef` mirrors `contentHeight` state (updated at both call sites of `setContentHeight`). `scrollToItem` reads `contentHeightRef.current` for the live value. Same pattern as existing `viewportHeightRef`.

## Bugs found and fixed

### 1. prepareForRecycle stale layoutCacheId

**Symptom:** Scroll buttons do nothing. `invokeScrollHandler` found registrySize=0.

**Root cause:** `prepareForRecycle` called `unregisterScrollHandler(_layoutCacheId)` but didn't reset `_layoutCacheId = 0`. When a recycled view received `updateProps:` with the same cacheId, the guard `newProps.layoutCacheId != _layoutCacheId` was false → registration skipped.

**Fix:** `_layoutCacheId = 0;` after unregistering in `prepareForRecycle`.

### 2. contentHeight stale closure

**Symptom:** `_scrollToX` called with `y=0` for all items. Logs showed `frame.y=3611, targetY=3391, contentH=0, maxY=0, clamped=0`.

**Root cause:** `contentHeight` (React state, init 0) captured in `useImperativeHandle` closure whose deps don't include it. The closure saw `contentHeight=0` forever → `maxY=0` → all offsets clamped to 0.

**Fix:** Added `contentHeightRef = useRef(0)`, updated at both `setContentHeight` call sites, read `contentHeightRef.current` in `scrollToItem`.

## Files modified

| File | Changes |
|---|---|
| `cpp/CollectionViewModule.cpp` | Scroll handler registry (static map + mutex), `scrollTo` JSI property in `get()` |
| `cpp/LayoutCacheRegistry.h` | Forward declarations for `registerScrollHandler`, `unregisterScrollHandler`, `invokeScrollHandler` |
| `ios/RNCollectionViewContainerView.mm` | `_scrollToX:y:animated:` method, handler registration in `updateProps:`, unregistration + reset in `prepareForRecycle` |
| `example/components/CollectionView.tsx` | `scrollToItem`/`scrollToOffset` in `useImperativeHandle`, `contentHeightRef`, `ScrollToItemOptions`/`ScrollToOffsetOptions` types, `RiffHandle` interface |
| `example/screens/comparison/LayoutsTab.tsx` | Wired "→ Top", "→ #42", "→ Bot" buttons |

## Next: L3 — Decoration Views

Create branch `cur-decorative-views` from main. See `ethereal-seeking-willow.md` for full design.
