# Riff Performance Optimization Plan

## Context

After implementing and reverting JS-level cell pooling, we did a thorough analysis comparing Riff's per-frame architecture with FlashList v2. The key finding: **Riff's biggest per-frame cost is not cell mount/unmount — it's the JSI spatial query marshalling on every scroll tick.** FlashList v2 (pure JS, zero native code) achieves good performance through cheap O(log n) binary search + JS key pooling. Riff's C++ ShadowNode gives superior measurement (1-frame convergence vs FlashList's 2+ frames), but the JSI boundary crossings on the scroll hot path negate that advantage.

---

## Per-Frame Cost Analysis: Riff vs FlashList v2

### What impacts UI FPS

| Work | Riff | FlashList v2 |
|------|------|-------------|
| Scroll offset tracking | `setScrollOffset` to C++ LayoutCache — O(1), mutex | Handled by RN ScrollView natively — zero cost |
| Position application | `applyPositionsFromState` — `child.frame = CGRect(...)` per cell, O(mounted) | JS `left`/`top` styles → Yoga → Fabric → UIKit — same pipeline |
| Sticky headers | KVO + `CATransform3D` — O(sticky_count), zero-lag | `Animated.Value` — 1 frame lag |
| Fabric commit processing | Custom ShadowNode `layout()` with Yoga + LayoutCache diffing — O(mounted) | Standard Fabric — no custom ShadowNode |

**Primary UI FPS bottleneck**: The custom ShadowNode's `correctChildPositionsIfNeeded()` runs on the Fabric BG thread. It iterates all mounted children twice (read cache positions, diff Yoga measurements) and may call `applyMeasurements()` to cascade. This delays the Fabric commit, which delays `applyPositionsFromState` on the UI thread.

### What impacts JS FPS

| Work | Riff | FlashList v2 |
|------|------|-------------|
| Visibility detection | 2 spatial queries via JSI → C++ (O(buckets+k), marshals ~30 attribute objects) | Binary search on JS array — O(log n), returns 2 integers |
| Range/budget computation | `applyBudget` + `computeMeasureRange` — 2 more JSI calls | Pure JS arithmetic |
| Per-scroll JSI calls | **4-6 total** (version check, 2x spatial, budget, measure range, blank area) | **Zero** |
| setState frequency | `setRenderRange` when window boundary moves | `setRenderId` when engaged indices change |
| Render loop | O(window_size) element creation for full window | O(window_size) but with recycled keys — Fiber UPDATE not CREATE |
| Measurement feedback | `useLayoutEffect` re-runs spatial query when LayoutCache version bumps | `useLayoutEffect` calls `measureLayout()` per cell |

**Primary JS FPS bottleneck**: The spatial query marshalling. `getAttributesInRect` returns an array of ~30 JSI objects, each with ~10 properties (frame, zIndex, alpha, sizingState, isSticky, etc.). That's ~300 JSI property constructions per scroll event. FlashList returns 2 integers.

---

## Why FlashList Can't Do Custom Layouts (and Riff Can)

FlashList's `getVisibleLayouts` assumes items are **sorted by primary axis position** and visible items form a **contiguous index range**. Binary search finds `startIndex` and `endIndex`. This is O(log n) but restricts layouts to:
- Linear (vertical/horizontal)
- Grid (row-major, sorted by Y)
- Masonry (column-major, approximately sorted by Y)

A circular layout, radial arc, or any layout where index order ≠ position order breaks the binary search assumption. Riff's spatial index (bucket-based, position-keyed) handles arbitrary item placement — the cost is O(buckets + k) regardless of index ordering.

---

## Positioning: Frames vs Transforms

**Current state**: Both Riff and FlashList v2 use frame-based positioning (`setFrame:` / `left`+`top`).

**Why transforms would be faster for repositioning**:
- `setFrame:` triggers UIView layout pass (bounds + center + subview adjustment)
- `layer.transform = CATransform3DMakeTranslation(x, y, 0)` is GPU-side compositing only — no layout pass
- For cells that just need repositioning (not resizing), transforms skip unnecessary work
- Riff already uses this for sticky headers — proven pattern

**Caveat**: Transforms work when the view's natural frame is fixed at origin. Cell content sizing (Yoga) must still use bounds. The transform only replaces the position offset, not the size.

---

## Optimizations (ordered by impact)

### Opt 1: Eliminate per-scroll spatial query for sorted layouts ✅
**Impact: HIGH** — removes the #1 JS thread cost

For layouts where items are sorted by primary axis: replace `getAttributesInRect` with O(log n) binary search returning `{first, last}` integers, like FlashList. Fall back to spatial query only when the layout opts in.

**Implementation**:
- Add `computeSortedRanges(scrollOffset, vpSize, renderMult, velocity)` to C++ WindowController
- It reads item positions from LayoutCache (already stored) and does binary search
- Returns `{renderFirst, renderLast, visibleFirst, visibleLast}` — 4 integers, single JSI call
- CollectionView.tsx uses this by default, falls back to `attributesForElements` only when `layout.needsSpatialQuery === true`
- **Per-scroll cost drops from**: 2 spatial queries + marshalling ~60 objects → 1 JSI call returning 4 integers

**Layout routing — `needsSpatialQuery` flag**:

Add an optional `needsSpatialQuery?: boolean` to the `CollectionViewLayout` protocol. Default: `false`. When `true`, the scroll handler falls back to `attributesForElements()` spatial query instead of binary search.

- Built-in layouts (`list`, `grid`, `masonry`, `flow`): always `false` — items are sorted, binary search works
- Custom layouts: default `true` (safe default — custom layouts may have non-contiguous visibility). Custom layout authors can explicitly set `false` if their layout produces sorted, contiguous visible ranges (e.g., a custom horizontal carousel that's still linear)

This gives custom layout authors control: `needsSpatialQuery: false` opts into the fast binary search path when they know their layout is sorted. The default protects against silent breakage for truly non-linear layouts.

**Files**: `cpp/WindowController.h`, `cpp/CollectionViewModule.cpp`, `src/types/protocol.ts`, `example/components/CollectionView.tsx`

### Opt 2: Batch JSI calls into single per-scroll call ✅
**Impact: MEDIUM** — reduces 4-6 JSI boundary crossings to 1

Combine version check + range computation + budget + measure range into one C++ function:
```cpp
struct ScrollState {
  int32_t renderFirst, renderLast, visibleFirst, visibleLast;
  int32_t measureFirst, measureLast;
  int32_t cacheVersion;
  float blankBefore, blankAfter;
};
ScrollState processScroll(scrollY, vpW, vpH, renderMult, velocity, ...);
```

Single JSI call, single mutex acquisition, zero intermediate JS work.

**Files**: `cpp/WindowController.h`, `cpp/CollectionViewModule.cpp`, `example/components/CollectionView.tsx`

### Opt 3: Transform-based cell positioning ⬜ (deferred)
**Impact: MEDIUM** — faster UI thread repositioning

Change `applyPositionsFromState` to use `layer.transform = CATransform3DMakeTranslation(x, y, 0)` instead of `setFrame:`. Cell's natural frame stays at `(0, 0, w, h)`.

**Consideration**: When cell SIZE changes (first measurement, or data change), need to update `bounds` too. Can check if w/h differ from current bounds and only set bounds when needed.

**Implementation note (2026-04-06)**: `layer.transform` (CATransform3D) does NOT affect UIKit's `view.frame` or `view.center`. Hit testing would break for cells whose natural frame is at `(0, 0)` while their visual position is at `(x, y)`. To use this approach, `_contentView` needs a `hitTest:withEvent:` override that accounts for layer transforms. Additionally: for position-only changes (same w/h), `setFrame:` already just calls `setCenter:` without triggering `layoutSubviews` — so the benefit is smaller than estimated. Deferred until other optimizations are validated and profiling confirms native positioning is still a bottleneck.

**Files**: `ios/RNCollectionViewContainerView.mm`

### Opt 4: JS-level cell recycling (revisit) ⬜
**Impact: MEDIUM** — reduces Fiber CREATE/DELETE overhead

The previous pooling attempt failed because native position application (via ShadowNode state) was desynced from React content updates. Two possible fixes:
- **Option A**: Apply positions from JS via transform styles (like FlashList does with `left`/`top`), bypassing the ShadowNode position pipeline for recycled cells
- **Option B**: Make the ShadowNode position application synchronous with the React commit by using the `cacheKey` prop (already works — the issue was the slot's `cacheKey` changing causing cache miss on the first frame)

SlotManager.ts is already written and correct. The integration needs the position sync fix.

**Files**: `example/components/CollectionView.tsx`, `example/components/SlotManager.ts`

### Opt 5: Return flat arrays from spatial queries ⬜
**Impact: LOW-MEDIUM** — for layouts that use per-scroll spatial queries

When a layout has `needsSpatialQuery: true` and the spatial query path is used, change `getAttributesInRect` to return `Float64Array` `[key_hash, x, y, w, h, section, index, ...]` instead of JSI objects. Eliminates ~300 JSI property constructions.

Applies to any layout that opts into spatial queries (custom layouts by default, or any layout that explicitly sets `needsSpatialQuery: true`).

**Files**: `cpp/LayoutCache.cpp`, JS consumer code

### Opt 6: Skip spatial query when range won't change ⬜ (reverted — too aggressive; needs smarter threshold)
**Impact: LOW** — pure JS optimization

Before calling into C++, check: `scrollOffset` is still within `(renderFirst_position + buffer, renderLast_position - buffer)`. If so, skip all JSI calls. Simple arithmetic in JS.

**Files**: `example/components/CollectionView.tsx`

### Opt 7: Incremental render loop (O(delta) instead of O(window_size)) ⬜
**Impact: MEDIUM** — reduces per-render JS work

Currently the `scrollContent` render loop rebuilds React elements for the FULL window (~30 cells) even when only 1-2 cells enter/leave. React's reconciler efficiently diffs the output, but element creation itself (JSX → `React.createElement` for 30 cells with nested view trees) is O(window_size) JS work per render.

**Two approaches:**

**A. Stable element map (recommended with SlotManager):** Maintain a `Map<slotKey, ReactElement>` across renders. SlotManager already knows which slots changed — only create/update elements for changed slots. Render loop becomes O(delta).

**B. React-only (no SlotManager):** Keep current approach — rebuild full array, let React diff. Simpler, but O(window_size) element creation remains. This is what we have today.

Approach A naturally pairs with Opt 4 (cell recycling). SlotManager tracks entering/leaving/unchanged slots. For unchanged slots, reuse the cached element reference — React sees referential equality and skips reconciliation entirely (not just skips DOM work, skips the diffing too).

**Files**: `example/components/CollectionView.tsx`, `example/components/SlotManager.ts`

---

## Recommended Execution Order

1. **Opt 1 + Opt 2 together** — combine sorted-range computation and batching into a single `processScroll` JSI call. This is the biggest bang-for-buck: removes 4-6 JSI calls and replaces them with 1 returning ~8 integers.
2. **Opt 3** — transform positioning. Independent of Opt 1/2, can be done in parallel.
3. **Opt 6** — quick JS-side win, add range-stability check before any JSI call.
4. **Opt 4 + Opt 7 together** — revisit recycling (SlotManager) + incremental render loop. SlotManager tracks the delta; render loop only creates elements for changed slots. Combined: fewer Fibers created AND less element creation per render.
5. **Opt 5** — only needed if spatial query layouts prove to be a bottleneck.

---

## Key Files

| File | Changes |
|------|---------|
| `cpp/WindowController.h` | Add `processScroll()` / `computeSortedRanges()` |
| `cpp/CollectionViewModule.cpp` | Expose new JSI bindings |
| `ios/RNCollectionViewContainerView.mm` | Transform-based positioning |
| `src/types/protocol.ts` | Add `needsSpatialQuery` to layout protocol |
| `example/components/CollectionView.tsx` | Use batched JSI call, range-stability check, layout routing |
| `example/components/SlotManager.ts` | Already exists — revisit for Opt 4 |

---

## Verification

1. **Benchmark before/after** each optimization using the existing benchmark suite (feed + search tabs)
2. **JS FPS** should improve most from Opt 1+2 (spatial query elimination)
3. **UI FPS** should improve from Opt 3 (transform positioning)
4. **Mount counts** should drop with Opt 4 (recycling revisit)
5. **Custom layouts** with `needsSpatialQuery: true` must still work — verify spatial query fallback path
6. **Custom layouts** with `needsSpatialQuery: false` must use binary search path — verify no visual breakage
7. **Sticky headers** — must still work with transform positioning (already use transforms)
8. **Variable-height cells** — measurement feedback loop must still converge in 1 frame
9. **MVC (maintain visible content position)** — offset correction must still work with transforms

---

## Commit status: Opt 1 + Opt 2

**Recorded on branch:** `cur-cell-pooling` — message `perf(rncv): Opt 1+2 batched processScroll; document API audit` (includes `PERF-PLAN.md` appendix + C++/iOS/JS hot path; `example/yarn.lock` left unstaged). Use `git log -1 --oneline` for the current hash.

**Scope:** Opt 1 (C++ spatial queries in `processScroll` + JS `windowController.processScroll`) and Opt 2 (single batched JSI call returning render/visible/measure ranges + `cacheVersion`) across `cpp/CollectionViewModule.cpp`, `cpp/WindowController.h`, related native/iOS glue, and `example/components/CollectionView.tsx`.

---

## API / packaging audit (post Opt 1+2)

### Where `CollectionView` lives today

The **consumer React component** (`Riff` / `CollectionView`) is implemented under **`packages/rn-collection-view/example/components/CollectionView.tsx`**, not under `packages/rn-collection-view/src/`.

**Why (POC constraint):** The library package can resolve a **different React instance** than the host app (`node_modules/react` inside the package vs the app). Mounting hooks from the wrong instance causes crashes and broken context. The repo comment in [`packages/rn-collection-view/src/index.ts`](packages/rn-collection-view/src/index.ts) states the component will be **re-exported from `src/` once the monorepo uses workspace-hoisted React** (single React for app + library).

**Implication:** [`src/index.ts`](packages/rn-collection-view/src/index.ts) today exports module, layouts, and types — **not** the `CollectionView` component as a drop-in import from the package root. Standalone-library ergonomics are incomplete until that move + Metro/tsconfig alignment.

### Callback and prop surface (current vs desired)

| Area | Current state | Gap vs “library should own internals” |
|------|----------------|----------------------------------------|
| Scroll callbacks | `onScroll` is wrapped internally then `scrollViewProps?.onScroll` is invoked. Drag/momentum handlers are passed **only** from `scrollViewProps` onto `RNCollectionViewContainer`. | Consumers still reach into `scrollViewProps` for several events; **not** a single top-level surface like FlatList. |
| `onContentSizeChange` | Supported as **top-level** `onContentSizeChange` **or** `scrollViewProps.onContentSizeChange` (single winner: top-level overrides scrollViewProps). | Desired: **one** CollectionView-level API; internally consume, adjust, then **forward** to consumer (and if both provided, policy should be explicit — e.g. call both or deprecate one). |
| Horizontal cross-axis height | **Internal:** auto-height from native `contentSize` with bootstrap guard (avoids latching full viewport height). **Demos:** `LayoutsTab` horizontal masonry / horizontal grid / adaptive H-grid still use **local `containerH` + `onContentSizeChange`** to size wrapper `View`s and show subtitles. | Demo code still duplicates work the component should own; should be removed once internal sizing is trusted everywhere. |
| Types | [`src/types/protocol.ts`](packages/rn-collection-view/src/types/protocol.ts) defines a slimmer `CollectionViewProps` (`onScroll` shape, `scrollViewProps?: Record<string, unknown>`) that **does not match** the rich `RiffProps` in the example component. | Public TS contract for a standalone package should be **one** aligned interface. |

### Recommended follow-ups (after perf branch is stable)

1. **API cleanup milestone:** Hoist ScrollView-equivalent callbacks to `CollectionView` top-level; keep `scrollViewProps` only for pass-through props that are not events (or document deprecation).
2. **Demo cleanup:** Remove `containerH` / `onContentSizeChange` wiring from horizontal demos in `LayoutsTab` so demos only use flex + `CollectionView` internal behavior.
3. **Package export:** Re-export `CollectionView` from `src/index.ts` when React hoisting is guaranteed; update Metro/tsconfig and example imports accordingly.
4. **Next perf work per this doc:** **Opt 3** (transform positioning) or **Opt 6** (range-stability skip), then **Opt 4+7** (recycling + incremental render), then **Opt 5** if spatial layouts remain hot.
