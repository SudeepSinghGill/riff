# Working Plan — Riff List Demo + Phase 5

## Status Summary

| Phase | Status |
|---|---|
| L1 – Re-verify ShadowNode (insets, spacing, multi-section, sticky) | ✅ Done |
| L2 – ListDemo: sticky identity, cell animation, insets+spacing, mutations | ✅ Done |
| MVC – insert/delete scroll correction | ✅ Done |
| MVC – size-change scroll correction | ✅ Done |
| L5 – Mutation buttons + MVC toggle in ListDemo | ✅ Done (wired in LayoutsTab.tsx) |
| L4 – ScrollToItem | ✅ Done (branch: `cur-scrollto`, merged to main) |
| L3 – Proper Decoration Views | 🔄 In progress (branch: `cur-section-footer-test`, built, fixing 8 bugs) |
| L3.fix – Decoration bug fixes (stale frames, MVC anchor, fingerprint, demo UI) | ⬜ Next |
| L3.1 – Decoration contentInsets API (frame adjustment like NSCollectionLayoutDecorationItem) | ⬜ After L3.fix |
| 5g – Extend ShadowNode to grid/masonry/flow | ⬜ After L3.1 |
| 5j – Remove JS cell wrapper positioning | ⬜ After 5g |

---

## Execution Order

```
L4 (scrollTo) ✅ → L3 (decoration views) → 5g (other layouts) → 5j (remove JS wrapper)
```

---

## L4 — ScrollToItem (DONE)

**Branch:** `cur-scrollto` (merged to main)

**What was built:**
- 3-layer architecture: JS imperative API → C++ JSI `scrollTo` binding → native `_scrollToX:y:animated:`
- Scroll handler registry in `CollectionViewModule.cpp` — static map keyed by `layoutCacheId`, decouples JSI from native view
- Native `RNCollectionViewContainerView` registers/unregisters handler in `updateProps:`/`prepareForRecycle`
- JS `scrollToItem(key, options)` reads frame from LayoutCache using stable keys, computes target offset for `top|center|bottom|nearest` positions, clamps to `[0, contentHeight - viewportHeight]`

**Key decisions:**
- **Stable keys end-to-end:** `scrollToItem` accepts `"sectionKey:itemId"` (e.g. `"cell-animation:s1-17"`) — same key format the C++ ListLayout stores in the cache when `keyExtractor` is provided via `layoutContext.sections[s].itemKeys`. No key translation needed.
- **contentHeightRef pattern:** `useImperativeHandle` deps don't include `contentHeight` (would recreate handle every layout pass). Instead, `contentHeightRef` mirrors the state — same pattern as existing `viewportHeightRef`.
- **No settling loop:** LayoutCache has positions for all items (estimates for unseen). Same as UICollectionView with `estimatedItemSize`. Settling can be added later.

**Bugs fixed during implementation:**
1. `prepareForRecycle` didn't reset `_layoutCacheId` → recycled views skipped scroll handler re-registration
2. `contentHeight` stale in `useImperativeHandle` closure → all scrolls clamped to y=0

---

## L3 — Proper Decoration Views

**Branch:** `cur-decorative-views` (committed, needs build + test in Xcode)

Replaces the JS-workaround `renderSectionBackground` (not windowed, manually positioned) with proper layout-driven decoration views.

**Design:**
- Layout engine emits decoration `LayoutAttributes` — no data, just frame + kind + sectionIndex
- Same position pipeline as cells (`applyPositionsFromState`, `applyMeasurements()` cascade)
- Windowed like cells (render range)
- Z-ordered: `zIndex: -1` for backgrounds, `zIndex: 0` for separators

**Consumer API:**
```typescript
// Separators: simple props (built-in renderer)
separatorEnabled={true}
separatorColor="#ccc"
separatorInsetLeading={16}
separatorInsetTrailing={0}

// Section backgrounds + custom kinds: provide renderer
decorationRenderers={{
  sectionBackground: (sectionIndex, frame) => <View style={...} />,
}}
```

**Implementation steps:**
- L3.1: `LayoutAttributes`: add `isDecoration: bool`, `decorationKind: string`
- L3.2: `ListLayout::computeSection()`: emit `sectionBackground` (full section rect) and `separator` (between items) entries when opted-in
- L3.3: `CollectionView.tsx`: read decorations from LayoutCache, render before cells, consumer API
- L3.4: Deprecate `renderSectionBackground` (map to `decorationRenderers.sectionBackground` internally)

