# Decoration Views (L3) — 01 Apr 2026

## What was done

Implemented L3: layout-driven decoration views (separators + section backgrounds). All code committed on branch `cur-decorative-views`. **Needs Xcode build + test before merge to main.**

## Architecture

```
JS: list({ separator: {...}, sectionBackground: true })
  → prepare() passes emitSeparators/emitSectionBackground to C++ computeSections

C++ ListLayout::computeSection():
  → emits separator-{si}-{i} attrs between items (zIndex 0)
  → emits decoration-{si}-sectionBackground attr spanning full section (zIndex -1)
  → stored in LayoutCache like cells — spatial index tracks them

JS CollectionView.tsx scrollContent IIFE:
  → nativeMod.layoutCache.getAttributesInRect(renderWindowRect)
  → filter by isDecoration
  → render as RNMeasuredCell type="decoration" with fixed-height inner View
  → ShadowNode positions via cacheKey lookup (same as cells)
  → rendered BEFORE cells → section backgrounds paint behind cells
```

## Key design decisions

### New LayoutAttributes fields
- `isDecoration: bool` — distinguishes from cells and supplementary
- `decorationKind: string` — "sectionBackground" or "separator"
- `section` field (existing) reused — no new field needed

### Windowing
`getAttributesInRect` naturally windows decorations. Section backgrounds (tall frames) are included when partially visible. Separators included only if their Y intersects the render window.

### Fixed-height inner View prevents Yoga deltas
Decoration elements render `<View style={{ width: frame.width, height: frame.height }}>` inside `RNMeasuredCell`. Yoga measures exactly frame.height → delta = 0 → `applyMeasurements` never called for decorations.

### applyMeasurements skips decorations
Decoration frames are owned by the layout engine. `applyMeasurements` skips them with `if (attr.isDecoration) continue;`. They're re-emitted correctly on next `computeSections()`.

### getSectionOffsets skips decorations
Added `|| attrs.isDecoration` check alongside `attrs.isSupplementary`.

### Separator color is JS-only
C++ only handles geometry (height, insets). Color is read from `delegate.separator.color` in the CollectionView rendering code. Default: `#C6C6C8`.

### Old renderSectionBackground API
Mapped to new `bgRenderer` inside the rendering loop. Still works but requires `sectionBackground: true` on list layout delegate (otherwise C++ doesn't emit the attr). `@deprecated` JSDoc added.

## Consumer API

```typescript
// Separator: list layout only (not a Riff-level prop)
list({ separator: { color: '#C6C6C8', height: 0.5, insetLeading: 16, insetTrailing: 0 } })

// Section background: enable emission in layout, provide renderer on component
list({ sectionBackground: true })
<Riff decorationRenderers={{ sectionBackground: (si, frame) => <MyBg /> }} />

// Windowing proof
<Riff onDecorationCountChange={(n) => setDecoCount(n)} />
```

## Demo (LayoutsTab.tsx)

- `AnimatedSectionBg`: slow sweeping band animation proves ONE view behind all cells per section
- `Sep: ON/OFF` toggle
- `Deco:N` counter in control bar — bounded to ~10–15 while scrolling (windowing proof)
- Separator color `#4a4a6a` with 16pt left inset

## Files modified

| File | Changes |
|---|---|
| `cpp/LayoutCache.h` | `isDecoration`, `decorationKind` fields on LayoutAttributes |
| `cpp/LayoutCache.cpp` | JSI serialization for new fields; `getSectionOffsets` skips decorations |
| `cpp/layouts/ListLayout.h` | `emitSectionBackground`, `emitSeparators`, `separatorHeight/InsetLeading/InsetTrailing` in ListLayoutParams |
| `cpp/layouts/ListLayout.cpp` | Separator emission (inline in item loops), section background emission (after footer), `applyMeasurements` skips decorations, `bln()` helper, new params in `paramsFromJSI()` |
| `src/types/layout.ts` | `isDecoration?`, `decorationKind?` on TS LayoutAttributes |
| `src/types/protocol.ts` | `separator` and `sectionBackground` on ListLayoutDelegate |
| `src/layouts/list.ts` | Pass decoration params to C++; `delegate` made public (for color access) |
| `example/components/CollectionView.tsx` | `getAttributesInRect` on nativeMod type; `decorationRenderers` prop; `onDecorationCountChange` prop; decoration rendering in scrollContent IIFE; decorationCountRef + useLayoutEffect |
| `example/screens/comparison/LayoutsTab.tsx` | `AnimatedSectionBg`, `sepEnabled`/`decoCount` state, updated `listLayout` useMemo, wired demo |

## What still needs to happen before merge

1. **Xcode build** — no C++ pod install needed (no new .cpp files, only changes to existing)
2. **Visual test**: separators appear between items with correct inset and color
3. **Visual test**: animated section backgrounds visible behind cells with slow sweep
4. **Windowing proof**: `Deco:N` counter stays bounded (~3 bg + ~10-15 sep) while scrolling
5. **Toggle test**: Sep: ON/OFF correctly adds/removes separators
6. **MVC test**: decorations move correctly after insert/delete
7. **Resize test**: section background height updates when items in section resize

## Next after L3 merge

5g — Extend ShadowNode to grid/masonry/flow layouts
