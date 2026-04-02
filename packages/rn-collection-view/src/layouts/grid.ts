/**
 * Grid Layout — fixed columns, row-aligned heights.
 *
 * Backed by the C++ GridLayout engine via JSI.
 * Items placed left-to-right in rows. Each row's height = tallest item in that row
 * (or fixed `rowHeight` if provided — actual height determined by Yoga for dynamic rows).
 *
 * Multi-section support: each section gets its own header/footer/background/separators.
 * The C++ engine writes all LayoutAttributes to the shared LayoutCache.
 * All queries go through the LayoutCache so ShadowNode corrections are immediately visible.
 *
 * Standard JSI contract (mirrors ListLayout):
 *   nativeMod.gridLayout.computeSections(sections[])       → void
 *   nativeMod.gridLayout.invalidateSectionsFrom(n, [])     → void
 */

import type {
  CollectionViewLayout,
  LayoutContext,
  GridLayoutDelegate,
  InvalidationScope,
} from '../types/protocol';
import type { LayoutAttributes, Rect, Size } from '../types';
import NativeCollectionViewModule from '../specs/NativeCollectionViewModule';

const nativeMod = NativeCollectionViewModule as unknown as {
  layoutCache: {
    getAttributesInRect(rect: { x: number; y: number; width: number; height: number }): LayoutAttributes[];
    getAttributes(key: string): LayoutAttributes | null;
    getTotalContentSize(): Size;
    clear(): void;
  };
  gridLayout: {
    /** Legacy single-section method. Kept for compatibility. */
    computeGridLayout(params: object): { positions: number[]; contentHeight: number };
    /** Standard multi-section method — preferred. */
    computeSections(sections: object[]): void;
    /** Standard partial re-layout from fromSection onward. */
    invalidateSectionsFrom(fromSection: number, sections: object[]): void;
  };
};

class GridLayoutEngine implements CollectionViewLayout {
  readonly type = 'grid';
  private readonly delegate: GridLayoutDelegate;

  constructor(delegate: GridLayoutDelegate) {
    this.delegate = delegate;
  }

  prepare(context: LayoutContext): void {
    const d = this.delegate;
    const w = context.containerWidth;

    if (w <= 0 || context.sections.length === 0) return;

    const effectiveColumns = typeof d.columns === 'function' ? d.columns(w) : d.columns;
    const effectiveRowHeight = typeof d.rowHeight === 'function' ? d.rowHeight(w) : (d.rowHeight ?? 0);

    const sections = context.sections.map((sec, sectionIndex) => {
      // Build per-item heights when rows are dynamic (no fixed rowHeight)
      let itemHeights: number[] | undefined;
      if (!effectiveRowHeight && (d.heightForItem || context.measuredHeightForItem)) {
        itemHeights = new Array(sec.itemCount);
        for (let i = 0; i < sec.itemCount; i++) {
          const measured = context.measuredHeightForItem?.(i, sectionIndex);
          itemHeights[i] = measured ?? (d.heightForItem ? d.heightForItem(i, sectionIndex, w) : 44);
        }
      }

      // Keys: prefer section.itemKeys for stable identity, else fallback prefix
      const keyPrefix = `grid-${sectionIndex}-`;
      const keys: string[] = sec.itemKeys
        ? Array.from(sec.itemKeys)
        : Array.from({ length: sec.itemCount }, (_, i) => `${keyPrefix}${i}`);

      // Use section config heights (ground truth from supplementaryItems) when available;
      // fall back to delegate if section has no header/footer configured.
      const headerInfo = sec.supplementaryItems.find(s => s.kind === 'header');
      const footerInfo = sec.supplementaryItems.find(s => s.kind === 'footer');
      const headerH = headerInfo ? headerInfo.size.height
        : (d.heightForHeader ? d.heightForHeader(sectionIndex) : (d.headerHeight ?? 0));
      const footerH = footerInfo ? footerInfo.size.height
        : (d.heightForFooter ? d.heightForFooter(sectionIndex) : (d.footerHeight ?? 0));

      return {
        itemCount: sec.itemCount,
        columns: effectiveColumns,
        columnSpacing: d.columnSpacing ?? 0,
        rowSpacing: d.rowSpacing ?? 0,
        viewportWidth: w,
        rowHeight: effectiveRowHeight,
        sectionInsetTop: sec.insets?.top ?? 0,
        sectionInsetBottom: sec.insets?.bottom ?? 0,
        sectionInsetLeft: sec.insets?.left ?? 0,
        sectionInsetRight: sec.insets?.right ?? 0,
        headerHeight: headerH,
        footerHeight: footerH,
        emitSectionBackground: d.sectionBackground ?? false,
        emitSeparators: !!d.separator,
        separatorHeight: d.separator?.height ?? 0.5,
        separatorInsetLeading: d.separator?.insetLeading ?? 0,
        separatorInsetTrailing: d.separator?.insetTrailing ?? 0,
        sectionSpacing: d.sectionSpacing ?? 0,
        keys,
        keyPrefix: '', // keys are provided explicitly above
        ...(itemHeights ? { itemHeights } : {}),
      };
    });

    nativeMod.gridLayout.computeSections(sections);
  }

  attributesForElements(inRect: Rect): LayoutAttributes[] {
    return nativeMod.layoutCache.getAttributesInRect(inRect);
  }

  attributesForItem(index: number, section: number): LayoutAttributes | null {
    // Keys are built as `grid-${section}-${index}` when no itemKeys provided.
    // When itemKeys are provided by the consumer, they form the key directly.
    // Since we can't look up itemKeys here, fall back to the positional key.
    return nativeMod.layoutCache.getAttributes(`grid-${section}-${index}`);
  }

  attributesForSupplementary(kind: string, section: number): LayoutAttributes | null {
    return nativeMod.layoutCache.getAttributes(`grid-${section}-${kind}`);
  }

  contentSize(): Size {
    return nativeMod.layoutCache.getTotalContentSize();
  }

  shouldInvalidate(oldBounds: Rect, newBounds: Rect): boolean {
    return Math.abs(oldBounds.width - newBounds.width) > 0.5;
  }

  invalidationScope(): InvalidationScope {
    return { type: 'full' };
  }
}

/**
 * Create a grid layout with the given delegate configuration.
 *
 * ```typescript
 * // Uniform row height (fast path — no measurement per row)
 * layout={grid({ columns: 3, rowHeight: 100, columnSpacing: 8, rowSpacing: 8 })}
 *
 * // Dynamic row height (row height = tallest item in each row)
 * layout={grid({ columns: 3, heightForItem: (i) => heights[i], columnSpacing: 8 })}
 *
 * // Multi-section with sticky headers and section backgrounds
 * layout={grid({
 *   columns: 2,
 *   rowHeight: 120,
 *   headerHeight: 44,
 *   footerHeight: 24,
 *   sectionBackground: true,
 *   stickyMode: 'push',
 * })}
 * ```
 */
export function grid(delegate: GridLayoutDelegate): CollectionViewLayout {
  return new GridLayoutEngine(delegate);
}
