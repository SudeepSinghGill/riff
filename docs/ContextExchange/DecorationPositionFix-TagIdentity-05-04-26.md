# Context Exchange — Decoration Position Fix + Tag Identity Design
**Date:** 2026-04-05
**Branch:** `cur-section-footer-test`
**Status:** Fix implemented and committed, ready to merge to main

---

## What Was Fixed

Section backgrounds were positioned at wrong locations (separator frames) when separators were toggled ON, in both list and grid layouts. All JS-level frames were correct. The ShadowNode position arrays were correct. Only the native application to wrong subviews was broken.

## Root Cause (Confirmed via Runtime Logs)

**Fabric's reconciler "last index" optimization** causes native subview ordering to become inconsistent with ShadowNode child ordering when new children are inserted before existing non-moved children.

When separators are added, the SpatialIndex returns decorations in spatial order, interleaving new seps before existing bg views in the React tree. React's reconciler sees bg views maintaining their relative order (old index >= lastIndex) so it doesn't generate MOVE operations for them. Instead, it inserts seps starting at native index 1 (after bg0 which stays at 0), not at index 0 where they should be.

Result:
- ShadowNode: `[sep0, bg0, sep1, bg1, ...]` → `positions[0]` = separator frame
- Native: `[bg0, sep0, sep1, ..., bg1, ...]` → `subviews[0]` = bg0

Index-based mapping applies separator frame to bg view → wrong.

Confirmed from `xc-logs.txt`:
```
apply[0] tag=3942 target=(28.0,125.7,353.0,0.5) current=(12.0,56.0,369.0,1979.0)
```
bg view (1979px tall) gets separator frame (0.5px tall).

## The Fix: Universal Tag-Based Positioning

**Three-file change:**

1. `cpp/CollectionViewContainerState.h` — added `std::vector<int32_t> childTags` parallel to `positions`
2. `cpp/CollectionViewContainerShadowNode.cpp` — records `children[i]->getTag()` for ALL children in Phase 1 loop, copies `childTags_` to state alongside positions
3. `ios/RNCollectionViewContainerView.mm` — builds `tag → UIView*` map from subviews, applies each position by tag identity not index

Universal — covers items, decorations, and supplementaries. Fallback to index-based if `childTags` is empty (defensive for state version mismatches).

## Key Design Principle: Two-Layer Identity

```
Layer 1: cacheKey (stable string) — "What position?"
  C++ engine → LayoutCache → SpatialIndex → JS → ShadowNode Phase 1
  Guarantee: Our code, deterministic

Layer 2: Fabric tag (int32) — "Which native view?"
  ShadowNode → state.childTags → native applyPositionsFromState
  Guarantee: Fabric's core contract (breaks = all of RN breaks)
```

These layers are orthogonal. Neither depends on reconciler ordering. The tag fix replaces a dependency on a reconciler optimization detail with a dependency on Fabric's most fundamental contract.

## Why Decorations Trigger This But Items Don't (Today)

Items are rendered in flat data order — new items never interleave with existing items in a way that puts new elements before existing non-moved elements. Decorations come from SpatialIndex in spatial order, which does interleave. Custom layouts or future spatial-order rendering could trigger the same issue for items. Universal tag lookup protects all cases.

## Other Changes This Session

- Separator default color changed to white (`#FFFFFF`) for visibility
- Diagnostic logs removed from CollectionView.tsx (POST-PREPARE, DECO-BG)
- Native logging disabled (`RNCV_ENABLE_NATIVE_LOGS = 0`) in ShadowNode and ContainerView
- `COLLECTIONVIEW_INTERNALS.md` — new "Two-Layer Identity" section (full analysis)
- `ARCHITECTURE.md` — note added to ShadowNode decision section
- `PLAN.md` — fix logged under R1.12
- `ethereal-seeking-willow.md` — L3.fix marked done, key finding documented

## What's Next

Per `ethereal-seeking-willow.md`:
1. **L3.1** — Decoration contentInsets API (like `NSCollectionLayoutDecorationItem.contentInsets`)
2. **5g** — Extend ShadowNode to grid/masonry/flow layouts
3. **5j** — Remove JS cell wrapper positioning

Grid layout is `cur-section-footer-test` → column separators in `computeSectionFromCache` were added last session and need build verification.

## Files Changed This Session

| File | Change |
|---|---|
| `cpp/CollectionViewContainerState.h` | Add `childTags` vector |
| `cpp/CollectionViewContainerShadowNode.h` | Add `childTags_` member |
| `cpp/CollectionViewContainerShadowNode.cpp` | Populate childTags_, disable logs |
| `ios/RNCollectionViewContainerView.mm` | Tag-based lookup, disable logs |
| `example/components/CollectionView.tsx` | White separators, remove diagnostic logs |
| `docs/COLLECTIONVIEW_INTERNALS.md` | Two-Layer Identity section |
| `docs/ARCHITECTURE.md` | ShadowNode two-layer identity note |
| `PLAN.md` | R1.12 fix logged |
| `ethereal-seeking-willow.md` | Status updated |
