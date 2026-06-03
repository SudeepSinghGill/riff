# Session Handoff — 2026-04-06

## What was done this session

All 5 branches from the consolidated multi-branch plan are complete and merged to main.

### Branch 1: `cur-cleanup` — 5j + API Audit
- **5j done**: Removed `position: 'absolute', left, top` from JS cell wrapper style (`containerStyle`). ShadowNode handles all positioning for all 4 layout types.
- `containerStyle` now only has `width: cellWidth` + optional H-supplementary `height`.
- Removed `estimatedTop`, `cellLeft`, dead `keyToFlatIndex` from `FlattenResult`, `measuredHeightForItemRef` dead initial body.
- Decoration `<RNMeasuredCell>` style: only `width`, `height`, `zIndex` (no position/left/top).
- Legacy section backgrounds in `renderScrollView` left untouched — they are ScrollView siblings, not ShadowNode children.
- Removed `computeGridLayout`, `computeMasonryLayout`, `computeFlowLayout` legacy declarations from nativeMod type casts.
- Added `readonly horizontal: boolean` to `CustomLayoutEngine` and `CustomLayoutDelegate`.
- Removed `keyToFlatIndex` entirely — only used in debug logging, `attrToFlatIndex` covers render-range.

### Branch 2: `cur-masonry-window-fix` — Windowing bugs
- `rectsIntersect` in `Geometry.h`: changed strict `< >` to `<= >=` (inclusive boundary — items touching scroll boundary are now included).
- Phase A stride fix in `CollectionView.tsx`: `effectiveStride = stride / colCount` where `colCount` is read via `(effectiveLayout as any).delegate?.columns` for grid/masonry. List/flow use `colCount = 1`. No protocol change needed.
- Rejected adding `columnsCount?` to the `CollectionViewLayout` protocol — windowing is an internal concern.

### Branch 3: `cur-riff-resize` — Resize + Circular/Carousel
- RiffDemo animated resize: `Animated.spring` on `widthAnim` interpolated to `['60%', '100%']`.
- Responsive column breakpoints: GridDemo `columns: (w) => w > 280 ? 3 : w > 180 ? 2 : 1`, MasonryDemo `columns: (w) => w > 200 ? 2 : 1`.
- Added `circular` and `carousel` tabs — `CircularList` and `Carousel3D` wired directly (plain ScrollView components, not CollectionView custom layout).
- `CIRCULAR_DATA` (12 items) and `CAROUSEL_DATA` (8 items) defined inline in RiffDemo.
- `RESIZE_TABS = ['grid-v', 'masonry-v', 'list-v', 'flow-v']` — resize button dimmed for other tabs.

### Branch 4: `cur-flashlist-comparison` — Feed + Search tabs
- `PerfHood.tsx`: floating bottom-right overlay with JS FPS (from `useFPS()`), mount count, render count. Pointer events disabled.
- `FeedComparisonTab.tsx`: 150 heterogeneous feed items — 5 cell types (image-card, text-post, multi-image, action-bar, banner), 5-10 nested Views each. Riff/FlashList toggle. PerfHood active.
- `SearchComparisonTab.tsx`: 1500 homogeneous search rows (icon + title + subtitle) at fixed 56px. FlashList's best case. Riff competitive. PerfHood active.
- `Comparison.tsx`: added `'feed'` and `'search'` to `TabId`, tab bar converted to horizontal `ScrollView` (was fixed `View` — only 8 tabs fit).

## Architecture notes

- **No `useNativeFPS` needed**: `useFPS()` (JS rAF) is sufficient for comparison. PerfTab set the precedent.
- **Responsive columns mechanism**: `columns` is a function `(w: number) => number` on the layout delegate. Called with measured `containerWidth` in `prepare()` on every invalidation. `shouldInvalidate` returns `true` when width changes by > 0.5pt. No separate system — this is the existing mechanism.
- **CircularList/Carousel3D not wired through CollectionView custom layout**: They remain standalone plain-ScrollView components. Wiring them through `custom()` layout would require extending `LayoutAttributes` with transform fields (scale, rotateY, perspective) and applying them in cell rendering. Not done — flagged as future if needed.

## Current state of main

All branches merged. Main is clean. No open branches.

## What's next (if continuing)

Nothing from the 5-branch plan is remaining. Possible next directions:
1. **Device testing session** (P6.2 from PLAN.md) — run on physical device, verify all tabs
2. **FlashList comparison writeup** — benchmark notes, screenshots, differentiator list
3. **Separators for Masonry-V / Flow-V** — deferred from this session
4. **H-masonry fix** — all items same size bug
5. **Transform fields in LayoutAttributes** — for true CollectionView-powered circular/carousel layouts
6. **PerfHood FPS accuracy** — rAF-based measurement on JS thread is inherently noisy; native CADisplayLink path would be more accurate

## Key files touched this session

| File | Change |
|---|---|
| `example/components/CollectionView.tsx` | 5j cleanup, Phase A stride fix |
| `cpp/Geometry.h` | rectsIntersect inclusive boundary |
| `src/layouts/grid.ts`, `masonry.ts`, `flow.ts` | removed legacy method declarations |
| `src/layouts/custom.ts` | added `horizontal` property |
| `src/types/protocol.ts` | `CustomLayoutDelegate.horizontal` added |
| `example/screens/RiffDemo.tsx` | resize toggle, circular/carousel tabs |
| `example/screens/comparison/LayoutsTab.tsx` | responsive columns for Grid/Masonry |
| `example/components/PerfHood.tsx` | new — perf overlay |
| `example/screens/comparison/FeedComparisonTab.tsx` | new — 150 hetero feed items |
| `example/screens/comparison/SearchComparisonTab.tsx` | new — 1500 search rows |
| `example/screens/Comparison.tsx` | Feed/Search tabs, horizontal tab bar |
