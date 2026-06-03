# Session Handoff — Flow V-mode Bug Fix + FlowDemo Redesign
**Date:** 2026-04-05

## What was completed

### Bug fixes: FlowLayout.cpp V-mode bin-packing

**Branch:** `cur-flow-fix` → merged to main

Two bugs in `computeSection` (and correspondingly `computeSectionFromCache`):

#### Bug 1: lineMaxCross advancement (wrong axis)
`lineMaxCross` tracked max CROSS-axis size (item WIDTH for V-flow) but was used to advance the PRIMARY cursor (Y). Result: Y advanced by ~80-200px (item width) instead of ~34px (item height) — rows 5-6× too far apart = huge vertical gaps between tag rows.

Fix: renamed to `lineMaxPrimary`, tracks max PRIMARY-axis size (item HEIGHT for V, item WIDTH for H).

```cpp
// BEFORE:
double lineMaxCross = 0.0;
primaryCursor += lineMaxCross + lineGap;  // Y += item width — WRONG
if (clampedCross > lineMaxCross) lineMaxCross = clampedCross;

// AFTER:
double lineMaxPrimary = 0.0;
primaryCursor += lineMaxPrimary + lineGap;  // Y += item height — CORRECT
if (itemPrimarySize > lineMaxPrimary) lineMaxPrimary = itemPrimarySize;
```

Also removed the wrong "normalize all items in a row to lineMaxCross width" — items now keep their natural cross-axis size.

#### Bug 2: Frame width/height swap (V-mode cache write)
`computeSection` stored frames as `{ Y, X, width, height }` but the non-H cache write emitted `{ x, y, height, width }` — width and height transposed. Items rendered as tall narrow strips instead of wide flat tags.

Fix: unified frame storage to always be `{ pos-along-primary, pos-along-cross, primaryAxisSize, crossAxisSize }` — same format as `computeSectionFromCache`. This makes the V-mode cache write `{ cross=X, primary=Y, crossSize=width, primarySize=height }` = `{ x, y, width, height }` correct.

```cpp
// BEFORE (computeSection non-H):
frames[i] = { primaryCursor, crossCursor, clampedCross, itemPrimarySize };  // slots: { Y, X, width, height }
attrs.frame = { cross, primary, crossSize, primarySize } = { X, Y, height, width }  // WRONG

// AFTER (unified, same as computeSectionFromCache):
frames[i] = { primaryCursor, crossCursor, itemPrimarySize, clampedCross };  // slots: { Y, X, height, width }
attrs.frame = { cross, primary, crossSize, primarySize } = { X, Y, width, height }  // CORRECT
```

Also fixed `sepPos` reference: `primaryCursor + lineMaxCross` → `primaryCursor + lineMaxPrimary`.

### FlowDemo redesign (LayoutsTab.tsx)

**S0 — Product cards with fractional widths:**
- `kind: 'banner'` — full row width (`containerWidth - 16`), height 80px. One per row.
- `kind: 'half'` — half row (`(containerWidth - 24) / 2`), height 120px. Two per row.
- `kind: 'third'` — third row (`(containerWidth - 32) / 3`), height 80px. Three per row.
- Pattern: `banner, half, half, third, third, third` × N
- Tap to expand (banner→160, half→200, third→140). `resizedIds` Set pattern (same as MasonryDemo).
- Insert (random kind), delete, `↕ S0[0]` button, MVC toggle, scrollTo.

**S1 — Tag cloud (two-pass demo):**
- Same behavior as before: estimated widths → Yoga measures → reflow.
- Uses `FLOW_S1_LABELS` (technology names, 20 tags).

**Key sizeForItem pattern:**
```typescript
sizeForItem: (i, s, containerWidth) => {
  if (s === 0) {
    const card = s0CardsRef.current[i];
    const avail = containerWidth - 16; // insetLeft + insetRight
    if (card.kind === 'banner') return { width: avail, height: expanded ? 160 : 80 };
    if (card.kind === 'half')   return { width: (avail - 8) / 2, height: expanded ? 200 : 120 };
    /* third */                 return { width: (avail - 16) / 3, height: expanded ? 140 : 80 };
  }
  // S1: tags
  return { width: tag?.estimatedWidth ?? 80, height: 34 };
}
```

The fractional widths + `itemSpacing: 8` + `insets: { left: 8, right: 8 }` ensure exact row-filling:
- banner: `avail` = `containerWidth - 16` fills one row exactly
- 2 halves: `2 × (avail-8)/2 + 8 = avail` fills row exactly
- 3 thirds: `3 × (avail-16)/3 + 16 = avail` fills row exactly (approx, rounding)

## What was NOT done / deferred to future

- **H-masonry:** broken (all items same size, not a popular layout) → PLAN.md future/research
- **H-flow demo:** not a priority → PLAN.md future scope
- **Separators in flow/masonry:** not working → PLAN.md future scope
- **S2 section in FlowDemo:** removed (now 2 sections: cards + tags)

## Current State

- Branch: `main`
- All bugs fixed and committed
- Next: API audit across all 4 layouts (see ethereal-seeking-willow.md)

## Key File Locations

| File | Change |
|---|---|
| `cpp/layouts/FlowLayout.cpp` | Bug fixes: lineMaxPrimary + frame W/H swap in computeSection + computeSectionFromCache |
| `example/screens/comparison/LayoutsTab.tsx` | FlowDemo redesign: S0=product cards, S1=tags |
