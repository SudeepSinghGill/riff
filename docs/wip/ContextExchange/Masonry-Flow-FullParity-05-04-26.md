# Session Handoff — Masonry + Flow Full Feature Parity
**Date:** 2026-04-05

## What was completed

### Masonry (`cur-masonry` → merged to main)

**C++ MasonryLayout.cpp/h:**
- Full multi-section engine: `computeSections` / `computeSectionFromCache` / `invalidateSectionsFrom`
- Shortest-lane placement with correct `maxLaneEnd` (trailing rowSpacing subtracted from each non-empty lane)
- Lane-divider separators: vertical lines between column gaps (V), horizontal lines between row gaps (H)
- Section headers/footers, sectionBackground with contentInsets, sectionSpacing
- H-masonry: 3-phase `applyMeasurements` (write deltas → update `_maxCrossAxisHeight` → full reflow via `computeSectionFromCache`)
- V-masonry: `firstChangedSection` partial reflow
- Section-aware key format: `"masonry-{section}-{index}"` (was `"masonry-{i}"` — violates section awareness)
- `_maxCrossAxisHeight` preserved across `computeSections` calls to avoid oscillation

**TS masonry.ts:**
- Rewritten to multi-section pattern (mirrors `grid.ts`)
- `computeSections` JSI call, `lastSectionKeys`, stable key chain
- `shouldInvalidate=false` for H-masonry (same oscillation risk as H-grid)
- All queries delegate to LayoutCache

**protocol.ts `MasonryLayoutDelegate`:**
- Added: `horizontal`, `estimatedCrossAxisHeight`, `sectionSpacing`, `separator`, `sectionBackground`, `sectionBackgroundContentInsets`

**Demo:**
- `MasonryDemo`: 3 sections (S0 mutable), sticky push H/F, animated section backgrounds, insert/delete/resize, MVC, Sep toggle, ScrollTo
- `HMasonryDemo`: 3 horizontal lanes, adaptive container height (onContentSizeChange), 2 sections, insert/delete, MVC
- Added `'hmasonry'` SubTab + callouts
- `RiffDemo`: wired Masonry ↕ and Masonry ↔ tabs

### Flow (`cur-flow` → merged to main)

**C++ FlowLayout.cpp/h:**
- Full multi-section engine: `computeSections` / `computeSectionFromCache` / `invalidateSectionsFrom`
- V-mode: greedy left→right bin-packing, row height = max item height in row
- H-mode: greedy top→bottom bin-packing, column width = max item width in column
- Between-row (V) / between-column (H) separators with section-aware keys
- Section headers/footers, sectionBackground, sectionSpacing
- `applyMeasurements`: always full reflow (any W/H change cascades to all subsequent rows/columns)
  - Uses stored `_sectionParams` — eliminates fragile reverse-engineering from cache
  - Delta classification: match `oldValue` against `frame.width` vs `frame.height`; ambiguous → prefer primary axis
- `computeSectionFromCache`: reads both width AND height from cache (ContentDimension::Both)

**TS flow.ts:**
- Rewritten to multi-section pattern (mirrors `grid.ts`)
- `computeSections` JSI call, `lastSectionKeys`, stable key chain
- H-flow: `shouldInvalidate` on height change (cross axis = container height = INPUT, not oscillation risk)
  - Unlike H-masonry/H-grid where container height is OUTPUT (content-determined)

**protocol.ts `FlowLayoutDelegate`:**
- Added: `horizontal`, `sectionSpacing`, `separator`, `sectionBackground`, `sectionBackgroundContentInsets`

**Demo:**
- `FlowDemo`: 3 sections of tags, sticky push H/F, animated section backgrounds, insert/delete, MVC, Sep toggle, ScrollTo. Preserves two-pass demo effect.
- `HFlowDemo`: fixed container height (220pt), items pack top→bottom into columns, every 3rd tag is taller (multi-line) demonstrating variable cross-axis sizes, 2 sections, insert/delete, MVC
- Added `'hflow'` SubTab + callouts
- `RiffDemo`: all 8 tabs wired (List/Grid/Masonry/Flow × ↕/↔), placeholders removed

### Other
- `PLAN.md`: added Future Enhancements section with F-Flow.1 (justification), F-Flow.2 (weight/stretch), F-Grid.1 (rowAlignment)
- `ethereal-seeking-willow.md`: status table updated

## Key Architectural Decisions

### H-flow vs H-masonry: container height role
| | H-masonry | H-flow |
|---|---|---|
| Container height | OUTPUT (content-determined, adaptive) | INPUT (fixed, consumer-provided) |
| shouldInvalidate on height | false (oscillation risk) | true (re-packs items into new cross extent) |
| contentDeterminedDimension | Both | Both |

### Flow separators: between-line only, not between items
Separators in flow are between rows (V) or between columns (H), not between individual items within a row/column. Each completed line gets one separator on its trailing edge.

### applyMeasurements always-full-reflow for flow
Any width change in V-flow can shift items to a different row — all downstream rows shift. Must reflow from section start. Using stored `_sectionParams` eliminates the fragile reverse-engineering from cache that the old single-section code did.

### `computeSectionFromCache` reads both dimensions
Unlike masonry (reads height for V, width for H separately), flow's `computeSectionFromCache` reads `frame.height` as cross-axis and `frame.width` as primary-axis in H-mode (or vice versa in V-mode), since Yoga may have corrected both.

## Current State
- Branch: `main`
- All 4 layout engines (list, grid, masonry, flow) have V + H modes with full multi-section parity
- RiffDemo: 8 working tabs
- Next: API audit across all 4 layouts (see plan in ethereal-seeking-willow.md)
