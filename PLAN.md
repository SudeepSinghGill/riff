# RNCollectionView — Implementation Plan (Performance-First)

Platform: iOS first, then Android. RN New Architecture (Fabric + JSI). RN 0.83.4 (React 19.2).
Graceful degradation: RN 0.76+ (new arch, Activity absent).

**Ordering principle:** Performance, speed, memory, stability, metrics, and traces FIRST.
Features second. Research last. Documentation closes it out.

---

## ✅ Completed Milestones

### Phase 0 — Project Foundation (DONE)
- **M0.1** Package scaffold — buildable package, example app
- **M0.2** Core TypeScript types — Rect, Size, Point, LayoutAttributes, etc.
- **M0.3** C++ JSI module boilerplate — `ping()→"pong"` synchronous JSI call

### Phase 1 — Layout Engine (DONE)
- **M1.1** LayoutCache (C++) — CRUD, version, getAll, getTotalContentSize, getSectionOffsets
- **M1.2** ListLayout: fixed height — 10k items × 72px in < 1ms
- **M1.3** ListLayout: estimated + invalidation — variable heights, invalidateFrom pivot
- **M1.3b** TS layout parity — TSLayoutCache, TSListLayout, CustomLayoutPlugin
- **M1.4** SpatialIndex — getAttributesInRect, bucket-based, < 0.05ms for 10k items
- **M1.5** ListLayout: multi-section — 10 sections × 1k items, headers, footers

### Phase 2 — Scroll + Rendering (DONE)
- **M2.1** CollectionView shell + pluggable ScrollView
- **M2.2** Native scroll bridge — UIScrollViewDelegate → C++ window controller on UI thread
- **M2.3** Render all cells (absolute positioning, no virtualization)
- **M2.4** Window controller — visible/render tiers, velocity-adaptive, mountedWindowSize budget

### Phase 3 — Virtualization (DONE)
- **M3.1** Activity-based cell suspension (CellWrapper with Activity=hidden/visible)
- **M3.3** Cold eviction — unmount outside render window, remount at correct position
- **M3.4** Velocity-adaptive window — render range expands toward direction of travel
- **M3.5** Cell budget (mountedWindowSize) — mounted content capped in viewport multiples

### Phase 4 — Sizing Strategies (DONE)
- **M4.1** Estimated sizing — variable-height mode, RAF-batched scroll corrections, measure range
- **M4.2** RNMeasuredCell Fabric component — native `layoutSubviews` fires `onMeasured` before paint
- **M4.3** Self-sizing cells — tap to expand/collapse, dynamic resize, automatic scroll correction

### JS Optimizations Applied (within M4.1/M4.2)
1. Activity mode fix — render-range=visible, only measure-range=hidden
2. Microtask flush for off-screen cells
3. Running average height fallback for unmeasured items
4. Measure range in scroll handler (same frame as render range)
5. Skip startTransition for measure range on fast scroll
6. Phantom correction fix — measure-only cells removed from RAF batch
7. `initialNumToRender` — first screenful on first frame
8. `useLayoutEffect` for layout pass — committed before paint
9. Eager initial range in `onContainerLayout` — 3-render init chain → 2
10. `React.memo` on cell content (MemoizedCellContent) + stable renderItem ref — 1fps → 40-50fps win on simulator. Scroll events no longer re-render stable cells.
11. `startTransition` placement — render range updates are intentionally synchronous (delayed range = blank frames). Only snapshot API data commits use startTransition. `useLayoutEffect` for layout pass is incompatible with startTransition by design.

---

## 🔥 Phase P1 — C++ Window Controller on UI Thread

> **Priority: HIGHEST.** The single hottest path. Every scroll event flows through here.

### P1.1 — Window controller → C++ JSI module

Port window boundary computation from JS to native C++. Not optional.

**Why:** JS window controller runs in Hermes on the main thread. Even with synchronous JSI,
the JS-side computation pays interpreter overhead (~2–5ms). C++ is sub-microsecond.

**Deliverable:** `cpp/WindowController.h/.cpp`
- Registered as synchronous JSI module
- Scroll position fed from UIScrollView delegate callback (C++ → C++, no JS)
- Window boundary computation in C++ (arithmetic + clamp)
- Notifies React via batched async dispatch when tier assignments change
- React processes tier changes via startTransition (non-urgent)

**Acceptance:**
- Window boundary update: < 0.5ms from UIScrollView delegate to boundary update
- Instruments Time Profiler: zero JS frames in scroll hot path
- All M2.4 + M3.x test cases pass unchanged
- Blank area at 2000px/s fling: same or better than JS baseline

**Deps:** M2.4 (behavioral reference), M0.3 (C++ JSI infrastructure)

---

## ~~Phase P2 — YGMeasureFunc: Native Cell Measurement~~ ✗ INFEASIBLE

### ~~P2.1~~ — Custom YGMeasureFunc for cell height — **INFEASIBLE**

