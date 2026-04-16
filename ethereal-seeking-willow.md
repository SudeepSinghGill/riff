# Riff — Working Plan & Roadmap

## Current State (2026-04-16)

**Branch:** `cur-cell-pooling` — all perf + layout work lives here.

**JS FPS:** ~60fps on Feed tab (parity with FlashList). Achieved by:
- measureAhead=0 (eliminates invisible cell Fabric creation cost)
- Binary search for sorted layouts (replaces spatial queries)
- SlotManager short-circuit, decoration cache, element cache cascade fix
- Reduced window defaults (renderMultiplier=0.5, mountedWindowSize=2.0)
- Velocity leadBoost cap (1.5x instead of 4.0x)
- PerfHUD disabled during comparison, Feed demo uses refs not setState

**Layouts working:** List V, Grid V, Grid H, Masonry V, Flow V (after flatIndex fix), Radial, Circular
**Layouts with known issues:** List H (cross-axis height bounce), Grid H (same), Masonry H (items same size — pre-existing), Flow H (untested)

---

## Roadmap (in order)

| # | Task | Status | Notes |
|---|------|--------|-------|
| 1 | **Remaining perf fixes** | ✅ Done | Change C: `processScroll` now returns flat frame array; `renderCell` reads width/height from it (eliminates ~30 JSI calls/render). `computeCacheKey` for headers/footers derived without JSI. Change F: entering/leaving loops deferred to `setImmediate` with coalescing (removes O(384) `keyExtractor` loop from `onScroll`). |
| 2 | **Flow V fix** | ✅ Done | FlowLayout flatIndex wiring verified; binary search path works for Flow V. |
| 3 | **H-list cross-axis height bounce** | ⬜ | Layout loop: measure → container resize → shouldInvalidate → re-layout → repeat. Affects List H, Grid H. Needs dedicated investigation. See `memory/project_hlist_bounce.md` |
| 4 | **H-list S[0] header half height** | ⬜ | Same root cause as #3 — _maxCrossAxisHeight starts from estimate, grows as items measured |
| 5 | **Perf findings writeup** | ✅ Done | Added `Performance Investigation Results (2026-04-12 → 2026-04-16)` section in `docs/COLLECTIONVIEW_INTERNALS.md`. |
| 6 | **Compositional layout** | ⬜ | UICollectionView CompositionalLayout-like API. Orthogonal scrolling sections. Custom layouts per section. Discuss API design (Compose LazyList-like?) |
| 7 | **Diff engine + Snapshot API** | ⬜ | F1.1 (C++ diff engine), F1.2 (Snapshot API for batch mutations) |
| 8 | **Cell animations** | ⬜ | F1.2b (enter/exit animations), F1.2c (UICollectionView-parity animations) |
| 9 | **H-masonry fix** | ⬜ | All items render same size in H mode. Pre-existing, needs investigation |
| 10 | **Flow justification** | ⬜ | F-Flow.1: leading/center/trailing/spaceBetween/spaceEvenly |
| 11 | **Flow item weight/stretching** | ⬜ | F-Flow.2 |
| 12 | **Grid rowAlignment** | ⬜ | F-Grid.1: top/center/bottom for uneven heightForItem rows |
| 13 | **State persistence & restoration** | ⬜ | F4 |
| 14 | **P6.2 — Device measurement session** | ⬜ | Release build, real device, FlashList comparison benchmarks |

---

## Key Files

| File | Purpose |
|------|---------|
| `example/components/CollectionView.tsx` | Main component (~2500 lines) |
| `example/components/SlotManager.ts` | Cell recycling (Opt 4) |
| `cpp/LayoutCache.h/.cpp` | C++ position cache + binary search |
| `cpp/CollectionViewModule.cpp` | TurboModule + processScroll JSI |
| `cpp/CollectionViewContainerShadowNode.cpp` | Fabric ShadowNode |
| `ios/RNCollectionViewContainerView.mm` | Native UIScrollView wrapper |
| `ios/RNMeasuredCellView.mm` | Cell native view (updateLayoutMetrics override) |
| `src/layouts/*.ts` | Layout engines (list, grid, masonry, flow) |
| `cpp/layouts/*.cpp` | C++ layout engines |
| `PERF-PLAN.md` | Full optimization plan + analysis |
| `docs/COLLECTIONVIEW_INTERNALS.md` | Architecture reference |
