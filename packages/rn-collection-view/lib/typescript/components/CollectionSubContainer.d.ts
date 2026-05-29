/**
 * RiffSubContainer — generic JS host for a single section that owns its
 * own layout (orthogonal, radial, spiral, carousel3D, hex, user-defined).
 *
 * Composition model:
 *   <RiffSubContainer
 *     layout={radial({ radius: 150, itemSize: 80 })}
 *     data={items}
 *     renderItem={...}
 *     layoutCacheId={cacheId}     // shared with parent CollectionView
 *     sectionIndex={2}            // slice of the cache this owns (default: 0)
 *   />
 *
 * What the component does:
 *   - Calls `layout.prepare(ctx)` on mount, viewport size change, or data change.
 *   - For scroll-driven layouts: forwards onSubScroll → `layout.processScroll(...)`,
 *     which writes new attributes to the cache via `setAttributesBatch`. The
 *     C++ ShadowNode picks them up on its next layout pass and the iOS view
 *     applies the new frames + transforms + opacity natively (no JS work in
 *     the apply path). The returned render range is ignored — the native
 *     sub-container renders all mounted cells.
 *   - Mounts each data item inside an RNMeasuredCell. Cells receive NO absolute
 *     positioning — their frames come from the sub-container ShadowNode.
 *
 * The scrollDirection is derived from the layout (`horizontal: true` →
 * 'horizontal'; `horizontal: false` → 'vertical'). Pass `scrollDirection="none"`
 * explicitly for static layouts (e.g. hex tiling that fits in the viewport).
 */
import * as React from 'react';
import type { RiffLayout } from '../types/protocol';
type ScrollDirection = 'vertical' | 'horizontal' | 'none';
export interface RiffSubContainerProps<T> {
    /** Layout engine for this section. Must implement RiffLayout. */
    layout: RiffLayout;
    /** Data items for this section. */
    data: T[];
    /** Render function for a single item. Cells get no absolute styles. */
    renderItem: (info: {
        item: T;
        index: number;
        section: number;
    }) => React.ReactElement;
    /** Stable identity for each item. Defaults to index-based string. */
    keyExtractor?: (item: T, index: number) => string;
    /** Cache ID — must match the parent CollectionView's. Defaults to nativeMod.layoutCacheId. */
    layoutCacheId?: number;
    /** Section index this sub-container owns within the parent cache. Default: 0. */
    sectionIndex?: number;
    /**
     * Override scroll direction. Defaults to derive-from-layout:
     *   layout.horizontal === true  → 'horizontal'
     *   layout.horizontal === false → 'vertical'
     * Pass 'none' for static layouts that don't need scrolling.
     */
    scrollDirection?: ScrollDirection;
    /** Optional fixed cross-axis size hint (height for H, width for V). */
    crossAxisSize?: number;
    /** Style passed through to the outer wrapper. */
    style?: object;
}
export declare const RiffSubContainer: <T>(props: RiffSubContainerProps<T>) => React.ReactElement;
/** @deprecated Use RiffSubContainer */
export declare const CollectionSubContainer: <T>(props: RiffSubContainerProps<T>) => React.ReactElement;
/** @deprecated Use RiffSubContainerProps */
export type CollectionSubContainerProps<T> = RiffSubContainerProps<T>;
export {};