**Why infeasible:** Fabric restricts `YGMeasureFunc` to **leaf nodes only** — nodes with
no Fabric children. Cell containers always have children (the consumer's component tree),
so they cannot be leaf nodes. No workaround without forking Fabric internals.

**What we have instead (M4.2):** `RNMeasuredCell` fires `onMeasured` from `layoutSubviews`
before the first paint — this fires synchronously during the native layout pass, before any
frame is drawn. It's effectively as good as YGMeasureFunc for the initial measurement case.
Self-sizing cells (M4.3) continue to use this path for dynamic resizes.

---

## ✅ Phase P3 — Offscreen Pre-rendering via Activity

### P3.1 — Activity-flip pre-rendering ✅ DONE

**Implemented approach (RN 0.83+ / Activity API):**
Measure-range cells mount at their real estimated position with `Activity=hidden`.
The cell is invisible to the user but Fabric lays it out at the correct location.
When the viewport reaches the cell, only the Activity mode flips (`hidden→visible`) —
a single atomic Fabric commit with no position change, no re-render, no blank flash.

**Degraded path (RN < 0.83, Activity absent):** cells park at `top: -9999`.

**Why the original plan's approach was abandoned:**
The plan described building shadow trees via Fabric's internal commit pipeline ahead of time.
This uses non-public Fabric APIs (`FabricUIManager.createNode`, scheduler internals) that are
not accessible from userspace RN. The Activity-flip approach achieves the same visible result
(cell appears instantly at correct position) without touching internals.

**Deps:** M3.1, M4.2

---

## ✅ Phase P4 — Memory Optimization

### P4.1 — Mounted cell budget refinement ✅ DONE

**Implemented:**
- `nativeMod.memory` JSI sub-object: `availableBytes()`, `pressureLevel()`, `onPressure(cb)`, `simulate(level)`
- `os_proc_available_memory()` (iOS) wired via ObjC callback — synchronous, called on JS thread
- `UIApplicationDidReceiveMemoryWarningNotification` → C++ `triggerMemoryPressure(2)` → `jsInvoker_->invokeAsync` → JS callback
- CollectionView internal `memoryMultiplier` state: 1.0 / 0.75 / 0.5 on levels 0/1/2
- `effectiveMountedWindowSize = mountedWindowSize × memoryMultiplier` — all applyBudget calls use this
- Test screen `P4_1_MemoryBudget.tsx`: live available MB, pressure level, mounted count, simulate buttons
- Android: deferred to F5 / R0.3 (uses `ActivityManager.getMemoryInfo()` + `onTrimMemory`)

**Deps:** M3.5

---

## ✅ Phase P5 — Instrumentation & Metrics

### P5.1 — Metric collection infrastructure ✅ DONE

Core metric pipeline baked into the component.

**Deliverable:** `src/metrics/MetricCollector.ts`
- Frame time: CADisplayLink callback (native) → circular buffer (C++) → JS read
- Blank area: computed from visibleRect vs cells in visible tier each frame
- Cell render time: timestamp at mount → timestamp at visible flip
- Cold mount rate, scroll correction count, pre-render hit rate
- All metrics collected always (low overhead), ring buffer storage

**Acceptance:**
- Frame time accurate to ±0.5ms (CADisplayLink hardware timer)
- Blank area ratio updates every frame during scroll
- Overhead: < 0.5ms/frame on iPhone 12

**Deps:** M3.3, M2.2

---

### P5.2 — Debug Perf HUD ✅ DONE

Live metrics overlay in the sample app.

**Deliverable:** `src/debug/CollectionViewHUD.tsx`
- Toggle via shake gesture or programmatic API
- Displays: FPS, blank area %, mounted cells, cold mount rate, corrections, memory
- Color overlay mode: tints cells by tier (visible/render/measure)
- Works in release builds

**Acceptance:**
- HUD appears within 100ms of toggle
- Metrics update at 10Hz (not 60Hz)
- HUD is a separate React root (doesn't affect CV perf)

**Deps:** P5.1

---

### P5.3 — Traces in sample app ✅ DONE

Instrument the example app with structured traces for release-build profiling.

**Deliverable:**
- `os_signpost` integration for scroll handler, layout pass, cell mount/unmount
- Instruments trace template for CollectionView analysis
- Sample app screens annotated with trace regions
- Export capability: trace summary as JSON for comparison

**Acceptance:**
- Instruments shows named signpost regions for all critical paths
- Trace data available in release builds (signpost, not NSLog)
- Example app produces reproducible trace sessions

**Deps:** P5.1

---

## Phase P6 — FlashList Comparison (Release Build)

### P6.1 — FlashList comparison demo

Side-by-side CollectionView vs FlashList across 6 tabs. Each tab isolates one
differentiator that is either impossible or visibly broken in FlashList. Same data,
same cell complexity, same interaction — only the list engine differs.

**Deliverable:** `example/screens/Comparison.tsx` (extend existing)

**Pre-requisites:**
- F3.2 — MasonryLayout (C++) for Tab 4
- Circular TS layout plugin for Tab 4
- FlashList installed in example app as a dependency

---

**Tab 1 — Prefetch + Simulated Loading** _(strongest visual)_

Cells contain simulated "images" — colored gradients behind a 300–800ms random delay
before they "load." CollectionView's `onPrefetch` fires 12× viewport ahead, so loading
starts well before cells are visible.

| | CollectionView | FlashList |
|---|---|---|
| Scroll at moderate speed | Cells arrive fully loaded, zero placeholders | Every new cell shows gray skeleton, pops in after delay |
| Mechanism | `onPrefetch(keys)` → start load → cell mounts with data ready | No prefetch API — load starts on mount |
| Visual | Smooth, populated content | Flickering gray → content transitions |

---

**Tab 2 — Sticky Headers: Push + Animation Continuity** _(undeniable)_

5+ sections. Each sticky header contains:
- A **millisecond ticker** updating every 16ms (shows elapsed time since section appeared)
- A **shimmer animation** — looping gradient sweep, purely cosmetic but continuous

Two FlashList bugs demonstrated at once:

| | CollectionView | FlashList |
|---|---|---|
| Push behavior | Incoming header pushes outgoing header up pixel-perfectly (RNScrollCoordinatedView, CATransform3D on UI thread) | Headers overlap at the top — no push logic |
| Animation continuity | Outgoing header's ticker and shimmer never reset — same component instance, just translated | Header re-mounts on section change — ticker resets to 0, shimmer restarts from frame 1 |
| JS per scroll frame | Zero — KVO on contentOffset, transform in native | JS handler repositions sticky view each frame |

---

**Tab 3 — Section Decorations with Animated Backgrounds** _(visual polish gap)_

5 sections, each with a distinct animated background:
- Looping shimmer gradients (e.g. gold shimmer, blue wave, green pulse)
- Animated via `Animated.loop` — continuous, never restarts
- Background spans behind all cells in the section (headers, items, footers)
- Cells float above the decoration with transparent/semi-transparent styling

The decoration is a real stateful React component — not a paint trick. Within the
render window, the animation runs continuously. Scrolling a section out and back in
(within render range) shows the shimmer exactly where it left off.

| | CollectionView | FlashList |
|---|---|---|
| Section backgrounds | `renderSectionBackground` — first-class API, real React component | No concept of decoration views |
| Animated backgrounds | Works — looping Animated.View behind cells | Would require absolute-positioned Views with manual height calc, breaks on dynamic content |
| Animation persistence | Within render window: animation continues seamlessly | N/A — can't be built |

---

**Tab 4 — Custom Layouts: Masonry + Circular** _(capability gap)_

Two sub-demos showing layouts that FlashList structurally cannot do:

**Masonry (C++):** Variable-height items in a 2–3 column waterfall. Items placed in
shortest column, no gaps, no overlaps. Layout computed in C++ in < 1ms for 1k items.
`cpp/layouts/MasonryLayout.h/.cpp`.

**Circular / Radial (TS CustomLayoutPlugin):** Items arranged in an arc.
`x = cx + r·cos(θ)`, `y = cy + r·sin(θ)`. Demonstrates arbitrary 2D positioning
via the TS layout plugin path. Scrolling rotates items around the arc.

| | CollectionView | FlashList |
|---|---|---|
| Masonry | Native C++ layout, sub-ms computation | Impossible — layout assumes linear sequential Y |
| Radial / circular | TS plugin: arbitrary x,y per item | Impossible — scroll container is linear, items must be sequential along one axis |
| Grid | C++ GridLayout (F3.1) | Possible (numColumns prop) — parity here |

---

**Tab 5 — Performance Metrics** _(hard numbers across 4 scenarios)_

Same metrics measured across 4 cell composition scenarios that exercise different
recycling/windowing trade-offs. Each scenario is a sub-tab or picker within Tab 5.

**Scenarios:**

| # | Scenario | What it tests | FlashList advantage | CollectionView advantage |
|---|---|---|---|---|
| 1 | **Homogeneous, fixed height** — 10k identical cells (text only, 44px) | FlashList's best case: single cell type, perfect pool reuse | Maximum recycling efficiency — one pool, instant reuse | C++ layout, zero-JS scroll path. Should be competitive even here. |
| 2 | **Homogeneous, dynamic height** — 10k cells, same component but varying text lengths (3–8 lines) | Measurement overhead. FlashList recycles but must re-measure. | Pool still useful (one type) but measurement causes layout shifts | Native measurement before first paint (M4.2). Scroll corrections < FlashList's. |
| 3 | **Heterogeneous, repeating** — 10k cells, 4–5 distinct item types (image card, text row, banner, compact row, separator) repeating in pattern | Recycling pool per type. FlashList benefits from type-based pools. | Multiple pools, each type recycled. This is FlashList's design target. | No recycling overhead. Same windowing cost regardless of type count. |
| 4 | **Heterogeneous, non-repeating** — Long product-detail-style page. Each cell is unique (hero image, description, specs table, reviews, related items, legal text). 50+ unique components, no repetition. | **Recycling is useless** — every cell is unique, pool never hits. FlashList pays mount cost on every recycle. | None. Pool miss on every cell = full mount cost, same as no recycling. | **Clear win.** Windowing + Activity suspension. Cells stay mounted within render range. No mount/unmount churn. Budget-controlled eviction only for far-off cells. |

**Metrics per scenario:**

| Metric | How measured | Notes |
|---|---|---|
| FPS | CADisplayLink hardware timer, 30-frame rolling average | Measured during sustained 2000px/s fling |
| Blank area % | Visible rect vs mounted cell rects, per frame | Key metric for scenario 2 (dynamic height) |
| Mounted cell count | `onRenderCountChange` callback | CV: budget-controlled. FlashList: pool size |
| Layout computation time | Timed in C++ / JS respectively | CV: < 1ms (C++). FlashList: 5–50ms (JS) |
| Mount/unmount rate | Count cell mount/unmount events per 1000 frames | **Scenario 4 killer metric**: FlashList mounts on every recycle, CV mounts once |
| Memory (available MB) | `os_proc_available_memory()` | CV: adaptive budget. FlashList: static |
| Memory pressure response | Simulate via P4.1 | CV: budget halves instantly. FlashList: no response |

**Expected narrative across scenarios:**
- Scenario 1: FlashList competitive or slightly ahead (its ideal case). CV shows parity.
- Scenario 2: CV ahead on scroll corrections and blank area (native measurement).
- Scenario 3: Close. FlashList pools help, but CV's zero-recycle avoids type-mismatch pool misses.
- Scenario 4: **CV clear winner.** FlashList degrades to full-mount-per-scroll. CV's windowed cells stay alive. Mount rate: CV near-zero vs FlashList ~every cell.

---

**Tab 6 — Dynamic Resize Reflow** _(architectural differentiator)_

Animated container resize (simulating iPad split-view or foldable) showing layout
adaptation per-frame. Container width animates 100%→50%→100% over ~2 seconds.

| | CollectionView | FlashList |
|---|---|---|
| Resize cost | O(window) — layout recomputes ~30 visible items per frame | O(N) — `relayoutFromIndex(0)` recomputes all items |
| C++ layout | Masonry reflows in <0.1ms per frame | N/A — all JS |
| Frame drops | None — windowed computation | Likely drops on large datasets |
| shouldInvalidate | Layout decides if bounds change requires recompute | Always full relayout on onLayout |

Demonstrates with:
1. C++ masonry layout (sub-ms windowed recompute)
2. TS custom layout (still fast — windowed)
3. Frame time overlay showing per-frame layout cost

---

**Tab 7 — State Bleed** _(soft demo, honest framing)_

Like buttons + TextInput in cells. Scroll away and back.

| | CollectionView | FlashList |
|---|---|---|
| Within render window (5× viewport) | State preserved — Activity suspension | State lost — cell recycled to different item, old state bleeds |
| Outside render window | Clean remount — correct initial state | Same recycling behavior |
| Failure mode | State absent (clean) — never shows wrong state | State corrupt — likes/text appear on wrong items |

Labeled honestly: "manageable in FlashList by lifting state, but default behavior
differs. CollectionView's default is correct; FlashList's default is broken."

---

**Acceptance criteria:**
- All 7 tabs functional on device (release build)
- Prefetch tab: zero visible loading placeholders in CollectionView at moderate scroll
- Sticky tab: millisecond ticker provably continuous across section transitions
- Decoration tab: shimmer animation visibly continuous, no restart on scroll
- Layout tab: masonry + circular render correctly, FlashList side shows "not possible"
- Metrics tab: all 6 metrics displayed live, comparable numbers
- Resize tab: masonry reflows smoothly during animated width change, frame time overlay shows <1ms layout cost
- State tab: state bleed reproducible in FlashList within 5 seconds of scrolling

**Deps:** F3.2 (masonry), circular TS layout, P5.1, F1.3, F2.2–F2.4, P4.1

---

### P6.2 — Device measurement session

Objective performance characterization on real hardware.

**Deliverable:** Documented benchmark results
- Instruments Time Profiler: JS thread occupancy during fast scroll (CV vs FlashList)
- Blank area ratio at 1000/2000/4000 px/s fling
- Memory: heap delta after scrolling 10k items
- Frame drop count per 1000 frames (CADisplayLink)
- Devices: iPhone 15 Pro (A17) + iPhone 12 (A14) minimum

**Acceptance:**
- All measurements recorded and committed
- Release build comparison documented
- Clear win/loss/parity assessment per metric

**Deps:** P5.1, P5.3, P6.1

---

## Phase F1 — Data Layer (Features)

### F1.1 — Diff engine (C++)

Key-based diff, runs off main thread.

**Deliverable:** `cpp/DiffEngine.h/.cpp`
```
diff(oldKeys, newKeys) → { inserted, removed, moved }
```
- Identity diff: same key = same item
- O(n) for pure insertions/deletions; O(n log n) for moves

**Acceptance:**
- 1k item diff (50 inserts, 20 deletes, 5 moves): < 2ms incl. JSI string marshalling
  (JSI utf8() costs ~0.65µs/string; pure C++ diff algorithm is sub-ms at any realistic size)
- Correctness: diff(A, B) applied to A produces B exactly

**Deps:** M0.3

---

### F1.2 — Snapshot API

Consumer-facing mutation API modelled after `NSDiffableDataSourceSnapshot`.
All mutation methods are **identity-based** (keys, not indices) — matching Apple's
design that eliminated the index-ordering bugs of the old `performBatchUpdates` era.

**Deliverable:** `example/components/CollectionSnapshot.ts` + CollectionView `handle` prop
```typescript
// Identity-based mutations (UICollectionView-style)
const snap = listRef.current.snapshot()
snap.appendItems(items)
snap.deleteItems(keys)                           // array of keys
snap.moveItem(key, { after: anchorKey })         // single move per call (sequential)
snap.reloadItems(keys)
listRef.current.apply(snap)                      // diff + LayoutAnimation + startTransition
// — or for simple refresh (FlashList-style): just change data prop, no snapshot needed
```

Move is intentionally singular (not array) — each move changes relative positions
for subsequent moves, so ordering is sequential. Same design as Apple's snapshot API.

**Acceptance:**
- Append 100 items: only new items computed in layout
- Delete item at index 50 of 1000: items 51–999 recomputed, 0–49 unchanged
- Apply during active scroll: no interruption (startTransition)
- Move/shift animations via LayoutAnimation on apply()

**Deps:** F1.1, M3.3

---

### F1.2b — Enter/exit cell animations _(follow-up)_

Per-item mount/unmount animations for snapshot transitions.

**Deliverable:**
- Deleted cells: keep mounted briefly, animate opacity→0 + collapse, then unmount
- Inserted cells: mount at opacity 0, animate to 1 + expand
- "Pending removal" queue in the cell renderer

**Acceptance:**
- Delete visually fades out before unmounting
- Insert visually fades in after mounting
- Interruptible: new snapshot applied mid-animation cancels gracefully

**Deps:** F1.2

---

### F1.2c — UICollectionView-parity animations _(follow-up)_

Full coordinated batch animations matching UICollectionView's native behavior.

**Deliverable:**
- Per-item animation types (fade, slide, custom)
- Interruptible spring physics
- Coordinated batch: inserts, deletes, and moves animate simultaneously
- Animation completion callbacks

**Acceptance:**
- Visual parity with `UICollectionViewDiffableDataSource.apply(snapshot, animatingDifferences: true)`
- Animations interruptible mid-flight by new snapshot apply
- Custom animation configuration per operation type

**Deps:** F1.2b

---

### F1.3 — Prefetch callbacks

Notify consumer as items enter/leave the data window.

**Deliverable:**
```typescript
dataSource.onPrefetch = (keys) => { /* fetch */ }
dataSource.onEvict = (keys) => { /* cancel, release */ }
```

**Acceptance:**
- `onPrefetch` fires ~12× viewport ahead
- `onEvict` fires after cell leaves render window + budget

**Deps:** M3.4, M2.4

---

## Phase F2 — Supplementary Views & Sticky Headers

### F2.1 — Non-sticky supplementary views

Section headers and footers, same tiering as cells.

**Deliverable:**
- `SupplementaryRegistry`: register(kind, Component)
- Layout cache includes supplementary LayoutAttributes (from M1.5)
- Rendered at layout positions, enters/leaves Activity tiers
- Budget: default 20

**Acceptance:**
- 10-section list: headers at correct Y positions
- Headers participate in windowing

**Deps:** M3.3, M1.5

---

### F2.2 — Sticky headers (basic)

Headers stick at top while their section is visible.

**Deliverable:**
- C++ sticky position calculation on UI thread:
  `stickyY(section, scrollY) = max(header.y, scrollY)`
- Separate native view positioned in viewport space
- C++ updates Y every scroll frame

**Acceptance:**
- Section header sticks at top during scroll
- Returns to natural position when scrolling back
- Zero JS involvement during scroll

**Deps:** F2.1, M2.2

---

### F2.3 — Sticky push behavior

Incoming header pushes current sticky header upward (UICollectionView correctness).

**Deliverable:**
```cpp
float stickyY(int section, float scrollY) {
  float naturalY = headers[section].frame.y;
  float nextHeaderY = headers[section + 1].frame.y;
  float clampedY = nextHeaderY - headerHeight[section];
  return max(naturalY, min(scrollY, clampedY));
}
```

**Acceptance:**
- Section 1 header pushed up exactly as section 2 header arrives at top
- Multiple sections in rapid succession: all push correctly

**Deps:** F2.2

---

### F2.4 — Decoration views

Data-free views: section backgrounds, separators.

**Deliverable:**
- Layout engine emits decoration LayoutAttributes (kind + frame, no data)
- `layout.registerDecoration(kind, NativeComponent)` — native, not React
- Not in Activity budget (simple static views, no fiber)

**Acceptance:**
- Section background matches section's total frame
- Decorations not in React DevTools (native views)

**Deps:** F2.1

---

## Phase F3 — Additional Layout Types

### F3.1 — GridLayout (C++)

Fixed-column equal-width grid.

**Deliverable:** `cpp/layouts/GridLayout.h/.cpp`
```
columns, columnSpacing, rowSpacing, estimatedItemHeight, sectionInsets
```
Item width = (viewportWidth - insets - spacing) / columns.

**Acceptance:**
- 3-column grid, 100 items: < 1ms, correct frames
- Integrates with selfSizing (row height = tallest item)

**Deps:** M1.5

---

### F3.2 — MasonryLayout (C++)

Variable-height column-packing.

**Deliverable:** `cpp/layouts/MasonryLayout.h/.cpp`
- Place each item in shortest column
- `invalidateFrom`: recompute masonry from that item onward

**Acceptance:**
- 2-column, 50 items (100–400px heights): no gaps, no overlaps
- `totalContentSize.height` = max column height

**Deps:** M4.2

---

### F3.5 — FlowLayout (C++)

Variable-width item packing with dynamic line wrapping (UICollectionViewFlowLayout equivalent).

**Deliverable:** `cpp/layouts/FlowLayout.h/.cpp`
- Items placed left-to-right, wrapping to next line when row is full
- Per-item `{width, height}` via `sizeForItem(index, section)` callback
- Row height = tallest item in that row
- `itemSpacing` (horizontal between items) + `lineSpacing` (vertical between rows)
- Section insets, header/footer support (same pattern as other layouts)
- Windowed computation: only compute positions for items in mounted range
- `shouldInvalidate(forBoundsChange:)`: returns true when container width changes (column count changes)

**Why C++:** Flow layout's bin-packing is the most compute-intensive built-in layout — items of varying width require per-item iteration to determine line breaks. C++ makes this sub-ms even for large datasets.

**Acceptance:**
- 1k items with varied widths (40–200px): correct wrapping, < 1ms
- Container resize: line breaks recomputed, items reflow
- Integrates with supplementary views (headers/footers between sections)

**Deps:** M1.5, R1.3 (layout protocol)

---

### F3.3 — CompositionalLayout

Sections with independent layout objects.

**Deliverable:** `src/layouts/CompositionalLayout.ts`
```typescript
new CompositionalLayout({
  sections: (sectionIndex, environment) => SectionLayoutDescriptor
})
```

**Acceptance:**
- 3 sections: list + grid + masonry — correct frames, sections stacked

**Deps:** F3.1, F3.2

---

### F3.4 — Orthogonal scrolling sections

Horizontal sections within vertical list.

**Deliverable:**
- `OrthogonalSection.tsx`: horizontal CollectionView with own window controller
- Parent treats it as single item of fixed/selfSized height
- Saves/restores own scroll position

**Acceptance:**
- App Store–style layout works
- Orthogonal section outside parent render window: all cells unmounted

**Deps:** F3.3, M3.3

---

## Phase F4 — State Persistence & Restoration

### F4.1 — Layout cache serialization (JSON scaffold)

Serialize LayoutCache to disk. Correctness focus.

**Deliverable:** `cpp/LayoutCacheSerializer.h/.cpp`
- JSON format (temporary — F4.4 replaces with FlatBuffers)
- MMKV via JSI storage
- Cache key: SHA1(listId + dataHash + viewportWidth + layoutConfig)

**Acceptance:**
- 10k items: round-trip byte-identical
- Cache key invalidates on viewport width change

**Deps:** M1.1

---

### F4.2 — Scroll position persistence (native iOS)

Restore scroll position before first Fabric commit.

**Deliverable:**
- On dealloc/viewWillDisappear: write contentOffset to NSUserDefaults
- On viewWillAppear: `setContentOffset:animated:NO` synchronously

**Acceptance:**
- Navigate away + back: exact previous position, zero flash at y=0
- Viewport size changed: stored position cleared

**Deps:** M2.1

---

### F4.3 — Full restoration sequence

Wire F4.1 + F4.2 with data validation.

**Deliverable:** `src/hooks/useStateRestoration.ts`
1. Read scroll offset from native storage → set UIScrollView contentOffset
2. Hydrate LayoutCache from MMKV
3. Compute initial visibleRect from restored scrollY
4. Enter cells into tiers
5. Validate cache: diff current vs cached keys
6. If data changed: invalidateFrom, re-layout

**Acceptance:**
- Full nav cycle (unchanged data): position exact, no layout recomputation
- Full cycle (data changed): position corrected for insertion delta
- Process death + relaunch: restored from MMKV + NSUserDefaults

**Deps:** F4.1, F4.2, F1.2

---

### F4.4 — FlatBuffers layout cache serialization

Replace JSON with FlatBuffers. Zero-copy mmap hydration.

**Why:** JSON parse of 10k LayoutAttributes ≈ 30–50ms. FlatBuffers mmap is zero-copy.

**Deliverable:**
- `fbs/LayoutCache.fbs` — FlatBuffers schema
- MMKV stores raw FlatBuffers bytes
- On restore: mmap MMKV value directly, wrap with FlatBuffers accessor
- Retire JSON serializer

**Acceptance:**
- Serialize 10k items: < 3ms
- Deserialize (mmap, no parse): < 0.1ms
- Layout cache available on frame 0

**Deps:** F4.1

---

## Phase F5 — Cross-Platform

### F5.1 — Android port

Port C++ modules and React component to Android (new architecture).

**Deliverable:**
- `android/` with CMakeLists wired to existing `cpp/` (no C++ duplication)
- `CollectionViewModule.kt` — TurboModule registration
- Scroll observer equivalent for Android
- All M1–M3 test screens pass on Android emulator

**Acceptance:**
- M1.1–M1.5 layout tests: green on Android
- M2.2 scroll bridge: scrollY tracking confirmed
- M3.3 cold eviction: cells unmount/remount as on iOS
- No iOS-only code in `cpp/`

**Deps:** M3.5. **Platform: Android new architecture (RN 0.76+).**

---

## Phase R1 — Research

### R0 — Memory Optimization (Future Research)

Additional memory optimizations beyond P4.1. None are needed for the POC but documented here for production hardening.

**R0.1 — Proactive memory polling**
Poll `availableBytes()` every 5s and pre-emptively reduce budget at level 1, before the OS warning arrives (which is very late). Add hysteresis: restore budget only after memory stays above threshold for 10s to prevent thrashing. Pure JS-side addition to CollectionView.tsx.

**R0.2 — measuredHeightsRef eviction**
The `measuredHeightsRef` Map grows unboundedly — one entry per unique cell key ever measured. At 100k items with variable heights this holds 100k entries (~3–4 MB in V8/Hermes). Fix: evict entries for cells more than `renderMultiplier + measureAhead` viewports away from the visible range. LRU or distance-based.

**R0.3 — Android memory integration**
`ActivityManager.getMemoryInfo()` for `availableBytes()`. `ComponentCallbacks2.onTrimMemory(level)` maps to pressure levels: `TRIM_MEMORY_RUNNING_LOW` → 1, `TRIM_MEMORY_RUNNING_CRITICAL` / `TRIM_MEMORY_COMPLETE` → 2. Wired in `CollectionViewModule.kt`, zero C++ changes needed.

**R0.4 — Image/asset pressure isolation**
When cells contain images, the image cache (not cell views) dominates memory. Track a `hasImages` hint and under pressure: evict cells that are images-only first (highest bytes/cell ratio) rather than furthest-first.

**R0.5 — Per-cell memory estimation**
`Instrumentation.newAllocatedSize()` (Android) / `mach_task_self()` vm_stats (iOS) to measure actual bytes per cell type. Use this to build a typed budget (e.g. "max 20 image cells + 60 text cells") rather than a flat count budget.

---

### R1.1 — UICollectionView host architecture (design + prototype)

Decouple JS component identity from native UIView allocation using UICollectionView
as the physical scroll and layout host.

**Architecture:**
- Native: UICollectionView owns scroll + pool of ~20 UICollectionViewCell shells
- JS/React: one component per item, stable identity, full local state
- Bridge: React component's UIView reparented into whichever cell slot displays it
- Activity mode="hidden": React component stays mounted, UIView held in holding container

**Open questions:**
1. Fabric shadow tree ownership when moving UIView out of shadow-tree parent
2. Scroll ownership: UICollectionView vs RN ScrollView gesture conflict
3. Concurrent React interaction: Fabric may reposition "borrowed" views

**Deliverable:** Design document + working prototype (not production-ready)

**Deps:** P6.2

---

### R1.2 — Virtual-to-physical ShadowNode mapping

Custom Fabric ComponentDescriptor: N virtual ShadowNodes → M physical UIViews (M << N).
React sees N unique components. UIKit sees M reused views.

**Status:** Research. Implement only if R1.1 proves insufficient.

**Deliverable:** Feasibility report + proof-of-concept if feasible

**Deps:** R1.1

---

### R1.3 — Layout Protocol & Unified API Design

Formalize the layout protocol, dimension provider contracts, and three-tier consumer API.

**Core Protocol (aligned with UICollectionView):**
- `CollectionViewLayout` interface: `prepare()`, `attributesForElements(inRect:)`, `attributesForItem()`, `attributesForSupplementary()`, `contentSize()`, `shouldInvalidate(forBoundsChange:)`, `invalidationScope()`
- Each layout type defines its own delegate contract (strict, not optional):
  - **ListLayoutDelegate**: `itemHeight` (fixed) OR `heightForItem(index, section)` (variable) + same pattern for header/footer
  - **MasonryLayoutDelegate**: `columns` + `heightForItem` (mandatory) + header/footer heights
  - **GridLayoutDelegate**: `columns` + `rowHeight` OR `heightForItem` + header/footer heights
  - **FlowLayoutDelegate**: `sizeForItem(index, section) → {width, height}` (mandatory) + header/footer heights
  - **CustomLayoutDelegate**: `attributesForItem(index, section, context) → LayoutAttributes`
- Sizing is symmetric: whatever pattern a layout uses for items, it uses for supplementary views too (fixed OR estimated OR per-index callback)
- Per-index callbacks (not bulk arrays) — layout calls only for windowed range, enabling O(window) not O(N) per frame

**Three-Tier Consumer API:**
- **Tier 1 (simple):** `data` + `renderItem` + `itemHeight` on CollectionView directly. `renderSectionHeader`/`renderSectionFooter` on component, sizing on layout. `stickyHeaderIndices`/`stickyFooterIndices` for index-based pinning.
- **Tier 2 (layout config):** `layout={masonry({columns: 3, heightForItem: fn, stickyMode: 'push'})}`. Layout owns sizing, pinning, behavior.
- **Tier 3 (power user):** `supplementaryItems` on section config — custom kinds, alignment, `pinToVisibleBounds`, `pinBehavior`. Full `attributesForItem` for custom layouts.

**Supplementary View Model (UICollectionView-aligned):**
- Supplementary items are per-section, with `kind`, `alignment`, `pinToVisibleBounds`, `pinBehavior`
- Tier 1 `header`/`footer` shorthand maps to supplementary items internally
- Renderers (`renderSectionHeader`, `renderSectionFooter`) live on CollectionView (view/data layer)
- Sizing/pinning lives on layout delegate (layout layer)
- `stickyMode` on layouts that support it; absent from custom layouts (custom handles own pinning)
- Any layout can support pinning if it defines what "pinned to visible bounds" means in its coordinate space

**Cache Integration:**
- All layouts (C++ and TS) write `LayoutAttributes` to the shared C++ LayoutCache via JSI
- Enables spatial indexing (`getAttributesInRect`) for all layout types
- C++ layouts write directly; TS layouts write via JSI bindings

**Deliverable:** TypeScript interfaces + protocol implementation + migration of existing layouts

**Deps:** F3.1, F3.2, F3.5

---

### R1.4 — TS-to-C++ layout codegen (was R1.3)

Auto-transpile `CustomLayoutPlugin.compute()` from TypeScript to C++ at build time.

**Why:** TS layouts run on the JS thread — one frame behind the native scroll event. C++ layouts run on the UI thread in the same CADisplayLink frame. The 3D carousel and circular layout demonstrate the frame-lag visually (slight jitter on fast scroll). Codegen eliminates it without asking developers to write C++.

**Feasibility:** Layout `compute()` functions are pure: typed numeric inputs → array of `{x, y, width, height, scale, rotateY, opacity}`. No closures, no GC, no dynamic dispatch. Operations map 1:1 to C++ (`Math.cos` → `std::cos`, array iteration → `for` loop, arithmetic → same). The `CustomLayoutPlugin` interface is already the right contract shape.

**Approach:**
1. Static analysis of the `compute()` function body — reject if it contains non-transpilable constructs (closures over mutable state, dynamic property access, async)
2. AST-to-AST transform: TS AST → C++ AST (arithmetic, trig, array ops, struct construction)
3. Generate `cpp/layouts/Generated_<PluginName>.h/.cpp` with JSI bindings
4. Wire into CollectionViewModule automatically (build-step registration)
5. Runtime: use generated C++ version; fall back to TS if codegen was skipped

**Alternatively:** Static Hermes (Meta's AOT compiler for typed JS) may make this unnecessary by compiling typed JS directly to native machine code. Monitor SH progress before building custom codegen.

**Deps:** F3.3 (CompositionalLayout — proves the plugin interface is stable)

---

## Phase DOC — Documentation

### DOC.1 — Solution document (HLD, LLD, optimizations)

Comprehensive technical document covering the entire implementation.

**Deliverable:** Solution document with:
- **High-Level Design (HLD):** Architecture overview, component relationships, data flow,
  platform strategy (iOS-first, Android port), technology choices and rationale
- **Low-Level Design (LLD):** C++ module internals (LayoutCache, ListLayout, SpatialIndex,
  WindowController), Fabric component (RNMeasuredCell), JS component (CollectionView.tsx),
  memory management, threading model
- **All optimizations:** Numbered list of every optimization applied, why it was needed,
  what it fixed, before/after impact (from JS optimizations #1–9 through P1–P3)
- **Design decisions log:** Key architectural decisions with alternatives considered,
  trade-offs, and reasoning (no-recycling, C++ vs TS layout, Activity API usage,
  dual-RN-instance workaround, YGMeasureFunc, etc.)
- **Problem-solving log:** All significant bugs/issues encountered, root cause analysis,
  and solutions (codegen pipeline, RCTThirdPartyComponentsProvider, Folly coroutine headers,
  slow init, phantom corrections, etc.)
- **Performance comparison:** FlashList vs CollectionView benchmark results and analysis

**Acceptance:**
- Complete, accurate, reviewable by someone unfamiliar with the project
- All design decisions and optimizations documented with rationale
- Can serve as onboarding material for new contributors

**Deps:** All prior milestones complete (or near-complete)

---

## Execution Order

```
COMPLETED:  M0.1–M0.3 → M1.1–M1.5 → M2.1–M2.4 → M3.1–M3.5 → M4.1–M4.3
                                                                    ↓
PERFORMANCE:  P1.1 (C++ window ctrl) ✅ → P2.1 (YGMeasureFunc — INFEASIBLE, leaf-node constraint) ✗
              P3.1 (Activity-flip pre-render) ✅ + React.memo opt ✅ + startTransition audit ✅
                                                                    ↓
METRICS:      P5.1 (collection) → P5.2 (HUD) → P5.3 (traces)
              [needed to quantify everything that follows]
                                                                    ↓
FEATURES:     F1.1 (diff engine) ✅ → F1.2 (snapshot API) ✅ → F1.3 (prefetch) ✅
              F2.1 (supplementary) ✅ → F2.2 (sticky) ✅ → F2.3 (sticky push) ✅ → F2.4 (decorations) ✅
              F3.1 (grid) → F3.2 (masonry) → F3.3 (compositional) → F3.4 (orthogonal)
              F4.1–F4.4 (persistence)
              F5.1 (Android)
                                                                    ↓
MEMORY:       P4.1 (budget refinement) ✅ — os_proc_available_memory + UIApplicationDidReceiveMemoryWarning
                                                                    ↓
COMPARISON:   P6.1 (full demo: state/animation/form/media/layout/sticky/snapshot/perf)
              → P6.2 (device benchmarks on release build)
              [all features complete — the money shot]
                                                                    ↓
RESEARCH:     R1.1 (UICollectionView host) → R1.2 (virtual ShadowNode)
                                                                    ↓
DOCS:         DOC.1 (solution document)
```

---

## POC Checkpoints

| After | What you can show |
|---|---|
| M4.3 ✅ | Variable height, self-sizing, scroll correction — all working |
| P1.1 ✅ | C++ window controller — JS scroll path largely native |
| P3.1 ✅ | Pre-rendering at real position — Activity-flip approach (Fabric-internal APIs not accessible) |
| memo ✅ | React.memo on cell content — JS FPS 1→40-50fps on simulator |
| P2.1 ✗ | YGMeasureFunc infeasible — leaf-node constraint; M4.2 (layoutSubviews) is equivalent |
| P5.2 ✅ | Perf HUD — live FPS, blank area, cold mount rate, memory overlay |
| P4.1 ✅ | Memory budget — os_proc_available_memory, pressure levels, automatic budget reduction |
| F1.2 | Snapshot API — insert/delete/move with per-item animation, O(delta) reconciliation |
| F2.3 | Sticky headers, UICollectionView-style — one instance repositioned, no duplication |
| F3.3 | CompositionalLayout — list + grid + masonry + carousel in one scroll view |
| F4.3 | State restoration — navigate away and back, exact position on frame 0 |
| P6.2 | **Full FlashList comparison on release build — the money shot** |
| F5.1 | Android port — same C++ engine, cross-platform |
| DOC.1 | Complete solution document with all optimizations and design decisions |