**Tricky parts:**
- Z-ordering: backgrounds behind cells — ensure insertion order or native `zPosition`
- Windowing: section backgrounds span full section height — partially visible when start is off-screen
- Lifecycle: decorations created/destroyed by layout engine output, not data changes

**Files:**
| File | Change |
|---|---|
| `cpp/LayoutCache.h` | `isDecoration`, `decorationKind` on `LayoutAttributes` |
| `cpp/LayoutCache.cpp` | JSI serialization for new fields |
| `cpp/layouts/ListLayout.h/.cpp` | Emit decoration attrs in `computeSection()` |
| `cpp/CollectionViewContainerShadowNode.cpp` | Handle decoration children in position pipeline |
| `example/components/CollectionView.tsx` | Render decorations, consumer API, deprecate old prop |

---

## L3.1 — Decoration contentInsets API (after L3.fix)

Mirrors `NSCollectionLayoutDecorationItem.contentInsets`. Lets consumers shift the C++-emitted decoration frame, enabling:
- "Jacket" backgrounds that start above section content (negative top)
- Partial-height decorations (positive bottom inset = ends before section end)
- Horizontal overflow beyond section insets (negative left/right)
- "Bookshelf" UI: shelf decorations with negative top inset overlap the section above

Consumer API:
```typescript
decorationRenderers: {
  sectionBackground: {
    render: (si, frame) => <JacketBg />,
    contentInsets: { top: -60, bottom: 0, left: -16, right: -16 },
  }
}
```
C++ applies insets when emitting the decoration frame so windowing and ShadowNode positioning are correct at source.

---

## 5g — Extend ShadowNode to All Layout Types

After L3. One layout type at a time:

1. **Horizontal list** — same as vertical list but flip axis
2. **Grid (vertical + horizontal)** — reads `frame.x` (column), `frame.width` from cache
3. **Flow (vertical + horizontal)** — two-pass: Yoga measures widths → cache → FlowLayout recomputes
4. **Masonry** — items not Y-sorted by index; use `getAttributesInRect()` for range queries

Non-frame attributes (`zIndex`, `alpha`, `transform3D`, `isHidden`) applied from LayoutCache by native view.

---

## 5j — Remove JS Cell Wrapper Positioning

After 5g. Remove `position: 'absolute'`, `left`, `top`, `width`, `height` from cell wrapper style. Cell wrapper becomes `{ flex: 1 }`. Remove `computedPositions` useMemo and `itemPositionsRef` from `CollectionView.tsx`.

---

## FlashList Differentiators

| Capability | Riff | FlashList |
|---|---|---|
| Sticky footer | ✅ | ❌ |
| Sticky push (UIKit-correct) | ✅ | ❌ basic only |
| View identity preservation (timer proof) | ✅ | ❌ recycles = re-creates |
| Cell animation state in window | ✅ Activity=hidden | ❌ lost on recycle |
| Dynamic height, zero layout shift | ✅ ShadowNode same commit | ❌ JS correction loop |
| Insert/delete with scroll stability | ✅ MVC prop | ❌ no per-mutation control |
| Size-change with scroll stability | ✅ snapshotAnchorIfNeeded | ❌ |
| Decoration views (arbitrary kinds) | planned L3 | ❌ |
| Separators (layout-driven) | planned L3 | basic, not layout-driven |
| scrollToItem by stable key | ✅ L4 | index only |
| Custom layouts (carousel, radial) | ✅ | ❌ list only |
| C++ window controller on UI thread | ✅ | ❌ JS |
| Memory pressure adaptation | ✅ | ❌ |

---

## Key Files

| File | Purpose |
|---|---|
| `example/screens/comparison/LayoutsTab.tsx` | ListDemo (L2 demo lives here) |
| `example/components/CollectionView.tsx` | Main component |
| `cpp/layouts/ListLayout.h/.cpp` | C++ list layout |
| `cpp/LayoutCache.h/.cpp` | Cache + LayoutAttributes |
| `cpp/CollectionViewContainerShadowNode.cpp` | ShadowNode positioning |
| `cpp/CollectionViewModule.h/.cpp` | TurboModule + JSI bindings |
| `cpp/LayoutCacheRegistry.h` | Thin header for LayoutCache + scroll handler lookup |
| `ios/RNCollectionViewContainerView.mm` | Native scroll container |
| `ios/RNScrollCoordinatedViewView.mm` | Sticky view KVO |
