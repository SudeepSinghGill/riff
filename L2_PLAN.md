# Phase L2 — Expanded ListDemo Implementation Plan

## Context

The ListDemo in LayoutsTab.tsx currently has 1 section with 50 items and minimal header/footer. L2 expands it to 3 sections demonstrating sticky identity, cell animation identity, and per-section insets — all capabilities that differentiate Riff from FlashList.

## Files to Modify

### 1. CollectionView.tsx — Bug fix: per-section insets

**Path:** `packages/rn-collection-view/example/components/CollectionView.tsx`

- **Line ~148**: Add `insets?: { top?: number; bottom?: number; left?: number; right?: number }` to `SectionConfig`
- **Line 956**: Replace `insets: undefined,` → `insets: s.insets,`

### 2. LayoutsTab.tsx — Main L2 work

**Path:** `packages/rn-collection-view/example/screens/comparison/LayoutsTab.tsx`

#### A. New data arrays (near top, after existing LIST_DATA)

| Array | Count | Profile |
|---|---|---|
| `S0_DATA` | 25 | Single-line subtitle, uniform height — mutation target |
| `S1_DATA` | 25 | Variable height (1-3 lines), every 4th has `animated: true` |
| `S2_DATA` | 20 | Single-line, for inset demonstration |

All use `ListItem` type (extended with optional `animated?: boolean`).

#### B. Replace animated components (lines 119-159)

Remove: `AnimatedSectionBackground`, `AnimatedTimerHeader`, `AnimatedTimerFooter`

Add:

| Component | Purpose |
|---|---|
| `LiveTimer` | `setInterval(100ms)` tick counter, displays `{ticks × 100} ms`. Proves view identity. |
| `ShimmerTimerHeader` | Shimmer sweep (`Animated.Value` translateX loop) + `LiveTimer`. For S0 header + footer. |
| `SectionTimerHeader` | Simple `LiveTimer` header for S1/S2. |
| `CtrlBtn` | Small pressable button for controls bar. Supports `disabled` + `active` states. |

#### C. Rewrite `ListDemo` component (lines 163-205)

**State:**
- `s0Items: ListItem[]` — mutable for insert/delete
- `resizedItemId: string | null` — which item has toggled subtitle
- `mvcEnabled: boolean` — placeholder toggle (L5 wires it)
- `insertCounter: useRef(25)` — unique IDs for inserts

**Layout:**
```ts
list({ estimatedItemHeight: 72, itemSpacing: 8 })
```
Note: `stickyMode` is a prop on `<CollectionView>`, NOT on `list()`.

**3 Sections:**

| Section | Key | Header | Footer | Insets | Items |
|---|---|---|---|---|---|
| S0 "Sticky Identity" | `sticky-identity` | `ShimmerTimerHeader` shimmer+timer, sticky | `ShimmerTimerFooter` timer+"STICKY FOOTER — Riff only" badge, sticky | 8/8/0/0 | `s0Items` (mutable), fixed height |
| S1 "Cell Animation" | `cell-animation` | `SectionTimerHeader` timer, sticky | none | 8/8/0/0 | `S1_DATA`, variable height, every 4th has shimmer + mount counter |
| S2 "Insets + Spacing" | `insets-spacing` | `SectionTimerHeader` timer, sticky | none | 24/24/16/16 | `S2_DATA`, annotated with inset values |

**renderItem** — single callback, switches on `sectionIndex`:
- S0: simple cell, checks `resizedItemId` for subtitle toggle
- S1: if `item.animated` → `AnimatedIdentityCell` (shimmer bg + mount counter badge), else normal variable-height cell
- S2: cell with inset annotation text

**AnimatedIdentityCell** (separate component for S1 shimmer items):
- `useRef(0)` mount counter, incremented once in `useEffect([], ...)`
- `Animated.loop` translateX shimmer on background
- Badge showing `Mounts: {count}` — green if 1, red if >1

**Controls bar** (above CollectionView):
- Scroll-to: `→ Top`, `→ #42`, `→ Bot` — **disabled** (L4)
- Mutations: `+Insert`, `×Delete`, `↕Resize` — **active**, update `s0Items`/`resizedItemId`
- `MVC: ON/OFF` — **active toggle** but no effect yet (L5)

#### D. Update `CALLOUTS.list` (lines 313-318)

```ts
list: [
  { type: 'green', text: 'S0: Header+footer timers survive all scroll — same instance, never remounted. FlashList: no sticky footer; header may remount.' },
  { type: 'red', text: 'FlashList: Recycled cell re-mounts → animation restarts, mount counter increments. No sticky footer at all.' },
  { type: 'blue', text: 'S1: Shimmer continues from same phase after scroll-away. Mount counter stays at 1 within render window.' },
  { type: 'blue', text: 'S2: Per-section insets (24/24/16/16) from C++ layout engine. Item spacing: 8px global.' },
  { type: 'blue', text: 'Controls: +Insert/×Delete/↕Resize mutate S0. Scroll-to (L4) and MVC (L5) coming.' },
],
```

## Verification

1. Build and run the app (Metro on port 8082, Xcode clean build)
2. Navigate to FlashList Comparison → Layouts → List tab
3. Verify:
   - **S0**: Shimmer sweeps on header+footer, timer counts up continuously, never resets while scrolling. Footer sticks at bottom when scrolled past.
   - **S1**: Variable-height items render correctly. Every 4th item has shimmer. Scroll S1 items off-screen and back — shimmer continues, mount counter shows 1.
   - **S2**: Items are visibly inset (16px left/right margins, 24px top/bottom padding around the section). Annotation text shows inset values.
   - **Controls**: Insert adds 3 items at top of S0. Delete removes 3. Resize toggles first S0 item's height. Scroll-to buttons are grayed out. MVC toggle switches but has no effect yet.
4. Check callout bullets update at top of List tab.
