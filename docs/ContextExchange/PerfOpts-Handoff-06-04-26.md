# Perf Optimizations Handoff — 2026-04-06 21:24

## Branch
`cur-cell-pooling` (all changes here, not merged to main yet)

## What Was Done This Session

### Opt 1 + Opt 2: `processScroll` batched JSI call ✅
**Files changed:**
- `packages/rn-collection-view/cpp/CollectionViewModule.cpp` — added `processScroll` function (~line 421)
- `packages/rn-collection-view/src/types/protocol.ts` — added `needsSpatialQuery?: boolean` to `CollectionViewLayout`
- `packages/rn-collection-view/src/layouts/custom.ts` — set `readonly needsSpatialQuery = true` on `CustomLayoutEngine`
- `packages/rn-collection-view/example/components/CollectionView.tsx` — uses `processScroll` in both `onScroll` handler and `useLayoutEffect`; added `sectionInfoPacked` memo

**What it does:** Replaces 4-6 per-scroll JSI calls with a single `processScroll` call that:
- Runs both spatial queries (renderRect + visibleRect) entirely in C++
- Computes applyBudget and measureRange in C++
- Returns 7 integers: `{renderFirst, renderLast, visibleFirst, visibleLast, measureFirst, measureLast, cacheVersion}`
- ~300 JSI property constructions → 7 integers per scroll event

**`sectionInfoPacked`:** flat array `[start0, headerOffset0, dataCount0, ...]` passed to C++ so it can compute flat indices for multi-section lists. Pass `null` for single-section lists.

### Opt 6: Range stability guard — REVERTED
Was implemented and reverted because it caused items to disappear during scroll. The guard skipped `processScroll` when scroll hadn't moved `budgetStride * 0.5`, but this was too aggressive. Needs smarter calibration — see PERF-PLAN.md.

## Active Bugs (Found During Testing)

### Bug 1: Items disappear on scroll, come back on more scroll
**Status:** Opt 6 was reverted — retest without Opt 6 to confirm fix.
**If still occurring after Opt 6 removal:** The `processScroll` C++ implementation itself may have a subtle bug. Key area to check: the `toFlatIdx` lambda in `CollectionViewModule.cpp` ~line 470. Compare against JS `attrToFlatIndex` at ~line 656 of CollectionView.tsx.

### Bug 2: Headers/footers lose position on size change when MVC is off
**Status:** Unresolved. Likely pre-existing (unrelated to processScroll) but needs confirmation.
**How to confirm if pre-existing:** Test on `main` branch with the same content/size-change scenario.
**If new:** The issue may be in how `processScroll`'s `toFlatIdx` handles supplementary views for sectioned lists. The C++ check is `a.supplementaryKind == "header"` / `"footer"`. Verify `"footer"` is actually set in ListLayout.cpp (grep for `supplementaryKind` in ListLayout.cpp — only "header" was visible in grep).

## Key Files to Read Before Continuing

1. `PERF-PLAN.md` — full optimization roadmap, Opt 1+2 marked done, Opt 3 deferred with notes
2. `packages/rn-collection-view/cpp/CollectionViewModule.cpp` lines 421-578 — `processScroll` implementation
3. `packages/rn-collection-view/example/components/CollectionView.tsx`:
   - Line ~993: `sectionInfoPacked` memo
   - Line ~1208: `sectionInfoPacked` memo definition
   - Line ~1243: `useLayoutEffect` processScroll call
   - Line ~1520: `onScroll` processScroll call

## What to Do Next

1. **Rebuild and retest** with Opt 6 removed:
   - Clean Xcode build (Cmd+Shift+K then Cmd+R)
   - Verify items no longer disappear during scroll
   - Verify sticky headers, sections, MVC all work

2. **Confirm bug 2 is pre-existing** by testing `main` branch separately. If it's new, debug the `toFlatIdx` supplementary handling.

3. **Opt 3 (transform positioning) — DEFERRED**. See PERF-PLAN.md for analysis. Key finding: `layer.transform` (CATransform3D) does NOT affect UIKit hit testing (`view.frame`), so cells would be invisible to touch events unless `hitTest:withEvent:` is overridden on `_contentView`. Also: for position-only changes (same size), `setFrame:` already just calls `setCenter:` without triggering `layoutSubviews` — so the benefit is smaller than originally estimated. Skip this for now.

4. **Opt 4 + 7** (cell recycling + incremental render loop) — the next big optimization. `SlotManager.ts` already exists in `example/components/`. The integration into `CollectionView.tsx` needs the position sync fix (see PERF-PLAN.md Opt 4 description).

## Build Notes
- No new .cpp files added → `pod install` NOT required
- Just clean Xcode build
- Metro: `nvm use && npx react-native start --port 8082 --reset-cache` from `example/`
